// #define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/io.h>
#include <zspace/toolsets.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace alice2;

namespace
{
    constexpr const char* kInputMeshPath = "data/block_01.obj";

    zSpace::zTs3DP ThreeDP;

    bool importMeshRequested = false;
    bool computeFeaturesRequested = false;
    bool computeSliceMeshesRequested = false;
    bool inputLoaded = false;
    bool slicingFeaturesComputed = false;
    bool drawCurrentUnroll = false;
    bool drawAllSlices = false;
    bool drawInputMesh = true;
    int currentSliceId = 0;
    int seedFromVertexId = 7;
    int seedToVertexId = 11;
    int seedHorizontalVertexId = 5;
    std::string statusMessage = "Press C to import mesh.";

    class ScopedStepTimer
    {
    public:
        explicit ScopedStepTimer(const std::string& name)
            : m_name(name), m_start(std::chrono::high_resolution_clock::now())
        {
        }

        double elapsedMs() const
        {
            const auto now = std::chrono::high_resolution_clock::now();
            return std::chrono::duration<double, std::milli>(now - m_start).count();
        }

        void print() const
        {
            std::cout << "[3DP timing] " << m_name << ": " << elapsedMs() << " ms" << std::endl;
        }

    private:
        std::string m_name;
        std::chrono::high_resolution_clock::time_point m_start;
    };

    std::string formatTimingStatus(const std::string& label, double milliseconds)
    {
        std::ostringstream stream;
        if (!label.empty()) stream << label << ": ";
        stream << milliseconds << " ms";
        return stream.str();
    }

    std::string sliceFeatureStatus()
    {
        const auto& features = ThreeDP.slicingFeatures();
        return "Slices: " + std::to_string(ThreeDP.sliceMeshes().size()) +
            " | loops: " + std::to_string(features.edgeLoopCount) +
            " | bottom/top faces: " + std::to_string(features.bottomStripFaceIds.size()) +
            "/" + std::to_string(features.topStripFaceIds.size());
    }

    void createFallbackMesh(zSpace::zObjectMesh& mesh)
    {
        zSpace::zPointArray positions = {
            zSpace::zPoint(-1.5f, -0.5f, 0.0f),
            zSpace::zPoint(1.5f, -0.5f, 0.0f),
            zSpace::zPoint(1.5f, 0.5f, 0.0f),
            zSpace::zPoint(-1.5f, 0.5f, 0.0f),
            zSpace::zPoint(-1.5f, -0.5f, 1.0f),
            zSpace::zPoint(1.5f, -0.5f, 1.0f),
            zSpace::zPoint(1.5f, 0.5f, 1.0f),
            zSpace::zPoint(-1.5f, 0.5f, 1.0f)
        };

        zSpace::zIntArray faceCounts = { 4, 4, 4, 4, 4, 4 };
        zSpace::zIntArray faceConnects = {
            0, 1, 2, 3,
            4, 7, 6, 5,
            0, 4, 5, 1,
            1, 5, 6, 2,
            2, 6, 7, 3,
            3, 7, 4, 0
        };

        zSpace::zFnMesh fn(mesh);
        fn.create(positions, faceCounts, faceConnects);
    }

    zSpace::zObjectMesh translatedMesh(const zSpace::zObjectMesh& source, const zSpace::zPoint& offset)
    {
        zSpace::zObjectMesh result = source;
        zSpace::zFnMesh fn(result);
        zSpace::zPointArray positions;
        fn.getVertexPositions(positions);
        for (auto& p : positions) p = p + offset;
        fn.setVertexPositions(positions);
        return result;
    }
}

