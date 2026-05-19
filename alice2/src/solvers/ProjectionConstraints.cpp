#include "ProjectionConstraints.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <vector>

namespace alice2 {

    static constexpr double kPi = 3.14159265358979323846;

    // Shape-Up local/global projection:
    // https://infoscience.epfl.ch/entities/publication/73724b50-05e0-4c05-a3a4-9523e51acce1
    // Planar mesh optimization:
    // https://roipo.github.io/publication/2013-poranne-interactive/planarization.pdf
    // ShapeOp constraint framework:
    // https://shapeop.org/ShapeOpDoc.0.1.0/tutorial_add.html
    // Conical quad mesh angle criterion:
    // https://www.microsoft.com/en-us/research/publication/angle-criterion-conical-mesh-vertices/

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

    static bool fitCircle2D_shapeOp(const Eigen::MatrixXd& input, Eigen::Vector2d& center, double& radius) {
        if (input.cols() < 3) return false;

        double Suu = 0.0;
        double Suv = 0.0;
        double Svv = 0.0;
        double Suuu = 0.0;
        double Suvv = 0.0;
        double Svuu = 0.0;
        double Svvv = 0.0;

        for (int j = 0; j < input.cols(); ++j) {
            double u = input(0, j);
            double v = input(1, j);
            double uu = u * u;
            double vv = v * v;
            Suu += uu;
            Svv += vv;
            Suv += u * v;
            Suuu += uu * u;
            Suvv += u * vv;
            Svuu += v * uu;
            Svvv += vv * v;
        }

        Eigen::Matrix2d A;
        A << Suu, Suv, Suv, Svv;
        if (std::abs(A.determinant()) <= 1e-5) return false;

        Eigen::Vector2d b(0.5 * (Suuu + Suvv), 0.5 * (Svvv + Svuu));
        center = A.inverse() * b;
        radius = std::sqrt(center.squaredNorm() + (Suu + Svv) / static_cast<double>(input.cols()));
        return std::isfinite(radius) && radius > 1e-12;
    }

    static bool projectAngle_shapeOp(const Eigen::Vector3d& v1,
                                     const Eigen::Vector3d& v2,
                                     double targetAngle,
                                     Eigen::Vector3d& projected1,
                                     Eigen::Vector3d& projected2) {
        projected1 = v1;
        projected2 = v2;

        double v1SqrNorm = v1.squaredNorm();
        double v2SqrNorm = v2.squaredNorm();
        double v1Norm = v1.norm();
        double v2Norm = v2.norm();
        if (v1Norm <= 1e-14 || v2Norm <= 1e-14) return false;

        Eigen::Vector3d unitV1 = v1 / v1Norm;
        Eigen::Vector3d unitV2 = v2 / v2Norm;
        if (!unitV1.allFinite() || !unitV2.allFinite()) return false;

        double cosGamma = std::clamp(unitV1.dot(unitV2), -1.0, 1.0);
        if (1.0 - std::abs(cosGamma) <= 1e-14) return false;

        double gamma = std::acos(cosGamma);
        double eta = std::abs(targetAngle - gamma);
        if (eta <= 1e-12) return true;

        double theta = 0.5 * std::atan2(v2SqrNorm * std::sin(2.0 * eta),
                                        v1SqrNorm + v2SqrNorm * std::cos(2.0 * eta));
        theta = std::max(0.0, std::min(eta, theta));
        double phi = eta - theta;

        Eigen::Vector3d unitV3 = unitV2 - unitV1 * cosGamma;
        Eigen::Vector3d unitV4 = unitV1 - unitV2 * cosGamma;
        if (unitV3.squaredNorm() <= 1e-14 || unitV4.squaredNorm() <= 1e-14) return false;
        unitV3.normalize();
        unitV4.normalize();

        if (targetAngle > gamma) {
            unitV3 *= -1.0;
            unitV4 *= -1.0;
        }

        projected1 = (unitV1 * std::cos(theta) + unitV3 * std::sin(theta)) * (v1Norm * std::cos(theta));
        projected2 = (unitV2 * std::cos(phi) + unitV4 * std::sin(phi)) * (v2Norm * std::cos(phi));
        return projected1.allFinite() && projected2.allFinite();
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

    static double angleAt(const Eigen::Vector3d& a, const Eigen::Vector3d& b, const Eigen::Vector3d& c) {
        Eigen::Vector3d u = a - b;
        Eigen::Vector3d v = c - b;
        double lu = u.norm();
        double lv = v.norm();
        if (lu <= 1e-12 || lv <= 1e-12) return 0.0;
        double dot = std::clamp(u.dot(v) / (lu * lv), -1.0, 1.0);
        return std::acos(dot);
    }

    struct VertexCorner {
        int face{-1};
        int prev{-1};
        int next{-1};
    };

    static bool orderedVertexCorners(const MeshData& data, int centerIndex, std::vector<VertexCorner>& ordered) {
        std::vector<VertexCorner> corners;

        for (int faceIndex = 0; faceIndex < static_cast<int>(data.faces.size()); ++faceIndex) {
            const MeshFace& face = data.faces[faceIndex];
            if (face.vertices.size() < 3) continue;

            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                if (face.vertices[i] != centerIndex) continue;
                int count = static_cast<int>(face.vertices.size());
                corners.push_back({faceIndex, face.vertices[(i + count - 1) % count], face.vertices[(i + 1) % count]});
                break;
            }
        }

        if (corners.size() != 4) return false;

        ordered.clear();
        ordered.reserve(corners.size());

        int current = 0;
        int enter = corners[current].prev;
        for (int step = 0; step < static_cast<int>(corners.size()); ++step) {
            ordered.push_back(corners[current]);
            int exit = enter == corners[current].prev ? corners[current].next : corners[current].prev;
            if (step + 1 == static_cast<int>(corners.size())) break;

            int nextCorner = -1;
            for (int i = 0; i < static_cast<int>(corners.size()); ++i) {
                if (i == current) continue;
                if (corners[i].prev == exit || corners[i].next == exit) {
                    bool alreadyUsed = false;
                    for (const auto& used : ordered) {
                        if (used.face == corners[i].face) alreadyUsed = true;
                    }
                    if (!alreadyUsed) {
                        nextCorner = i;
                        break;
                    }
                }
            }

            if (nextCorner < 0) return false;
            enter = exit;
            current = nextCorner;
        }

        return ordered.size() == corners.size();
    }

