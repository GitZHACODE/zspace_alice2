#include <computeGeom/ScalarField.h>
#include "../objects/GraphObject.h"
#include <unordered_map>
#include <cmath>
#include <limits>


// Implementation of key methods
ScalarField2D::ScalarField2D(const Vec3& min_bb, const Vec3& max_bb, int res_x, int res_y)
    : m_min_bounds(min_bb), m_max_bounds(max_bb), m_res_x(res_x), m_res_y(res_y) {
    if (res_x <= 0 || res_y <= 0) {
        throw std::invalid_argument("Resolution must be positive");
    }

    const int total_points = m_res_x * m_res_y;
    m_grid_points.reserve(total_points);
    m_field_values.resize(total_points, 0.0f);
    m_normalized_values.resize(total_points, 0.0f);
    m_gradient_field.resize(total_points, Vec3(0, 0, 0));
    m_has_valid_sdf = false;

    initialize_grid();
}

ScalarField2D::ScalarField2D(const ScalarField2D& other)
    : m_min_bounds(other.m_min_bounds), m_max_bounds(other.m_max_bounds)
    , m_res_x(other.m_res_x), m_res_y(other.m_res_y)
    , m_grid_points(other.m_grid_points), m_field_values(other.m_field_values)
    , m_normalized_values(other.m_normalized_values), m_gradient_field(other.m_gradient_field)
    , m_has_valid_sdf(other.m_has_valid_sdf) {
}

ScalarField2D& ScalarField2D::operator=(const ScalarField2D& other) {
    if (this != &other) {
        m_min_bounds = other.m_min_bounds;
        m_max_bounds = other.m_max_bounds;
        m_res_x = other.m_res_x;
        m_res_y = other.m_res_y;
        m_grid_points = other.m_grid_points;
        m_field_values = other.m_field_values;
        m_normalized_values = other.m_normalized_values;
        m_gradient_field = other.m_gradient_field;
        m_has_valid_sdf = other.m_has_valid_sdf;
    }
    return *this;
}

ScalarField2D::ScalarField2D(ScalarField2D&& other) noexcept
    : m_min_bounds(std::move(other.m_min_bounds)), m_max_bounds(std::move(other.m_max_bounds))
    , m_res_x(other.m_res_x), m_res_y(other.m_res_y)
    , m_grid_points(std::move(other.m_grid_points)), m_field_values(std::move(other.m_field_values))
    , m_normalized_values(std::move(other.m_normalized_values)), m_gradient_field(std::move(other.m_gradient_field))
    , m_has_valid_sdf(other.m_has_valid_sdf) {
    other.m_res_x = other.m_res_y = 0;
    other.m_has_valid_sdf = false;
}

ScalarField2D& ScalarField2D::operator=(ScalarField2D&& other) noexcept {
    if (this != &other) {
        m_min_bounds = std::move(other.m_min_bounds);
        m_max_bounds = std::move(other.m_max_bounds);
        m_res_x = other.m_res_x;
        m_res_y = other.m_res_y;
        m_grid_points = std::move(other.m_grid_points);
        m_field_values = std::move(other.m_field_values);
        m_normalized_values = std::move(other.m_normalized_values);
        m_gradient_field = std::move(other.m_gradient_field);
        m_has_valid_sdf = other.m_has_valid_sdf;
        other.m_res_x = other.m_res_y = 0;
        other.m_has_valid_sdf = false;
    }
    return *this;
}

// Helper method implementations
void ScalarField2D::initialize_grid() {
    m_grid_points.clear();
    m_grid_points.reserve(m_res_x * m_res_y);

    const Vec3 span = m_max_bounds - m_min_bounds;
    const float step_x = span.x / (m_res_x - 1);
    const float step_y = span.y / (m_res_y - 1);

    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const float x = m_min_bounds.x + i * step_x;
            const float y = m_min_bounds.y + j * step_y;
            m_grid_points.emplace_back(x, y, 0.0f);
        }
    }
}

