#include <computeGeom/scalarField.h>


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

    initialize_grid();
}

ScalarField2D::ScalarField2D(const ScalarField2D& other)
    : m_min_bounds(other.m_min_bounds), m_max_bounds(other.m_max_bounds)
    , m_res_x(other.m_res_x), m_res_y(other.m_res_y)
    , m_grid_points(other.m_grid_points), m_field_values(other.m_field_values)
    , m_normalized_values(other.m_normalized_values), m_gradient_field(other.m_gradient_field) {
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
    }
    return *this;
}

ScalarField2D::ScalarField2D(ScalarField2D&& other) noexcept
    : m_min_bounds(std::move(other.m_min_bounds)), m_max_bounds(std::move(other.m_max_bounds))
    , m_res_x(other.m_res_x), m_res_y(other.m_res_y)
    , m_grid_points(std::move(other.m_grid_points)), m_field_values(std::move(other.m_field_values))
    , m_normalized_values(std::move(other.m_normalized_values)), m_gradient_field(std::move(other.m_gradient_field)) {
    other.m_res_x = other.m_res_y = 0;
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
        other.m_res_x = other.m_res_y = 0;
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

    // find extrema
    auto [min_it, max_it] = std::minmax_element(m_field_values.begin(),
                                                 m_field_values.end());
    const float min_val = *min_it;
    const float max_val = *max_it;
    const float range   = std::max(max_val - min_val, 1e-6f);

    // do we need a [-1,1] stretch?
    const bool use_bipolar = (min_val < 0.0f);

    for (size_t i = 0, n = m_field_values.size(); i < n; ++i) {
        // first normalize into [0,1]
        float t = (m_field_values[i] - min_val) / range;
        // if any value was negative, remap [0,1] -> [-1,1]
        m_normalized_values[i] = use_bipolar ? (t * 2.0f - 1.0f)
                                              :  t;
    }
}

void ScalarField2D::clear_field() {
    std::fill(m_field_values.begin(), m_field_values.end(), 0.0f);
    std::fill(m_normalized_values.begin(), m_normalized_values.end(), 0.0f);
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
            ScalarFieldUtils::get_jet_color(f * 2.0f - 1.0f, r, g, b);
            const Vec3 color(r, g, b);

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
    const ContourData contours = get_contours(threshold);

    for (const auto& segment : contours.line_segments) {
        renderer.drawLine(segment.first, segment.second, renderer.getCurrentColor(), 2.0f);
    }
}

// Analysis methods - simplified implementations
ContourData ScalarField2D::get_contours(float threshold) const {
    ContourData result(threshold);

    // Simple marching squares implementation for contour extraction
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

            // Check for threshold crossings and create line segments
            std::vector<Vec3> crossings;

            // Check each edge of the quad
            if ((v00 < threshold && v10 >= threshold) || (v10 < threshold && v00 >= threshold)) {
                const float t = (threshold - v00) / (v10 - v00);
                crossings.push_back(Vec3::lerp(m_grid_points[idx00], m_grid_points[idx10], t));
            }
            if ((v10 < threshold && v11 >= threshold) || (v11 < threshold && v10 >= threshold)) {
                const float t = (threshold - v10) / (v11 - v10);
                crossings.push_back(Vec3::lerp(m_grid_points[idx10], m_grid_points[idx11], t));
            }
            if ((v11 < threshold && v01 >= threshold) || (v01 < threshold && v11 >= threshold)) {
                const float t = (threshold - v11) / (v01 - v11);
                crossings.push_back(Vec3::lerp(m_grid_points[idx11], m_grid_points[idx01], t));
            }
            if ((v01 < threshold && v00 >= threshold) || (v00 < threshold && v01 >= threshold)) {
                const float t = (threshold - v01) / (v00 - v01);
                crossings.push_back(Vec3::lerp(m_grid_points[idx01], m_grid_points[idx00], t));
            }

            if (crossings.size() == 2) {
                result.line_segments.emplace_back(crossings[0], crossings[1]);
            }
        }
    }

    return result;
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
}

void ScalarField2D::apply_scalar_polygon(const std::vector<Vec3>& vertices) {
    // Simple polygon SDF - placeholder implementation
    if (vertices.empty()) return;

    for (int j = 0; j < m_res_y; ++j) {
        for (int i = 0; i < m_res_x; ++i) {
            const int idx = get_index(i, j);
            const Vec3& pt = m_grid_points[idx];

            // Find distance to closest vertex (simplified)
            float min_dist = std::numeric_limits<float>::max();
            for (const auto& vertex : vertices) {
                const float dist = ScalarFieldUtils::distance_to(pt, vertex);
                min_dist = std::min(min_dist, dist);
            }

            m_field_values[idx] = min_dist;
        }
    }
}

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
