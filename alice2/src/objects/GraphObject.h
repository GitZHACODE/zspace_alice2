#pragma once

#ifndef ALICE2_GRAPH_OBJECT_H
#define ALICE2_GRAPH_OBJECT_H

#include "SceneObject.h"
#include "../utils/Math.h"
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace alice2 {

    class Renderer;
    class Camera;

    struct GraphVertex {
        Vec3 position;
        Color color;

        GraphVertex();
        GraphVertex(const Vec3& pos, const Color& col = Color(1.0f, 1.0f, 1.0f));
    };

    struct GraphEdge {
        int vertexA;
        int vertexB;

        GraphEdge();
        GraphEdge(int a, int b);
        bool isValid() const { return vertexA >= 0 && vertexB >= 0; }
    };

    struct GraphData {
        std::vector<GraphVertex> vertices;
        std::vector<GraphEdge> edges;

        void clear();
        int addVertex(const Vec3& position, const Color& color = Color(1.0f, 1.0f, 1.0f));
        int addEdge(int vertexA, int vertexB);
        void updateBounds(Vec3& minBounds, Vec3& maxBounds) const;
    };

    class GraphObject : public SceneObject {
    public:
        GraphObject(const std::string& name = "GraphObject");
        virtual ~GraphObject() = default;

        ObjectType getType() const override { return ObjectType::Graph; }

        void setGraphData(std::shared_ptr<GraphData> graphData);
        std::shared_ptr<GraphData> getGraphData() const { return m_graphData; }

        GraphObject duplicate() const;
        void applyTransform();
        float getLength() const;

        void readFromObj(const std::string& filename);
        void writeToObj(const std::string& filename);

        bool isClosed() const { return m_isClosed; }
        bool isPolyline() const { return m_isPolyline; }

        void weld(float epsilon = 1e-6f);
        void combineWith(const GraphObject& other);
        void resample(float sampleDistance);
        void resampleByCount(int sampleCount);
        std::vector<GraphObject> separate() const;

        void createFromPositionsAndEdges(const std::vector<Vec3>& positions,
                                         const std::vector<std::pair<int, int>>& edges,
                                         const std::vector<Color>& colors = {});
        int addVertex(const Vec3& position, const Color& color = Color(1.0f, 1.0f, 1.0f));
        int addEdge(int vertexA, int vertexB);

        void setShowVertices(bool show) { m_showVertices = show; }
        bool getShowVertices() const { return m_showVertices; }

        void setShowEdges(bool show) { m_showEdges = show; }
        bool getShowEdges() const { return m_showEdges; }

        void setVertexSize(float size) { m_vertexSize = size; }
        float getVertexSize() const { return m_vertexSize; }

        void setEdgeWidth(float width) { m_edgeWidth = width; }
        float getEdgeWidth() const { return m_edgeWidth; }

        void setDefaultVertexColor(const Color& color) { m_defaultVertexColor = color; }
        const Color& getDefaultVertexColor() const { return m_defaultVertexColor; }

        void setEdgeColor(const Color& color) { m_edgeColor = color; }
        const Color& getEdgeColor() const { return m_edgeColor; }

        void renderImpl(Renderer& renderer, Camera& camera) override;
        void calculateBounds() override;

    private:
        void updateTopologyFlags();
        bool buildAdjacency(std::vector<std::vector<int>>& adjacency, std::vector<int>& degree, size_t& validEdgeCount) const;
        std::vector<int> buildPolylineOrder(const std::vector<std::vector<int>>& adjacency, const std::vector<int>& degree, size_t validEdgeCount) const;

        std::shared_ptr<GraphData> m_graphData;
        bool m_isClosed;
        bool m_isPolyline;
        bool m_showVertices;
        bool m_showEdges;
        float m_vertexSize;
        float m_edgeWidth;
        Color m_defaultVertexColor;
        Color m_edgeColor;

        void renderVertices(Renderer& renderer);
        void renderEdges(Renderer& renderer);
    };

} // namespace alice2

#endif // ALICE2_GRAPH_OBJECT_H

