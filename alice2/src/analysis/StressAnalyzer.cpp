#include "StressAnalyzer.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace alice2 {

namespace {
    static constexpr double kStressEpsilon = 1e-12;

    static Eigen::Vector2d toEigen2(const Vec3& v) {
        return Eigen::Vector2d(v.x, v.y);
    }

    static Vec3 toVec3XY(const Eigen::Vector2d& v) {
        return Vec3(static_cast<float>(v.x()), static_cast<float>(v.y()), 0.0f);
    }

    static Vec3 faceCenter(const MeshData& data, const MeshFace& face) {
        Vec3 center;
        if (face.vertices.empty()) return center;
        for (int id : face.vertices) {
            if (id >= 0 && id < static_cast<int>(data.vertices.size())) center += data.vertices[id].position;
        }
        return center / static_cast<float>(face.vertices.size());
    }

    static bool trianglePlaneStress(const MeshData& data,
                                    const MeshFace& face,
                                    const Eigen::VectorXd& u,
                                    FaceStressTensor& result) {
        if (face.vertices.size() != 3) return false;
        int i0 = face.vertices[0];
        int i1 = face.vertices[1];
        int i2 = face.vertices[2];
        if (i0 < 0 || i1 < 0 || i2 < 0) return false;
        if (i0 >= static_cast<int>(data.vertices.size()) ||
            i1 >= static_cast<int>(data.vertices.size()) ||
            i2 >= static_cast<int>(data.vertices.size())) return false;

        Eigen::Vector2d p0 = toEigen2(data.vertices[i0].position);
        Eigen::Vector2d p1 = toEigen2(data.vertices[i1].position);
        Eigen::Vector2d p2 = toEigen2(data.vertices[i2].position);
        double area2 = (p1.x() - p0.x()) * (p2.y() - p0.y()) - (p2.x() - p0.x()) * (p1.y() - p0.y());
        double area = 0.5 * std::abs(area2);
        if (area <= kStressEpsilon) return false;

        double b0 = p1.y() - p2.y();
        double b1 = p2.y() - p0.y();
        double b2 = p0.y() - p1.y();
        double c0 = p2.x() - p1.x();
        double c1 = p0.x() - p2.x();
        double c2 = p1.x() - p0.x();

        Eigen::Matrix<double, 3, 6> B;
        B << b0, 0.0, b1, 0.0, b2, 0.0,
             0.0, c0, 0.0, c1, 0.0, c2,
             c0, b0, c1, b1, c2, b2;
        B /= (2.0 * area);

        constexpr double youngModulus = 1.0;
        constexpr double poissonRatio = 0.3;
        Eigen::Matrix3d D;
        D << 1.0, poissonRatio, 0.0,
             poissonRatio, 1.0, 0.0,
             0.0, 0.0, (1.0 - poissonRatio) * 0.5;
        D *= youngModulus / (1.0 - poissonRatio * poissonRatio);

        Eigen::Matrix<double, 6, 1> ue;
        int ids[3] = {i0, i1, i2};
        for (int i = 0; i < 3; ++i) {
            ue(2 * i) = u(2 * ids[i]);
            ue(2 * i + 1) = u(2 * ids[i] + 1);
        }

        Eigen::Vector3d stress = D * B * ue;
        result.xx = static_cast<float>(stress.x());
        result.yy = static_cast<float>(stress.y());
        result.xy = static_cast<float>(stress.z());

        Eigen::Matrix2d tensor;
        tensor << stress.x(), stress.z(), stress.z(), stress.y();
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> eig(tensor);
        if (eig.info() != Eigen::Success) return false;

        result.minorValue = static_cast<float>(eig.eigenvalues()(0));
        result.majorValue = static_cast<float>(eig.eigenvalues()(1));
        result.minorDirection = toVec3XY(eig.eigenvectors().col(0).normalized()).normalized();
        result.majorDirection = toVec3XY(eig.eigenvectors().col(1).normalized()).normalized();
        result.magnitude = static_cast<float>(std::sqrt(stress.x() * stress.x() + stress.y() * stress.y() + 2.0 * stress.z() * stress.z()));
        return true;
    }

