#include "BrickGenerator.h"
#include <Eigen/Geometry>

#include <igl/exact_geodesic.h>
#include <igl/isolines.h>
#include <igl/readOBJ.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

namespace alice2::bricks {
BrickGenerator::BrickGenerator() = default;
BrickGenerator::~BrickGenerator() = default;

void BrickGenerator::setMesh(const Eigen::MatrixXd& v, const Eigen::MatrixXi& f) {
    if (v.cols() != 3 || v.rows() < 3 || !v.allFinite() || f.cols() != 3 || f.rows() == 0)
        throw std::runtime_error("Expected finite 3D vertices and nonempty triangle faces.");
    const double scale = (v.colwise().maxCoeff() - v.colwise().minCoeff()).norm();
    if (!std::isfinite(scale) || scale <= 0) throw std::runtime_error("Mesh has zero extent.");
    std::vector<std::vector<int>> adjacent(v.rows());
    std::map<std::pair<int,int>, int> incidence;
    std::set<std::vector<int>> uniqueFaces;
    for (int i = 0; i < f.rows(); ++i) {
        std::vector<int> ids{f(i,0), f(i,1), f(i,2)};
        for (int id : ids) if (id < 0 || id >= v.rows()) throw std::runtime_error("Face index out of range.");
        std::sort(ids.begin(), ids.end());
        if (!uniqueFaces.insert(ids).second) throw std::runtime_error("Duplicate triangle.");
        const Eigen::Vector3d a = v.row(f(i,1)) - v.row(f(i,0));
        const Eigen::Vector3d b = v.row(f(i,2)) - v.row(f(i,0));
        if (a.cross(b).norm() <= scale * scale * 1e-14) throw std::runtime_error("Degenerate triangle.");
        for (int j = 0; j < 3; ++j) {
            int x = f(i,j), y = f(i,(j+1)%3);
            adjacent[x].push_back(y); adjacent[y].push_back(x);
            if (++incidence[std::minmax(x,y)] > 2) throw std::runtime_error("Non-manifold edge.");
        }
    }
    std::vector<bool> seen(v.rows(), false);
    std::queue<int> pending; pending.push(0); seen[0] = true;
    while (!pending.empty()) {
        int x = pending.front(); pending.pop();
        for (int y : adjacent[x]) if (!seen[y]) { seen[y] = true; pending.push(y); }
    }
    if (std::find(seen.begin(), seen.end(), false) != seen.end())
        throw std::runtime_error("Use one connected mesh without unused vertices.");
    vertices_ = v; triangles_ = f;
    start_.clear(); end_.clear(); field_.resize(0); fromStart_.resize(0); fromEnd_.resize(0);
    contours_ = {}; paths_.clear(); bricks_.clear(); contourSpacing_ = 0; blendLength_ = 0;
}

void BrickGenerator::loadObj(const std::string& path) {
    std::vector<std::vector<double>> v, uv, normals;
    std::vector<std::vector<int>> faces, ft, fn;
    if (!igl::readOBJ(path, v, uv, normals, faces, ft, fn))
        throw std::runtime_error("Cannot read OBJ: " + path);
    // Maya face-vertex-color exports repeat positions per polygon corner.
    // Reconstruct shared topology before validation and geodesic solving.
    std::map<std::array<double,3>, int> welded;
    std::vector<std::array<double,3>> uniquePoints;
    std::vector<int> vertexMap(v.size());
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i].size() < 3) throw std::runtime_error("Invalid OBJ vertex.");
        const std::array<double,3> p{v[i][0], v[i][1], v[i][2]};
        for (double value : p) if (!std::isfinite(value)) throw std::runtime_error("Non-finite OBJ vertex.");
        const auto [entry, inserted] = welded.emplace(p, static_cast<int>(uniquePoints.size()));
        if (inserted) uniquePoints.push_back(p);
        vertexMap[i] = entry->second;
    }
    Eigen::MatrixXd points(uniquePoints.size(), 3);
    for (int i = 0; i < points.rows(); ++i) for (int j = 0; j < 3; ++j) points(i,j) = uniquePoints[i][j];
    std::vector<Eigen::Vector3i> tris;
    for (const auto& face : faces) {
        if (face.size() < 3) throw std::runtime_error("Invalid OBJ face.");
        for (int id : face) if (id < 0 || id >= static_cast<int>(vertexMap.size())) throw std::runtime_error("OBJ face index out of range.");
        for (size_t j = 1; j + 1 < face.size(); ++j)
            tris.emplace_back(vertexMap[face[0]], vertexMap[face[j]], vertexMap[face[j+1]]);
    }
    Eigen::MatrixXi triangles(tris.size(), 3);
    for (int i = 0; i < static_cast<int>(tris.size()); ++i) triangles.row(i) = tris[i];
    std::ifstream input(path);
    std::string line, tag;
    std::vector<int> start, end;
    int index = 0;
    while (std::getline(input, line)) {
        std::istringstream stream(line); stream >> tag;
        if (tag != "v") continue;
        double x, y, z, r, g, b;
        stream >> x >> y >> z;
        if (stream >> r >> g >> b) {
            const double maximum = std::max({r,g,b});
            if (maximum > 1) { r /= 255; g /= 255; b /= 255; }
            if (r >= 0.7 && g <= 0.3 && b <= 0.3) start.push_back(vertexMap.at(index));
            if (b >= 0.7 && r <= 0.3 && g <= 0.3) end.push_back(vertexMap.at(index));
        }
        ++index;
    }
    // Validate in a temporary object so a failed reload keeps the previous mesh.
    BrickGenerator loaded;
    loaded.setMesh(points, triangles);
    loaded.setBoundaryVertices(start, end);
    std::swap(vertices_, loaded.vertices_); std::swap(triangles_, loaded.triangles_);
    std::swap(start_, loaded.start_); std::swap(end_, loaded.end_);
    field_.resize(0); fromStart_.resize(0); fromEnd_.resize(0);
    contours_ = {}; paths_.clear(); bricks_.clear(); contourSpacing_ = 0; blendLength_ = 0;
}

