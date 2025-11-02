#pragma once
#include <vector>
#include <functional>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <charconv>
#include <limits>


#include <alice2.h>
#include "genericMLP.h"
#include <computeGeom/scalarField.h>

//#define DEBUG_OUTPUT 1
#ifdef DEBUG_OUTPUT
#include <chrono>
#include <iostream>

  // 1) Pick one clock type (steady_clock is fine on MSVC)
  using TimerClock = std::chrono::steady_clock;

  // 2) One shared start‐time variable
  static thread_local TimerClock::time_point __timer_start;

  // 3) Macro to record "now" into that one variable
  #define TIMER_START() \
    (__timer_start = TimerClock::now())

  // 4) Macro to compare now against that same variable
  #define TIMER_END(msg)                                                      \
    do {                                                                       \
      auto __timer_end = TimerClock::now();                                    \
      /* direct-construction of a double-millisecond duration */               \
      double __elapsed_ms =                                                     \
        std::chrono::duration<double, std::milli>(                              \
          __timer_end - __timer_start                                           \
        ).count();                                                              \
      std::cout << "[TIMER] " << msg << " took "                                \
                << __elapsed_ms << " ms\n";                                     \
    } while(0)

#else
  #define TIMER_START()    (void)0
  #define TIMER_END(msg)   (void)0
#endif

/**
 * PolygonSDF_MLP: Multi-Layer Perceptron for learning polygon SDF approximation
 *
 * This class separates concerns into:
 * - Data generation and management
 * - SDF computation and analysis
 * - MLP training and optimization
 * - Visualization support (minimal interface)
 */
class PolygonSDF_MLP : public MLP
{
public:
    using MLP::MLP;

    // === Data Management ===
    std::vector<Vec3> polygon;
    std::vector<Vec3> trainingSamples;
    std::vector<float> sdfGT;
    std::vector<float> losses;
    std::vector<float> losses_ang;

    std::vector<Vec3> fittedCenters;
    std::vector<float> fittedRadii;

    // === Training Parameters ===
    int number_sdf = 8;
    double radius = 8.0;
    float smoothK = 3.0f;
    Vec3 sunDir = Vec3(1, 1, 0);
    int epoch = 0;

    // === Field Generation ===
    ScalarField2D generatedField;
    float building_width = 30.0f;
    float building_height = 40.0f;

    // === PUBLIC API METHODS ===

    /**
     * Generate scalar field from MLP output
     */
    void generate_field(std::vector<float>& x)
    {
        auto out = forward(x);
        std::vector<Vec3> centers(number_sdf);
        std::vector<float> radii(number_sdf);
        decode_output(out, centers, radii);
        generate_field(centers, radii);
    }

    /**
     * Generate scalar field from explicit centers and radii
     */
    void generate_field(std::vector<Vec3>& centers, std::vector<float>& radii)
    {
        generatedField.clearField();

        std::pair<int, int> resolution = generatedField.get_resolution();
        std::vector<float> field_values = generatedField.get_values();
        std::vector<Vec3> field_points = generatedField.get_points();

        for (size_t i = 0; i < field_values.size(); ++i) {
            field_values[i] = blend_oriented_box_sdfs(field_points[i], centers, radii);
        }

        rescale_to_range(field_values);
        generatedField.set_values(field_values);
    }

    /**
     * Generate training data from polygon
     */
    void sample_points(std::vector<Vec3>& training_samples, std::vector<float>& sdf_gt,
                      std::vector<Vec3>& poly)
    {
        training_samples.clear();
        sdf_gt.clear();

        std::pair<Vec3,Vec3> minMaxBB = generatedField.get_bounds();
        float tStep = 5.0f;

        for (float x = minMaxBB.first.x; x <= minMaxBB.second.x; x += tStep) {
            for (float y = minMaxBB.first.y; y <= minMaxBB.second.y; y += tStep) {
                Vec3 pt(x, y, 0);
                if (is_inside_polygon(pt, poly)) {
                    training_samples.push_back(pt);
                    sdf_gt.push_back(polygon_sdf(pt, poly));
                }
            }
        }

        std::cout << "Generated " << training_samples.size() << " training samples" << std::endl;
    }

    // === LEGACY API SUPPORT (for backward compatibility) ===
    void GenerateField(std::vector<float>& x) { generate_field(x); }
    void GenerateField(std::vector<Vec3>& centers, std::vector<float>& radii) { generate_field(centers, radii); }
    void samplePoints(std::vector<Vec3>& ts, std::vector<float>& sdf, std::vector<Vec3>& poly) { sample_points(ts, sdf, poly); }