    static Color lerpColor(const Color& a, const Color& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color(a.r + (b.r - a.r) * t,
                     a.g + (b.g - a.g) * t,
                     a.b + (b.b - a.b) * t,
                     a.a + (b.a - a.a) * t);
    }

    static std::pair<int, int> sortedEdge(int a, int b) {
        return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    static bool raySegmentIntersectionXY(const Vec3& origin,
                                         const Vec3& direction,
                                         const Vec3& a,
                                         const Vec3& b,
                                         float& rayT) {
        Vec3 edge = b - a;
        float det = direction.x * (-edge.y) - direction.y * (-edge.x);
        if (std::abs(det) <= 1e-8f) return false;

        Vec3 delta = a - origin;
        float t = (delta.x * (-edge.y) - delta.y * (-edge.x)) / det;
        float u = (direction.x * delta.y - direction.y * delta.x) / det;
        if (t <= 1e-5f || u < -1e-4f || u > 1.0001f) return false;
        rayT = t;
        return true;
    }

    static void appendPolylineSegments(const std::vector<Vec3>& polyline, std::vector<Vec3>& segments) {
        if (polyline.size() < 2) return;
        segments.reserve(segments.size() + (polyline.size() - 1) * 2);
        for (size_t i = 0; i + 1 < polyline.size(); ++i) {
            segments.push_back(polyline[i]);
            segments.push_back(polyline[i + 1]);
        }
    }
}

    void StressAnalyzer::setFixedVertices(const std::vector<int>& vertexIds) {
        fixedVertices_ = vertexIds;
        std::sort(fixedVertices_.begin(), fixedVertices_.end());
        fixedVertices_.erase(std::unique(fixedVertices_.begin(), fixedVertices_.end()), fixedVertices_.end());
    }

    void StressAnalyzer::setForce(int vertexId, Vec3 force) {
        forces_[vertexId] = force;
    }

    void StressAnalyzer::setForces(const std::vector<int>& vertexIds, Vec3 force) {
        for (int id : vertexIds) setForce(id, force);
    }

    void StressAnalyzer::clearBoundaryConditions() {
        fixedVertices_.clear();
    }

    void StressAnalyzer::clearForces() {
        forces_.clear();
    }

    void StressAnalyzer::setFieldSmoothingIterations(int iterations) {
        fieldSmoothingIterations_ = std::max(0, iterations);
    }

    void StressAnalyzer::setStressMagnitudeThreshold(double threshold) {
        stressMagnitudeThreshold_ = std::max(0.0, threshold);
    }

    std::vector<Vec3> StressAnalyzer::getPrincipalStressDirections() const {
        return stressField_.majorDirections();
    }

    std::vector<float> StressAnalyzer::getStressMagnitudes() const {
        return stressField_.magnitudes();
    }