class zSpace3DPSdfSlicerSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace 3DP SDF Slicer"; }
    std::string getDescription() const override { return "Runs zTs3DP from key-triggered update logic."; }
    std::string getAuthor() const override { return "alice2 + zspace_toolsets"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(8.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

        ThreeDP.setPrintLayerHeight(1.0f);
        ThreeDP.setFieldResolution(200, 80);
        ThreeDP.setPrintWidth(0.004f);
        ThreeDP.setPrintSpacing(0.005f);
        ThreeDP.setSDFThreshold(0.0f);
        ThreeDP.setSlicingSeedVertices(seedFromVertexId, seedToVertexId, seedHorizontalVertexId);

        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);
        m_ui->addToggle("Unroll", UIRect{10.0f, 118.0f, 128.0f, 24.0f}, drawCurrentUnroll);
        m_ui->addToggle("All Slices", UIRect{10.0f, 148.0f, 128.0f, 24.0f}, drawAllSlices);
        m_ui->addToggle("Input Mesh", UIRect{10.0f, 178.0f, 128.0f, 24.0f}, drawInputMesh);
    }

    void update(float /*deltaTime*/) override
    {
        if (importMeshRequested)
        {
            ScopedStepTimer timer("C import mesh");
            importMeshRequested = false;
            slicingFeaturesComputed = false;
            currentSliceId = 0;

            ThreeDP.clearResults();
            inputLoaded = ThreeDP.readMesh(kInputMeshPath);

            if (!inputLoaded)
            {
                zSpace::zObjectMesh fallbackMesh;
                createFallbackMesh(fallbackMesh);
                ThreeDP.fromMesh(fallbackMesh);
                statusMessage = "Input mesh missing. Loaded fallback mesh.";
            }
            else
            {
                statusMessage = "Imported data/block_01.obj.";
            }
            const double elapsedMs = timer.elapsedMs();
            timer.print();
            statusMessage += " | " + formatTimingStatus("import", elapsedMs);
        }

        if (computeFeaturesRequested)
        {
            ScopedStepTimer timer("P compute feature loops");
            computeFeaturesRequested = false;
            zSpace::zFnMesh fnInput(ThreeDP.inputMesh());
            if (fnInput.numVertices() == 0)
            {
                statusMessage = "Press C to import a mesh before computing features.";
                return;
            }
            ThreeDP.computeSlicingFeatures();
            currentSliceId = 0;
            const auto& features = ThreeDP.slicingFeatures();
            slicingFeaturesComputed = !features.cornerVertexIds.empty() || features.edgeLoopCount > 0;
            statusMessage = sliceFeatureStatus();
            const double elapsedMs = timer.elapsedMs();
            timer.print();
            statusMessage += " | " + formatTimingStatus("loops", elapsedMs);
        }

        if (computeSliceMeshesRequested)
        {
            ScopedStepTimer totalTimer("O total slice mesh step");
            computeSliceMeshesRequested = false;
            zSpace::zFnMesh fnInput(ThreeDP.inputMesh());
            if (fnInput.numVertices() == 0)
            {
                statusMessage = "Press C to import a mesh before slicing.";
                return;
            }

            double sliceMs = 0.0;
            double unrollMs = 0.0;
            {
                ScopedStepTimer timer("O compute slice meshes");
                ThreeDP.computeSlices();
                sliceMs = timer.elapsedMs();
                timer.print();
            }
            {
                ScopedStepTimer timer("O compute unrolled slices");
                ThreeDP.computeUnrolledSlices();
                unrollMs = timer.elapsedMs();
                timer.print();
            }
            currentSliceId = 0;
            const auto& features = ThreeDP.slicingFeatures();
            slicingFeaturesComputed = !ThreeDP.sliceMeshes().empty() || !features.cornerVertexIds.empty() || features.edgeLoopCount > 0;
            statusMessage = sliceFeatureStatus();
            if (ThreeDP.sliceMeshes().empty())
            {
                statusMessage += " | O produced no meshes.";
            }
            const double totalMs = totalTimer.elapsedMs();
            totalTimer.print();
            statusMessage += " | slices: " + formatTimingStatus("", sliceMs);
            statusMessage += " | unroll: " + formatTimingStatus("", unrollMs);
            statusMessage += " | total: " + formatTimingStatus("", totalMs);
        }
    }

    void draw(Renderer& renderer, Camera& /*camera*/) override
    {
        zDisplayMeshSetting inputDisplay;
        inputDisplay.showVertices = false;
        inputDisplay.showEdges = true;
        inputDisplay.showFaces = true;
        inputDisplay.faceColor = Color(0.32f, 0.34f, 0.36f, 0.22f);
        inputDisplay.edgeColor = Color(0.72f, 0.72f, 0.76f, 0.8f);
        inputDisplay.edgeWidth = 1.0f;

        if (drawInputMesh)
        {
            scene().draw(ThreeDP.inputMesh(), inputDisplay);
        }

        if (slicingFeaturesComputed)
        {
            scene().draw(ThreeDP.cornerPoints(), Display::points(Color(1.0f, 0.0f, 0.0f, 1.0f), 8.0f));

            zDisplayMeshSetting topDisplay;
            topDisplay.showVertices = false;
            topDisplay.showEdges = true;
            topDisplay.showFaces = false;
            topDisplay.edgeColor = Color(0.0f, 0.0f, 1.0f, 1.0f); // blue
            topDisplay.edgeWidth = 2.0f;
            {
                zSpace::zFnMesh fnTop(ThreeDP.topMesh());
                if (fnTop.numVertices() > 0)
                {
                    scene().draw(ThreeDP.topMesh(), topDisplay);
                }
            }

            zDisplayMeshSetting bottomDisplay;
            bottomDisplay.showVertices = false;
            bottomDisplay.showEdges = true;
            bottomDisplay.showFaces = false;
            bottomDisplay.edgeColor = Color(0.0f, 1.0f, 0.0f, 1.0f); // green
            bottomDisplay.edgeWidth = 2.0f;
            {
                zSpace::zFnMesh fnBottom(ThreeDP.bottomMesh());
                if (fnBottom.numVertices() > 0)
                {
                    scene().draw(ThreeDP.bottomMesh(), bottomDisplay);
                }
            }

        }

        if (slicingFeaturesComputed)
        {
            auto& slices = ThreeDP.sliceMeshes();
            auto& unrolled = ThreeDP.unrolledSliceMeshes();

            if (!slices.empty())
            {
                currentSliceId = std::max(0, std::min(currentSliceId, static_cast<int>(slices.size()) - 1));

                zDisplayMeshSetting sliceDisplay;
                sliceDisplay.showVertices = false;
                sliceDisplay.showEdges = false;
                sliceDisplay.showFaces = true;
                sliceDisplay.faceColor = Color(0.1f, 0.45f, 1.0f, 0.18f);
                sliceDisplay.edgeColor = Color(0.1f, 0.55f, 1.0f, 1.0f);
                sliceDisplay.edgeWidth = 1.5f;

                if (drawAllSlices)
                {
                    for (auto& slice : slices) scene().draw(slice, sliceDisplay);
                }
                else
                {
                    scene().draw(slices[currentSliceId], sliceDisplay);
                }

                if (drawCurrentUnroll && currentSliceId < static_cast<int>(unrolled.size()))
                {
                    zDisplayMeshSetting unrollDisplay;
                    unrollDisplay.showVertices = true;
                    unrollDisplay.showEdges = true;
                    unrollDisplay.showFaces = true;
                    unrollDisplay.faceColor = Color(1.0f, 0.1f, 0.62f, 0.2f);
                    unrollDisplay.edgeColor = Color(1.0f, 0.1f, 0.62f, 1.0f);
                    unrollDisplay.edgeWidth = 2.0f;

                    zSpace::zObjectMesh unrollMesh = translatedMesh(unrolled[currentSliceId], zSpace::zPoint(0.0f, 2.0f, 0.0f));
                    scene().draw(unrollMesh, unrollDisplay);
                }
            }
        }

        renderer.setColor(Color(0.02f, 0.02f, 0.02f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString("C import mesh | P compute loops | O compute slice meshes | W/S change slice", 10, 52);
        renderer.drawString("Slice: " + std::to_string(currentSliceId) + " / " + std::to_string(ThreeDP.sliceMeshes().empty() ? 0 : static_cast<int>(ThreeDP.sliceMeshes().size()) - 1), 10, 74);
        renderer.drawString(statusMessage, 10, 96);
        if (m_ui) m_ui->draw(renderer);
    }

    bool onKeyPress(unsigned char key, int /*x*/, int /*y*/) override
    {
        if (key == 'c' || key == 'C') importMeshRequested = true;
        if (key == 'p' || key == 'P') computeFeaturesRequested = true;
        if (key == 'o' || key == 'O') computeSliceMeshesRequested = true;
        if ((key == 'w' || key == 'W') && !ThreeDP.sliceMeshes().empty()) ++currentSliceId;
        if ((key == 's' || key == 'S') && !ThreeDP.sliceMeshes().empty() && currentSliceId > 0) --currentSliceId;
        return true;
    }

    bool onMousePress(int button, int state, int x, int y) override
    {
        if (m_ui && m_ui->onMousePress(button, state, x, y)) return true;
        return false;
    }

    bool onMouseMove(int x, int y) override
    {
        if (m_ui && m_ui->onMouseMove(x, y)) return true;
        return false;
    }

private:
    std::unique_ptr<SimpleUI> m_ui;
};

ALICE2_REGISTER_SKETCH_AUTO(zSpace3DPSdfSlicerSketch)

#endif // __MAIN__