    // === CORE SDF COMPUTATION METHODS ===

    /**
     * Rescale field values to target range while preserving sign
     */
    void rescale_to_range(std::vector<float>& values, float target_min = -1.0f, float target_max = 1.0f)
    {
        if (values.empty()) return;

        // Find per-sign extrema
        float min_val[2] = {
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity()
        };
        float max_val[2] = {
            -std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()
        };

        for (float v : values) {
            int idx = (v >= 0.0f ? 0 : 1);
            min_val[idx] = std::min(min_val[idx], v);
            max_val[idx] = std::max(max_val[idx], v);
        }

        // Avoid zero-division
        float range[2] = {
            std::max(max_val[0] - min_val[0], 1e-6f),
            std::max(max_val[1] - min_val[1], 1e-6f)
        };

        // Remap each value:
        // - positives → [0, target_max]
        // - negatives → [target_min, 0]
        for (float& v : values) {
            if (v >= 0.0f) {
                float t = (v - min_val[0]) / range[0];
                v = std::lerp(0.0f, target_max, t);
            } else {
                float t = (v - min_val[1]) / range[1];
                v = std::lerp(target_min, 0.0f, t);
            }
        }
    }

    /**
     * Blend multiple oriented box SDFs
     */
    float blend_oriented_box_sdfs(Vec3 pt, std::vector<Vec3>& centers, std::vector<float>& angles,float k = 3.0f)
    {
        float d = 1e6;
        for (size_t i = 0; i < centers.size(); i++) {
            float dist = oriented_box_sdf(pt, centers[i], building_width, building_height, angles[i]);
            d = std::min(d, dist);
        }
        return d;
    }

    /**
     * Oriented box SDF computation
     */
    float oriented_box_sdf(Vec3 pt, Vec3 center, float width, float height, float angle_rad)
    {
        Vec3 d = pt - center;

        float cos_a = cos(angle_rad);
        float sin_a = sin(angle_rad);

        float local_x = d.x * cos_a + d.y * sin_a;
        float local_y = -d.x * sin_a + d.y * cos_a;

        float dx = fabs(local_x) - width * 0.5f;
        float dy = fabs(local_y) - height * 0.5f;

        float ax = std::max(dx, 0.0f);
        float ay = std::max(dy, 0.0f);

        float inside_dist = std::min(std::max(dx, dy), 0.0f);
        return sqrtf(ax * ax + ay * ay) + inside_dist;
    }

    // === LEGACY API SUPPORT ===
    void rescaleToRange(std::vector<float>& values, float target_min = -1.0f, float target_max = 1.0f) {
        rescale_to_range(values, target_min, target_max);
    }
    float blendOrientedBoxSDFs(Vec3 pt, std::vector<Vec3>& centers, std::vector<float>& angles, float k) {
        return  blend_oriented_box_sdfs(pt, centers, angles, k);
    }
    float orientedBoxSDF(Vec3 pt, Vec3 center, float width, float height, float angle_rad) {
        return oriented_box_sdf(pt, center, width, height, angle_rad);
    }



    // === MLP OUTPUT PROCESSING ===

    /**
     * Decode MLP output into centers and angles
     */
    void decode_output(const std::vector<float>& out, std::vector<Vec3>& centers, std::vector<float>& angles)
    {
        centers.resize(number_sdf);
        angles.resize(number_sdf);

        for (int i = 0; i < number_sdf; i++) {
            int idx = i * 4;
            centers[i] = Vec3(out[idx + 0], out[idx + 1], 0);

            Vec3 dir(out[idx + 2], out[idx + 3], 0);
            dir.normalize();
            angles[i] = atan2(dir.y, dir.x);
        }
    }

    // === LEGACY API SUPPORT ===
    void decodeOutput(const std::vector<float>& out, std::vector<Vec3>& centers, std::vector<float>& angles) {
        decode_output(out, centers, angles);
    }

