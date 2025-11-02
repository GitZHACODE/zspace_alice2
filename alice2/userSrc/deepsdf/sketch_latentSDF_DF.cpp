//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <ML/MLP/genericMLP.h>
#include <ML/MLP/AutoDecoderTrainer.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace alice2;
using json = nlohmann::json;

namespace {
std::pair<float, float> readRange(const json& j, const std::string& key)
{
    const auto& arr = j.at(key);
    if (!arr.is_array() || arr.size() != 2)
        throw std::runtime_error("Expected array[2] for key: " + key);
    return { static_cast<float>(arr[0].get<double>()), static_cast<float>(arr[1].get<double>()) };
}

std::vector<float> readScalarArray(const json& j)
{
    std::vector<float> values;
    values.reserve(j.size());
    for (const auto& entry : j)
    {
        values.push_back(static_cast<float>(entry.get<double>()));
    }
    return values;
}

inline Color valueToGray(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return Color(t, t, t);
}
}

class AutoDecoderSDFSketch : public ISketch {
public:
    AutoDecoderSDFSketch()
        : m_status("Press 'P' to train"),
          m_modelOutputPath("auto_decoder_model.json")
    {
    }

    std::string getName() const override { return "SDF AutoDecoder Trainer"; }
    std::string getDescription() const override { return "Train latent SDF decoder from JSON grid"; }
    std::string getAuthor() const override { return "alice2 User"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.0f, 0.0f, 0.0f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        const std::filesystem::path defaultPath = std::filesystem::path("inFieldStack.json");
        loadDataset(defaultPath.string());
    }

    void update(float /*time*/) override {}

    void draw(Renderer& renderer, Camera& /*camera*/) override
    {
        renderer.setColor(Color(0.9f, 0.9f, 0.9f));
        renderer.drawString(getName(), 15, 30);
        renderer.drawString(m_status, 15, 50);
        renderer.drawString("Loaded fields: " + std::to_string(m_originalGridFields.size()), 15, 70);
        renderer.drawString("Samples: " + std::to_string(m_samples.size()), 15, 90);
        renderer.drawString("Epochs run: " + std::to_string(m_totalEpochs), 15, 110);
        renderer.drawString("Press 'P' to train (" + std::to_string(m_epochsPerTrigger) + " epochs)", 15, 130);
        renderer.drawString("Press 'R' to reload dataset", 15, 150);

        const float topOriginal = 200.0f;
        const float topReconstruction = topOriginal + m_tileSize + 80.0f;

        if (!m_originalGridFields.empty())
        {
            renderer.setColor(Color(0.85f, 0.8f, 0.95f));
            renderer.drawString("Original Fields", 15, topOriginal - 20.0f);
            drawFieldRow(renderer, m_originalGridFields, topOriginal);
        }

        if (m_hasReconstruction && !m_reconstructedGridFields.empty())
        {
            renderer.setColor(Color(0.75f, 0.9f, 0.75f));
            renderer.drawString("Reconstructed Fields", 15, topReconstruction - 20.0f);
            drawFieldRow(renderer, m_reconstructedGridFields, topReconstruction);
        }
    }

    bool onKeyPress(unsigned char key, int /*x*/, int /*y*/) override
    {
        switch (key)
        {
        case 'p':
        case 'P':
            runTraining();
            return true;
        case 'r':
        case 'R':
            reloadDataset();
            return true;
        default:
            break;
        }
        return false;
    }

private:
    struct GridField
    {
        std::vector<float> values;
        float minValue = 0.0f;
        float maxValue = 0.0f;
    };

    bool loadDataset(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            m_status = "Failed to open: " + path;
            return false;
        }

        json data;
        try
        {
            file >> data;
        }
        catch (const std::exception& e)
        {
            m_status = std::string("JSON parse error: ") + e.what();
            return false;
        }

