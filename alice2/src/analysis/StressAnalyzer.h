#pragma once

#ifndef ALICE2_STRESS_ANALYZER_H
#define ALICE2_STRESS_ANALYZER_H

#include "../computeGeom/TensorField.h"
#include "../core/Renderer.h"
#include "../objects/MeshObject.h"
#include <unordered_map>
#include <vector>

namespace alice2 {

    struct TensorCrossSign {
        int faceIndex{-1};
        Vec3 center;
        Vec3 majorStart;
        Vec3 majorEnd;
        Vec3 minorStart;
        Vec3 minorEnd;
        float magnitude{0.0f};
    };

    using TensorStreamline = std::vector<Vec3>;

    struct StressAnalysisDrawSettings {
        bool drawColoredMesh{true};
        bool drawMeshEdges{true};
        bool drawBoundaryConditions{true};
        bool drawCrossField{true};
        bool drawStreamlines{false};
        bool drawMajorStreamlines{true};
        bool drawMinorStreamlines{true};
        float crossScale{0.055f};
        float loadScale{0.045f};
        float fixedVertexSize{7.0f};
        float edgeWidth{1.0f};
        float majorLineWidth{1.5f};
        float minorLineWidth{1.0f};
        int streamlineSeedStride{2};
        int streamlineSteps{48};
        float streamlineStepLength{0.026f};
        Color edgeColor{0.82f, 0.82f, 0.82f, 1.0f};
        Color loadColor{0.95f, 0.15f, 0.05f, 1.0f};
        Color fixedVertexColor{0.0f, 0.0f, 0.0f, 1.0f};
        Color majorColor{0.90f, 0.05f, 0.12f, 1.0f};
        Color minorColor{0.0f, 0.28f, 0.95f, 1.0f};
        Color lowMagnitudeColor{0.08f, 0.22f, 0.95f, 1.0f};
        Color highMagnitudeColor{0.95f, 0.12f, 0.06f, 1.0f};
    };

    class StressAnalyzer {
    public:
        bool solveLinearPlaneStress(const MeshObject& mesh);
        bool solveVerticalSlab(const MeshObject& mesh);

        void setFixedVertices(const std::vector<int>& vertexIds);
        void setForce(int vertexId, Vec3 force);
        void setForces(const std::vector<int>& vertexIds, Vec3 force);
        void clearBoundaryConditions();
        void clearForces();

        const std::vector<int>& getFixedVertices() const { return fixedVertices_; }
        const std::unordered_map<int, Vec3>& getForces() const { return forces_; }

        void setFieldSmoothingIterations(int iterations);
        void setStressMagnitudeThreshold(double threshold);
        void setUseMajorStressDirection(bool enabled) { useMajorStressDirection_ = enabled; }

        const std::vector<Vec3>& getDisplacements() const { return displacements_; }
        const TensorField& getElementStressTensors() const { return stressField_; }
        const TensorField& getSmoothedCrossField() const { return smoothedCrossField_; }
        std::vector<Vec3> getPrincipalStressDirections() const;
        std::vector<float> getStressMagnitudes() const;

        void colorMeshByMagnitude(MeshObject& mesh,
                                  Color low = Color(0.08f, 0.22f, 0.95f, 1.0f),
                                  Color high = Color(0.95f, 0.12f, 0.06f, 1.0f)) const;
        std::vector<TensorCrossSign> extractCrossSigns(const MeshData& mesh, float scale = 0.06f) const;
        std::vector<TensorStreamline> extractStreamlines(const MeshData& mesh,
                                                         int seedStride = 8,
                                                         int steps = 12,
                                                         float stepLength = 0.08f,
                                                         bool useMajorDirection = true) const;
        void draw(Renderer& renderer, const MeshObject& mesh, const StressAnalysisDrawSettings& settings) const;
        void drawColoredMesh(Renderer& renderer, const MeshData& mesh) const;
        void drawMeshEdges(Renderer& renderer, const MeshData& mesh, const Color& color, float width) const;
        void drawBoundaryConditions(Renderer& renderer, const MeshData& mesh, float loadScale, const Color& loadColor, const Color& fixedColor, float fixedSize) const;
        void drawCrossField(Renderer& renderer, const MeshData& mesh, float scale, const Color& majorColor, const Color& minorColor) const;
        void drawStreamlines(Renderer& renderer, const MeshData& mesh, const StressAnalysisDrawSettings& settings) const;

    private:
        void smoothStressField(const MeshData& data);

        std::vector<int> fixedVertices_;
        std::unordered_map<int, Vec3> forces_;
        std::vector<Vec3> displacements_;
        TensorField stressField_;
        TensorField smoothedCrossField_;
        int fieldSmoothingIterations_{3};
        double stressMagnitudeThreshold_{0.0};
        bool useMajorStressDirection_{true};
    };

} // namespace alice2

#endif // ALICE2_STRESS_ANALYZER_H