    float evaluateLoss(std::vector<Vec3> &centers,
                       std::vector<float> &angles)
    {
        const int N = trainingSamples.size();
        const int numLossTypes = 2; // 0: coverage, 1: angular

        TIMER_START();

        std::vector<std::vector<float>> lossesByType(
            numLossTypes, std::vector<float>(N, 0.0f));

        Vec3 sunDir(1, 1, 0);
        sunDir.normalize();

        TIMER_START();
        // Step 1: compute raw losses
        for (int i = 0; i < N; i++)
        {
            Vec3 pt = trainingSamples[i];

            // Loss 0: coverage (MSE)
            float pred = blendCircleSDFs(pt, centers, angles, smoothK);
            float err = pred - sdfGT[i];
            lossesByType[0][i] = err * err;

            // Loss 1: angular alignment (squared angle)
            Vec3 grad = gradient_at(pt, centers, angles);
            Vec3 grad_polygon = gradient_at_polygon_sdf(pt, polygon);
            grad.normalize();
            grad = grad.cross(Vec3(0, 0, 1));
            grad_polygon.normalize();

            float angleErr = angleBetween(grad, sunDir);

            lossesByType[1][i] = angleErr * angleErr;
        }

        TIMER_END("evaluateLoss::computeRawLosses");

        TIMER_START();
        // Step 2: normalize each loss type to [0,1]
        std::vector<bool> normalizeLoss = {false, true};
        for (int t = 0; t < numLossTypes; t++)
        {
            if (!normalizeLoss[t])
                continue;

            float minVal = 1e6f, maxVal = -1e6f;
            for (float v : lossesByType[t])
            {
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }

            float range = std::max(maxVal - minVal, 1e-6f);
            for (float &v : lossesByType[t])
            {
                v = (v - minVal) / range;
            }
        }
        TIMER_END("evaluateLoss::normalizeLosses");

        TIMER_START();
        // Step 3: weighted sum of all loss types
        std::vector<float> weights = {1, 1};
        float totalLoss = 0.0f;
        for (int i = 0; i < N; i++)
        {
            float combined = 0.0f;
            for (int t = 0; t < numLossTypes; t++)
            {
                combined += weights[t] * lossesByType[t][i];
            }
            totalLoss += combined;
        }
        TIMER_END("evaluateLoss::computeTotalLoss");

        // Optional debug access
        losses = lossesByType[0];
        losses_ang = lossesByType[1];

        TIMER_END("evaluateLoss::overall_evaluateLoss");

        float finalLoss = totalLoss / static_cast<float>(N);
        return finalLoss;
    }

    float computeLoss(std::vector<float> &x, std::vector<float> &dummy) override
    {
        auto out = forward(x);
        std::vector<Vec3> centers;
        std::vector<float> angles;
        decodeOutput(out, centers, angles);

        epoch++;
        return evaluateLoss(centers, angles);
    }

    void computeGradient(std::vector<float> &x, std::vector<float> &dummy, std::vector<float> &gradOut) override
    {
        auto out = forward(x);
        float eps = 0.01f;

        TIMER_START();
        std::vector<Vec3> baseCenters;
        std::vector<float> baseAngles;
        decodeOutput(out, baseCenters, baseAngles);
        TIMER_END("computeGradient::decodeOutput");

        TIMER_START();
        float baseLoss = evaluateLoss(baseCenters, baseAngles);
        TIMER_END("computeGradient::evaluateLoss");

        gradOut.assign(out.size(), 0.0f);

        TIMER_START();
        for (int i = 0; i < out.size(); ++i)
        {
            std::vector<float> outPerturbed = out;
            outPerturbed[i] += eps;

            TIMER_START();
            std::vector<Vec3> centers;
            std::vector<float> angles;
            decodeOutput(outPerturbed, centers, angles);
            TIMER_END("computeGradient::decodeOutput");

            TIMER_START();
            float lossPerturbed = evaluateLoss(centers, angles);
            TIMER_END("computeGradient::evaluateloss");

            gradOut[i] = (lossPerturbed - baseLoss) / eps;
        }
        // std::cout << "out size: " << out.size() << std::endl;
        TIMER_END("computeGradient::end_computeGradient");
    }

    // === POLYGON SDF COMPUTATION ===

    /**
     * Check if point is inside polygon using winding number
     */
    bool is_inside_polygon(const Vec3& p, std::vector<Vec3>& poly)
    {
        int winding_number = 0;

        for (size_t i = 0; i < poly.size(); i++) {
            Vec3& a = poly[i];
            Vec3& b = poly[(i + 1) % poly.size()];

            if (a.y <= p.y) {
                if (b.y > p.y && ((b - a).cross(p - a)).z > 0)
                    ++winding_number;
            } else {
                if (b.y <= p.y && ((b - a).cross(p - a)).z < 0)
                    --winding_number;
            }
        }

        return (winding_number != 0);
    }