    static double conicalVertexError(const MeshData& data, int centerIndex, const std::vector<VertexCorner>& corners) {
        if (centerIndex < 0 || centerIndex >= static_cast<int>(data.vertices.size())) return -1.0;
        if (corners.size() != 4) return -1.0;

        const Vec3& c = data.vertices[centerIndex].position;
        Eigen::Vector3d center(c.x, c.y, c.z);
        double angles[4];

        for (int i = 0; i < 4; ++i) {
            if (corners[i].prev < 0 || corners[i].prev >= static_cast<int>(data.vertices.size())) return -1.0;
            if (corners[i].next < 0 || corners[i].next >= static_cast<int>(data.vertices.size())) return -1.0;

            const Vec3& p0 = data.vertices[corners[i].prev].position;
            const Vec3& p1 = data.vertices[corners[i].next].position;
            angles[i] = angleAt(Eigen::Vector3d(p0.x, p0.y, p0.z), center, Eigen::Vector3d(p1.x, p1.y, p1.z));
        }

        return std::abs((angles[0] + angles[2]) - (angles[1] + angles[3]));
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
            if (face.vertices.size() < 3) continue;

            Eigen::MatrixXd input(3, face.vertices.size());
            Eigen::Vector3d mean = Eigen::Vector3d::Zero();
            bool valid = true;
            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                int index = face.vertices[i];
                if (index < 0 || index >= static_cast<int>(data->vertices.size())) {
                    valid = false;
                    break;
                }
                const Vec3& p = data->vertices[index].position;
                input.col(i) = Eigen::Vector3d(p.x, p.y, p.z);
                mean += input.col(i);
            }
            if (!valid) continue;

            mean /= static_cast<double>(face.vertices.size());
            input.colwise() -= mean;

            Eigen::JacobiSVD<Eigen::MatrixXd> svd(input, Eigen::ComputeFullU);
            Eigen::Matrix3d basis = svd.matrixU();
            input = basis.transpose() * input;
            double maxPlaneDistance = input.row(2).cwiseAbs().maxCoeff();
            input.row(2).setZero();

            Eigen::Vector2d circleCenter;
            double radius = 0.0;
            if (!fitCircle2D_shapeOp(input, circleCenter, radius)) continue;

