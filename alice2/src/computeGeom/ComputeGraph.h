#pragma once

#ifndef ALICE2_COMPUTE_GRAPH_H
#define ALICE2_COMPUTE_GRAPH_H

#include "../objects/GraphObject.h"
#include "../utils/Math.h"
#include <memory>
#include <utility>
#include <vector>

namespace alice2 {

    class HeGraphHalfedge;
    class HeGraphEdge;

    class HeGraphVertex {
    public:
        HeGraphVertex(int id, const Vec3& position, const Color& color);

        int getId() const { return m_id; }
        const Vec3& getPosition() const { return m_position; }
        const Color& getColor() const { return m_color; }

        const std::vector<std::shared_ptr<HeGraphHalfedge>>& getOutgoingHalfedges() const { return m_outgoingHalfedges; }
        std::vector<std::shared_ptr<HeGraphEdge>> getEdges() const;
        std::vector<std::shared_ptr<HeGraphVertex>> getNeighbors() const;
        int getValency() const;

        void addOutgoingHalfedge(const std::shared_ptr<HeGraphHalfedge>& halfedge);

    private:
        int m_id;
        Vec3 m_position;
        Color m_color;
        std::vector<std::shared_ptr<HeGraphHalfedge>> m_outgoingHalfedges;
    };

    class HeGraphHalfedge {
    public:
        HeGraphHalfedge(int id);

        int getId() const { return m_id; }

        std::shared_ptr<HeGraphVertex> getVertex() const { return m_targetVertex; }
        std::shared_ptr<HeGraphVertex> getStartVertex() const;
        std::shared_ptr<HeGraphEdge> getEdge() const { return m_parentEdge; }
        std::shared_ptr<HeGraphHalfedge> getTwin() const { return m_twin; }
        Vec3 getVector() const;

        void setTargetVertex(const std::shared_ptr<HeGraphVertex>& vertex) { m_targetVertex = vertex; }
        void setParentEdge(const std::shared_ptr<HeGraphEdge>& edge) { m_parentEdge = edge; }
        void setTwin(const std::shared_ptr<HeGraphHalfedge>& twin) { m_twin = twin; }

    private:
        int m_id;
        std::shared_ptr<HeGraphVertex> m_targetVertex;
        std::shared_ptr<HeGraphEdge> m_parentEdge;
        std::shared_ptr<HeGraphHalfedge> m_twin;
    };

    class HeGraphEdge : public std::enable_shared_from_this<HeGraphEdge> {
    public:
        HeGraphEdge(int id);

        int getId() const { return m_id; }

        std::pair<std::shared_ptr<HeGraphVertex>, std::shared_ptr<HeGraphVertex>> getVertices() const;
        std::pair<std::shared_ptr<HeGraphHalfedge>, std::shared_ptr<HeGraphHalfedge>> getHalfedges() const;

        void setHalfedges(const std::shared_ptr<HeGraphHalfedge>& forward,
                          const std::shared_ptr<HeGraphHalfedge>& backward);

    private:
        int m_id;
        std::shared_ptr<HeGraphHalfedge> m_forwardHalfedge;
        std::shared_ptr<HeGraphHalfedge> m_backwardHalfedge;
    };

    struct HeGraphData {
        std::vector<std::shared_ptr<HeGraphVertex>> vertices;
        std::vector<std::shared_ptr<HeGraphHalfedge>> halfedges;
        std::vector<std::shared_ptr<HeGraphEdge>> edges;

        void clear();
    };

    class ComputeGraph : public GraphObject {
    public:
        ComputeGraph(const std::string& name = "ComputeGraph");
        ComputeGraph(const std::string& name, const GraphData& graphData, bool buildHalfEdge = true);

        void createHalfEdgeGraph(const GraphData& graphData);

        void updateHalfEdgeData();

        const HeGraphData& getHeGraphData() const { return m_heGraphData; }

        std::shared_ptr<HeGraphVertex> getVertex(int id) const;
        std::shared_ptr<HeGraphEdge> getEdge(int id) const;
        std::shared_ptr<HeGraphHalfedge> getHalfedge(int id) const;

        const std::vector<std::shared_ptr<HeGraphVertex>>& getVertices() const { return m_heGraphData.vertices; }
        const std::vector<std::shared_ptr<HeGraphEdge>>& getEdges() const { return m_heGraphData.edges; }
        const std::vector<std::shared_ptr<HeGraphHalfedge>>& getHalfedges() const { return m_heGraphData.halfedges; }

    private:
        HeGraphData m_heGraphData;

        void buildHalfEdgeStructure(const GraphData& graphData);
    };

} // namespace alice2

#endif // ALICE2_COMPUTE_GRAPH_H
