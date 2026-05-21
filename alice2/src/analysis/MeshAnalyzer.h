#pragma once

#ifndef ALICE2_MESH_ANALYZER_H
#define ALICE2_MESH_ANALYZER_H

#include "../objects/MeshObject.h"
#include "../core/Renderer.h"
#include <string>
#include <vector>

namespace alice2 {

    enum class MeshAnalysisMode {
        PlanarVolume,
        PlanarPlane,
        Circular,
        Conical
    };

    struct MeshAnalysisResult {
        int total{0};
        int valid{0};
        int satisfied{0};
        int unsatisfied{0};
        float maxError{0.0f};
        float rmsError{0.0f};
    };

    struct MeshAnalysisDrawSettings {
        Color satisfiedColor{120.0f / 255.0f, 1.0f, 0.0f, 1.0f};
        Color unsatisfiedColor{1.0f, 0.0f, 120.0f / 255.0f, 1.0f};
        Color edgeColor{0.02f, 0.02f, 0.02f, 1.0f};
        Color fixedVertexColor{0.0f, 0.0f, 0.0f, 1.0f};
        Color circleColor{0.0f, 0.2f, 1.0f, 1.0f};
        Color tangentColor{1.0f, 0.55f, 0.0f, 1.0f};
        Color coneColor{0.0f, 0.35f, 1.0f, 1.0f};
        Color coneAxisColor{0.9f, 0.0f, 1.0f, 1.0f};
        bool drawFaces{true};
        bool drawEdges{true};
        bool drawFixedVertices{false};
        bool drawConstraintGuides{true};
        bool drawCones{true};
        bool drawConeAxes{true};
        float edgeWidth{1.0f};
        float fixedVertexSize{8.0f};
        float guideLineWidth{2.0f};
        float tangentScale{0.18f};
        float coneScale{0.35f};
        float coneAxisLength{0.35f};
        int circleSegments{64};
        int coneSegments{32};
    };

    class MeshAnalyzer {
    public:
        MeshAnalysisMode mode{MeshAnalysisMode::Circular};
        int iteration{0};
        float planarVolumeTolerance{1e-5f};
        float planarPlaneTolerance{1e-4f};
        float circularTolerance{1e-5f};
        float conicalTolerance{1e-5f};
        MeshAnalysisDrawSettings drawSettings;
        std::vector<int> fixedVertices;

        const MeshAnalysisResult& analyze(const MeshObject& mesh);
        std::string print() const;
        void draw(Renderer& renderer) const;

    private:
        const MeshObject* m_mesh{nullptr};
        MeshAnalysisResult m_result;
        std::vector<float> m_errors;

        float activeTolerance() const;
        float planarVolumeFaceError(const MeshData& data, const MeshFace& face) const;
        float planarPlaneFaceError(const MeshData& data, const MeshFace& face) const;
        float circularFaceError(const MeshData& data, const MeshFace& face) const;
        std::vector<float> conicalVertexErrors(const MeshData& data) const;
    };

} // namespace alice2

#endif // ALICE2_MESH_ANALYZER_H