            double maxCircleError = maxPlaneDistance;
            for (int j = 0; j < input.cols(); ++j) {
                Eigen::Vector2d d = input.block<2, 1>(0, j) - circleCenter;
                maxCircleError = std::max(maxCircleError, std::abs(d.norm() - radius));
            }
            if (maxCircleError <= static_cast<double>(settings.tolerance)) continue;

            std::vector<float> coefficients(face.vertices.size(), -1.0f / static_cast<float>(face.vertices.size()));
            float diagonalCoefficient = 1.0f - 1.0f / static_cast<float>(face.vertices.size());

            for (int j = 0; j < input.cols(); ++j) {
                Eigen::Vector2d dir = input.block<2, 1>(0, j) - circleCenter;
                if (dir.squaredNorm() <= 1e-12) continue;
                dir.normalize();

                input.block<2, 1>(0, j) = circleCenter + dir * radius;
                Eigen::Vector3d target = basis * input.col(j);
                coefficients[j] = diagonalCoefficient;

                targets.push_back({-1,
                                   Vec3(static_cast<float>(target.x()),
                                        static_cast<float>(target.y()),
                                        static_cast<float>(target.z())),
                                   weight,
                                   face.vertices,
                                   coefficients});

                coefficients[j] = -1.0f / static_cast<float>(face.vertices.size());
            }
        }
    }

    void ConicalVertexConstraint::project(const MeshObject& mesh,
                                          const ProjectionSolverSettings& settings,
                                          std::vector<ProjectionTarget>& targets) const {
        if (!enabled) return;

        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return;

        for (int centerIndex = 0; centerIndex < static_cast<int>(data->vertices.size()); ++centerIndex) {
            std::vector<VertexCorner> corners;
            if (!orderedVertexCorners(*data, centerIndex, corners)) continue;

            double error = conicalVertexError(*data, centerIndex, corners);
            if (error < 0.0 || error <= static_cast<double>(settings.tolerance)) continue;

            const Vec3& c = data->vertices[centerIndex].position;
            Eigen::Vector3d center(c.x, c.y, c.z);
            double angles[4];
            bool valid = true;
            for (int i = 0; i < 4; ++i) {
                if (corners[i].prev < 0 || corners[i].prev >= static_cast<int>(data->vertices.size())) valid = false;
                if (corners[i].next < 0 || corners[i].next >= static_cast<int>(data->vertices.size())) valid = false;
                if (!valid) break;

                const Vec3& p0 = data->vertices[corners[i].prev].position;
                const Vec3& p1 = data->vertices[corners[i].next].position;
                angles[i] = angleAt(Eigen::Vector3d(p0.x, p0.y, p0.z), center, Eigen::Vector3d(p1.x, p1.y, p1.z));
            }
            if (!valid) continue;

            double signedError = (angles[0] + angles[2]) - (angles[1] + angles[3]);
            double targetAngles[4] = {
                std::clamp(angles[0] - signedError * 0.25, 1e-6, kPi - 1e-6),
                std::clamp(angles[1] + signedError * 0.25, 1e-6, kPi - 1e-6),
                std::clamp(angles[2] - signedError * 0.25, 1e-6, kPi - 1e-6),
                std::clamp(angles[3] + signedError * 0.25, 1e-6, kPi - 1e-6)
            };

            for (int i = 0; i < 4; ++i) {
                const Vec3& p0 = data->vertices[corners[i].prev].position;
                const Vec3& p1 = data->vertices[corners[i].next].position;
                Eigen::Vector3d v1(p0.x - c.x, p0.y - c.y, p0.z - c.z);
                Eigen::Vector3d v2(p1.x - c.x, p1.y - c.y, p1.z - c.z);
                Eigen::Vector3d projected1, projected2;
                if (!projectAngle_shapeOp(v1, v2, targetAngles[i], projected1, projected2)) continue;

                targets.push_back({-1,
                                   Vec3(static_cast<float>(projected1.x()),
                                        static_cast<float>(projected1.y()),
                                        static_cast<float>(projected1.z())),
                                   weight,
                                   {centerIndex, corners[i].prev},
                                   {-1.0f, 1.0f}});
                targets.push_back({-1,
                                   Vec3(static_cast<float>(projected2.x()),
                                        static_cast<float>(projected2.y()),
                                        static_cast<float>(projected2.z())),
                                   weight,
                                   {centerIndex, corners[i].next},
                                   {-1.0f, 1.0f}});
            }
        }
    }

} // namespace alice2
