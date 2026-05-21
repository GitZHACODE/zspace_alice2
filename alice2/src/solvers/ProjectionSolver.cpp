#include "ProjectionSolver.h"
#include <Eigen/Sparse>
#include <algorithm>
#include <map>

namespace alice2 {

    // Local/global projection solve after Shape-Up:
    // https://infoscience.epfl.ch/entities/publication/73724b50-05e0-4c05-a3a4-9523e51acce1
    // Constraint-list structure is close to ShapeOp:
    // https://shapeop.org/ShapeOpDoc.0.1.0/tutorial_add.html

    void ProjectionSolver::addConstraint(std::shared_ptr<ProjectionConstraint> constraint) {
        if (constraint) {
            m_constraints.push_back(constraint);
        }
    }

    void ProjectionSolver::clearConstraints() {
        m_constraints.clear();
    }

    bool ProjectionSolver::computeProjectionTargets(const MeshObject& mesh, std::vector<ProjectionTarget>& targets) const {
        for (const auto& constraint : m_constraints) {
            if (!constraint || !constraint->enabled) continue;
            constraint->project(mesh, settings, targets);
        }

        return !targets.empty();
    }

    bool ProjectionSolver::step(MeshObject& mesh) const {
        auto data = mesh.getMeshData();
        if (!data || data->vertices.empty() || data->faces.empty()) return false;

        std::vector<ProjectionTarget> targets;
        if (!computeProjectionTargets(mesh, targets) || targets.empty()) return false;

        int vertexCount = static_cast<int>(data->vertices.size());
        std::vector<bool> fixed = buildFixedVertexMask(*data);
        float strength = std::clamp(settings.strength, 0.0f, 1.0f);

        std::vector<Eigen::Triplet<double>> triplets;
        Eigen::VectorXd bx(targets.size() + vertexCount * 2);
        Eigen::VectorXd by(targets.size() + vertexCount * 2);
        Eigen::VectorXd bz(targets.size() + vertexCount * 2);
        int row = 0;

        triplets.reserve(targets.size() + vertexCount);

        for (const ProjectionTarget& target : targets) {
            double w = std::sqrt(std::max(0.0f, target.weight));
            if (w <= 0.0) continue;

            if (!target.vertices.empty() && target.vertices.size() == target.coefficients.size()) {
                bool valid = true;
                for (int vertex : target.vertices) {
                    if (vertex < 0 || vertex >= vertexCount) valid = false;
                }
                if (!valid) continue;

                for (size_t i = 0; i < target.vertices.size(); ++i) {
                    triplets.emplace_back(row, target.vertices[i], static_cast<double>(target.coefficients[i]) * w);
                }
            } else {
                if (target.vertex < 0 || target.vertex >= vertexCount) continue;
                triplets.emplace_back(row, target.vertex, w);
            }

            bx(row) = target.position.x * w;
            by(row) = target.position.y * w;
            bz(row) = target.position.z * w;
            ++row;
        }

        double shapeWeight = std::sqrt(std::max(0.0f, settings.shapePreservationWeight));
        for (int i = 0; i < vertexCount; ++i) {
            if (shapeWeight <= 0.0) continue;

            const Vec3& p = data->vertices[i].position;
            triplets.emplace_back(row, i, shapeWeight);
            bx(row) = p.x * shapeWeight;
            by(row) = p.y * shapeWeight;
            bz(row) = p.z * shapeWeight;
            ++row;
        }

        double anchorWeight = std::sqrt(std::max(0.0f, settings.anchorWeight));
        for (int i = 0; i < vertexCount; ++i) {
            if (!fixed[i]) continue;

            const Vec3& p = data->vertices[i].position;
            triplets.emplace_back(row, i, anchorWeight);
            bx(row) = p.x * anchorWeight;
            by(row) = p.y * anchorWeight;
            bz(row) = p.z * anchorWeight;
            ++row;
        }

        if (row == 0) return false;

        bx.conservativeResize(row);
        by.conservativeResize(row);
        bz.conservativeResize(row);

        Eigen::SparseMatrix<double> A(row, vertexCount);
        A.setFromTriplets(triplets.begin(), triplets.end());

        Eigen::SparseMatrix<double> AtA = A.transpose() * A;
        Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
        solver.compute(AtA);
        if (solver.info() != Eigen::Success) return false;

        Eigen::VectorXd rhsX = A.transpose() * bx;
        Eigen::VectorXd rhsY = A.transpose() * by;
        Eigen::VectorXd rhsZ = A.transpose() * bz;
        Eigen::VectorXd x = solver.solve(rhsX);
        Eigen::VectorXd y = solver.solve(rhsY);
        Eigen::VectorXd z = solver.solve(rhsZ);

        if (solver.info() != Eigen::Success) return false;

        bool changed = false;
        for (size_t i = 0; i < data->vertices.size(); ++i) {
            if (fixed[i]) continue;

            Vec3 target(static_cast<float>(x(static_cast<int>(i))),
                        static_cast<float>(y(static_cast<int>(i))),
                        static_cast<float>(z(static_cast<int>(i))));
            Vec3 delta = (target - data->vertices[i].position) * strength;
            if (delta.lengthSquared() <= 1e-16f) continue;
            data->vertices[i].position += delta;
            changed = true;
        }

        if (!changed) return false;

        data->calculateNormals();
        data->triangulationDirty = true;
        mesh.calculateBounds();
        return true;
    }