        try
        {
            m_gridResolutionX = data.at("scalar_field_XCount").get<int>();
            m_gridResolutionY = data.at("scalar_field_YCount").get<int>();
            std::tie(m_xMin, m_xMax) = readRange(data, "scalar_field_XSize");
            std::tie(m_yMin, m_yMax) = readRange(data, "scalar_field_YSize");

            if (m_gridResolutionX <= 0 || m_gridResolutionY <= 0)
                throw std::runtime_error("Counts must be positive");
            if (m_xMax <= m_xMin || m_yMax <= m_yMin)
                throw std::runtime_error("Invalid bounds in JSON");

            const std::size_t expectedSize = static_cast<std::size_t>(m_gridResolutionX) * static_cast<std::size_t>(m_gridResolutionY);

            struct FieldEntry
            {
                int index = 0;
                std::vector<float> values;
            };

            std::vector<FieldEntry> fieldEntries;
            fieldEntries.reserve(data.size());

            for (const auto& [key, value] : data.items())
            {
                constexpr const char* prefix = "scalar_field";
                constexpr const char* suffix = "_data";
                if (key.size() <= std::strlen(prefix) + std::strlen(suffix))
                    continue;
                if (!value.is_array())
                    continue;
                if (key.rfind(prefix, 0) != 0)
                    continue;
                if (key.find(suffix) != key.size() - std::strlen(suffix))
                    continue;

                const std::string indexString = key.substr(std::strlen(prefix), key.size() - std::strlen(prefix) - std::strlen(suffix));
                if (indexString.empty() || !std::all_of(indexString.begin(), indexString.end(), ::isdigit))
                    continue;

                FieldEntry entry;
                entry.index = std::stoi(indexString);
                entry.values = readScalarArray(value);
                if (entry.values.size() != expectedSize)
                    throw std::runtime_error("Field " + key + " has unexpected sample count");

                fieldEntries.emplace_back(std::move(entry));
            }

            if (fieldEntries.empty())
                throw std::runtime_error("No scalar_field*_data entries found in JSON");

            std::sort(fieldEntries.begin(), fieldEntries.end(),
                      [](const FieldEntry& a, const FieldEntry& b) { return a.index < b.index; });

            m_originalGridFields.clear();
            m_originalGridFields.reserve(fieldEntries.size());
            m_reconstructedGridFields.clear();
            m_hasReconstruction = false;
            m_samples.clear();
            m_samples.reserve(fieldEntries.size() * expectedSize);

            const float xStep = (m_gridResolutionX > 1)
                                    ? (m_xMax - m_xMin) / static_cast<float>(m_gridResolutionX - 1)
                                    : 0.0f;
            const float yStep = (m_gridResolutionY > 1)
                                    ? (m_yMax - m_yMin) / static_cast<float>(m_gridResolutionY - 1)
                                    : 0.0f;

            for (std::size_t idx = 0; idx < fieldEntries.size(); ++idx)
            {
                m_originalGridFields.emplace_back();
                GridField& grid = m_originalGridFields.back();
                grid.values = std::move(fieldEntries[idx].values);

                auto [minIt, maxIt] = std::minmax_element(grid.values.begin(), grid.values.end());
                grid.minValue = *minIt;
                grid.maxValue = *maxIt;

                const int shapeIndex = static_cast<int>(idx);
                for (int y = 0; y < m_gridResolutionY; ++y)
                {
                    const float yCoord = m_yMin + yStep * static_cast<float>(y);
                    for (int x = 0; x < m_gridResolutionX; ++x)
                    {
                        const float xCoord = m_xMin + xStep * static_cast<float>(x);
                        const std::size_t linearIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_gridResolutionX) + static_cast<std::size_t>(x);

                        AutoDecoderSample sample;
                        sample.shapeIndex = shapeIndex;
                        sample.coordinate = { xCoord, yCoord };
                        sample.sdf = grid.values[linearIndex];
                        m_samples.push_back(std::move(sample));
                    }
                }
            }

