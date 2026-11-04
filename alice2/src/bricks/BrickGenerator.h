#pragma once

#include <Eigen/Core>
#include <array>
#include <string>
#include <vector>

namespace alice2::bricks {

enum class FieldMethod { Height, ExactGeodesic, ExactBlend };

struct Contours {
    Eigen::VectorXd levels;
    Eigen::MatrixXd vertices;
    Eigen::MatrixXi edges;
    Eigen::VectorXi levelIndex; // One level index per edge; shared topology for later stages.
};

struct BrickOptions {
    double gap = 0.01;
    double depth = 0.05;
    double lengthA = 0.20;
    double lengthB = 0.10;
};

struct ContourPath {
    int levelIndex = -1;
    bool closed = false;
    std::vector<Eigen::Vector3d> points; // Closed paths repeat the seam point.
    double length = 0;
};

struct Brick {
    // Base rectangle 0..3 CCW about normal; top 4..7 = base + depth*normal.
    std::array<Eigen::Vector3d, 8> vertices;
    Eigen::Vector3d origin, tangent, widthDirection, normal;
    Eigen::Vector3d barycentric; // Closest triangle weights at origin.
    double length = 0, width = 0, depth = 0;
    double startDistance = 0, endDistance = 0; // Occupied interval along the path.
    int levelIndex = -1, pathIndex = -1, triangleIndex = -1;
    bool isLengthA = true, trimmed = false;
};

// Mesh -> field -> contours -> Flemish bond bricks (no plane splitting yet).
// No viewer dependency. Inputs and outputs retain original mesh units.
// setMesh preserves vertex IDs; loadObj welds identical positions and returns welded IDs.
class BrickGenerator {
public:
    BrickGenerator();
    ~BrickGenerator();
    BrickGenerator(const BrickGenerator&) = delete;
    BrickGenerator& operator=(const BrickGenerator&) = delete;

    // OBJ supports v x y z r g b (RGB in [0,1] or [0,255]).
    // Polygon faces are fan-triangulated; use triangles or convex planar polygons.
    // Identical positions are welded; color seeds are combined across corner copies.
    void loadObj(const std::string& path);
    void setMesh(const Eigen::MatrixXd& vertices, const Eigen::MatrixXi& triangles);
    void setBoundaryVertices(const std::vector<int>& start, const std::vector<int>& end);
    void computeField(FieldMethod method);
    void extractContours(double spacing);
    // Course centerlines follow contours. A/B alternate with a half-repeat shift
    // on odd levels. Terminal bricks are shortened; closed paths have one seam.
    void generateBricks(const BrickOptions& options);

    const Eigen::MatrixXd& vertices() const { return vertices_; }
    const Eigen::MatrixXi& triangles() const { return triangles_; }
    const std::vector<int>& startVertices() const { return start_; }
    const std::vector<int>& endVertices() const { return end_; }
    const Eigen::VectorXd& field() const { return field_; }
    const Contours& contours() const { return contours_; }
    const std::vector<ContourPath>& contourPaths() const { return paths_; }
    const std::vector<Brick>& bricks() const { return bricks_; }
    // ExactBlend range is the mean of the two opposite-boundary mean distances.
    double blendLength() const { return blendLength_; }

private:
    Eigen::MatrixXd vertices_;
    Eigen::MatrixXi triangles_;
    Eigen::VectorXd field_, fromStart_, fromEnd_;
    std::vector<int> start_, end_;
    Contours contours_;
    std::vector<ContourPath> paths_;
    std::vector<Brick> bricks_;
    double contourSpacing_ = 0;
    double blendLength_ = 0;
};

} // namespace alice2::bricks