void ScalarField2D::normalize_field() {
    if (m_field_values.empty()) return;

    const auto [min_it, max_it] = std::minmax_element(m_field_values.begin(), m_field_values.end());
    const float min_val = *min_it;
    const float max_val = *max_it;

    m_normalized_values.resize(m_field_values.size(), 0.0f);

    const bool has_neg = (min_val < 0.0f);
    const bool has_pos = (max_val > 0.0f);
    const float neg_scale = has_neg ? (-1.0f / min_val) : 0.0f;
    const float pos_scale = has_pos ? ( 1.0f / max_val) : 0.0f;

    for (size_t i = 0, n = m_field_values.size(); i < n; ++i) {
        const float v = m_field_values[i];
        float out = 0.0f;
        if (v < 0.0f && has_neg) out = v * neg_scale;
        else if (v > 0.0f && has_pos) out = v * pos_scale;
        m_normalized_values[i] = std::clamp(out, -1.0f, 1.0f);
    }

    m_is_normalized = true;
}

void ScalarField2D::clear_field() {
    std::fill(m_field_values.begin(), m_field_values.end(), 0.0f);
    std::fill(m_normalized_values.begin(), m_normalized_values.end(), 0.0f);
    m_has_valid_sdf = false;
}

Vec3 ScalarField2D::cellPosition(int x, int y) const
{
    int index = y * m_res_x + x;
    return m_grid_points[index];
}

float ScalarField2D::sample_nearest(const Vec3 &p) const
{
    float fx = (p.x - m_min_bounds.x) / (m_max_bounds.x - m_min_bounds.x) * (m_res_x - 1);
    float fy = (p.y - m_min_bounds.y) / (m_max_bounds.y - m_min_bounds.y) * (m_res_y - 1);

    int ix = std::round(fx);
    int iy = std::round(fy);

    ix = std::clamp(ix, 0, m_res_x - 1);
    iy = std::clamp(iy, 0, m_res_y - 1);

    return m_field_values[iy * m_res_x + ix];
}

Vec3 ScalarField2D::gradientAt(const Vec3 &p) const
{
    float eps = 1.0f;
    Vec3 a(p.x + eps, p.y, 0);
    Vec3 b(p.x - eps, p.y, 0);

    Vec3 c(p.x, p.y + eps, 0);
    Vec3 d(p.x, p.y - eps, 0);

    float dx = sample_nearest(a) - sample_nearest(b);
    float dy = sample_nearest(c) - sample_nearest(d);
    return Vec3(dx, dy, 0.0f) * 0.5f;
}


// Scalar function implementations

void ScalarField2D::apply_scalar_circle(const Vec3& center, float radius) {
    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const int idx = get_index(i, j);
            const Vec3& pt = m_grid_points[idx];
            const float d = ScalarFieldUtils::distance_to(pt, center);
            const float sdf = d - radius; // SDF: negative inside, positive outside
            m_field_values[idx] = sdf;
        }
    }
    m_has_valid_sdf = true;
}

void ScalarField2D::apply_scalar_rect(const Vec3& center, const Vec3& half_size, float angle_radians) {
    const float cos_angle = std::cos(angle_radians);
    const float sin_angle = std::sin(angle_radians);

    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const int idx = get_index(i, j);
            const Vec3 p = m_grid_points[idx] - center;

            // Rotate point into box's local frame
            const Vec3 pr(
                cos_angle * p.x + sin_angle * p.y,
                -sin_angle * p.x + cos_angle * p.y,
                0.0f
            );

            const Vec3 d = ScalarFieldUtils::vec_max(Vec3(std::abs(pr.x), std::abs(pr.y), 0.0f) - half_size, Vec3(0, 0, 0));
            const float outside_dist = d.length();
            const float inside_dist = std::min(std::max(std::abs(pr.x) - half_size.x, std::abs(pr.y) - half_size.y), 0.0f);

            // SDF: negative inside, positive outside
            const float sdf = (outside_dist > 0.0f) ? outside_dist : inside_dist;
            m_field_values[idx] = sdf;
        }
    }
    m_has_valid_sdf = true;
}

void ScalarField2D::apply_scalar_voronoi(const std::vector<Vec3>& sites) {
    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const int idx = get_index(i, j);
            const Vec3& pt = m_grid_points[idx];

            float min_dist = std::numeric_limits<float>::max();
            float second_min_dist = std::numeric_limits<float>::max();

            for (const auto& site : sites) {
                const float d = ScalarFieldUtils::distance_to(pt, site);
                if (d < min_dist) {
                    second_min_dist = min_dist;
                    min_dist = d;
                } else if (d < second_min_dist) {
                    second_min_dist = d;
                }
            }

            // Voronoi edge distance (distance to second closest minus closest)
            m_field_values[idx] = second_min_dist - min_dist;
        }
    }
}

