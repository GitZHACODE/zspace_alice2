#include "BrickGenerator.h"
#include <Eigen/Geometry>
#include <igl/AABB.h>
#include <igl/per_vertex_normals.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace alice2::bricks {
namespace {
// Each edge is visited once. Split naturally at branches rather than joining
// unrelated contour arms; level IDs preserve course parity across components.
std::vector<ContourPath> orderPaths(const Contours& c) {
    std::vector<ContourPath> paths;
    std::map<int, std::map<int, std::vector<int>>> adjacency;
    for (int e = 0; e < c.edges.rows(); ++e) for (int k = 0; k < 2; ++k)
        adjacency[c.levelIndex(e)][c.edges(e,k)].push_back(e);
    std::vector<bool> used(c.edges.rows(), false);
    const auto less = [&](int a, int b) {
        for (int k = 0; k < 3; ++k) {
            if (c.vertices(a,k) < c.vertices(b,k)) return true;
            if (c.vertices(a,k) > c.vertices(b,k)) return false;
        }
        return a < b;
    };
    for (auto& [level, graph] : adjacency) {
        std::vector<int> starts;
        for (const auto& [v, edges] : graph) starts.push_back(v);
        std::sort(starts.begin(), starts.end(), less);
        // Neighbor order gives closed contours a repeatable seam direction.
        for (auto& [v, edges] : graph) std::sort(edges.begin(), edges.end(), [&](int a, int b) {
            const int va = c.edges(a,0) == v ? c.edges(a,1) : c.edges(a,0);
            const int vb = c.edges(b,0) == v ? c.edges(b,1) : c.edges(b,0);
            return less(va,vb);
        });
        const auto walk = [&](int start, int firstEdge) {
            ContourPath path; path.levelIndex = level;
            int current = start, edge = firstEdge;
            path.points.push_back(c.vertices.row(current));
            while (!used[edge]) {
                used[edge] = true;
                current = c.edges(edge,0) == current ? c.edges(edge,1) : c.edges(edge,0);
                const Eigen::Vector3d next = c.vertices.row(current);
                const double step = (next-path.points.back()).norm();
                if (step > 0) { path.length += step; path.points.push_back(next); }
                if (current == start) { path.closed = true; break; }
                const auto& incident = graph.at(current);
                if (incident.size() != 2) break;
                const int following = used[incident[0]] ? incident[1] : incident[0];
                if (used[following]) break;
                edge = following;
            }
            if (path.points.size() >= 2 && path.length > 0) paths.push_back(std::move(path));
        };
        // Open paths begin at the lexicographically smallest available endpoint.
        for (int v : starts) if (graph.at(v).size() != 2)
            for (int edge : graph.at(v)) if (!used[edge]) walk(v,edge);
        for (int v : starts) for (int edge : graph.at(v)) if (!used[edge]) walk(v,edge);
    }
    return paths;
}

Eigen::Vector3d sample(const ContourPath& path, const std::vector<double>& arc, double distance) {
    distance = std::clamp(distance, 0.0, arc.back());
    const auto next = std::upper_bound(arc.begin(), arc.end(), distance);
    const size_t i = std::min(static_cast<size_t>(next-arc.begin()), arc.size()-1);
    if (i == 0) return path.points.front();
    const double t = (distance-arc[i-1])/(arc[i]-arc[i-1]);
    return (1-t)*path.points[i-1]+t*path.points[i];
}
}

