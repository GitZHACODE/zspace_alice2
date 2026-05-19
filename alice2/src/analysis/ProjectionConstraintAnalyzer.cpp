#include "ProjectionConstraintAnalyzer.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace alice2 {

    static bool analyzerPlaneBasis(const MeshData& data, const std::vector<int>& indices,
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

        normal = eigensolver.eigenvectors().col(0).normalized();
        u = eigensolver.eigenvectors().col(1).normalized();
        v = eigensolver.eigenvectors().col(2).normalized();
        return true;
    }

    static bool analyzerFitCircle2D(const std::vector<Eigen::Vector2d>& points, Eigen::Vector2d& center, double& radius) {
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

    const ProjectionAnalysisResult& ProjectionConstraintAnalyzer::analyze(const MeshObject& mesh) {
        m_mesh = &mesh;
        m_result = ProjectionAnalysisResult{};
        m_errors.clear();

        auto data = mesh.getMeshData();
        if (!data) return m_result;

        if (mode == ProjectionAnalysisMode::Planar || mode == ProjectionAnalysisMode::Circular) {
            m_result.total = static_cast<int>(data->faces.size());
            m_errors.reserve(data->faces.size());

            for (const MeshFace& face : data->faces) {
                float error = mode == ProjectionAnalysisMode::Planar ? planarFaceError(*data, face) : circularFaceError(*data, face);
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

    float ProjectionConstraintAnalyzer::planarFaceError(const MeshData& data, const MeshFace& face) const {
        return analyzerPlanarVolumeError(data, face.vertices);
    }

    float ProjectionConstraintAnalyzer::circularFaceError(const MeshData& data, const MeshFace& face) const {
        Eigen::Vector3d center3, normal, u, v;
        if (!analyzerPlaneBasis(data, face.vertices, center3, normal, u, v)) return -1.0f;

        std::vector<Eigen::Vector2d> points;
        points.reserve(face.vertices.size());
        for (int index : face.vertices) {
            const Vec3& p = data.vertices[index].position;
            Eigen::Vector3d q(p.x, p.y, p.z);
            q -= center3;
            points.emplace_back(q.dot(u), q.dot(v));
        }

        Eigen::Vector2d circleCenter;
        double radius = 0.0;
        if (!analyzerFitCircle2D(points, circleCenter, radius)) return -1.0f;

        double maxError = 0.0;
        for (const auto& point : points) {
            maxError = std::max(maxError, std::abs((point - circleCenter).norm() - radius));
        }

        return static_cast<float>(maxError);
    }

    std::vector<float> ProjectionConstraintAnalyzer::conicalVertexErrors(const MeshData& data) const {
        std::vector<std::vector<int>> adjacency(data.vertices.size());
        for (const MeshEdge& edge : data.edges) {
            if (edge.vertexA < 0 || edge.vertexA >= static_cast<int>(data.vertices.size())) continue;
            if (edge.vertexB < 0 || edge.vertexB >= static_cast<int>(data.vertices.size())) continue;
            adjacency[edge.vertexA].push_back(edge.vertexB);
            adjacency[edge.vertexB].push_back(edge.vertexA);
        }

        std::vector<float> errors(data.vertices.size(), -1.0f);

        for (size_t centerIndex = 0; centerIndex < adjacency.size(); ++centerIndex) {
            const auto& ring = adjacency[centerIndex];
            if (ring.size() < 3) continue;

            const Vec3& c = data.vertices[centerIndex].position;
            Eigen::Vector3d center(c.x, c.y, c.z);
            Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
            std::vector<Eigen::Vector3d> vectors;

            for (int index : ring) {
                const Vec3& p = data.vertices[index].position;
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
            for (const auto& q : vectors) meanAbsCos += std::abs(q.normalized().dot(axis));
            meanAbsCos = std::clamp(meanAbsCos / static_cast<double>(vectors.size()), 0.0, 1.0);

            double maxError = 0.0;
            for (const auto& q : vectors) {
                maxError = std::max(maxError, std::abs(std::abs(q.normalized().dot(axis)) - meanAbsCos));
            }

            errors[centerIndex] = static_cast<float>(maxError);
        }

        return errors;
    }

    std::string ProjectionConstraintAnalyzer::print() const {
        std::string modeName = "conical";
        if (mode == ProjectionAnalysisMode::Planar) modeName = "planar";
        if (mode == ProjectionAnalysisMode::Circular) modeName = "circular";

        std::ostringstream ss;
        ss << "iteration: " << iteration
           << ", mode: " << modeName
           << ", total: " << m_result.total
           << ", valid: " << m_result.valid
           << ", satisfied: " << m_result.satisfied
           << ", unsatisfied: " << m_result.unsatisfied
           << ", max error: " << m_result.maxError
           << ", rms error: " << m_result.rmsError;
        return ss.str();
    }

    void ProjectionConstraintAnalyzer::draw(Renderer& renderer) const {
        if (!m_mesh) return;

        auto data = m_mesh->getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return;

        if (drawSettings.drawFaces) {
            std::vector<Vec3> vertices;
            std::vector<Vec3> normals;
            std::vector<Color> colors;

            for (size_t faceIndex = 0; faceIndex < data->faces.size(); ++faceIndex) {
                const MeshFace& face = data->faces[faceIndex];
                if (face.vertices.size() < 3) continue;

                bool satisfied = true;
                if (mode == ProjectionAnalysisMode::Planar || mode == ProjectionAnalysisMode::Circular) {
                    satisfied = faceIndex < m_errors.size() && m_errors[faceIndex] >= 0.0f && m_errors[faceIndex] <= tolerance;
                } else {
                    for (int index : face.vertices) {
                        if (index < 0 || index >= static_cast<int>(m_errors.size())) continue;
                        if (m_errors[index] > tolerance) satisfied = false;
                    }
                }

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