void ScalarField2D::apply_scalar_line(const Vec3& start, const Vec3& end, float thickness) {
    // Simple line SDF implementation
    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const int idx = get_index(i, j);
            const Vec3& pt = m_grid_points[idx];

            const Vec3 pa = pt - start;
            const Vec3 ba = end - start;
            const float h = clamp(pa.dot(ba) / ba.dot(ba), 0.0f, 1.0f);
            const float dist = (pa - ba * h).length();

            m_field_values[idx] = dist - thickness; // SDF: negative inside, positive outside
        }
    }
    m_has_valid_sdf = true;
}

void ScalarField2D::apply_scalar_polygon(const std::vector<Vec3>& vertices) {
    if (vertices.size() < 3) return;

    auto pointInPolygon = [](float px, float py, const std::vector<Vec3>& verts) {
        bool inside = false;
        const size_t n = verts.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const float xi = verts[i].x;
            const float yi = verts[i].y;
            const float xj = verts[j].x;
            const float yj = verts[j].y;
            const bool intersect =
                ((yi > py) != (yj > py)) &&
                (px < (xj - xi) * (py - yi) / ((yj - yi) + 1e-8f) + xi);
            if (intersect) inside = !inside;
        }
        return inside;
    };

    auto segmentDistance = [](const Vec3& p, const Vec3& a, const Vec3& b) {
        const Vec3 ab = b - a;
        const Vec3 ap = p - a;
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = (len2 > 1e-12f) ? ((ap.x * ab.x + ap.y * ab.y) / len2) : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float cx = a.x + t * ab.x;
        const float cy = a.y + t * ab.y;
        const float dx = p.x - cx;
        const float dy = p.y - cy;
        return std::sqrt(dx * dx + dy * dy);
    };

    auto signedArea = [](const std::vector<Vec3>& verts) {
        double area = 0.0;
        const size_t n = verts.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            area += double(verts[j].x) * double(verts[i].y) -
                    double(verts[i].x) * double(verts[j].y);
        }
        return float(0.5 * area);
    };

    const float area = signedArea(vertices);
    const bool isHole = area < 0.0f;
    const bool firstPolygon = !m_has_valid_sdf;

    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const int idx = get_index(i, j);
            const Vec3& pt = m_grid_points[idx];

            float minDist = std::numeric_limits<float>::max();
            for (size_t k = 0, n = vertices.size(); k < n; ++k) {
                const Vec3& a = vertices[k];
                const Vec3& b = vertices[(k + 1) % n];
                minDist = std::min(minDist, segmentDistance(pt, a, b));
            }

            const bool inside = pointInPolygon(pt.x, pt.y, vertices);
            float sdf = inside ? -minDist : minDist;
            if (isHole) sdf = -sdf;

            if (firstPolygon) {
                m_field_values[idx] = sdf;
            } else {
                if (isHole) {
                    m_field_values[idx] = std::max(m_field_values[idx], sdf);
                } else {
                    m_field_values[idx] = std::min(m_field_values[idx], sdf);
                }
            }
        }
    }

    m_has_valid_sdf = true;
}

void ScalarField2D::apply_scalar_ellipse(const Vec3 &center, float radiusX, float radiusY, const float rotation)
{
    float cosR = std::cos(rotation);
    float sinR = std::sin(rotation);

    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            int idx = get_index(i, j);
            Vec3 p = m_grid_points[idx] - center;
            // Rotate the point around the center by -rotation to align major axis
            float xRot = p.x * cosR - p.y * sinR;
            float yRot = p.x * sinR + p.y * cosR;
            float xNorm = xRot / radiusX;
            float yNorm = yRot / radiusY;
            float k = std::sqrt(xNorm * xNorm + yNorm * yNorm);
            float sdf = (k - 1.0f) * std::min(radiusX, radiusY);
            m_field_values[idx] = sdf;
        }
    }
    m_has_valid_sdf = true;
}

void ScalarField2D::apply_scalar_manhattan_voronoi(const std::vector<Vec3> &sites)
{
    for (int j = 0; j < m_res_y; ++j)
    {
        for (int i = 0; i < m_res_x; ++i)
        {
            int idx = get_index(i, j);
            const Vec3 &p = m_grid_points[idx];
            float minDist = std::numeric_limits<float>::max();
            for (const auto &site : sites)
            {
                float d = std::abs(p.x - site.x) + std::abs(p.y - site.y);
                if (d < minDist)
                    minDist = d;
            }
            m_field_values[idx] = minDist;
        }
    }
}