void BrickGenerator::setBoundaryVertices(const std::vector<int>& start, const std::vector<int>& end) {
    std::set<int> s(start.begin(), start.end()), e(end.begin(), end.end());
    for (const auto* set : {&s, &e}) for (int id : *set)
        if (id < 0 || id >= vertices_.rows()) throw std::runtime_error("Boundary vertex index out of range.");
    for (int id : s) if (e.count(id)) throw std::runtime_error("Start and end sets overlap.");
    start_.assign(s.begin(), s.end()); end_.assign(e.begin(), e.end());
    fromStart_.resize(0); fromEnd_.resize(0); field_.resize(0);
    contours_ = {}; paths_.clear(); bricks_.clear(); contourSpacing_ = 0; blendLength_ = 0;
}

void BrickGenerator::computeField(FieldMethod method) {
    field_.resize(0); contours_ = {}; paths_.clear(); bricks_.clear(); contourSpacing_ = 0;
    if (vertices_.rows() == 0) throw std::runtime_error("Load a mesh first.");
    if (method == FieldMethod::Height) { field_ = vertices_.col(2).array() - vertices_.col(2).minCoeff(); return; }
    if (method != FieldMethod::ExactBlend && method != FieldMethod::ExactGeodesic)
        throw std::runtime_error("Unknown field method.");
    if (start_.empty()) throw std::runtime_error("Geodesic fields need red start vertices (OBJ vertex RGB).");
    if (method == FieldMethod::ExactBlend && end_.empty()) throw std::runtime_error("Exact blend needs blue end vertices too.");
    const auto solve = [&](const std::vector<int>& ids, Eigen::VectorXd& cachedDistances) {
        if (!cachedDistances.size()) {
            Eigen::VectorXi sources(ids.size());
            for (int i = 0; i < sources.size(); ++i) sources(i) = ids[i];
            const Eigen::VectorXi targets = Eigen::VectorXi::LinSpaced(static_cast<int>(vertices_.rows()), 0,
                                                                     static_cast<int>(vertices_.rows())-1);
            const Eigen::VectorXi noFaces;
            Eigen::VectorXd distances;
            igl::exact_geodesic(vertices_, triangles_, sources, noFaces, targets, noFaces, distances);
            if (distances.size() != vertices_.rows() || !distances.allFinite() ||
                distances.minCoeff() < 0 || distances.maxCoeff() >= 1e99)
                throw std::runtime_error("Exact geodesic failed or returned unreachable vertices.");
            for (int id : ids) distances(id) = 0;
            cachedDistances = std::move(distances);
        }
    };
    solve(start_, fromStart_);
    if (method == FieldMethod::ExactGeodesic) { field_ = fromStart_; return; }
    solve(end_, fromEnd_);
    double length = 0;
    for (int id : end_) length += fromStart_(id) / end_.size();
    for (int id : start_) length += fromEnd_(id) / start_.size();
    blendLength_ = length * 0.5;
    if (blendLength_ <= 0) throw std::runtime_error("Start and end have zero geodesic separation.");
    Eigen::VectorXd blend(vertices_.rows());
    for (int i = 0; i < blend.size(); ++i) {
        const double sum = fromStart_(i) + fromEnd_(i);
        if (sum <= blendLength_ * 1e-12) throw std::runtime_error("Exact blend denominator vanished; check seed separation/mesh quality.");
        blend(i) = blendLength_ * fromStart_(i) / sum;
    }
    for (int id : start_) blend(id) = 0;
    for (int id : end_) blend(id) = blendLength_;
    field_ = std::move(blend);
}

void BrickGenerator::extractContours(double spacing) {
    contours_ = {}; paths_.clear(); bricks_.clear(); contourSpacing_ = 0;
    if (field_.size() == 0) throw std::runtime_error("Compute a field first.");
    if (!std::isfinite(spacing) || spacing <= 0) throw std::runtime_error("Spacing must be positive and finite.");
    const double low = field_.minCoeff(), high = field_.maxCoeff();
    // Interior contours only; marked seed boundaries are rendered separately.
    const double count = std::ceil((high - low) / spacing) - 1;
    if (count > 2000) throw std::runtime_error("More than 2000 contour levels; increase spacing.");
    const int n = static_cast<int>(std::max(0.0, count));
    contours_.levels.resize(n);
    for (int i = 0; i < n; ++i) contours_.levels(i) = low + (i+1) * spacing;
    if (n) igl::isolines(vertices_, triangles_, field_, contours_.levels,
                         contours_.vertices, contours_.edges, contours_.levelIndex);
    contourSpacing_ = spacing;
}
} // namespace alice2::bricks
