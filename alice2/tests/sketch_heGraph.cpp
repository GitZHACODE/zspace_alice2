// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include "../src/computeGeom/ComputeGraph.h"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>
#include <cmath>
#include <iostream>
#include <memory>

using namespace alice2;

class HeGraphSketch : public ISketch {
private:
    std::shared_ptr<ComputeGraph> m_computeGraph;

    bool b_showVertices;
    bool b_showEdges;
    bool b_showHalfEdges;
    bool d_showNeighbors;

    int m_selectedVertexId;

public:
    HeGraphSketch()
        : b_showVertices(true)
        , b_showEdges(true)
        , b_showHalfEdges(false)
        , d_showNeighbors(false)
        , m_selectedVertexId(0) {
    }

    std::string getName() const override {
        return "Half-Edge Graph Demo";
    }

    std::string getDescription() const override {
        return "Visualizes graphs with half-edge connectivity queries";
    }

    void setup() override {
        scene().setBackgroundColor(Color(0.12f, 0.12f, 0.12f));
        scene().setShowGrid(true);
        scene().setGridSize(8.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

        createTestGraph();

        std::cout << "Half-Edge Graph Sketch initialized" << std::endl;
    }

    void update(float /*deltaTime*/) override {}

    void draw(Renderer& renderer, Camera& /*camera*/) override {
        if (!m_computeGraph) {
            return;
        }

        if (b_showHalfEdges) {
            drawHalfEdges(renderer);
        }

        if (d_showNeighbors) {
            drawVertexNeighbors(renderer);
        }

        drawUI(renderer);
    }

    bool onKeyPress(unsigned char key, int /*x*/, int /*y*/) override {
        if (!m_computeGraph) {
            return false;
        }

        switch (key) {
            case 'v':
                b_showVertices = !b_showVertices;
                m_computeGraph->setShowVertices(b_showVertices);
                return true;

            case 'e':
                b_showEdges = !b_showEdges;
                m_computeGraph->setShowEdges(b_showEdges);
                return true;

            case 'h':
                b_showHalfEdges = !b_showHalfEdges;
                return true;

            case 'n':
                d_showNeighbors = !d_showNeighbors;
                if (d_showNeighbors) {
                    logSelectedVertexInfo();
                }
                return true;

            case '1':
                cycleSelection(-1);
                return true;

            case '2':
                cycleSelection(1);
                return true;

            case 'w': {
                m_computeGraph->weld(0.15f);
                m_computeGraph->updateHalfEdgeData();
                const int vertexCount = static_cast<int>(m_computeGraph->getVertices().size());
                if (vertexCount > 0) {
                    m_selectedVertexId = std::clamp(m_selectedVertexId, 0, vertexCount - 1);
                } else {
                    m_selectedVertexId = 0;
                }
                printGraphStatistics();
                return true;
            }

            case 'u': {
                auto m_graphs = m_computeGraph->separate();
                std::cout << "Number of graphs after separating: " << m_graphs.size() << std::endl;
                return true;
            }

            case 'r':
                if (m_computeGraph->isPolyline()) {
                    m_computeGraph->resample(0.25f);
                    m_computeGraph->updateHalfEdgeData();
                    const int vertexCount = static_cast<int>(m_computeGraph->getVertices().size());
                    if (vertexCount > 0) {
                        m_selectedVertexId = std::clamp(m_selectedVertexId, 0, vertexCount - 1);
                    } else {
                        m_selectedVertexId = 0;
                    }
                    printGraphStatistics();
                } else {
                    std::cout << "Resample skipped: graph is not a single polyline" << std::endl;
                }
                return true;

            case 't':
                runGraphOperationSmokeTests();
                return true;
        }
        return false;
    }

    void cleanup() override {}

    std::string getAuthor() const override {
        return "alice2 User";
    }

private:
    void cycleSelection(int delta) {
        if (!m_computeGraph) {
            return;
        }

        const int count = static_cast<int>(m_computeGraph->getVertices().size());
        if (count <= 0) {
            m_selectedVertexId = 0;
            return;
        }

        m_selectedVertexId += delta;
        if (m_selectedVertexId < 0) {
            m_selectedVertexId = count - 1;
        } else if (m_selectedVertexId >= count) {
            m_selectedVertexId = 0;
        }

        if (d_showNeighbors) {
            logSelectedVertexInfo();
        }
    }

    void createTestGraph() {
        std::vector<Vec3> positions = {
            // Vec3(-3.0f, -1.0f, 0.0f),
            // Vec3(-2.99f, -1.0f, 0.0f),
            // Vec3(-1.0f, 0.5f, 0.0f),
            // Vec3(0.5f, -0.5f, 0.0f),
            // Vec3(2.0f, 1.5f, 0.0f),
            // Vec3(3.5f, 0.0f, 0.0f),
            // Vec3(0.0f, 3.0f, 0.0f),
            // Vec3(-2.0f, 2.5f, 0.0f),
            // Vec3(2.5f, -2.0f, 0.0f)

            Vec3(0.0f, 0.0f, 0.0f),
            Vec3(0.49f, 0.0f, 0.0f),
            Vec3(0.5f, 0.0f, 0.0f),
            Vec3(1.0f, 0.0f, 0.0f),

            Vec3(2.0f, 0.0f, 0.0f),
            Vec3(2.0f, 1.0f, 0.0f)

        };

        std::vector<std::pair<int, int>> edges = {
            // {0, 1},
            // {1, 2},
            // {2, 3},
            // {3, 4},
            // {1, 5},
            // {5, 6},
            // {2, 7},
            // {7, 4}

            {0, 1},
            {1, 2},
            {2, 3},

            {4, 5},
        };

        std::vector<Color> colors;
        colors.reserve(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(positions.size() - 1);
            colors.emplace_back(Color(0.2f + 0.6f * t, 0.6f + 0.3f * t, 1.0f - 0.5f * t, 1.0f));
        }

        m_computeGraph = std::make_shared<ComputeGraph>("HalfEdgeGraph", GraphData{}, false);
        m_computeGraph->createFromPositionsAndEdges(positions, edges, colors);
        m_computeGraph->updateHalfEdgeData();

        m_computeGraph->setShowVertices(b_showVertices);
        m_computeGraph->setShowEdges(b_showEdges);
        m_computeGraph->setVertexSize(9.0f);
        m_computeGraph->setEdgeWidth(2.5f);
        m_computeGraph->setEdgeColor(Color(0.7f, 0.7f, 0.9f));

        m_selectedVertexId = 0;
        logSelectedVertexInfo();

        scene().addObject(m_computeGraph);

        printGraphStatistics();
        runGraphOperationSmokeTests();
    }

    void logSelectedVertexInfo() const {
        if (!m_computeGraph) {
            return;
        }

        const auto& vertices = m_computeGraph->getVertices();
        if (vertices.empty() || m_selectedVertexId < 0 || m_selectedVertexId >= static_cast<int>(vertices.size())) {
            return;
        }

        auto vertex = m_computeGraph->getVertex(m_selectedVertexId);
        if (!vertex) {
            return;
        }

        auto neighbors = vertex->getNeighbors();
        std::cout << "Selected vertex " << vertex->getId() << " has " << neighbors.size() << " neighbors:";
        for (const auto& neighbor : neighbors) {
            if (neighbor) {
                std::cout << ' ' << neighbor->getId();
            }
        }
        std::cout << std::endl;
    }

    void runGraphOperationSmokeTests() {
        if (!m_computeGraph) {
            return;
        }

        std::cout << std::boolalpha;
        GraphObject baseGraph = m_computeGraph->duplicate();
        auto baseData = baseGraph.getGraphData();
        const size_t baseVerts = baseData ? baseData->vertices.size() : 0;
        const size_t baseEdges = baseData ? baseData->edges.size() : 0;

        std::cout << "\n=== GraphObject Operation Smoke Test ===" << std::endl;
        std::cout << "Base graph -> vertices: " << baseVerts
                  << ", edges: " << baseEdges
                  << ", polyline: " << baseGraph.isPolyline()
                  << ", closed: " << baseGraph.isClosed() << std::endl;

        GraphObject weldedGraph = baseGraph.duplicate();
        weldedGraph.weld(1e-4f);
        if (auto weldedData = weldedGraph.getGraphData()) {
            std::cout << "After weld -> vertices: " << weldedData->vertices.size()
                      << ", edges: " << weldedData->edges.size() << std::endl;
        }

        if (baseGraph.isPolyline()) {
            GraphObject resampledGraph = baseGraph.duplicate();
            resampledGraph.resample(0.25f);
            if (auto resampledData = resampledGraph.getGraphData()) {
                std::cout << "After resample(0.25) -> vertices: " << resampledData->vertices.size()
                          << ", edges: " << resampledData->edges.size()
                          << ", closed: " << resampledGraph.isClosed() << std::endl;
            }
        } else {
            std::cout << "Resample test skipped: base graph is not a single polyline" << std::endl;
        }

        auto components = baseGraph.separate();
        std::cout << "Separate -> components: " << components.size() << std::endl;
        if (!components.empty()) {
            const auto& first = components.front();
            if (auto compData = first.getGraphData()) {
                std::cout << "  Component[0] -> vertices: " << compData->vertices.size()
                          << ", edges: " << compData->edges.size()
                          << ", polyline: " << first.isPolyline()
                          << ", closed: " << first.isClosed() << std::endl;
            }
        }

        if (components.size() > 1) {
            GraphObject recombined = components.front().duplicate();
            for (size_t i = 1; i < components.size(); ++i) {
                recombined.combineWith(components[i]);
            }
            if (auto recombinedData = recombined.getGraphData()) {
                std::cout << "Recombined components -> vertices: " << recombinedData->vertices.size()
                          << ", edges: " << recombinedData->edges.size() << std::endl;
            }
        }

        std::cout << std::noboolalpha;
    }

    void printGraphStatistics() {
        if (!m_computeGraph) {
            return;
        }

        const auto& heData = m_computeGraph->getHeGraphData();
        std::cout << "\n=== Half-Edge Graph Statistics ===" << std::endl;
        std::cout << "Vertices: " << heData.vertices.size() << std::endl;
        std::cout << "Half-edges: " << heData.halfedges.size() << std::endl;
        std::cout << "Edges: " << heData.edges.size() << std::endl;
    }

    void drawHalfEdges(Renderer& renderer) {
        const auto& heData = m_computeGraph->getHeGraphData();
        for (const auto& halfedge : heData.halfedges) {
            auto start = halfedge->getStartVertex();
            auto end = halfedge->getVertex();
            if (!start || !end) {
                continue;
            }

            Vec3 startPos = start->getPosition();
            Vec3 endPos = end->getPosition();
            Vec3 direction = (endPos - startPos).normalized();
            Vec3 offsetEnd = startPos + direction * (endPos - startPos).length() * 0.85f;

            Color color = Color(0.2f, 1.0f, 0.6f);
            renderer.drawLine(startPos, offsetEnd, color, 2.0f);
            renderer.drawPoint(offsetEnd, color, 5.0f);
        }
    }

    void drawVertexNeighbors(Renderer& renderer) {
        if (!m_computeGraph) {
            return;
        }
        const auto& vertices = m_computeGraph->getVertices();
        if (vertices.empty()) {
            return;
        }
        if (m_selectedVertexId < 0 || m_selectedVertexId >= static_cast<int>(vertices.size())) {
            return;
        }

        auto vertex = m_computeGraph->getVertex(m_selectedVertexId);
        if (!vertex) {
            return;
        }

        renderer.drawPoint(vertex->getPosition(), Color(1.0f, 1.0f, 0.2f), 10.0f);

        auto neighbors = vertex->getNeighbors();
        for (const auto& neighbor : neighbors) {
            if (!neighbor) {
                continue;
            }
            Color edgeColor(1.0f, 0.4f, 0.2f);
            renderer.drawLine(vertex->getPosition(), neighbor->getPosition(), edgeColor, 3.0f);
            renderer.drawPoint(neighbor->getPosition(), Color(1.0f, 0.6f, 0.3f), 8.0f);
        }
    }

    void drawUI(Renderer& renderer) {
        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Color(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 70);

        renderer.setColor(Color(0.7f, 0.7f, 0.7f));
        renderer.drawString("Controls:", 10, 110);
        renderer.drawString("'v' - Toggle vertices", 10, 130);
        renderer.drawString("'e' - Toggle edges", 10, 150);
        renderer.drawString("'h' - Toggle half-edges", 10, 170);
        renderer.drawString("'n' - Toggle neighbor highlight", 10, 190);
        renderer.drawString("'1/2' - Cycle selected vertex (" + std::to_string(m_selectedVertexId) + ")", 10, 210);
        renderer.drawString("'w' - Weld close vertices", 10, 230);
        renderer.drawString("'r' - Resample polyline (0.25)", 10, 250);
        renderer.drawString("'t' - Print GraphObject tests", 10, 270);

        if (m_computeGraph) {
            const auto& heData = m_computeGraph->getHeGraphData();
            renderer.setColor(Color(0.9f, 0.9f, 0.5f));
            renderer.drawString("Graph Info:", 10, 310);
            renderer.drawString("Vertices: " + std::to_string(heData.vertices.size()), 10, 330);
            renderer.drawString("Half-edges: " + std::to_string(heData.halfedges.size()), 10, 350);
            renderer.drawString("Edges: " + std::to_string(heData.edges.size()), 10, 370);
        }
    }
        };

// ALICE2_REGISTER_SKETCH_AUTO(HeGraphSketch)

#endif // __MAIN__


