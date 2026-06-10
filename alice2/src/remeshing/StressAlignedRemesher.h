#pragma once

#ifndef ALICE2_STRESS_ALIGNED_REMESHER_H
#define ALICE2_STRESS_ALIGNED_REMESHER_H

#include "../analysis/StressAnalyzer.h"
#include "../objects/MeshObject.h"
#include <vector>

namespace alice2 {

    struct StressAlignedStreamlines {
        std::vector<TensorStreamline> primary;
        std::vector<TensorStreamline> secondary;
    };

    class StressAlignedRemesher {
    public:
        void setSpacing(float spacing);
        void setPrimaryOffset(float offset);
        void setSecondaryOffset(float offset);
        void setMaxSteps(int steps);

        float getSpacing() const { return spacing_; }
        float getPrimaryOffset() const { return primaryOffset_; }
        float getSecondaryOffset() const { return secondaryOffset_; }
        int getMaxSteps() const { return maxSteps_; }

        bool extractStreamlines(const MeshData& mesh, const TensorField& field);
        bool extractStreamlines(const MeshObject& mesh, const StressAnalyzer& analyzer);
        void clear();

        const StressAlignedStreamlines& getStreamlines() const { return streamlines_; }

    private:
        std::vector<TensorStreamline> extractDirection(const MeshData& mesh,
                                                       const TensorField& field,
                                                       bool usePrimary) const;

        float spacing_{0.12f};
        float primaryOffset_{0.0f};
        float secondaryOffset_{0.0f};
        int maxSteps_{160};
        StressAlignedStreamlines streamlines_;
    };

} // namespace alice2

#endif // ALICE2_STRESS_ALIGNED_REMESHER_H
