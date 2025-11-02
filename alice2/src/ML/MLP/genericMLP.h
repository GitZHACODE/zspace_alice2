#pragma once
#include <vector>
#include <functional>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdio>

#include <vector>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include "alice2.h"

using namespace alice2;

//------------------------------------------------------------------ MLP base class

class MLP
{
public:
    int inputDim = 2;
    int outputDim = 1;
    std::vector<int> hiddenDims = { 8, 8 };

    std::vector<std::vector<std::vector<float>>> W;
    std::vector<std::vector<float>> b;
    std::vector<std::vector<float>> activations;

    MLP(){}

    MLP(int inDim, std::vector<int> hidden, int outDim)
    {
        initialize(inDim, hidden, outDim);
    }

    void initialize(int inDim, std::vector<int> hidden, int outDim)
    {
        inputDim = inDim;
        hiddenDims = hidden;
        outputDim = outDim;

        std::vector<int> layerDims = { inputDim };
        layerDims.insert(layerDims.end(), hiddenDims.begin(), hiddenDims.end());
        layerDims.push_back(outputDim);

        W.clear(); b.clear();
        for (int l = 0; l < layerDims.size() - 1; ++l)
        {
            int inSize = layerDims[l];
            int outSize = layerDims[l + 1];
            W.push_back(std::vector<std::vector<float>>(outSize, std::vector<float>(inSize)));
            b.push_back(std::vector<float>(outSize));
            for (auto& w_row : W[l])
                for (auto& w : w_row)
                    w = ((float)rand() / RAND_MAX - 0.5f) * 0.1f;
        }
    }

    std::vector<float> forward(std::vector<float>& x)
    {
        activations.clear();
        activations.push_back(x);
        std::vector<float> a = x;

        for (int l = 0; l < W.size(); ++l)
        {
            std::vector<float> z(b[l]);
            for (int i = 0; i < W[l].size(); ++i)
                for (int j = 0; j < W[l][i].size(); ++j)
                    z[i] += W[l][i][j] * a[j];

            if (l < W.size() - 1)
                for (auto& val : z) val = std::tanh(val);

            activations.push_back(z);
            a = z;
        }
        return a;
    }

    virtual float computeLoss(std::vector<float>& y_pred, std::vector<float>& y_true)
    {
        float loss = 0.0f;
        for (int i = 0; i < y_pred.size(); ++i)
        {
            float err = y_pred[i] - y_true[i];
            loss += err * err;
        }
        return loss / y_pred.size();
    }

    virtual void computeGradient(std::vector<float>& x, std::vector<float>& y_true, std::vector<float>& gradOut)
    {
        std::vector<float> y_pred = forward(x);
        gradOut.assign(outputDim, 0.0f);
        for (int i = 0; i < outputDim; ++i)
        {
            gradOut[i] = 2.0f * (y_pred[i] - y_true[i]) / outputDim;
        }
    }

    void backward(std::vector<float>& gradOut, float lr)
    {
        std::vector<float> delta = gradOut;

        for (int l = W.size() - 1; l >= 0; --l)
        {
            std::vector<float> prev = activations[l];
            std::vector<float> newDelta(prev.size(), 0.0f);

            for (int i = 0; i < W[l].size(); ++i)
            {
                for (int j = 0; j < W[l][i].size(); ++j)
                {
                    newDelta[j] += delta[i] * W[l][i][j];
                    W[l][i][j] -= lr * delta[i] * prev[j];
                }
                b[l][i] -= lr * delta[i];
            }

            if (l > 0)
            {
                for (int i = 0; i < newDelta.size(); ++i)
                {
                    float a = activations[l][i];
                    newDelta[i] *= (1 - a * a); // tanh derivative
                }
                delta = newDelta;
            }
        }
    }

    std::vector<float> backwardWithInputGrad(const std::vector<float>& gradOut, float lr)
    {
        if (activations.empty())
            throw std::runtime_error("MLP::backwardWithInputGrad requires a prior forward pass.");

        if (gradOut.size() != static_cast<size_t>(outputDim))
            throw std::runtime_error("MLP::backwardWithInputGrad received gradOut with unexpected size.");

        std::vector<float> delta(gradOut);
        std::vector<float> inputGrad(inputDim, 0.0f);

        for (int l = static_cast<int>(W.size()) - 1; l >= 0; --l)
        {
            const std::vector<float>& prev = activations[l];
            std::vector<float> newDelta(prev.size(), 0.0f);

            for (int i = 0; i < W[l].size(); ++i)
            {
                for (int j = 0; j < W[l][i].size(); ++j)
                {
                    newDelta[j] += delta[i] * W[l][i][j];
                    W[l][i][j] -= lr * delta[i] * prev[j];
                }
                b[l][i] -= lr * delta[i];
            }

            if (l == 0)
            {
                inputGrad = newDelta;
            }

            if (l > 0)
            {
                for (int i = 0; i < newDelta.size(); ++i)
                {
                    float a = activations[l][i];
                    newDelta[i] *= (1 - a * a); // tanh derivative
                }
                delta = newDelta;
            }
        }

        return inputGrad;
    }

