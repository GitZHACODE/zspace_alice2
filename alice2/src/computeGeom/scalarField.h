#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include "../include/alice2.h"

using namespace alice2;

// Forward declarations
struct ContourData;

// Utility functions for scalar field operations
namespace ScalarFieldUtils {
    inline Vec3 vec_max(const Vec3& a, const Vec3& b) {
        return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    }

    inline float smooth_min(float a, float b, float k) {
        float r = exp2(-a / k) + exp2(-b / k);
        return -k * log2(r);
    }

    inline float smooth_min_weighted(float a, float b, float k, float wt){
        //   (1-wt)*exp(-a/k) + wt*exp(-b/k)
        float termA = (1.0f - wt) * exp2(-a / k);
        float termB = wt * exp2(-b / k);
        float r = termA + termB;

        // Avoid log(0)
        if (r < 1e-14f)
        {
            // Return something large negative or handle underflow
            return -1e6f;
        }

        // Weighted exponential SMin formula:
        return -k * log2(r);
    }

    inline float map_range(float value, float inputMin, float inputMax, float outputMin, float outputMax) {
        return outputMin + (outputMax - outputMin) * ((value - inputMin) / (inputMax - inputMin));
    }

    inline float lerp(float start, float stop, float amt) {
        return start + (stop - start) * amt;
    }

    inline float distance_to(const Vec3& a, const Vec3& b) {
        return (a - b).length();
    }

    inline void get_jet_color(float value, float& r, float& g, float& b) {
        value = clamp(value, -1.0f, 1.0f);
        float normalized = (value + 1.0f) * 0.5f;
        float fourValue = 4.0f * normalized;

        r = clamp(std::min(fourValue - 1.5f, -fourValue + 4.5f), 0.0f, 1.0f);
        g = clamp(std::min(fourValue - 0.5f, -fourValue + 3.5f), 0.0f, 1.0f);
        b = clamp(std::min(fourValue + 0.5f, -fourValue + 2.5f), 0.0f, 1.0f);
    }
}

// Contour data structure
struct ContourData {
    std::vector<std::vector<Vec3>> contours;
    std::vector<std::pair<Vec3, Vec3>> line_segments;
    float threshold;

    ContourData() : threshold(0.0f) {}
    explicit ContourData(float t) : threshold(t) {}
};

/**
 * Modern C++ 2D Scalar Field class with RAII principles
 * Supports dynamic resolution, proper memory management, and clean API
 */
class ScalarField2D {
private:
    // Grid properties
    Vec3 m_min_bounds;
    Vec3 m_max_bounds;
    int m_res_x;
    int m_res_y;

    // Dynamic data storage
    std::vector<Vec3> m_grid_points;
    std::vector<float> m_field_values;
    std::vector<float> m_normalized_values;
    std::vector<Vec3> m_gradient_field;

    // Cached contour data
    mutable std::vector<ContourData> m_cached_contours;

    // Helper methods
    inline int get_index(int x, int y) const {
        return y * m_res_x + x;
    }

    inline std::pair<int, int> get_coords(int index) const {
        return {index % m_res_x, index / m_res_x};
    }

    inline bool is_valid_coords(int x, int y) const {
        return x >= 0 && x < m_res_x && y >= 0 && y < m_res_y;
    }

    void initialize_grid();
    void normalize_field();

public:
    // Constructor with RAII principles
    ScalarField2D(const Vec3& min_bb = Vec3(-75, -75, 0),
                  const Vec3& max_bb = Vec3(75, 75, 0),
                  int res_x = 100,
                  int res_y = 100);

    // Destructor
    ~ScalarField2D() = default;

    // Copy constructor and assignment operator
    ScalarField2D(const ScalarField2D& other);
    ScalarField2D& operator=(const ScalarField2D& other);

    // Move constructor and assignment operator
    ScalarField2D(ScalarField2D&& other) noexcept;
    ScalarField2D& operator=(ScalarField2D&& other) noexcept;

    // Getter/Setter methods
    const std::vector<Vec3>& get_points() const { return m_grid_points; }
    const std::vector<float>& get_values() const { return m_field_values; }
    void set_values(const std::vector<float>& values);
    std::pair<int, int> get_resolution() const { return {m_res_x, m_res_y}; }
    std::pair<Vec3, Vec3> get_bounds() const { return {m_min_bounds, m_max_bounds}; }

    // Field generation methods (snake_case naming)
    void clear_field();
    float get_scalar_circle(const Vec3& center, float radius) const;
    float get_scalar_square(const Vec3& center, const Vec3& half_size, float angle_radians) const;
    float get_scalar_line(const Vec3& start, const Vec3& end, float thickness) const;
    float get_scalar_polygon(const std::vector<Vec3>& vertices) const;
    float get_scalar_voronoi(const std::vector<Vec3>& sites, const Vec3& query_point) const;

    // Apply scalar functions to entire field
    void apply_scalar_circle(const Vec3& center, float radius);
    void apply_scalar_rect(const Vec3& center, const Vec3& half_size, float angle_radians);
    void apply_scalar_line(const Vec3& start, const Vec3& end, float thickness);
    void apply_scalar_polygon(const std::vector<Vec3>& vertices);
    void apply_scalar_voronoi(const std::vector<Vec3>& sites);

    // Boolean operations (snake_case naming)
    void boolean_union(const ScalarField2D& other);
    void boolean_intersect(const ScalarField2D& other);
    void boolean_subtract(const ScalarField2D& other);
    void boolean_difference(const ScalarField2D& other);
    void boolean_smin(const ScalarField2D& other, float smoothing = 1.0f);
    void boolean_smin_weighted(const ScalarField2D& other, float smoothing = 1.0f, float wt = 0.5f);

    // Interpolation
    void interpolate(const ScalarField2D& other, float t);

    // Analysis methods
    ContourData get_contours(float threshold) const;
    std::vector<Vec3> get_gradient() const;

    // Rendering methods
    void draw_points(Renderer& renderer, int step = 4) const;
    void draw_values(Renderer& renderer, int step = 8) const;

    // Legacy compatibility methods (deprecated)
    void addVoronoi(const std::vector<Vec3>& sites) { apply_scalar_voronoi(sites); }
    void addCircleSDF(const Vec3& center, float radius) { apply_scalar_circle(center, radius); }
    void addOrientedRectSDF(const Vec3& center, const Vec3& half_size, float angle) { apply_scalar_rect(center, half_size, angle); }
    void clearField() { clear_field(); }
    void drawFieldPoints(Renderer& renderer, bool debug = false) const { draw_points(renderer); }
    void drawIsocontours(Renderer& renderer, float threshold) const;
    void normalise() { normalize_field(); }
};
