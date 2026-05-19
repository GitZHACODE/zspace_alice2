#pragma once

#ifndef ALICE2_PROJECTION_CONSTRAINT_ANALYZER_H
#define ALICE2_PROJECTION_CONSTRAINT_ANALYZER_H

#include "../objects/MeshObject.h"
#include "../core/Renderer.h"
#include <string>
#include <vector>

namespace alice2 {

    enum class ProjectionAnalysisMode {
        Planar,
        Circular,
        Conical
    };

    struct ProjectionAnalysisResult {
        int total{0};
        int valid{0};
        int satisfied{0};
        int unsatisfied{0};
        float maxError{0.0f};
        float rmsError{0.0f};
    };

    struct ProjectionAnalysisDrawSettings {
        Color satisfiedColor{120.0f / 255.0f, 1.0f, 0.0f, 1.0f};
        Color unsatisfiedColor{1.0f, 0.0f, 120.0f / 255.0f, 1.0f};
        Color edgeColor{0.02f, 0.02f, 0.02f, 1.0f};
        Color fixedVertexColor{0.0f, 0.0f, 0.0f, 1.0f};
        bool drawFaces{true};
        bool drawEdges{true};
        bool drawFixedVertices{false};
        float edgeWidth{1.0f};
        float fixedVertexSize{8.0f};
    };

    class ProjectionConstraintAnalyzer {
    public:
        ProjectionAnalysisMode mode{ProjectionAnalysisMode::Circular};
        int iteration{0};
        float tolerance{1e-5f};
        ProjectionAnalysisDrawSettings drawSettings;
        std::vector<int> fixedVertices;

        const ProjectionAnalysisResult& analyze(const MeshObject& mesh);
        std::string print() const;
        void draw(Renderer& renderer) const;

    private:
        const MeshObject* m_mesh{nullptr};
        ProjectionAnalysisResult m_result;
        std::vector<float> m_errors;

        float planarFaceError(const MeshData& data, const MeshFace& face) const;
        float circularFaceError(const MeshData& data, const MeshFace& face) const;
        std::vector<float> conicalVertexErrors(const MeshData& data) const;
    };

} // namespace alice2

#endif // ALICE2_PROJECTION_CONSTRAINT_ANALYZER_H
