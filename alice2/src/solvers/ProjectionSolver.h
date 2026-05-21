#pragma once

#ifndef ALICE2_PROJECTION_SOLVER_H
#define ALICE2_PROJECTION_SOLVER_H

#include "../objects/MeshObject.h"
#include <memory>
#include <utility>
#include <vector>

namespace alice2 {

    struct ProjectionTarget {
        int vertex{-1};
        Vec3 position;
        float weight{1.0f};
        std::vector<int> vertices;
        std::vector<float> coefficients;
    };

    struct ProjectionSolverSettings {
        int maxIterations{100};
        float strength{1.0f};
        float tolerance{1e-5f};
        float shapePreservationWeight{0.001f};
        float anchorWeight{1000.0f};
        bool fixBoundaryVertices{false};
        std::vector<int> fixedVertices;
    };

    class ProjectionConstraint {
    public:
        virtual ~ProjectionConstraint() = default;

        bool enabled{true};
        float weight{1.0f};

        virtual void project(const MeshObject& mesh,
                             const ProjectionSolverSettings& settings,
                             std::vector<ProjectionTarget>& targets) const = 0;
        virtual bool providesConicalAxes() const { return false; }
    };

    class ProjectionSolver {
    public:
        ProjectionSolverSettings settings;
        virtual ~ProjectionSolver() = default;

        void addConstraint(std::shared_ptr<ProjectionConstraint> constraint);

        template <typename T, typename... Args>
        std::shared_ptr<T> addConstraint(Args&&... args) {
            auto constraint = std::make_shared<T>(std::forward<Args>(args)...);
            addConstraint(constraint);
            return constraint;
        }

        void clearConstraints();
        bool step(MeshObject& mesh) const;
        int solve(MeshObject& mesh) const;
        const std::vector<Vec3>& resultVertexNormals() const;
        std::vector<int> fixedVertexIndices_allBoundary(const MeshObject& mesh) const;
        std::vector<int> fixedVertexIndices(const MeshObject& mesh, const std::vector<int>& vertexIds) const;

    protected:
        std::vector<std::shared_ptr<ProjectionConstraint>> m_constraints;
        mutable MeshObject* m_resultMesh{nullptr};
        mutable std::vector<Vec3> m_resultVertexNormals;

        bool computeProjectionTargets(const MeshObject& mesh, std::vector<ProjectionTarget>& targets) const;
        bool hasConicalAxesProvider() const;
        void updateResultNormals() const;
        std::vector<bool> buildFixedVertexMask(const MeshData& data) const;
        void addFixedVerticesToFixedMask(const std::vector<int>& vertexIds, std::vector<bool>& fixed) const;
        void addBoundaryVerticesToFixedMask(const MeshData& data, std::vector<bool>& fixed) const;
    };

} // namespace alice2

#endif // ALICE2_PROJECTION_SOLVER_H