    bool StressAnalyzer::solveLinearPlaneStress(const MeshObject& mesh) {
        displacements_.clear();
        stressField_.clear();
        smoothedCrossField_.clear();

        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return false;

        const int vertexCount = static_cast<int>(data->vertices.size());
        const int dofCount = vertexCount * 2;
        std::vector<Eigen::Triplet<double>> triplets;
        triplets.reserve(data->faces.size() * 36);

        constexpr double youngModulus = 1.0;
        constexpr double poissonRatio = 0.3;
        Eigen::Matrix3d D;
        D << 1.0, poissonRatio, 0.0,
             poissonRatio, 1.0, 0.0,
             0.0, 0.0, (1.0 - poissonRatio) * 0.5;
        D *= youngModulus / (1.0 - poissonRatio * poissonRatio);

        for (const MeshFace& face : data->faces) {
            if (face.vertices.size() != 3) continue;
            int ids[3] = {face.vertices[0], face.vertices[1], face.vertices[2]};
            bool valid = true;
            for (int id : ids) {
                if (id < 0 || id >= vertexCount) valid = false;
            }
            if (!valid) continue;

            Eigen::Vector2d p0 = toEigen2(data->vertices[ids[0]].position);
            Eigen::Vector2d p1 = toEigen2(data->vertices[ids[1]].position);
            Eigen::Vector2d p2 = toEigen2(data->vertices[ids[2]].position);
            double area2 = (p1.x() - p0.x()) * (p2.y() - p0.y()) - (p2.x() - p0.x()) * (p1.y() - p0.y());
            double area = 0.5 * std::abs(area2);
            if (area <= kStressEpsilon) continue;

            double b0 = p1.y() - p2.y();
            double b1 = p2.y() - p0.y();
            double b2 = p0.y() - p1.y();
            double c0 = p2.x() - p1.x();
            double c1 = p0.x() - p2.x();
            double c2 = p1.x() - p0.x();

            Eigen::Matrix<double, 3, 6> B;
            B << b0, 0.0, b1, 0.0, b2, 0.0,
                 0.0, c0, 0.0, c1, 0.0, c2,
                 c0, b0, c1, b1, c2, b2;
            B /= (2.0 * area);
            Eigen::Matrix<double, 6, 6> Ke = B.transpose() * D * B * area;
            int dofs[6] = {2 * ids[0], 2 * ids[0] + 1, 2 * ids[1], 2 * ids[1] + 1, 2 * ids[2], 2 * ids[2] + 1};
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) triplets.emplace_back(dofs[r], dofs[c], Ke(r, c));
            }
        }

        Eigen::SparseMatrix<double> K(dofCount, dofCount);
        K.setFromTriplets(triplets.begin(), triplets.end());

        Eigen::VectorXd f = Eigen::VectorXd::Zero(dofCount);
        for (const auto& item : forces_) {
            int id = item.first;
            if (id < 0 || id >= vertexCount) continue;
            f(2 * id) += item.second.x;
            f(2 * id + 1) += item.second.y;
        }

        std::vector<char> fixed(dofCount, 0);
        for (int id : fixedVertices_) {
            if (id < 0 || id >= vertexCount) continue;
            fixed[2 * id] = 1;
            fixed[2 * id + 1] = 1;
        }

        std::vector<int> freeDofs;
        for (int i = 0; i < dofCount; ++i) {
            if (!fixed[i]) freeDofs.push_back(i);
        }
        if (freeDofs.empty()) return false;

        std::vector<int> reducedIndex(dofCount, -1);
        for (int i = 0; i < static_cast<int>(freeDofs.size()); ++i) reducedIndex[freeDofs[i]] = i;

        std::vector<Eigen::Triplet<double>> reducedTriplets;
        for (int outer = 0; outer < K.outerSize(); ++outer) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(K, outer); it; ++it) {
                int r = reducedIndex[it.row()];
                int c = reducedIndex[it.col()];
                if (r >= 0 && c >= 0) reducedTriplets.emplace_back(r, c, it.value());
            }
        }

        Eigen::SparseMatrix<double> Kr(static_cast<int>(freeDofs.size()), static_cast<int>(freeDofs.size()));
        Kr.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
        Eigen::VectorXd fr(static_cast<int>(freeDofs.size()));
        for (int i = 0; i < static_cast<int>(freeDofs.size()); ++i) fr(i) = f(freeDofs[i]);

        Eigen::SparseLU<Eigen::SparseMatrix<double>> solver;
        solver.analyzePattern(Kr);
        solver.factorize(Kr);
        if (solver.info() != Eigen::Success) return false;

        Eigen::VectorXd ur = solver.solve(fr);
        if (solver.info() != Eigen::Success) return false;

        Eigen::VectorXd u = Eigen::VectorXd::Zero(dofCount);
        for (int i = 0; i < static_cast<int>(freeDofs.size()); ++i) u(freeDofs[i]) = ur(i);

        displacements_.resize(vertexCount);
        for (int i = 0; i < vertexCount; ++i) {
            displacements_[i] = Vec3(static_cast<float>(u(2 * i)), static_cast<float>(u(2 * i + 1)), 0.0f);
        }

        stressField_.resize(data->faces.size());
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            FaceStressTensor tensor;
            if (trianglePlaneStress(*data, data->faces[fi], u, tensor)) stressField_[fi] = tensor;
        }

        smoothStressField(*data);
        return true;
    }

    bool StressAnalyzer::solveVerticalSlab(const MeshObject& mesh) {
        displacements_.clear();
        stressField_.clear();
        smoothedCrossField_.clear();

        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return false;

        const int vertexCount = static_cast<int>(data->vertices.size());
        std::set<std::pair<int, int>> edges;
        for (const MeshFace& face : data->faces) {
            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % face.vertices.size()];
                if (a < 0 || b < 0 || a >= vertexCount || b >= vertexCount || a == b) continue;
                if (a > b) std::swap(a, b);
                edges.insert({a, b});
            }
        }
        if (edges.empty()) return false;

        std::vector<Eigen::Triplet<double>> triplets;
        for (const auto& edge : edges) {
            const Vec3& pa = data->vertices[edge.first].position;
            const Vec3& pb = data->vertices[edge.second].position;
            double k = 1.0 / std::max(1e-6, static_cast<double>((pb - pa).length()));
            triplets.emplace_back(edge.first, edge.first, k);
            triplets.emplace_back(edge.second, edge.second, k);
            triplets.emplace_back(edge.first, edge.second, -k);
            triplets.emplace_back(edge.second, edge.first, -k);
        }

        Eigen::SparseMatrix<double> K(vertexCount, vertexCount);
        K.setFromTriplets(triplets.begin(), triplets.end());

        Eigen::VectorXd f = Eigen::VectorXd::Zero(vertexCount);
        for (const auto& item : forces_) {
            int id = item.first;
            if (id >= 0 && id < vertexCount) f(id) += item.second.z;
        }

        std::vector<char> fixed(vertexCount, 0);
        for (int id : fixedVertices_) {
            if (id >= 0 && id < vertexCount) fixed[id] = 1;
        }

        std::vector<int> freeDofs;
        for (int i = 0; i < vertexCount; ++i) {
            if (!fixed[i]) freeDofs.push_back(i);
        }
        if (freeDofs.empty()) return false;

        std::vector<int> reducedIndex(vertexCount, -1);
        for (int i = 0; i < static_cast<int>(freeDofs.size()); ++i) reducedIndex[freeDofs[i]] = i;

        std::vector<Eigen::Triplet<double>> reducedTriplets;
        for (int outer = 0; outer < K.outerSize(); ++outer) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(K, outer); it; ++it) {
                int r = reducedIndex[it.row()];
                int c = reducedIndex[it.col()];
                if (r >= 0 && c >= 0) reducedTriplets.emplace_back(r, c, it.value());
            }
        }

        Eigen::SparseMatrix<double> Kr(static_cast<int>(freeDofs.size()), static_cast<int>(freeDofs.size()));
        Kr.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
        Eigen::VectorXd fr(static_cast<int>(freeDofs.size()));
        for (int i = 0; i < static_cast<int>(freeDofs.size()); ++i) fr(i) = f(freeDofs[i]);

        Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
        solver.compute(Kr);
        if (solver.info() != Eigen::Success) return false;
        Eigen::VectorXd wr = solver.solve(fr);
        if (solver.info() != Eigen::Success) return false;

        Eigen::VectorXd w = Eigen::VectorXd::Zero(vertexCount);
        for (int i = 0; i < static_cast<int>(freeDofs.size()); ++i) w(freeDofs[i]) = wr(i);

        displacements_.resize(vertexCount);
        for (int i = 0; i < vertexCount; ++i) displacements_[i] = Vec3(0.0f, 0.0f, static_cast<float>(w(i)));

        stressField_.resize(data->faces.size());
        for (int fi = 0; fi < static_cast<int>(data->faces.size()); ++fi) {
            const MeshFace& face = data->faces[fi];
            if (face.vertices.size() != 3) continue;
            int i0 = face.vertices[0];
            int i1 = face.vertices[1];
            int i2 = face.vertices[2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;

            Eigen::Matrix3d A;
            A << data->vertices[i0].position.x, data->vertices[i0].position.y, 1.0,
                 data->vertices[i1].position.x, data->vertices[i1].position.y, 1.0,
                 data->vertices[i2].position.x, data->vertices[i2].position.y, 1.0;
            if (std::abs(A.determinant()) <= kStressEpsilon) continue;

            Eigen::Vector3d plane = A.colPivHouseholderQr().solve(Eigen::Vector3d(w(i0), w(i1), w(i2)));
            Vec3 gradient(static_cast<float>(plane.x()), static_cast<float>(plane.y()), 0.0f);
            double magnitude = gradient.length();
            if (magnitude <= stressMagnitudeThreshold_) {
                gradient = Vec3(1.0f, 0.0f, 0.0f);
                magnitude = 0.0;
            } else {
                gradient.normalize();
            }

            Vec3 normal = data->calculateFaceNormal(face);
            Vec3 minor = normal.cross(gradient).normalized();
            if (minor.lengthSquared() <= 1e-8f) minor = Vec3(-gradient.y, gradient.x, 0.0f).normalized();

            FaceStressTensor tensor;
            tensor.xx = static_cast<float>(plane.x() * plane.x());
            tensor.yy = static_cast<float>(plane.y() * plane.y());
            tensor.xy = static_cast<float>(plane.x() * plane.y());
            tensor.majorValue = static_cast<float>(magnitude);
            tensor.magnitude = static_cast<float>(magnitude);
            tensor.majorDirection = gradient;
            tensor.minorDirection = minor;
            stressField_[fi] = tensor;
        }

        smoothStressField(*data);
        return true;
    }

    void StressAnalyzer::smoothStressField(const MeshData& data) {
        smoothedCrossField_ = stressField_;
        if (fieldSmoothingIterations_ <= 0 || smoothedCrossField_.empty()) return;

        std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
        for (int fi = 0; fi < static_cast<int>(data.faces.size()); ++fi) {
            const auto& face = data.faces[fi];
            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % face.vertices.size()];
                if (a > b) std::swap(a, b);
                edgeFaces[{a, b}].push_back(fi);
            }
        }

        std::vector<std::vector<int>> neighbors(data.faces.size());
        for (const auto& item : edgeFaces) {
            for (int a : item.second) {
                for (int b : item.second) {
                    if (a != b) neighbors[a].push_back(b);
                }
            }
        }

        for (int iter = 0; iter < fieldSmoothingIterations_; ++iter) {
            TensorField next = smoothedCrossField_;
            for (int fi = 0; fi < static_cast<int>(data.faces.size()); ++fi) {
                Vec3 base = useMajorStressDirection_ ? smoothedCrossField_[fi].majorDirection : smoothedCrossField_[fi].minorDirection;
                Vec3 sum = base;
                for (int nb : neighbors[fi]) {
                    Vec3 d = useMajorStressDirection_ ? smoothedCrossField_[nb].majorDirection : smoothedCrossField_[nb].minorDirection;
                    if (sum.dot(d) < 0.0f) d = -d;
                    sum += d;
                }
                if (sum.lengthSquared() <= 1e-8f) continue;
                Vec3 major = sum.normalized();
                Vec3 normal = data.calculateFaceNormal(data.faces[fi]);
                major -= normal * major.dot(normal);
                if (major.lengthSquared() <= 1e-8f) major = base;
                major.normalize();
                next[fi].majorDirection = major;
                next[fi].minorDirection = normal.cross(major).normalized();
            }
            smoothedCrossField_ = next;
        }
    }

    void StressAnalyzer::colorMeshByMagnitude(MeshObject& mesh, Color low, Color high) const {
        auto data = mesh.getMeshData();
        if (!data || data->faces.empty() || smoothedCrossField_.empty()) return;

        float maxMagnitude = 0.0f;
        for (const auto& tensor : smoothedCrossField_.tensors()) maxMagnitude = std::max(maxMagnitude, tensor.magnitude);
        if (maxMagnitude <= 1e-8f) maxMagnitude = 1.0f;

        std::vector<Color> accum(data->vertices.size(), Color(0.0f, 0.0f, 0.0f, 1.0f));
        std::vector<int> counts(data->vertices.size(), 0);
        const size_t count = std::min(data->faces.size(), smoothedCrossField_.size());
        for (size_t fi = 0; fi < count; ++fi) {
            Color c = lerpColor(low, high, smoothedCrossField_[fi].magnitude / maxMagnitude);
            data->faces[fi].color = c;
            for (int id : data->faces[fi].vertices) {
                if (id < 0 || id >= static_cast<int>(data->vertices.size())) continue;
                accum[id].r += c.r;
                accum[id].g += c.g;
                accum[id].b += c.b;
                ++counts[id];
            }
        }

        for (size_t i = 0; i < data->vertices.size(); ++i) {
            if (counts[i] <= 0) continue;
            data->vertices[i].color = Color(accum[i].r / counts[i], accum[i].g / counts[i], accum[i].b / counts[i], 1.0f);
        }
        data->triangulationDirty = true;
    }

    std::vector<TensorCrossSign> StressAnalyzer::extractCrossSigns(const MeshData& mesh, float scale) const {
        std::vector<TensorCrossSign> signs;
        const size_t count = std::min(mesh.faces.size(), smoothedCrossField_.size());
        signs.reserve(count);
        for (size_t fi = 0; fi < count; ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() < 3) continue;
            Vec3 c = faceCenter(mesh, face);
            float length = std::max(scale, scale * (1.0f + smoothedCrossField_[fi].magnitude));
            Vec3 d0 = smoothedCrossField_[fi].majorDirection.normalized() * length;
            Vec3 d1 = smoothedCrossField_[fi].minorDirection.normalized() * length;
            signs.push_back({static_cast<int>(fi), c, c - d0, c + d0, c - d1, c + d1, smoothedCrossField_[fi].magnitude});
        }
        return signs;
    }

    std::vector<TensorStreamline> StressAnalyzer::extractStreamlines(const MeshData& mesh,
                                                                     int seedStride,
                                                                     int steps,
                                                                     float stepLength,
                                                                     bool useMajorDirection) const {
        std::vector<TensorStreamline> lines;
        if (smoothedCrossField_.empty() || mesh.faces.empty()) return lines;
        seedStride = std::max(1, seedStride);
        steps = std::max(1, steps);

        std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() != 3) continue;
            for (int i = 0; i < 3; ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % 3];
                edgeFaces[sortedEdge(a, b)].push_back(fi);
            }
        }

        auto trace = [&](int seedFace, Vec3 seed, Vec3 initialDirection) {
            TensorStreamline line;
            line.push_back(seed);

            int faceIndex = seedFace;
            Vec3 current = seed;
            Vec3 direction = initialDirection.normalized();
            float traveled = 0.0f;
            const float maxLength = std::max(stepLength, stepLength * static_cast<float>(steps));

            for (int step = 0; step < steps * 2; ++step) {
                if (faceIndex < 0 || faceIndex >= static_cast<int>(mesh.faces.size())) break;
                if (faceIndex >= static_cast<int>(smoothedCrossField_.size())) break;

                Vec3 fieldDir = useMajorDirection
                    ? smoothedCrossField_[faceIndex].majorDirection
                    : smoothedCrossField_[faceIndex].minorDirection;
                if (fieldDir.lengthSquared() <= 1e-8f) break;
                fieldDir.normalize();
                if (fieldDir.dot(direction) < 0.0f) fieldDir = -fieldDir;
                direction = fieldDir;

                const MeshFace& face = mesh.faces[faceIndex];
                if (face.vertices.size() != 3) break;

                float bestT = std::numeric_limits<float>::max();
                int bestEdge = -1;
                for (int ei = 0; ei < 3; ++ei) {
                    int aId = face.vertices[ei];
                    int bId = face.vertices[(ei + 1) % 3];
                    if (aId < 0 || bId < 0 || aId >= static_cast<int>(mesh.vertices.size()) || bId >= static_cast<int>(mesh.vertices.size())) continue;

                    float t = 0.0f;
                    if (raySegmentIntersectionXY(current, direction, mesh.vertices[aId].position, mesh.vertices[bId].position, t) && t < bestT) {
                        bestT = t;
                        bestEdge = ei;
                    }
                }

                if (bestEdge < 0 || !std::isfinite(bestT)) break;
                float segmentLength = std::min(bestT, maxLength - traveled);
                if (segmentLength <= 1e-5f) break;

                Vec3 next = current + direction * segmentLength;
                line.push_back(next);
                traveled += segmentLength;
                if (traveled >= maxLength - 1e-5f) break;

                int aId = face.vertices[bestEdge];
                int bId = face.vertices[(bestEdge + 1) % 3];
                auto it = edgeFaces.find(sortedEdge(aId, bId));
                if (it == edgeFaces.end() || it->second.size() < 2) break;

                int nextFace = -1;
                for (int candidate : it->second) {
                    if (candidate != faceIndex) {
                        nextFace = candidate;
                        break;
                    }
                }
                if (nextFace < 0) break;

                faceIndex = nextFace;
                current = next + direction * 1e-4f;
            }

            return line;
        };

        for (int fi = 0; fi < static_cast<int>(std::min(mesh.faces.size(), smoothedCrossField_.size())); fi += seedStride) {
            Vec3 dir = useMajorDirection ? smoothedCrossField_[fi].majorDirection : smoothedCrossField_[fi].minorDirection;
            dir.normalize();
            if (dir.lengthSquared() <= 1e-8f) continue;
            Vec3 c = faceCenter(mesh, mesh.faces[fi]);

            TensorStreamline negative = trace(fi, c, -dir);
            std::reverse(negative.begin(), negative.end());
            TensorStreamline positive = trace(fi, c, dir);

            TensorStreamline line;
            line.reserve(negative.size() + positive.size());
            line.insert(line.end(), negative.begin(), negative.end());
            if (!line.empty() && !positive.empty()) positive.erase(positive.begin());
            line.insert(line.end(), positive.begin(), positive.end());
            if (line.size() > 1) lines.push_back(std::move(line));
        }
        return lines;
    }

    void StressAnalyzer::draw(Renderer& renderer, const MeshObject& mesh, const StressAnalysisDrawSettings& settings) const {
        auto data = mesh.getMeshData();
        if (!data) return;

        if (settings.drawColoredMesh) {
            drawColoredMesh(renderer, *data);
        }
        if (settings.drawMeshEdges) {
            drawMeshEdges(renderer, *data, settings.edgeColor, settings.edgeWidth);
        }
        if (settings.drawBoundaryConditions) {
            drawBoundaryConditions(renderer,
                                   *data,
                                   settings.loadScale,
                                   settings.loadColor,
                                   settings.fixedVertexColor,
                                   settings.fixedVertexSize);
        }
        if (settings.drawCrossField) {
            drawCrossField(renderer,
                           *data,
                           settings.crossScale,
                           settings.majorColor,
                           settings.minorColor);
        }
        if (settings.drawStreamlines) {
            drawStreamlines(renderer, *data, settings);
        }
    }

    void StressAnalyzer::drawColoredMesh(Renderer& renderer, const MeshData& mesh) const {
        std::vector<Vec3> vertices;
        std::vector<Vec3> normals;
        std::vector<Color> colors;

        for (const auto& face : mesh.faces) {
            if (face.vertices.size() < 3) continue;
            Vec3 normal = mesh.calculateFaceNormal(face);
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                int tri[3] = {face.vertices[0], face.vertices[i], face.vertices[i + 1]};
                for (int id : tri) {
                    if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
                    vertices.push_back(mesh.vertices[id].position);
                    normals.push_back(normal);
                    colors.push_back(mesh.vertices[id].color);
                }
            }
        }

        if (!vertices.empty()) {
            renderer.drawMesh(vertices.data(), normals.data(), colors.data(), static_cast<int>(vertices.size()), nullptr, 0, false);
        }
    }

    void StressAnalyzer::drawMeshEdges(Renderer& renderer, const MeshData& mesh, const Color& color, float width) const {
        for (const auto& edge : mesh.edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0) continue;
            if (edge.vertexA >= static_cast<int>(mesh.vertices.size()) || edge.vertexB >= static_cast<int>(mesh.vertices.size())) continue;
            renderer.drawLine(mesh.vertices[edge.vertexA].position, mesh.vertices[edge.vertexB].position, color, width);
        }
    }

    void StressAnalyzer::drawBoundaryConditions(Renderer& renderer,
                                                const MeshData& mesh,
                                                float loadScale,
                                                const Color& loadColor,
                                                const Color& fixedColor,
                                                float fixedSize) const {
        for (const auto& item : forces_) {
            int id = item.first;
            if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
            Vec3 force = item.second;
            if (force.lengthSquared() <= 1e-10f) continue;
            Vec3 p = mesh.vertices[id].position;
            renderer.drawLine(p, p + force.normalized() * loadScale, loadColor, 1.0f);
        }

        for (int id : fixedVertices_) {
            if (id < 0 || id >= static_cast<int>(mesh.vertices.size())) continue;
            renderer.drawPoint(mesh.vertices[id].position, fixedColor, fixedSize);
        }
    }

    void StressAnalyzer::drawCrossField(Renderer& renderer,
                                        const MeshData& mesh,
                                        float scale,
                                        const Color& majorColor,
                                        const Color& minorColor) const {
        auto signs = extractCrossSigns(mesh, scale);
        for (const auto& sign : signs) {
            renderer.drawLine(sign.majorStart, sign.majorEnd, majorColor, 1.4f);
            renderer.drawLine(sign.minorStart, sign.minorEnd, minorColor, 1.0f);
        }
    }

    void StressAnalyzer::drawStreamlines(Renderer& renderer, const MeshData& mesh, const StressAnalysisDrawSettings& settings) const {
        if (settings.drawMajorStreamlines) {
            auto majorLines = extractStreamlines(mesh,
                                                 settings.streamlineSeedStride,
                                                 settings.streamlineSteps,
                                                 settings.streamlineStepLength,
                                                 true);
            std::vector<Vec3> segments;
            for (const auto& line : majorLines) {
                appendPolylineSegments(line, segments);
            }
            if (!segments.empty()) {
                renderer.drawLines(segments.data(), static_cast<int>(segments.size()), settings.majorColor, settings.majorLineWidth);
            }
        }

        if (settings.drawMinorStreamlines) {
            auto minorLines = extractStreamlines(mesh,
                                                 settings.streamlineSeedStride,
                                                 settings.streamlineSteps,
                                                 settings.streamlineStepLength,
                                                 false);
            std::vector<Vec3> segments;
            for (const auto& line : minorLines) {
                appendPolylineSegments(line, segments);
            }
            if (!segments.empty()) {
                renderer.drawLines(segments.data(), static_cast<int>(segments.size()), settings.minorColor, settings.minorLineWidth);
            }
        }
    }

} // namespace alice2
