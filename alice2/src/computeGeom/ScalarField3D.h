#pragma once

#ifndef ALICE2_SCALAR_FIELD_3D_H
#define ALICE2_SCALAR_FIELD_3D_H

#include <vector>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include "../utils/Math.h"

namespace alice2 {

    // Forward declarations
    class Renderer;
    struct MeshData;

    // Marching cubes lookup tables (defined in cpp file)
    extern const int EDGE_TABLE[256];
    extern const int TRI_TABLE[256][16];

    // Vertex classification for extended marching cubes
    enum class VertexClass {
        NEGATIVE = -1,  // value < isolevel
        ZERO = 0,       // value == isolevel (within tolerance)
        POSITIVE = 1    // value > isolevel
    };

    // Grid cell structure for marching cubes
    struct GridCell {
        Vec3 vertices[8];           // 8 corner vertices of the cube
        float values[8];            // Scalar values at each vertex
        alice2::VertexClass classes[8];     // Vertex classifications for extended MC
    };

    // Triangle structure for marching cubes output
    struct MCTriangle {
        Vec3 vertices[3];
        Vec3 normal;
        
        MCTriangle() : normal(0, 0, 1) {}
    };

    /**
     * Modern C++ 3D Scalar Field class with RAII principles
     * Supports dynamic resolution, proper memory management, marching cubes algorithm
     * Follows established codebase patterns from ScalarField2D
     */
    class ScalarField3D {
    private:
        // Grid properties
        Vec3 m_min_bounds;
        Vec3 m_max_bounds;
        int m_res_x;
        int m_res_y;
        int m_res_z;

        // Dynamic data storage
        std::vector<Vec3> m_grid_points;
        std::vector<float> m_field_values;
        std::vector<float> m_normalized_values;

        // Helper methods
        inline int get_index(int x, int y, int z) const {
            return z * (m_res_x * m_res_y) + y * m_res_x + x;
        }

        inline std::tuple<int, int, int> get_coords(int index) const {
            int z = index / (m_res_x * m_res_y);
            int remainder = index % (m_res_x * m_res_y);
            int y = remainder / m_res_x;
            int x = remainder % m_res_x;
            return {x, y, z};
        }

        inline bool is_valid_coords(int x, int y, int z) const {
            return x >= 0 && x < m_res_x && y >= 0 && y < m_res_y && z >= 0 && z < m_res_z;
        }

        bool is_inside_bounds(const Vec3& p) const;
        Vec3 clamp_to_bounds(const Vec3& p) const;
        void initialize_grid();
        void normalize_field();

        // Marching cubes helper methods
        alice2::VertexClass classify_vertex(float value, float isolevel, float tolerance = 1e-6f) const;
        Vec3 vertex_interpolate(float isolevel, const Vec3& p1, const Vec3& p2, float val1, float val2) const;
        Vec3 vertex_interpolate_robust(float isolevel, const Vec3& p1, const Vec3& p2, float val1, float val2) const;
        GridCell get_grid_cell(int x, int y, int z) const;
        int polygonize_cell(const GridCell& cell, float isolevel, std::vector<MCTriangle>& triangles) const;
        bool is_triangle_degenerate(const MCTriangle& triangle, float tolerance = 1e-6f) const;
        bool validate_triangle_quality(const MCTriangle& triangle, float min_area = 1e-8f) const;
        int polygonize_cell_tetra(const GridCell& cell,
                                         float iso,
                                         std::vector<MCTriangle>& tris) const;

    public:
        // Constructor with RAII principles
        ScalarField3D(const Vec3& min_bb = Vec3(-50, -50, -50),
                      const Vec3& max_bb = Vec3(50, 50, 50),
                      int res_x = 50,
                      int res_y = 50,
                      int res_z = 50);

        // Destructor
        ~ScalarField3D() = default;

        // Copy constructor and assignment operator
        ScalarField3D(const ScalarField3D& other);
        ScalarField3D& operator=(const ScalarField3D& other);

        // Move constructor and assignment operator
        ScalarField3D(ScalarField3D&& other) noexcept;
        ScalarField3D& operator=(ScalarField3D&& other) noexcept;

        // Getter/Setter methods
        const std::vector<Vec3>& get_points() const { return m_grid_points; }
        const void set_points(std::vector<Vec3>& grid_points) { m_grid_points = grid_points; }
        const std::vector<float>& get_values() const { return m_field_values; }
        const void set_values(std::vector<float>& field_values) { m_field_values = field_values; }
        void set_values(const std::vector<float>& values);
        std::tuple<int, int, int> get_resolution() const { return {m_res_x, m_res_y, m_res_z}; }
        std::pair<Vec3, Vec3> get_bounds() const { return {m_min_bounds, m_max_bounds}; }
        
        Vec3 cell_position(int x, int y, int z) const;
        float sample_nearest(const Vec3& p) const;
        float sample_trilinear(const Vec3& p) const;
        Vec3 gradient_at(const Vec3& p) const;
        Vec3 gradient_normalized(const Vec3& p) const;
        Vec3 project_onto_isosurface(const Vec3& start, float isoLevel = 0.0f, int maxIterations = 8, float tolerance = 1e-4f) const;
        Vec3 get_cell_size() const;
        float value_at(const Vec3& p) const;
        bool contains_point(const Vec3& p) const;

        // Field generation methods (snake_case naming)
        void clear_field();
        float get_scalar_sphere(const Vec3& center, float radius) const;
        float get_scalar_box(const Vec3& center, const Vec3& half_size) const;
        float get_scalar_torus(const Vec3& center, float major_radius, float minor_radius) const;
        float get_scalar_plane(const Vec3& point, const Vec3& normal) const;

        // Apply scalar functions to entire field
        void apply_scalar_sphere(const Vec3& center, float radius);
        void apply_scalar_box(const Vec3& center, const Vec3& half_size);
        void apply_scalar_torus(const Vec3& center, float major_radius, float minor_radius);
        void apply_scalar_plane(const Vec3& point, const Vec3& normal);
        void apply_scalar_noise(float frequency = 0.1f, float amplitude = 1.0f);

        // Boolean operations (snake_case naming)
        void boolean_union(const ScalarField3D& other);
        void boolean_intersect(const ScalarField3D& other);
        void boolean_subtract(const ScalarField3D& other);
        void boolean_smin(const ScalarField3D& other, float smoothing = 1.0f);

        // Marching cubes mesh generation
        std::shared_ptr<MeshData> generate_mesh(float isolevel = 0.0f) const;
        std::vector<MCTriangle> extract_triangles(float isolevel = 0.0f) const;

        // Rendering methods
        void draw_points(Renderer& renderer, int step = 4) const;
        void draw_values(Renderer& renderer, int step = 8) const;
        void draw_slice(Renderer& renderer, int z_slice, float point_size = 2.0f) const;

        // Tetra
        int polygonize_tetra(const Vec3 p[4], const float val[4], float iso, std::vector<MCTriangle> &out) const;
    };

} // namespace alice2

#endif // ALICE2_SCALAR_FIELD_3D_H