// Boolean operations
void ScalarField2D::boolean_union(const ScalarField2D& other) {
    if (m_field_values.size() != other.m_field_values.size()) {
        throw std::invalid_argument("Field dimensions must match for boolean operations");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i) {
        m_field_values[i] = std::min(m_field_values[i], other.m_field_values[i]);
    }
}

void ScalarField2D::boolean_intersect(const ScalarField2D& other) {
    if (m_field_values.size() != other.m_field_values.size()) {
        throw std::invalid_argument("Field dimensions must match for boolean operations");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i) {
        m_field_values[i] = std::max(m_field_values[i], other.m_field_values[i]);
    }
}

void ScalarField2D::boolean_inverseintersect(const ScalarField2D &other)
{
    if (m_field_values.size() != other.m_field_values.size())
    {
        throw std::invalid_argument("Field dimensions must match for boolean operations");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i)
    {
        m_field_values[i] = std::min(m_field_values[i], -other.m_field_values[i]);
    }
}

void ScalarField2D::boolean_subtract(const ScalarField2D& other) {
    if (m_field_values.size() != other.m_field_values.size()) {
        throw std::invalid_argument("Field dimensions must match for boolean operations");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i) {
        m_field_values[i] = std::max(m_field_values[i], -other.m_field_values[i]);
    }
}

void ScalarField2D::boolean_smin(const ScalarField2D& other, float smoothing) {
    if (m_field_values.size() != other.m_field_values.size()) {
        throw std::invalid_argument("Field dimensions must match for boolean operations");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i) {
        m_field_values[i] = ScalarFieldUtils::smooth_min(m_field_values[i], other.m_field_values[i], smoothing);
    }
}

void ScalarField2D::boolean_smin_weighted(const ScalarField2D& other, float smoothing, float wt) {
    if (m_field_values.size() != other.m_field_values.size()) {
        throw std::invalid_argument("Field dimensions must match for boolean operations");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i) {
        m_field_values[i] = ScalarFieldUtils::smooth_min_weighted(m_field_values[i], other.m_field_values[i], smoothing, wt);
    }
}

void ScalarField2D::interpolate(const ScalarField2D& other, float t) {
    if (m_field_values.size() != other.m_field_values.size()) {
        throw std::invalid_argument("Field dimensions must match for interpolation");
    }

    for (size_t i = 0; i < m_field_values.size(); ++i) {
        m_field_values[i] = (1.0f - t) * m_field_values[i] + t * other.m_field_values[i];
    }
}

// Rendering methods
void ScalarField2D::draw_points(Renderer& renderer, int step) const {
    // We need to cast away const to normalize - this is a design compromise
    const_cast<ScalarField2D*>(this)->normalize_field();

    for (int j = 0; j < m_res_y; j += step) {
        for (int i = 0; i < m_res_x; i += step) {
            const int idx = get_index(i, j);
            const float f = m_normalized_values[idx];

            float r, g, b;
            // ScalarFieldUtils::get_jet_color(f * 2.0f - 1.0f, r, g, b);
            ScalarFieldUtils::get_hsv_color(f, r, g, b);
            const Color color(r, g, b);

            renderer.drawPoint(m_grid_points[idx], color, 3.0f);
        }
    }
}

void ScalarField2D::draw_values(Renderer& renderer, int step) const {
    for (int j = 0; j < m_res_y; j += step) {
        for (int i = 0; i < m_res_x; i += step) {
            const int idx = get_index(i, j);
            const float value = m_field_values[idx];

            // Draw 3D text showing the scalar value
            const std::string text = std::to_string(value).substr(0, 5); // Limit to 5 characters
            renderer.drawText(text, m_grid_points[idx] + Vec3(0, 0, 0), 0.8f);
        }
    }
}

// Legacy compatibility method for contour drawing
void ScalarField2D::drawIsocontours(Renderer& renderer, float threshold) const {
    GraphObject contours = get_contours(threshold);
    auto data = contours.getGraphData();
    if (!data) {
        return;
    }

    for (const auto& edge : data->edges) {
        if (edge.vertexA < 0 || edge.vertexB < 0 ||
            edge.vertexA >= static_cast<int>(data->vertices.size()) ||
            edge.vertexB >= static_cast<int>(data->vertices.size())) {
            continue;
        }

        const Vec3& start = data->vertices[edge.vertexA].position;
        const Vec3& end = data->vertices[edge.vertexB].position;
        renderer.drawLine(start, end, renderer.getCurrentColor(), 2.0f);
    }
}

