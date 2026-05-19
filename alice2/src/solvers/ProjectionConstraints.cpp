#include "ProjectionConstraints.h"
#include <Eigen/Dense>
#include <algorithm>
#include <vector>

namespace alice2 {

    // Shape-Up local/global projection:
    // https://infoscience.epfl.ch/entities/publication/73724b50-05e0-4c05-a3a4-9523e51acce1
    // Planar mesh optimization:
    // https://roipo.github.io/publication/2013-poranne-interactive/planarization.pdf
    // ShapeOp constraint framework:
    // https://shapeop.org/ShapeOpDoc.0.1.0/tutorial_add.html

    static bool bestFitPlaneBasis(const MeshData& data, const std::vector<int>& indices,
                                  Eigen::Vector3d& center, Eigen::Vector3d& normal,
                                  Eigen::Vector3d& u, Eigen::Vector3d& v) {
        if (indices.size() < 3) return false;

        center.setZero();
        for (int index : indices) {
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return false;
            const Vec3& p = data.vertices[index].position;
            center += Eigen::Vector3d(p.x, p.y, p.z);
        }
        center /= static_cast<double>(indices.size());

        Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
        for (int index : indices) {
            const Vec3& p = data.vertices[index].position;
            Eigen::Vector3d q(p.x, p.y, p.z);
            q -= center;
            covariance += q * q.transpose();
        }

        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigensolver(covariance);
        if (eigensolver.info() != Eigen::Success) return false;

        normal = eigensolver.eigenvectors().col(0);
        u = eigensolver.eigenvectors().col(1);
        v = eigensolver.eigenvectors().col(2);
        if (normal.squaredNorm() <= 1e-12 || u.squaredNorm() <= 1e-12 || v.squaredNorm() <= 1e-12) return false;

        normal.normalize();
        u.normalize();
        v.normalize();
        return true;
    }

    static bool fitCircle2D(const std::vector<Eigen::Vector2d>& points, Eigen::Vector2d& center, double& radius) {
        if (points.size() < 3) return false;

        Eigen::MatrixXd A(points.size(), 3);
        Eigen::VectorXd b(points.size());

        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            double x = points[i].x();
            double y = points[i].y();
            A(i, 0) = 2.0 * x;
            A(i, 1) = 2.0 * y;
            A(i, 2) = 1.0;
            b(i) = x * x + y * y;
        }

        Eigen::Vector3d solution = A.colPivHouseholderQr().solve(b);
        center = Eigen::Vector2d(solution.x(), solution.y());
        double r2 = solution.z() + center.squaredNorm();
        if (r2 <= 1e-12) return false;

