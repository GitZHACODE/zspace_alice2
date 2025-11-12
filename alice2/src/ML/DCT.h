#pragma once

#include <alice2.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

struct DCT2Basis2D {
    int resX = 0;
    int resY = 0;
    int Kx = 0;
    int Ky = 0;
    std::vector<std::pair<int, int>> modes;
    std::vector<std::vector<float>> B;

    void setup(int gridX, int gridY, int KxIn, int KyIn) {
        resX = gridX;
        resY = gridY;
        Kx = std::max(1, KxIn);
        Ky = std::max(1, KyIn);

        modes.clear();
        modes.reserve(Kx * Ky);
        for (int ky = 0; ky < Ky; ++ky) {
            for (int kx = 0; kx < Kx; ++kx) {
                modes.emplace_back(kx, ky);
            }
        }

        const size_t P = static_cast<size_t>(resX) * static_cast<size_t>(resY);
        B.assign(modes.size(), std::vector<float>(P, 0.f));

        auto alphaX = [&](int k) { return (k == 0) ? std::sqrt(1.0f / float(resX)) : std::sqrt(2.0f / float(resX)); };
        auto alphaY = [&](int k) { return (k == 0) ? std::sqrt(1.0f / float(resY)) : std::sqrt(2.0f / float(resY)); };

        std::vector<std::vector<float>> cx(Kx, std::vector<float>(resX));
        std::vector<std::vector<float>> cy(Ky, std::vector<float>(resY));
        for (int kx = 0; kx < Kx; ++kx) {
            for (int x = 0; x < resX; ++x) {
                cx[kx][x] = std::cos((alice2::PI / float(resX)) * (float(x) + 0.5f) * float(kx));
            }
        }
        for (int ky = 0; ky < Ky; ++ky) {
            for (int y = 0; y < resY; ++y) {
                cy[ky][y] = std::cos((alice2::PI / float(resY)) * (float(y) + 0.5f) * float(ky));
            }
        }

        for (size_t k = 0; k < modes.size(); ++k) {
            const int kx = modes[k].first;
            const int ky = modes[k].second;
            const float s = alphaX(kx) * alphaY(ky);
            auto& basis = B[k];
            for (int y = 0; y < resY; ++y) {
                for (int x = 0; x < resX; ++x) {
                    const size_t p = static_cast<size_t>(y) * static_cast<size_t>(resX) + static_cast<size_t>(x);
                    basis[p] = s * cx[kx][x] * cy[ky][y];
                }
            }
        }
    }

    int Kfull() const { return static_cast<int>(modes.size()); }
};