    /**
     * Compute signed distance to polygon
     */
    float polygon_sdf(const Vec3& p, std::vector<Vec3>& poly)
    {
        float min_dist = 1e6;
        int n = poly.size();

        for (int i = 0; i < n; i++) {
            Vec3 a = poly[i];
            Vec3 b = poly[(i + 1) % n];

            Vec3 ab = b - a;
            Vec3 ap = p - a;

            float t = std::max(0.0f, std::min(1.0f, (ab.dot(ap)) / (ab.dot(ab))));
            Vec3 proj = a + ab * t;
            float d = p.distanceTo(proj);
            min_dist = std::min(min_dist, d);
        }

        return min_dist * (is_inside_polygon(p, poly) ? -1.0f : 1.0f);
    }

    // === GRADIENT COMPUTATION ===

    /**
     * Compute gradient at point for blended SDF
     */
    Vec3 gradient_at(Vec3 pt, std::vector<Vec3>& centers, std::vector<float>& angles, float h = 0.1f)
    {
        float dx = blend_oriented_box_sdfs(pt + Vec3(h, 0, 0), centers, angles) -
                   blend_oriented_box_sdfs(pt - Vec3(h, 0, 0), centers, angles);

        float dy = blend_oriented_box_sdfs(pt + Vec3(0, h, 0), centers, angles) -
                   blend_oriented_box_sdfs(pt - Vec3(0, h, 0), centers, angles);

        Vec3 ret(dx, dy, 0);
        ret.normalize();
        return ret;
    }

    /**
     * Compute gradient at point for polygon SDF
     */
    Vec3 gradient_at_polygon_sdf(const Vec3& pt, std::vector<Vec3>& poly, float h = 0.1f)
    {
        float dx = polygon_sdf(pt + Vec3(h, 0, 0), poly) -
                   polygon_sdf(pt - Vec3(h, 0, 0), poly);

        float dy = polygon_sdf(pt + Vec3(0, h, 0), poly) -
                   polygon_sdf(pt - Vec3(0, h, 0), poly);

        Vec3 ret(dx, dy, 0);
        ret.normalize();
        return ret;
    }

    /**
     * Compute angle between two vectors
     */
    float angle_between(Vec3& a, Vec3& b)
    {
        float dot = a.x * b.x + a.y * b.y;
        float det = a.x * b.y - a.y * b.x;
        return atan2(det, dot);
    }

    /**
     * Blend circle SDFs with smooth minimum
     */
    float blend_circle_sdfs(Vec3 pt, std::vector<Vec3>& centers, std::vector<float>& radii, float k)
    {
        float d = 1e6;
        for (size_t i = 0; i < centers.size(); i++) {
            float dist = pt.distanceTo(centers[i]) - radii[i];
            d = ScalarFieldUtils::smooth_min(d, dist, k);
        }
        return d;
    }

    // === LEGACY API SUPPORT ===
    bool isInsidePolygon(const Vec3& p, std::vector<Vec3>& poly) { return is_inside_polygon(p, poly); }
    float polygonSDF(const Vec3& p, std::vector<Vec3>& poly) { return polygon_sdf(p, poly); }
    Vec3 gradientAt(Vec3 pt, std::vector<Vec3>& centers, std::vector<float>& angles, float h) {
        return gradient_at(pt, centers, angles, h);
    }
    Vec3 gradientAt_polygonSDF(const Vec3& pt, std::vector<Vec3>& poly, float h) {
        return gradient_at_polygon_sdf(pt, poly, h);
    }
    float angleBetween(Vec3& a, Vec3& b) { return angle_between(a, b); }
    float blendCircleSDFs(Vec3 pt, std::vector<Vec3>& centers, std::vector<float>& radii, float k) {
        return blend_circle_sdfs(pt, centers, radii, k);
    }



    // === VISUALIZATION SUPPORT METHODS ===
    // Note: These methods provide minimal rendering interface
    // Main visualization logic should be in the sketch class

    /**
     * Draw loss information as text
     */
    void draw_loss_text(Renderer& renderer, float start_y = 150)
    {
        if (losses.empty() || losses_ang.empty()) return;

        float loss_sum = 0;
        float loss_ang_sum = 0;

        for (size_t i = 0; i < losses_ang.size(); i++) {
            loss_sum += losses[i];
            loss_ang_sum += losses_ang[i];
        }

        renderer.setColor(Color(0.0f, 0.0f, 0.0f));

        char buffer[100];
        sprintf(buffer, "Loss: %.3f", loss_sum / trainingSamples.size());
        renderer.drawString(std::string(buffer), 10, start_y);

        sprintf(buffer, "Angular Loss: %.3f", loss_ang_sum);
        renderer.drawString(std::string(buffer), 10, start_y + 15);
    }

