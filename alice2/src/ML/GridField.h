#pragma once

#include <alice2.h>

#include <computeGeom/scalarField.h>
#include <objects/GraphObject.h>

#include <algorithm>
#include <vector>

struct GridField {
    struct Segment {
        alice2::Vec3 a;
        alice2::Vec3 b;
    };

    void configure(int resX, int resY, float xMin, float xMax, float yMin, float yMax) {
        resX_ = resX;
        resY_ = resY;
        xMin_ = xMin;
        xMax_ = xMax;
        yMin_ = yMin;
        yMax_ = yMax;
        samples_.assign(static_cast<size_t>(std::max(0, resX_)) * static_cast<size_t>(std::max(0, resY_)), 0.f);
        segments_.clear();
    }

    void updateValues(const std::vector<float>& samples) {
        const size_t expected = static_cast<size_t>(std::max(0, resX_)) * static_cast<size_t>(std::max(0, resY_));
        if (samples.size() != expected || expected == 0) {
            return;
        }
        samples_ = samples;
        rebuildSegments();
    }

    void draw(alice2::Renderer& renderer, float left, float top, float cellW, float cellH,
              const alice2::Color& color, float thickness = 1.5f) const {
        if (segments_.empty()) {
            return;
        }

        const float sxMax = float(std::max(1, resX_ - 1));
        const float syMax = float(std::max(1, resY_ - 1));
        const float invX = 1.0f / std::max(1e-6f, xMax_ - xMin_);
        const float invY = 1.0f / std::max(1e-6f, yMax_ - yMin_);

        for (const auto& seg : segments_) {
            auto mapPoint = [&](const alice2::Vec3& p) -> alice2::Vec2 {
                const float gx = (p.x - xMin_) * invX * sxMax;
                const float gy = (p.y - yMin_) * invY * syMax;
                const float px = left + gx * cellW;
                const float py = top + (syMax - gy) * cellH;
                return alice2::Vec2(px, py);
            };
            const alice2::Vec2 a = mapPoint(seg.a);
            const alice2::Vec2 b = mapPoint(seg.b);
            renderer.draw2dLine(a, b, color, thickness);
        }
    }

    bool empty() const { return samples_.empty(); }
    int resX() const { return resX_; }
    int resY() const { return resY_; }

private:
    void rebuildSegments() {
        segments_.clear();
        if (resX_ <= 1 || resY_ <= 1 || samples_.empty()) {
            return;
        }

        try {
            ScalarField2D field(alice2::Vec3(xMin_, yMin_, 0.f), alice2::Vec3(xMax_, yMax_, 0.f), resX_, resY_);
            field.set_values(samples_);
            const GraphObject contours = field.get_contours(0.0f);
            const auto data = contours.getGraphData();
            if (!data) {
                return;
            }
            segments_.reserve(data->edges.size());
            for (const auto& edge : data->edges) {
                if (edge.vertexA < 0 || edge.vertexB < 0 ||
                    edge.vertexA >= static_cast<int>(data->vertices.size()) ||
                    edge.vertexB >= static_cast<int>(data->vertices.size())) {
                    continue;
                }
                const alice2::Vec3& a = data->vertices[edge.vertexA].position;
                const alice2::Vec3& b = data->vertices[edge.vertexB].position;
                segments_.push_back(Segment{a, b});
            }
        } catch (...) {
            segments_.clear();
        }
    }

    int resX_ = 0;
    int resY_ = 0;
    float xMin_ = 0.f;
    float xMax_ = 0.f;
    float yMin_ = 0.f;
    float yMax_ = 0.f;
    std::vector<float> samples_;
    std::vector<Segment> segments_;
};