void BrickGenerator::generateBricks(const BrickOptions& options) {
    bricks_.clear(); paths_.clear();
    if (contourSpacing_ <= 0) throw std::runtime_error("Extract contours before generating bricks.");
    for (double value : {options.gap, options.depth, options.lengthA, options.lengthB})
        if (!std::isfinite(value) || value <= 0) throw std::runtime_error("Brick dimensions and gap must be positive and finite.");
    if (options.gap >= contourSpacing_) throw std::runtime_error("Gap must be smaller than contour spacing.");
    paths_ = orderPaths(contours_);
    if (paths_.empty()) return;

    igl::AABB<Eigen::MatrixXd,3> tree;
    tree.init(vertices_, triangles_);
    Eigen::MatrixXd normals;
    // Area-weighted vertex normals, then barycentric interpolation at closest point.
    igl::per_vertex_normals(vertices_, triangles_, igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_AREA, normals);
    std::vector<Brick> result;
    const double width = contourSpacing_ - options.gap;
    const double repeat = options.lengthA + options.lengthB + 2*options.gap;
    const double epsilon = std::min({options.lengthA,options.lengthB,width,options.depth})*1e-8;
    constexpr size_t maxBricks = 250000;
    for (int pi = 0; pi < static_cast<int>(paths_.size()); ++pi) {
        const auto& path = paths_[pi];
        std::vector<double> arc{0};
        for (size_t i = 1; i < path.points.size(); ++i)
            arc.push_back(arc.back()+(path.points[i]-path.points[i-1]).norm());
        const double phase = path.levelIndex % 2 ? repeat/2 : 0;
        // Gap is shared equally between cells, including the end/seam margin.
        // Odd courses begin with the clipped tail of the previous repeat.
        double cellStart = -phase;
        bool isA = true;
        while (cellStart < path.length) {
            const double nominal = isA ? options.lengthA : options.lengthB;
            const double begin = std::max(options.gap/2, cellStart+options.gap/2);
            const double end = std::min(path.length-options.gap/2, cellStart+options.gap/2+nominal);
            if (end-begin > epsilon) {
                if (result.size() >= maxBricks) throw std::runtime_error("More than 250000 bricks; increase dimensions or contour spacing.");
                Brick brick;
                brick.length = end-begin; brick.width = width; brick.depth = options.depth;
                brick.startDistance = begin; brick.endDistance = end;
                brick.levelIndex = path.levelIndex; brick.pathIndex = pi; brick.isLengthA = isA;
                brick.trimmed = brick.length < nominal-epsilon;
                const double middle = (begin+end)/2;
                const Eigen::RowVector3d query = sample(path,arc,middle);
                Eigen::RowVector3d closest;
                tree.squared_distance(vertices_,triangles_,query,brick.triangleIndex,closest);
                brick.origin = closest;
                const int face = brick.triangleIndex;
                if (face < 0) throw std::runtime_error("Cannot find closest mesh triangle for brick.");
                const Eigen::Vector3d a = vertices_.row(triangles_(face,0));
                const Eigen::Vector3d e1 = vertices_.row(triangles_(face,1))-a.transpose();
                const Eigen::Vector3d e2 = vertices_.row(triangles_(face,2))-a.transpose();
                const Eigen::Vector3d delta = brick.origin-a;
                const double d00=e1.dot(e1), d01=e1.dot(e2), d11=e2.dot(e2);
                const double determinant = d00*d11-d01*d01;
                brick.barycentric.y() = (d11*delta.dot(e1)-d01*delta.dot(e2))/determinant;
                brick.barycentric.z() = (d00*delta.dot(e2)-d01*delta.dot(e1))/determinant;
                brick.barycentric.x() = 1-brick.barycentric.y()-brick.barycentric.z();
                brick.normal.setZero();
                for (int k=0; k<3; ++k) brick.normal += brick.barycentric(k)*normals.row(triangles_(face,k)).transpose();
                if (brick.normal.norm() < 1e-12) brick.normal = e1.cross(e2);
                brick.normal.normalize();
                const double probe = std::min(brick.length/4, contourSpacing_/4);
                brick.tangent = sample(path,arc,middle+probe)-sample(path,arc,middle-probe);
                brick.tangent -= brick.normal*brick.normal.dot(brick.tangent);
                if (brick.tangent.norm() < epsilon) throw std::runtime_error("Contour tangent vanished at a brick; check contour branches or mesh folds.");
                brick.tangent.normalize();
                brick.widthDirection = brick.normal.cross(brick.tangent).normalized();
                const Eigen::Vector3d x = brick.tangent*brick.length/2;
                const Eigen::Vector3d y = brick.widthDirection*width/2;
                brick.vertices[0] = brick.origin-x-y;
                brick.vertices[1] = brick.origin+x-y;
                brick.vertices[2] = brick.origin+x+y;
                brick.vertices[3] = brick.origin-x+y;
                for (int k=0; k<4; ++k) brick.vertices[k+4] = brick.vertices[k]+options.depth*brick.normal;
                result.push_back(std::move(brick));
            }
            const double next = cellStart+nominal+options.gap;
            if (next <= cellStart) throw std::runtime_error("Brick length is too small for this mesh scale.");
            cellStart = next; isA = !isA;
        }
    }
    bricks_ = std::move(result);
}
} // namespace alice2::bricks