    /**
     * Draw loss bar graph
     */
    void draw_loss_bar_graph(Renderer& renderer, const std::vector<float>& loss_data,
                            float start_x, float start_y, float width = 200, float height = 40)
    {
        if (loss_data.empty()) return;

        int n = loss_data.size();
        float bar_spacing = width / (float)n;

        // Normalize losses to [0, 1]
        float min_val = 1e6f, max_val = -1e6f;
        for (float v : loss_data) {
            min_val = std::min(min_val, v);
            max_val = std::max(max_val, v);
        }
        float range = std::max(max_val - min_val, 1e-6f);

        for (int i = 0; i < n; i++) {
            float normalized = (loss_data[i] - min_val) / range;
            float x = start_x + i * bar_spacing;
            float h = height * normalized;

            float r, g, b;
            ScalarFieldUtils::get_jet_color(normalized * 2.0f - 1.0f, r, g, b);

            Vec2 start(x, start_y);
            Vec2 end(x, start_y + h);
            renderer.draw2dLine(start, end, Color(r, g, b));
        }
    }

    /**
     * Visualize scalar field
     */
    void visualize_field(Renderer& renderer, float threshold = 0.01, bool draw_field = true)
    {
        if (draw_field) generatedField.draw_points(renderer, 1);
        //generatedField.draw_values(renderer);
    }

    /**
     * Visualize gradients
     */
    void visualize_gradients(Renderer& renderer, std::vector<float>& x)
    {
        auto out = forward(x);
        std::vector<Vec3> centers(number_sdf);
        std::vector<float> angles(number_sdf);
        decode_output(out, centers, angles);

        // Draw gradients for SDF centers
        for (int i = 0; i < number_sdf; i++) {
            Vec3 grad_polygon = gradient_at_polygon_sdf(centers[i], polygon);
            grad_polygon.normalize();

            renderer.drawLine(centers[i], centers[i] + grad_polygon * 3.0f, Color(0.0f, 0.0f, 0.0f));

            // Local coordinate system visualization
            float cos_a = cos(angles[i]);
            float sin_a = sin(angles[i]);
            Vec3 axis_y(sin_a, cos_a, 0);  // local Y direction
            axis_y.normalize();

            renderer.drawLine(centers[i], centers[i] + axis_y * 4.0f, Color(1.0f, 0.0f, 0.0f));
        }

        // Draw gradients for training samples
        for (size_t i = 0; i < trainingSamples.size(); i++) {
            Vec3 a = trainingSamples[i];

            Vec3 grad_polygon = gradient_at_polygon_sdf(trainingSamples[i], polygon);
            grad_polygon.normalize();
            renderer.drawLine(a, a + grad_polygon, Color(0.0f, 0.0f, 0.0f));

            Vec3 grad = gradient_at(trainingSamples[i], centers, angles);
            grad.normalize();
            renderer.drawLine(a, a + grad, Color(1.0f, 0.0f, 0.0f));
        }
    }

    /**
     * Draw a circle using line segments
     */
    void draw_circle(Renderer& renderer, const Vec3& center, float radius, int segments, const Color& color)
    {
        renderer.setColor(color);
        const float PI = 3.14159265359f;
        for (int i = 0; i < segments; i++) {
            float angle1 = (float)i / segments * 2.0f * PI;
            float angle2 = (float)(i + 1) / segments * 2.0f * PI;

            Vec3 p1 = center + Vec3(cos(angle1) * radius, sin(angle1) * radius, 0);
            Vec3 p2 = center + Vec3(cos(angle2) * radius, sin(angle2) * radius, 0);

            renderer.drawLine(p1, p2);
        }
    }

    // === LEGACY API SUPPORT ===
    void drawLossText(Renderer& renderer, float start_y = 150) { draw_loss_text(renderer, start_y); }
    void drawLossBarGraph(Renderer& renderer, const std::vector<float>& losses, float start_x, float start_y, float width = 800, float height = 50) {
        draw_loss_bar_graph(renderer, losses, start_x, start_y, width, height);
    }
    void visualiseField(Renderer& renderer, float threshold = 0.01, bool draw_field = true) { visualize_field(renderer, threshold, draw_field); }
    void visualiseGradients(Renderer& renderer, std::vector<float>& x) { visualize_gradients(renderer, x); }
    void drawCircle(Renderer& renderer, const Vec3& center, float radius, int segments, const Color& color) {
        draw_circle(renderer, center, radius, segments, color);
    }
};
