#include "MeshAnalyzer.h"
#include "../computeGeom/ComputeMesh.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace alice2 {

    static constexpr double kTwoPi = 6.28318530717958647692;
    static constexpr double kEpsilon = 1e-12;

    struct AnalyzerCircleGuide {
        Eigen::Vector3d center;
        Eigen::Matrix3d basis;
        Eigen::Vector2d circleCenter;
        double radius{0.0};
    };

    struct AnalyzerVertexCorner {
        int face{-1};
        int prev{-1};
        int next{-1};
    };

    static Vec3 toVec3(const Eigen::Vector3d& v) {
        return Vec3(static_cast<float>(v.x()), static_cast<float>(v.y()), static_cast<float>(v.z()));
    }

    static bool analyzerFitCircle2D(const Eigen::MatrixXd& input, Eigen::Vector2d& center, double& radius) {
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
        return std::isfinite(radius) && radius > kEpsilon;
    }

    static float analyzerPlanarVolumeError(const MeshData& data, const std::vector<int>& indices) {
        if (indices.size() < 4) return 0.0f;

        std::vector<Eigen::Vector3d> points;
        points.reserve(indices.size());
        for (int index : indices) {
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return -1.0f;
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

        return static_cast<float>(maxVolume);
    }

    static float analyzerPlanarPlaneError(const MeshData& data, const std::vector<int>& indices) {
        if (indices.size() < 4) return 0.0f;

        std::vector<Eigen::Vector3d> points;
        points.reserve(indices.size());
        for (int index : indices) {
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return -1.0f;
            const Vec3& p = data.vertices[index].position;
            points.emplace_back(p.x, p.y, p.z);
        }

        Eigen::Vector3d normal = Eigen::Vector3d::Zero();
        Eigen::Vector3d origin = points[0];
        bool foundPlane = false;
        for (size_t i = 1; i + 1 < points.size(); ++i) {
            normal = (points[i] - origin).cross(points[i + 1] - origin);
            if (normal.squaredNorm() > kEpsilon) {
                normal.normalize();
                foundPlane = true;
                break;
            }
        }
        if (!foundPlane) return 0.0f;

        double maxDistance = 0.0;
        for (const Eigen::Vector3d& point : points) {
            maxDistance = std::max(maxDistance, std::abs(normal.dot(point - origin)));
        }

        return static_cast<float>(maxDistance);
    }

    static double analyzerAngleAt(const Eigen::Vector3d& a, const Eigen::Vector3d& b, const Eigen::Vector3d& c) {
        Eigen::Vector3d u = a - b;
        Eigen::Vector3d v = c - b;
        double lu = u.norm();
        double lv = v.norm();
        if (lu <= kEpsilon || lv <= kEpsilon) return 0.0;
        return std::acos(std::clamp(u.dot(v) / (lu * lv), -1.0, 1.0));
    }

    static std::vector<std::vector<AnalyzerVertexCorner>> analyzerOrderedVertexCorners(const MeshData& data) {
        ComputeMesh heMesh("mesh_analyzer_topology", data, true);
        std::vector<std::vector<AnalyzerVertexCorner>> all(data.vertices.size());

        for (int centerIndex = 0; centerIndex < static_cast<int>(data.vertices.size()); ++centerIndex) {
            auto vertex = heMesh.getVertex(centerIndex);
            if (!vertex || vertex->onBoundary() || vertex->getValency() != 4) continue;

            auto halfedges = vertex->getHalfedges();
            if (halfedges.size() != 4) continue;

            std::vector<AnalyzerVertexCorner> ordered;
            ordered.reserve(4);

            bool valid = true;
            for (const auto& he : halfedges) {
                if (!he || !he->getFace() || !he->getPrev() || !he->getNext()) {
                    valid = false;
                    break;
                }

                auto prevVertex = he->getPrev()->getVertex();
                auto nextVertex = he->getVertex();
                if (!prevVertex || !nextVertex) {
                    valid = false;
                    break;
                }

                ordered.push_back({
                    he->getFace()->getId(),
                    prevVertex->getId(),
                    nextVertex->getId()
                });
            }

            if (valid && ordered.size() == 4) all[centerIndex] = ordered;
        }

        return all;
    }

    static bool analyzerCircleGuide(const MeshData& data, const MeshFace& face, AnalyzerCircleGuide& guide) {
        if (face.vertices.size() < 3) return false;

        Eigen::MatrixXd input(3, face.vertices.size());
        guide.center.setZero();

        for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
            int index = face.vertices[i];
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return false;
            const Vec3& p = data.vertices[index].position;
            input.col(i) = Eigen::Vector3d(p.x, p.y, p.z);
            guide.center += input.col(i);
        }

        guide.center /= static_cast<double>(face.vertices.size());
        input.colwise() -= guide.center;

        Eigen::JacobiSVD<Eigen::MatrixXd> svd(input, Eigen::ComputeFullU);
        guide.basis = svd.matrixU();
        input = guide.basis.transpose() * input;
        input.row(2).setZero();

        return analyzerFitCircle2D(input, guide.circleCenter, guide.radius);
    }

    static bool analyzerFaceCenter(const MeshData& data, int faceIndex, Eigen::Vector3d& center) {
        if (faceIndex < 0 || faceIndex >= static_cast<int>(data.faces.size())) return false;
        const MeshFace& face = data.faces[faceIndex];
        if (face.vertices.empty()) return false;

        center.setZero();
        for (int index : face.vertices) {
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return false;
            const Vec3& p = data.vertices[index].position;
            center += Eigen::Vector3d(p.x, p.y, p.z);
        }

        center /= static_cast<double>(face.vertices.size());
        return true;
    }

    static bool analyzerConeAxis(const MeshData& data,
                                 const std::vector<AnalyzerVertexCorner>& corners,
                                 int vertexIndex,
                                 Eigen::Vector3d& axis) {
        if (corners.size() < 3 || vertexIndex < 0 || vertexIndex >= static_cast<int>(data.vertices.size())) return false;

        Eigen::Vector3d c0;
        Eigen::Vector3d c1;
        Eigen::Vector3d c2;
        if (!analyzerFaceCenter(data, corners[0].face, c0)) return false;
        if (!analyzerFaceCenter(data, corners[1].face, c1)) return false;
        if (!analyzerFaceCenter(data, corners[2].face, c2)) return false;

        axis = (c1 - c0).cross(c2 - c0);
        if (axis.squaredNorm() <= kEpsilon) return false;
        axis.normalize();

        const Vec3& normal = data.vertices[vertexIndex].normal;
        Eigen::Vector3d currentNormal(normal.x, normal.y, normal.z);
        if (currentNormal.squaredNorm() > kEpsilon && axis.dot(currentNormal) < 0.0) axis = -axis;
        return true;
    }

    static bool analyzerFaceSatisfied(const std::vector<float>& errors,
                                      const MeshFace& face,
                                      MeshAnalysisMode mode,
                                      float tolerance,
                                      size_t faceIndex) {
        if (mode == MeshAnalysisMode::PlanarVolume ||
            mode == MeshAnalysisMode::PlanarPlane ||
            mode == MeshAnalysisMode::Circular) {
            return faceIndex < errors.size() && errors[faceIndex] >= 0.0f && errors[faceIndex] <= tolerance;
        }

        bool hasValidVertex = false;
        for (int index : face.vertices) {
            if (index < 0 || index >= static_cast<int>(errors.size())) continue;
            if (errors[index] < 0.0f) continue;
            hasValidVertex = true;
            if (errors[index] > tolerance) return false;
        }
        return hasValidVertex;
    }

    float MeshAnalyzer::activeTolerance() const {
        switch (mode) {
            case MeshAnalysisMode::PlanarVolume: return planarVolumeTolerance;
            case MeshAnalysisMode::PlanarPlane: return planarPlaneTolerance;
            case MeshAnalysisMode::Circular: return circularTolerance;
            case MeshAnalysisMode::Conical: return conicalTolerance;
        }

        return planarVolumeTolerance;
    }

    const MeshAnalysisResult& MeshAnalyzer::analyze(const MeshObject& mesh) {
        m_mesh = &mesh;
        m_result = MeshAnalysisResult{};
        m_errors.clear();

        auto data = mesh.getMeshData();
        if (!data) return m_result;

        const float tolerance = activeTolerance();

        if (mode == MeshAnalysisMode::PlanarVolume ||
            mode == MeshAnalysisMode::PlanarPlane ||
            mode == MeshAnalysisMode::Circular) {
            m_result.total = static_cast<int>(data->faces.size());
            m_errors.reserve(data->faces.size());

            for (const MeshFace& face : data->faces) {
                float error = -1.0f;
                if (mode == MeshAnalysisMode::PlanarVolume) error = planarVolumeFaceError(*data, face);
                else if (mode == MeshAnalysisMode::PlanarPlane) error = planarPlaneFaceError(*data, face);
                else error = circularFaceError(*data, face);

                m_errors.push_back(error);
                if (error < 0.0f) continue;
                ++m_result.valid;
                m_result.maxError = std::max(m_result.maxError, error);
                m_result.rmsError += error * error;
                if (error <= tolerance) ++m_result.satisfied;
                else ++m_result.unsatisfied;
            }
        } else {
            m_errors = conicalVertexErrors(*data);
            m_result.total = static_cast<int>(m_errors.size());
            for (float error : m_errors) {
                if (error < 0.0f) continue;
                ++m_result.valid;
                m_result.maxError = std::max(m_result.maxError, error);
                m_result.rmsError += error * error;
                if (error <= tolerance) ++m_result.satisfied;
                else ++m_result.unsatisfied;
            }
        }

        if (m_result.valid > 0) {
            m_result.rmsError = std::sqrt(m_result.rmsError / static_cast<float>(m_result.valid));
        }

        return m_result;
    }

    float MeshAnalyzer::planarVolumeFaceError(const MeshData& data, const MeshFace& face) const {
        return analyzerPlanarVolumeError(data, face.vertices);
    }

    float MeshAnalyzer::planarPlaneFaceError(const MeshData& data, const MeshFace& face) const {
        return analyzerPlanarPlaneError(data, face.vertices);
    }

    float MeshAnalyzer::circularFaceError(const MeshData& data, const MeshFace& face) const {
        if (face.vertices.size() < 3) return -1.0f;

        Eigen::MatrixXd input(3, face.vertices.size());
        Eigen::Vector3d mean = Eigen::Vector3d::Zero();
        for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
            int index = face.vertices[i];
            if (index < 0 || index >= static_cast<int>(data.vertices.size())) return -1.0f;
            const Vec3& p = data.vertices[index].position;
            input.col(i) = Eigen::Vector3d(p.x, p.y, p.z);
            mean += input.col(i);
        }

        mean /= static_cast<double>(face.vertices.size());
        input.colwise() -= mean;

        Eigen::JacobiSVD<Eigen::MatrixXd> svd(input, Eigen::ComputeFullU);
        Eigen::Matrix3d basis = svd.matrixU();
        input = basis.transpose() * input;

        double maxPlaneDistance = input.row(2).cwiseAbs().maxCoeff();
        input.row(2).setZero();

        Eigen::Vector2d circleCenter;
        double radius = 0.0;
        if (!analyzerFitCircle2D(input, circleCenter, radius)) return -1.0f;

        double maxError = maxPlaneDistance;
        for (int j = 0; j < input.cols(); ++j) {
            Eigen::Vector2d point = input.block<2, 1>(0, j);
            maxError = std::max(maxError, std::abs((point - circleCenter).norm() - radius));
        }

        return static_cast<float>(maxError);
    }

    std::vector<float> MeshAnalyzer::conicalVertexErrors(const MeshData& data) const {
        std::vector<float> errors(data.vertices.size(), -1.0f);
        auto orderedCorners = analyzerOrderedVertexCorners(data);

        for (int centerIndex = 0; centerIndex < static_cast<int>(data.vertices.size()); ++centerIndex) {
            const auto& corners = orderedCorners[centerIndex];
            if (corners.size() != 4) continue;

            const Vec3& c = data.vertices[centerIndex].position;
            Eigen::Vector3d center(c.x, c.y, c.z);
            double angles[4];
            bool valid = true;
            for (int i = 0; i < 4; ++i) {
                if (corners[i].prev < 0 || corners[i].prev >= static_cast<int>(data.vertices.size())) valid = false;
                if (corners[i].next < 0 || corners[i].next >= static_cast<int>(data.vertices.size())) valid = false;
                if (!valid) break;

                const Vec3& p0 = data.vertices[corners[i].prev].position;
                const Vec3& p1 = data.vertices[corners[i].next].position;
                angles[i] = analyzerAngleAt(Eigen::Vector3d(p0.x, p0.y, p0.z), center, Eigen::Vector3d(p1.x, p1.y, p1.z));
            }
            if (!valid) continue;

            errors[centerIndex] = static_cast<float>(std::abs((angles[0] + angles[2]) - (angles[1] + angles[3])));
            for (const auto& corner : corners) {
                if (corner.face < 0 || corner.face >= static_cast<int>(data.faces.size())) continue;
                errors[centerIndex] = std::max(errors[centerIndex], planarVolumeFaceError(data, data.faces[corner.face]));
            }
        }

        return errors;
    }

    std::string MeshAnalyzer::print() const {
        std::string modeName = "conical";
        if (mode == MeshAnalysisMode::PlanarVolume) modeName = "planar_volume";
        if (mode == MeshAnalysisMode::PlanarPlane) modeName = "planar_plane";
        if (mode == MeshAnalysisMode::Circular) modeName = "circular";

        std::ostringstream ss;
        ss << "iteration: " << iteration
           << ", mode: " << modeName
           << ", tolerance: " << activeTolerance()
           << ", total: " << m_result.total
           << ", valid: " << m_result.valid
           << ", satisfied: " << m_result.satisfied
           << ", unsatisfied: " << m_result.unsatisfied
           << ", max error: " << m_result.maxError
           << ", rms error: " << m_result.rmsError;
        return ss.str();
    }

    void MeshAnalyzer::draw(Renderer& renderer) const {
        if (!m_mesh) return;

        auto data = m_mesh->getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return;

        const float tolerance = activeTolerance();

        if (drawSettings.drawFaces) {
            std::vector<Vec3> vertices;
            std::vector<Vec3> normals;
            std::vector<Color> colors;

            for (size_t faceIndex = 0; faceIndex < data->faces.size(); ++faceIndex) {
                const MeshFace& face = data->faces[faceIndex];
                if (face.vertices.size() < 3) continue;

                bool satisfied = analyzerFaceSatisfied(m_errors, face, mode, tolerance, faceIndex);
                Color color = satisfied ? drawSettings.satisfiedColor : drawSettings.unsatisfiedColor;
                Vec3 normal = data->calculateFaceNormal(face);

                for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                    int tri[3] = {face.vertices[0], face.vertices[i], face.vertices[i + 1]};
                    bool valid = true;
                    for (int index : tri) {
                        if (index < 0 || index >= static_cast<int>(data->vertices.size())) valid = false;
                    }
                    if (!valid) continue;

                    for (int index : tri) {
                        vertices.push_back(data->vertices[index].position);
                        normals.push_back(normal);
                        colors.push_back(color);
                    }
                }
            }

            if (!vertices.empty()) {
                renderer.drawMesh(vertices.data(), normals.data(), colors.data(), static_cast<int>(vertices.size()), nullptr, 0, false);
            }
        }

        if (drawSettings.drawConstraintGuides && mode == MeshAnalysisMode::Circular) {
            for (size_t faceIndex = 0; faceIndex < data->faces.size(); ++faceIndex) {
                const MeshFace& face = data->faces[faceIndex];
                if (face.vertices.size() < 3) continue;
                if (!analyzerFaceSatisfied(m_errors, face, mode, tolerance, faceIndex)) continue;

                AnalyzerCircleGuide guide;
                if (!analyzerCircleGuide(*data, face, guide)) continue;

                int segments = std::max(8, drawSettings.circleSegments);
                for (int i = 0; i < segments; ++i) {
                    double a0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
                    double a1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
                    Eigen::Vector3d p0 = guide.center + guide.basis * Eigen::Vector3d(
                        guide.circleCenter.x() + std::cos(a0) * guide.radius,
                        guide.circleCenter.y() + std::sin(a0) * guide.radius,
                        0.0);
                    Eigen::Vector3d p1 = guide.center + guide.basis * Eigen::Vector3d(
                        guide.circleCenter.x() + std::cos(a1) * guide.radius,
                        guide.circleCenter.y() + std::sin(a1) * guide.radius,
                        0.0);
                    renderer.drawLine(toVec3(p0), toVec3(p1), drawSettings.circleColor, drawSettings.guideLineWidth);
                }

                double tangentLength = guide.radius * std::max(0.01f, drawSettings.tangentScale);
                for (int index : face.vertices) {
                    if (index < 0 || index >= static_cast<int>(data->vertices.size())) continue;
                    const Vec3& p = data->vertices[index].position;
                    Eigen::Vector3d q = guide.basis.transpose() * (Eigen::Vector3d(p.x, p.y, p.z) - guide.center);
                    Eigen::Vector2d radial(q.x() - guide.circleCenter.x(), q.y() - guide.circleCenter.y());
                    if (radial.squaredNorm() <= kEpsilon) continue;
                    radial.normalize();
                    Eigen::Vector2d tangent(-radial.y(), radial.x());
                    Eigen::Vector3d circlePoint = guide.center + guide.basis * Eigen::Vector3d(
                        guide.circleCenter.x() + radial.x() * guide.radius,
                        guide.circleCenter.y() + radial.y() * guide.radius,
                        0.0);
                    Eigen::Vector3d dir = guide.basis * Eigen::Vector3d(tangent.x(), tangent.y(), 0.0);
                    renderer.drawLine(toVec3(circlePoint - dir * tangentLength),
                                      toVec3(circlePoint + dir * tangentLength),
                                      drawSettings.tangentColor,
                                      drawSettings.guideLineWidth);
                }
            }
        }

        if (drawSettings.drawConstraintGuides && mode == MeshAnalysisMode::Conical) {
            auto orderedCorners = analyzerOrderedVertexCorners(*data);

            for (int centerIndex = 0; centerIndex < static_cast<int>(data->vertices.size()); ++centerIndex) {
                if (centerIndex >= static_cast<int>(m_errors.size())) continue;
                if (m_errors[centerIndex] < 0.0f || m_errors[centerIndex] > tolerance) continue;

                const auto& corners = orderedCorners[centerIndex];
                if (corners.size() != 4) continue;

                const Vec3& c = data->vertices[centerIndex].position;
                Eigen::Vector3d center(c.x, c.y, c.z);

                if (drawSettings.drawConeAxes) {
                    Eigen::Vector3d axis;
                    if (analyzerConeAxis(*data, corners, centerIndex, axis)) {
                        Eigen::Vector3d offset = axis * std::max(0.0f, drawSettings.coneAxisLength);
                        renderer.drawLine(toVec3(center - offset),
                                          toVec3(center + offset),
                                          drawSettings.coneAxisColor,
                                          drawSettings.guideLineWidth);
                    }
                }

                if (!drawSettings.drawCones) continue;

                std::vector<Eigen::Vector3d> faceCenters;
                faceCenters.reserve(corners.size());
                for (const auto& corner : corners) {
                    Eigen::Vector3d faceCenter;
                    if (analyzerFaceCenter(*data, corner.face, faceCenter)) faceCenters.push_back(faceCenter);
                }
                if (faceCenters.size() != 4) continue;

                Eigen::Vector3d averageFaceCenter = Eigen::Vector3d::Zero();
                for (const auto& p : faceCenters) averageFaceCenter += p;
                averageFaceCenter /= static_cast<double>(faceCenters.size());

                Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
                for (const auto& p : faceCenters) {
                    Eigen::Vector3d q = p - averageFaceCenter;
                    covariance += q * q.transpose();
                }

                Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigensolver(covariance);
                if (eigensolver.info() != Eigen::Success) continue;

                Eigen::Vector3d normal = eigensolver.eigenvectors().col(0).normalized();
                Eigen::Vector3d u = eigensolver.eigenvectors().col(2).normalized();
                Eigen::Vector3d v = normal.cross(u);
                if (v.squaredNorm() <= kEpsilon) continue;
                v.normalize();
                u = v.cross(normal).normalized();

                Eigen::MatrixXd circleInput(3, faceCenters.size());
                for (int i = 0; i < static_cast<int>(faceCenters.size()); ++i) {
                    Eigen::Vector3d d = faceCenters[i] - averageFaceCenter;
                    circleInput(0, i) = d.dot(u);
                    circleInput(1, i) = d.dot(v);
                    circleInput(2, i) = 0.0;
                }

                Eigen::Vector2d circleCenter;
                double radius = 0.0;
                if (!analyzerFitCircle2D(circleInput, circleCenter, radius)) continue;

                radius *= std::max(0.0f, drawSettings.coneScale);
                Eigen::Vector3d fittedCenter = averageFaceCenter + u * circleCenter.x() + v * circleCenter.y();

                int segments = std::max(12, drawSettings.coneSegments);
                for (int i = 0; i < segments; ++i) {
                    double a0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
                    double a1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
                    Eigen::Vector3d p0 = fittedCenter + u * std::cos(a0) * radius + v * std::sin(a0) * radius;
                    Eigen::Vector3d p1 = fittedCenter + u * std::cos(a1) * radius + v * std::sin(a1) * radius;
                    renderer.drawLine(toVec3(p0), toVec3(p1), drawSettings.coneColor, drawSettings.guideLineWidth);
                }

                for (const auto& faceCenter : faceCenters) {
                    Eigen::Vector3d d = faceCenter - fittedCenter;
                    d -= normal * d.dot(normal);
                    if (d.squaredNorm() <= kEpsilon) continue;
                    d.normalize();
                    Eigen::Vector3d contact = fittedCenter + d * radius;
                    renderer.drawLine(toVec3(center), toVec3(contact), drawSettings.tangentColor, drawSettings.guideLineWidth);
                }
            }
        }

        if (drawSettings.drawEdges && !data->edges.empty()) {
            std::vector<Vec3> edgeVertices;
            std::vector<int> edgeIndices;
            std::vector<Color> edgeColors;

            edgeVertices.reserve(data->vertices.size());
            for (const auto& vertex : data->vertices) edgeVertices.push_back(vertex.position);

            for (const auto& edge : data->edges) {
                if (edge.vertexA < 0 || edge.vertexA >= static_cast<int>(data->vertices.size())) continue;
                if (edge.vertexB < 0 || edge.vertexB >= static_cast<int>(data->vertices.size())) continue;
                edgeIndices.push_back(edge.vertexA);
                edgeIndices.push_back(edge.vertexB);
                edgeColors.push_back(drawSettings.edgeColor);
            }

            renderer.setLineWidth(drawSettings.edgeWidth);
            renderer.drawMeshEdges(edgeVertices.data(), edgeIndices.data(), edgeColors.data(), static_cast<int>(edgeColors.size()));
        }

        if (drawSettings.drawFixedVertices) {
            for (int index : fixedVertices) {
                if (index < 0 || index >= static_cast<int>(data->vertices.size())) continue;
                renderer.drawPoint(data->vertices[index].position, drawSettings.fixedVertexColor, drawSettings.fixedVertexSize);
            }
        }
    }

} // namespace alice2
