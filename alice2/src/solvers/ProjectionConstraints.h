#pragma once

#ifndef ALICE2_PROJECTION_CONSTRAINTS_H
#define ALICE2_PROJECTION_CONSTRAINTS_H

#include "ProjectionSolver.h"

namespace alice2 {

    class PlanarFaceConstraint : public ProjectionConstraint {
    public:
        void project(const MeshObject& mesh,
                     const ProjectionSolverSettings& settings,
                     std::vector<ProjectionTarget>& targets) const override;
    };

    class CircularFaceConstraint : public ProjectionConstraint {
    public:
        void project(const MeshObject& mesh,
                     const ProjectionSolverSettings& settings,
                     std::vector<ProjectionTarget>& targets) const override;
    };

    class ConicalVertexConstraint : public ProjectionConstraint {
    public:
        void project(const MeshObject& mesh,
                     const ProjectionSolverSettings& settings,
                     std::vector<ProjectionTarget>& targets) const override;
    };

} // namespace alice2

#endif // ALICE2_PROJECTION_CONSTRAINTS_H
