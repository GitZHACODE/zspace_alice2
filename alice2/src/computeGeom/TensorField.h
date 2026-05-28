#pragma once

#ifndef ALICE2_TENSOR_FIELD_H
#define ALICE2_TENSOR_FIELD_H

#include "../utils/Math.h"
#include <vector>

namespace alice2 {

    struct FaceStressTensor {
        float xx{0.0f};
        float yy{0.0f};
        float xy{0.0f};
        float majorValue{0.0f};
        float minorValue{0.0f};
        float magnitude{0.0f};
        Vec3 majorDirection{1.0f, 0.0f, 0.0f};
        Vec3 minorDirection{0.0f, 1.0f, 0.0f};
    };

    class TensorField {
    public:
        void clear() { tensors_.clear(); }
        void resize(size_t count) { tensors_.resize(count); }
        size_t size() const { return tensors_.size(); }
        bool empty() const { return tensors_.empty(); }

        FaceStressTensor& operator[](size_t index) { return tensors_[index]; }
        const FaceStressTensor& operator[](size_t index) const { return tensors_[index]; }

        std::vector<FaceStressTensor>& tensors() { return tensors_; }
        const std::vector<FaceStressTensor>& tensors() const { return tensors_; }

        std::vector<Vec3> majorDirections() const {
            std::vector<Vec3> directions;
            directions.reserve(tensors_.size());
            for (const auto& tensor : tensors_) directions.push_back(tensor.majorDirection);
            return directions;
        }

        std::vector<Vec3> minorDirections() const {
            std::vector<Vec3> directions;
            directions.reserve(tensors_.size());
            for (const auto& tensor : tensors_) directions.push_back(tensor.minorDirection);
            return directions;
        }

        std::vector<float> magnitudes() const {
            std::vector<float> values;
            values.reserve(tensors_.size());
            for (const auto& tensor : tensors_) values.push_back(tensor.magnitude);
            return values;
        }

    private:
        std::vector<FaceStressTensor> tensors_;
    };

} // namespace alice2

#endif // ALICE2_TENSOR_FIELD_H