// Analysis methods - simplified implementations
GraphObject ScalarField2D::get_contours(float threshold) const {
    GraphObject graph("ScalarFieldContours");
    auto data = graph.getGraphData();
    if (!data) {
        return graph;
    }

    struct VecKey {
        int x;
        int y;
        int z;

        bool operator==(const VecKey& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct VecKeyHash {
        std::size_t operator()(const VecKey& key) const {
            std::size_t h1 = std::hash<int>{}(key.x);
            std::size_t h2 = std::hash<int>{}(key.y);
            std::size_t h3 = std::hash<int>{}(key.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    const float quantizeScale = 10000.0f;
    std::unordered_map<VecKey, int, VecKeyHash> vertexLookup;

    auto getOrCreateVertex = [&](const Vec3& position) -> int {
        VecKey key{
            static_cast<int>(std::round(position.x * quantizeScale)),
            static_cast<int>(std::round(position.y * quantizeScale)),
            static_cast<int>(std::round(position.z * quantizeScale))
        };

        if (auto it = vertexLookup.find(key); it != vertexLookup.end()) {
            return it->second;
        }

        int index = data->addVertex(position);
        vertexLookup.emplace(key, index);
        return index;
    };

    auto addCrossing = [&](float valueA, float valueB, const Vec3& pointA, const Vec3& pointB, std::vector<Vec3>& crossings) {
        if ((valueA < threshold && valueB >= threshold) || (valueB < threshold && valueA >= threshold)) {
            float denom = valueB - valueA;
            float t = (std::abs(denom) > 1e-6f) ? (threshold - valueA) / denom : 0.5f;
            crossings.push_back(Vec3::lerp(pointA, pointB, t));
        }
    };

    for (int j = 0; j < m_res_y - 1; ++j) {
        for (int i = 0; i < m_res_x - 1; ++i) {
            const int idx00 = get_index(i, j);
            const int idx10 = get_index(i + 1, j);
            const int idx01 = get_index(i, j + 1);
            const int idx11 = get_index(i + 1, j + 1);

            const float v00 = m_field_values[idx00];
            const float v10 = m_field_values[idx10];
            const float v01 = m_field_values[idx01];
            const float v11 = m_field_values[idx11];

            std::vector<Vec3> crossings;
            crossings.reserve(4);

            addCrossing(v00, v10, m_grid_points[idx00], m_grid_points[idx10], crossings);
            addCrossing(v10, v11, m_grid_points[idx10], m_grid_points[idx11], crossings);
            addCrossing(v11, v01, m_grid_points[idx11], m_grid_points[idx01], crossings);
            addCrossing(v01, v00, m_grid_points[idx01], m_grid_points[idx00], crossings);

            if (crossings.size() == 2) {
                int vertexA = getOrCreateVertex(crossings[0]);
                int vertexB = getOrCreateVertex(crossings[1]);
                data->addEdge(vertexA, vertexB);
            }
        }
    }

    return graph;
}

std::vector<Vec3> ScalarField2D::get_gradient() const {
    std::vector<Vec3> gradient(m_field_values.size(), Vec3(0, 0, 0));

    for (int j = 1; j < m_res_y - 1; ++j) {
        for (int i = 1; i < m_res_x - 1; ++i) {
            const int idx = get_index(i, j);
            const int idx_left = get_index(i - 1, j);
            const int idx_right = get_index(i + 1, j);
            const int idx_down = get_index(i, j - 1);
            const int idx_up = get_index(i, j + 1);

            const float dx = (m_field_values[idx_right] - m_field_values[idx_left]) * 0.5f;
            const float dy = (m_field_values[idx_up] - m_field_values[idx_down]) * 0.5f;

            gradient[idx] = Vec3(dx, dy, 0.0f);
        }
    }

    return gradient;
}

// Additional helper methods for missing functionality

void ScalarField2D::set_values(const std::vector<float>& values) {
    if (values.size() != m_field_values.size()) {
        throw std::invalid_argument("Value array size must match field resolution");
    }
    m_field_values = values;
}

void ScalarField2D::boolean_difference(const ScalarField2D& other) {
    // Difference is A - B = A ∩ ¬B = max(A, -B)
    boolean_subtract(other);
}