    int ProjectionSolver::solve(MeshObject& mesh) const {
        int steps = 0;
        int maxSteps = std::max(0, settings.maxIterations);

        while (steps < maxSteps && step(mesh)) {
            ++steps;
        }

        return steps;
    }

    std::vector<int> ProjectionSolver::fixedVertexIndices_allBoundary(const MeshObject& mesh) const {
        auto data = mesh.getMeshData();
        if (!data) return {};

        std::vector<bool> mask(data->vertices.size(), false);
        addBoundaryVerticesToFixedMask(*data, mask);
        std::vector<int> indices;
        for (size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) indices.push_back(static_cast<int>(i));
        }

        return indices;
    }

    std::vector<int> ProjectionSolver::fixedVertexIndices(const MeshObject& mesh, const std::vector<int>& vertexIds) const {
        auto data = mesh.getMeshData();
        if (!data) return {};

        std::vector<bool> mask(data->vertices.size(), false);
        addFixedVerticesToFixedMask(vertexIds, mask);
        std::vector<int> indices;
        for (size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) indices.push_back(static_cast<int>(i));
        }

        return indices;
    }

    std::vector<bool> ProjectionSolver::buildFixedVertexMask(const MeshData& data) const {
        std::vector<bool> fixed(data.vertices.size(), false);

        addFixedVerticesToFixedMask(settings.fixedVertices, fixed);

        if (settings.fixBoundaryVertices) {
            addBoundaryVerticesToFixedMask(data, fixed);
        }

        return fixed;
    }

    void ProjectionSolver::addFixedVerticesToFixedMask(const std::vector<int>& vertexIds, std::vector<bool>& fixed) const {
        for (int index : vertexIds) {
            if (index >= 0 && index < static_cast<int>(fixed.size())) {
                fixed[index] = true;
            }
        }
    }

    void ProjectionSolver::addBoundaryVerticesToFixedMask(const MeshData& data, std::vector<bool>& fixed) const {
        if (fixed.size() != data.vertices.size()) fixed.assign(data.vertices.size(), false);

        std::map<std::pair<int, int>, int> edgeUseCount;

        for (const MeshFace& face : data.faces) {
            for (size_t i = 0; i < face.vertices.size(); ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % face.vertices.size()];
                if (a < 0 || b < 0) continue;
                if (a >= static_cast<int>(data.vertices.size())) continue;
                if (b >= static_cast<int>(data.vertices.size())) continue;
                if (a > b) std::swap(a, b);
                ++edgeUseCount[{a, b}];
            }
        }

        for (const auto& edge : edgeUseCount) {
            if (edge.second != 1) continue;
            fixed[edge.first.first] = true;
            fixed[edge.first.second] = true;
        }
    }

} // namespace alice2