    /**
     * Visualize MLP network structure with nodes and connections
     * @param renderer The renderer to draw with
     * @param camera The camera (unused but kept for API compatibility)
     * @param topLeft Top-left corner position for the visualization
     * @param bboxWidth Width of the visualization bounding box
     * @param bboxHeight Height of the visualization bounding box
     */
    void visualize(Renderer& renderer, Camera& camera, const Vec3& topLeft = Vec3(400, 200, 0), float bboxWidth = 300.0f, float bboxHeight = 200.0f)
    {
        if (activations.empty()) return; // No data to visualize

        int numLayers = activations.size();
        float nodeRadius = 3.0f;

        // Compute max nodes per layer for vertical spacing
        int maxNodesPerLayer = 0;
        for (const auto& layer : activations) {
            maxNodesPerLayer = std::max(maxNodesPerLayer, (int)layer.size());
        }

        // Ensure reasonable spacing
        float layerSpacing = (numLayers > 1) ? bboxWidth / (numLayers - 1) : 150.0f;
        float verticalSpacing = (maxNodesPerLayer > 1) ? std::min(bboxHeight / (maxNodesPerLayer - 1), 30.0f) : 20.0f;

        std::vector<std::vector<Vec3>> nodePositions(numLayers);

        // Compute node positions with better centering
        for (int l = 0; l < numLayers; l++) {
            int numNodes = activations[l].size();
            if (numNodes == 0) continue;

            // Center nodes vertically within the bounding box
            float totalHeight = (numNodes - 1) * verticalSpacing;
            float yStart = topLeft.y + (bboxHeight - totalHeight) * 0.5f;

            for (int i = 0; i < numNodes; i++) {
                float x = topLeft.x + l * layerSpacing;
                float y = yStart + i * verticalSpacing;
                nodePositions[l].push_back(Vec3(x, y, topLeft.z));
            }
        }

        // Draw weight connections (only significant weights to avoid clutter)
        for (int l = 0; l < numLayers - 1; l++) {
            if (l >= W.size()) continue; // Safety check

            int fromSize = activations[l].size();
            int toSize = activations[l + 1].size();

            for (int i = 0; i < fromSize && i < nodePositions[l].size(); i++) {
                for (int j = 0; j < toSize && j < nodePositions[l + 1].size(); j++) {
                    if (j >= W[l].size() || i >= W[l][j].size()) continue; // Safety check

                    float w = W[l][j][i];
                    float absW = fabs(w);

                    // Only draw significant connections to reduce visual clutter
                    if (absW < 0.05f) continue;

                    // Color based on weight value
                    float val = std::clamp(w * 3.0f, -1.0f, 1.0f);
                    float r, g, b;
                    get_jet_color(val, r, g, b);

                    Color color(r, g, b);
                    float width = std::clamp(absW * 3.0f, 0.5f, 1.0f);
                    width = 1.0f;

                    Vec2 start = Vec2(nodePositions[l][i].x, nodePositions[l][i].y);
                    Vec2 end = Vec2(nodePositions[l + 1][j].x, nodePositions[l + 1][j].y);
                    renderer.draw2dLine(start, end, color, width);
                }
            }
        }

        // Draw nodes with activation-based coloring
        for (int l = 0; l < numLayers; l++) {
            for (int i = 0; i < activations[l].size() && i < nodePositions[l].size(); i++) {
                float act = activations[l][i];

                // Clamp activation for better color visualization
                float clampedAct = std::clamp(act, -1.0f, 1.0f);
                float r, g, b;
                get_jet_color(clampedAct, r, g, b);

                Color color(r, g, b);
                Vec2 pos = Vec2(nodePositions[l][i].x, nodePositions[l][i].y);

                // Draw node with size based on activation magnitude
                float size = 2.0f + 2.0f * fabs(clampedAct);
                renderer.draw2dPoint(pos, color, size);
            }
        }

        // Draw layer labels for clarity
        renderer.setColor(Color(0.8f, 0.8f, 0.8f));
        for (int l = 0; l < numLayers && !nodePositions[l].empty(); l++) {
            std::string label = (l == 0) ? "Input" : (l == numLayers - 1) ? "Output" : "Hidden";
            float x = nodePositions[l][0].x;
            float y = topLeft.y - 20;
            renderer.drawString(label, x - 15, y);
        }
    }

    inline void get_jet_color(float value, float& r, float& g, float& b) {
        value = clamp(value, -1.0f, 1.0f);
        float normalized = (value + 1.0f) * 0.5f;
        float fourValue = 4.0f * normalized;

        r = clamp(std::min(fourValue - 1.5f, -fourValue + 4.5f), 0.0f, 1.0f);
        g = clamp(std::min(fourValue - 0.5f, -fourValue + 3.5f), 0.0f, 1.0f);
        b = clamp(std::min(fourValue + 0.5f, -fourValue + 2.5f), 0.0f, 1.0f);
    }
};