            setupTrainer(static_cast<int>(m_originalGridFields.size()));
            m_status = "Dataset loaded: " + path;
            m_datasetPath = path;
            return true;
        }
        catch (const std::exception& e)
        {
            m_status = std::string("Load failed: ") + e.what();
            m_originalGridFields.clear();
            m_reconstructedGridFields.clear();
            m_samples.clear();
            m_trainer.reset();
        }

        return false;
    }

    void setupTrainer(int numShapes)
    {
        if (numShapes <= 0)
            return;

        m_coordDim = 2;
        m_latentDim = std::max(8, std::min(32, numShapes * 4));

        const std::vector<int> hidden = { 64, 64, 64 };
        m_decoder.initialize(m_latentDim + m_coordDim, hidden, 1);

        m_trainer = std::make_unique<AutoDecoderTrainer>(m_decoder);
        m_trainer->initialize(numShapes, m_latentDim, m_coordDim);
        m_trainer->setSamples(m_samples);

        m_trainingConfig.epochs = 1;
        m_trainingConfig.learningRateWeights = 5e-4f;
        m_trainingConfig.learningRateLatent = 1e-3f;
        m_trainingConfig.latentRegularization = 1e-4f;
        m_trainingConfig.latentInitStd = 0.01f;
        m_trainingConfig.shuffleSeed = 2025u;

        m_totalEpochs = 0;
    }

    void reloadDataset()
    {
        if (m_datasetPath.empty())
            return;
        loadDataset(m_datasetPath);
    }

    void runTraining()
    {
        if (!m_trainer)
        {
            m_status = "Trainer not ready";
            return;
        }

        std::cout << "[AutoDecoder] Training start" << std::endl;
        for (int i = 0; i < m_epochsPerTrigger; ++i)
        {
            AutoDecoderTrainingStats stats = m_trainer->train(m_trainingConfig);
            ++m_totalEpochs;
            std::cout << "  Epoch " << m_totalEpochs
                      << ": avgLoss=" << stats.averageLoss
                      << ", lastLoss=" << stats.lastLoss
                      << ", samples=" << stats.totalSamples << std::endl;
        }

        generateReconstruction();

        const bool saved = m_trainer->saveToJson(m_modelOutputPath);
        if (saved)
        {
            std::cout << "[AutoDecoder] Saved model to " << m_modelOutputPath << std::endl;
            m_status = "Training complete. Model saved.";
        }
        else
        {
            std::cout << "[AutoDecoder] Failed to save model." << std::endl;
            m_status = "Training complete. Save failed.";
        }
    }

    void generateReconstruction()
    {
        if (!m_trainer || m_latentDim <= 0 || m_coordDim != 2)
            return;
        if (m_gridResolutionX <= 0 || m_gridResolutionY <= 0)
            return;

        const auto& latentCodes = m_trainer->getLatentCodes();
        if (latentCodes.empty())
            return;

        m_reconstructedGridFields.clear();
        m_reconstructedGridFields.resize(latentCodes.size());

        const float xStep = (m_gridResolutionX > 1)
                                ? (m_xMax - m_xMin) / static_cast<float>(m_gridResolutionX - 1)
                                : 0.0f;
        const float yStep = (m_gridResolutionY > 1)
                                ? (m_yMax - m_yMin) / static_cast<float>(m_gridResolutionY - 1)
                                : 0.0f;

        std::vector<float> input(static_cast<std::size_t>(m_latentDim + m_coordDim));

        for (std::size_t shape = 0; shape < latentCodes.size(); ++shape)
        {
            const auto& latent = latentCodes[shape];
            if (latent.size() != static_cast<std::size_t>(m_latentDim))
                continue;

            GridField& grid = m_reconstructedGridFields[shape];
            grid.values.resize(static_cast<std::size_t>(m_gridResolutionX) * static_cast<std::size_t>(m_gridResolutionY));
            grid.minValue = std::numeric_limits<float>::max();
            grid.maxValue = -std::numeric_limits<float>::max();

            for (int y = 0; y < m_gridResolutionY; ++y)
            {
                const float yCoord = m_yMin + yStep * static_cast<float>(y);
                for (int x = 0; x < m_gridResolutionX; ++x)
                {
                    const float xCoord = m_xMin + xStep * static_cast<float>(x);
                    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_gridResolutionX) + static_cast<std::size_t>(x);

                    std::copy(latent.begin(), latent.end(), input.begin());
                    input[m_latentDim + 0] = xCoord;
                    input[m_latentDim + 1] = yCoord;

                    const std::vector<float> prediction = m_decoder.forward(input);
                    const float value = prediction.empty() ? 0.0f : prediction[0];

                    grid.values[idx] = value;
                    grid.minValue = std::min(grid.minValue, value);
                    grid.maxValue = std::max(grid.maxValue, value);
                }
            }

            if (grid.minValue == std::numeric_limits<float>::max())
            {
                grid.minValue = 0.0f;
                grid.maxValue = 0.0f;
            }
        }

        m_hasReconstruction = true;
    }

    void drawFieldRow(Renderer& renderer, const std::vector<GridField>& grids, float top) const
    {
        if (grids.empty() || m_gridResolutionX <= 0 || m_gridResolutionY <= 0)
            return;

        const float gap = 25.0f;
        const float cellWidth = m_tileSize / static_cast<float>(m_gridResolutionX);
        const float cellHeight = m_tileSize / static_cast<float>(m_gridResolutionY);

        for (std::size_t fieldIdx = 0; fieldIdx < grids.size(); ++fieldIdx)
        {
            const float left = 20.0f + static_cast<float>(fieldIdx) * (m_tileSize + gap);
            drawFieldHeatmap(renderer, grids[fieldIdx], left, top, cellWidth, cellHeight);
            renderer.setColor(Color(0.7f, 0.7f, 0.9f));
            renderer.drawString("#" + std::to_string(fieldIdx), left, top + m_tileSize + 16.0f);
        }
    }

    void drawFieldHeatmap(Renderer& renderer, const GridField& field, float left, float top, float cellWidth, float cellHeight) const
    {
        const float invWidth = 1.0f / std::max(1.0f, static_cast<float>(m_gridResolutionX));
        const float invHeight = 1.0f / std::max(1.0f, static_cast<float>(m_gridResolutionY));

        const float safeMin = field.minValue;
        const float safeMax = field.maxValue;
        const float range = (safeMax - safeMin == 0.0f) ? 1.0f : (safeMax - safeMin);

        for (int y = 0; y < m_gridResolutionY; ++y)
        {
            for (int x = 0; x < m_gridResolutionX; ++x)
            {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_gridResolutionX) + static_cast<std::size_t>(x);
                const float value = field.values[idx];
                const float norm = (value - safeMin) / range;
                const Color color = valueToGray(norm);

                const float px = left + (static_cast<float>(x) + 0.5f) * cellWidth;
                const float py = top + (static_cast<float>(y) + 0.5f) * cellHeight;

                const float pointSize = std::max(cellWidth, cellHeight) * 0.8f;
                renderer.draw2dPoint(Vec2(px, py), color, pointSize);
            }
        }

        renderer.setColor(Color(0.2f, 0.2f, 0.2f));
        // renderer.draw2dRect(Vec2(left - cellWidth * 0.5f, top - cellHeight * 0.5f),
        //                     Vec2(m_tileSize + cellWidth, m_tileSize + cellHeight), 1.0f);
    }

    MLP m_decoder;
    std::unique_ptr<AutoDecoderTrainer> m_trainer;
    AutoDecoderTrainingConfig m_trainingConfig;

    std::vector<GridField> m_originalGridFields;
    std::vector<GridField> m_reconstructedGridFields;
    std::vector<AutoDecoderSample> m_samples;

    std::string m_status;
    std::string m_datasetPath;
    std::string m_modelOutputPath;

    int m_totalEpochs = 0;
    int m_epochsPerTrigger = 5;

    int m_gridResolutionX = 0;
    int m_gridResolutionY = 0;
    float m_xMin = 0.0f;
    float m_xMax = 0.0f;
    float m_yMin = 0.0f;
    float m_yMax = 0.0f;

    int m_latentDim = 0;
    int m_coordDim = 0;

    bool m_hasReconstruction = false;
    float m_tileSize = 120.0f;
};

ALICE2_REGISTER_SKETCH_AUTO(AutoDecoderSDFSketch)

#endif // __MAIN__
