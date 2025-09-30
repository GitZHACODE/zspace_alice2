#include "GraphObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"
#include <algorithm>
#include <iostream>
#include <limits>

namespace alice2 {

    GraphVertex::GraphVertex()
        : position(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f) {
    }

    GraphVertex::GraphVertex(const Vec3& pos, const Color& col)
        : position(pos)
        , color(col) {
    }

    GraphEdge::GraphEdge()
        : vertexA(-1)
        , vertexB(-1) {
    }

    GraphEdge::GraphEdge(int a, int b)
        : vertexA(a)
        , vertexB(b) {
    }

    void GraphData::clear() {
        vertices.clear();
        edges.clear();
    }

    int GraphData::addVertex(const Vec3& position, const Color& color) {
        vertices.emplace_back(position, color);
        return static_cast<int>(vertices.size()) - 1;
    }

    int GraphData::addEdge(int vertexA, int vertexB) {
        if (vertexA < 0 || vertexB < 0 ||
            vertexA >= static_cast<int>(vertices.size()) ||
            vertexB >= static_cast<int>(vertices.size())) {
            std::cout << "GraphData::addEdge: Invalid vertex indices (" << vertexA << ", " << vertexB << ")" << std::endl;
            return -1;
        }

        edges.emplace_back(vertexA, vertexB);
        return static_cast<int>(edges.size()) - 1;
    }

    void GraphData::updateBounds(Vec3& minBounds, Vec3& maxBounds) const {
        if (vertices.empty()) {
            minBounds = Vec3(-0.5f, -0.5f, -0.5f);
            maxBounds = Vec3(0.5f, 0.5f, 0.5f);
            return;
        }

        Vec3 minPoint(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Vec3 maxPoint(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        for (const auto& vertex : vertices) {
            minPoint.x = std::min(minPoint.x, vertex.position.x);
            minPoint.y = std::min(minPoint.y, vertex.position.y);
            minPoint.z = std::min(minPoint.z, vertex.position.z);

            maxPoint.x = std::max(maxPoint.x, vertex.position.x);
            maxPoint.y = std::max(maxPoint.y, vertex.position.y);
            maxPoint.z = std::max(maxPoint.z, vertex.position.z);
        }

        minBounds = minPoint;
        maxBounds = maxPoint;
    }

    GraphObject::GraphObject(const std::string& name)
        : SceneObject(name)
        , m_graphData(std::make_shared<GraphData>())
        , m_showVertices(true)
        , m_showEdges(true)
        , m_vertexSize(5.0f)
        , m_edgeWidth(2.0f)
        , m_defaultVertexColor(1.0f, 1.0f, 1.0f)
        , m_edgeColor(0.85f, 0.85f, 0.85f) {
    }

    void GraphObject::setGraphData(std::shared_ptr<GraphData> graphData) {
        m_graphData = graphData;
        calculateBounds();
    }

    GraphObject GraphObject::duplicate() const {
        GraphObject copy;

        if (m_graphData) {
            copy.setGraphData(std::make_shared<GraphData>(*m_graphData));
        }

        copy.m_showVertices = m_showVertices;
        copy.m_showEdges = m_showEdges;
        copy.m_vertexSize = m_vertexSize;
        copy.m_edgeWidth = m_edgeWidth;
        copy.m_defaultVertexColor = m_defaultVertexColor;
        copy.m_edgeColor = m_edgeColor;

        return copy;
    }

    void GraphObject::createFromPositionsAndEdges(const std::vector<Vec3>& positions,
                                                  const std::vector<std::pair<int, int>>& edges,
                                                  const std::vector<Color>& colors) {
        if (!m_graphData) {
            m_graphData = std::make_shared<GraphData>();
        }

        m_graphData->clear();
        m_graphData->vertices.reserve(positions.size());

        for (size_t i = 0; i < positions.size(); ++i) {
            Color vertexColor = (i < colors.size()) ? colors[i] : m_defaultVertexColor;
            m_graphData->vertices.emplace_back(positions[i], vertexColor);
        }

        m_graphData->edges.reserve(edges.size());
        for (const auto& edge : edges) {
            m_graphData->addEdge(edge.first, edge.second);
        }

        calculateBounds();
    }

    int GraphObject::addVertex(const Vec3& position, const Color& color) {
        if (!m_graphData) {
            m_graphData = std::make_shared<GraphData>();
        }

        int index = m_graphData->addVertex(position, color);
        calculateBounds();
        return index;
    }

    int GraphObject::addEdge(int vertexA, int vertexB) {
        if (!m_graphData) {
            return -1;
        }

        int index = m_graphData->addEdge(vertexA, vertexB);
        return index;
    }

    void GraphObject::renderImpl(Renderer& renderer, Camera& camera) {
        (void)camera;

        if (!m_graphData || m_graphData->vertices.empty()) {
            std::cout << "GraphObject::renderImpl: No graph data" << std::endl;
            return;
        }

        if (m_showEdges) {
            renderEdges(renderer);
        }

        if (m_showVertices) {
            renderVertices(renderer);
        }
    }

    void GraphObject::calculateBounds() {
        if (!m_graphData) {
            setBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
            return;
        }

        Vec3 minBounds;
        Vec3 maxBounds;
        m_graphData->updateBounds(minBounds, maxBounds);
        setBounds(minBounds, maxBounds);
    }

    void GraphObject::renderVertices(Renderer& renderer) {
        renderer.setPointSize(m_vertexSize);

        for (const auto& vertex : m_graphData->vertices) {
            Color drawColor = vertex.color;
            if (drawColor.a <= 0.0f) {
                drawColor.a = 1.0f;
            }
            renderer.drawPoint(vertex.position, drawColor, m_vertexSize);
        }
    }

    void GraphObject::renderEdges(Renderer& renderer) {
        renderer.setLineWidth(m_edgeWidth);

        for (const auto& edge : m_graphData->edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_graphData->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_graphData->vertices.size())) {
                continue;
            }

            const Vec3& start = m_graphData->vertices[edge.vertexA].position;
            const Vec3& end = m_graphData->vertices[edge.vertexB].position;
            renderer.drawLine(start, end, m_edgeColor, m_edgeWidth);
        }
    }

} // namespace alice2