        radius = std::sqrt(r2);
        return true;
    }

    static double planarFaceVolumeError(const MeshData& data, const std::vector<int>& indices) {
        if (indices.size() < 4) return 0.0;

        std::vector<Eigen::Vector3d> points;
        points.reserve(indices.size());
        for (int index : indices) {
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return -1.0;
            const Vec3& p = data.vertices[index].position;
            points.emplace_back(p.x, p.y, p.z);
        }

        double maxVolume = 0.0;
        const Eigen::Vector3d& p0 = points[0];
        for (size_t i = 1; i + 1 < points.size(); ++i) {
            Eigen::Vector3d n = (points[i] - p0).cross(points[i + 1] - p0);
            for (size_t j = i + 2; j < points.size(); ++j) {
                maxVolume = std::max(maxVolume, std::abs(n.dot(points[j] - p0)) / 6.0);
            }
        }

        return maxVolume;
    }

    void PlanarFaceConstraint::project(const MeshObject& mesh,
                                       const ProjectionSolverSettings& settings,
                                       std::vector<ProjectionTarget>& targets) const {
        if (!enabled) return;

        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return;

        for (const MeshFace& face : data->faces) {
            Eigen::Vector3d center, normal, u, v;
            if (!bestFitPlaneBasis(*data, face.vertices, center, normal, u, v)) continue;

            double volumeError = planarFaceVolumeError(*data, face.vertices);
            if (volumeError >= 0.0 && volumeError <= static_cast<double>(settings.tolerance)) continue;

            for (int index : face.vertices) {
                const Vec3& p = data->vertices[index].position;
                Eigen::Vector3d q(p.x, p.y, p.z);
                double d = (q - center).dot(normal);
                Eigen::Vector3d projected = q - normal * d;

                targets.push_back({index,
                                   Vec3(static_cast<float>(projected.x()),
                                        static_cast<float>(projected.y()),
                                        static_cast<float>(projected.z())),
                                   weight});
            }
        }
    }

    void CircularFaceConstraint::project(const MeshObject& mesh,
                                         const ProjectionSolverSettings& settings,
                                         std::vector<ProjectionTarget>& targets) const {
        if (!enabled) return;

        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return;

        for (const MeshFace& face : data->faces) {
            Eigen::Vector3d center3, normal, u, v;
            if (!bestFitPlaneBasis(*data, face.vertices, center3, normal, u, v)) continue;

            std::vector<Eigen::Vector2d> points2;
            points2.reserve(face.vertices.size());
            for (int index : face.vertices) {
                const Vec3& p = data->vertices[index].position;
                Eigen::Vector3d q(p.x, p.y, p.z);
                q -= center3;
                points2.emplace_back(q.dot(u), q.dot(v));
            }

            Eigen::Vector2d circleCenter;
            double radius = 0.0;
            if (!fitCircle2D(points2, circleCenter, radius)) continue;

            for (size_t i = 0; i < face.vertices.size(); ++i) {
                Eigen::Vector2d dir = points2[i] - circleCenter;
                if (dir.squaredNorm() <= 1e-12) continue;
                dir.normalize();

                Eigen::Vector2d target2 = circleCenter + dir * radius;
                Eigen::Vector3d target3 = center3 + u * target2.x() + v * target2.y();

                targets.push_back({face.vertices[i],
                                   Vec3(static_cast<float>(target3.x()),
                                        static_cast<float>(target3.y()),
                                        static_cast<float>(target3.z())),
                                   weight});
            }
        }
    }

    void ConicalVertexConstraint::project(const MeshObject& mesh,
                                          const ProjectionSolverSettings& settings,
                                          std::vector<ProjectionTarget>& targets) const {
        if (!enabled) return;

        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->edges.empty()) return;

        std::vector<std::vector<int>> adjacency(data->vertices.size());
        for (const MeshEdge& edge : data->edges) {
            if (edge.vertexA < 0 || edge.vertexA >= static_cast<int>(data->vertices.size())) continue;
            if (edge.vertexB < 0 || edge.vertexB >= static_cast<int>(data->vertices.size())) continue;
            adjacency[edge.vertexA].push_back(edge.vertexB);
            adjacency[edge.vertexB].push_back(edge.vertexA);
        }

        for (size_t centerIndex = 0; centerIndex < adjacency.size(); ++centerIndex) {
            const auto& ring = adjacency[centerIndex];
            if (ring.size() < 3) continue;

            const Vec3& c = data->vertices[centerIndex].position;
            Eigen::Vector3d center(c.x, c.y, c.z);

            Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
            std::vector<Eigen::Vector3d> vectors;
            vectors.reserve(ring.size());

            for (int index : ring) {
                const Vec3& p = data->vertices[index].position;
                Eigen::Vector3d q(p.x, p.y, p.z);
                q -= center;
                if (q.squaredNorm() <= 1e-12) continue;
                vectors.push_back(q);
                Eigen::Vector3d n = q.normalized();
                covariance += n * n.transpose();
            }

            if (vectors.size() < 3) continue;

            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigensolver(covariance);
            if (eigensolver.info() != Eigen::Success) continue;

            Eigen::Vector3d axis = eigensolver.eigenvectors().col(2).normalized();
            double meanAbsCos = 0.0;
            for (const auto& q : vectors) {
                meanAbsCos += std::abs(q.normalized().dot(axis));
            }
            meanAbsCos = std::clamp(meanAbsCos / static_cast<double>(vectors.size()), 0.0, 1.0);
            double sinTheta = std::sqrt(std::max(0.0, 1.0 - meanAbsCos * meanAbsCos));

            for (size_t i = 0; i < vectors.size(); ++i) {
                int index = ring[i];
                Eigen::Vector3d q = vectors[i];
                double length = q.norm();
                double sign = q.dot(axis) < 0.0 ? -1.0 : 1.0;
                Eigen::Vector3d radial = q - axis * q.dot(axis);
                if (radial.squaredNorm() <= 1e-12) continue;
                radial.normalize();

                Eigen::Vector3d target = center + axis * (sign * length * meanAbsCos) + radial * (length * sinTheta);
                targets.push_back({index,
                                   Vec3(static_cast<float>(target.x()),
                                        static_cast<float>(target.y()),
                                        static_cast<float>(target.z())),
                                   weight});
            }
        }
    }

} // namespace alice2
