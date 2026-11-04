#pragma once

#include <Eigen/Core>
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

// Stage 1 of mesh -> field -> contours -> split segments -> bricks.
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

    const Eigen::MatrixXd& vertices() const { return vertices_; }
    const Eigen::MatrixXi& triangles() const { return triangles_; }
    const std::vector<int>& startVertices() const { return start_; }
    const std::vector<int>& endVertices() const { return end_; }
    const Eigen::VectorXd& field() const { return field_; }
    const Contours& contours() const { return contours_; }
    // ExactBlend range is the mean of the two opposite-boundary mean distances.
    double blendLength() const { return blendLength_; }

private:
    Eigen::MatrixXd vertices_;
    Eigen::MatrixXi triangles_;
    Eigen::VectorXd field_, fromStart_, fromEnd_;
    std::vector<int> start_, end_;
    Contours contours_;
    double blendLength_ = 0;
};

} // namespace alice2::bricks
