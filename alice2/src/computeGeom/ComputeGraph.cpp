#include "ComputeGraph.h"
#include <iostream>
#include <unordered_set>

namespace alice2 {

    HeGraphVertex::HeGraphVertex(int id, const Vec3& position, const Color& color)
        : m_id(id)
        , m_position(position)
        , m_color(color) {
    }

    void HeGraphVertex::addOutgoingHalfedge(const std::shared_ptr<HeGraphHalfedge>& halfedge) {
        if (!halfedge) {
            return;
        }

        for (const auto& existing : m_outgoingHalfedges) {
            if (existing == halfedge) {
                return;
            }
        }

        m_outgoingHalfedges.push_back(halfedge);
    }

    std::vector<std::shared_ptr<HeGraphEdge>> HeGraphVertex::getEdges() const {
        std::vector<std::shared_ptr<HeGraphEdge>> edges;
        std::unordered_set<int> seenIds;

        for (const auto& halfedge : m_outgoingHalfedges) {
            if (!halfedge) {
                continue;
            }

            auto edge = halfedge->getEdge();
            if (edge && seenIds.insert(edge->getId()).second) {
                edges.push_back(edge);
            }
        }

        return edges;
    }

    std::vector<std::shared_ptr<HeGraphVertex>> HeGraphVertex::getNeighbors() const {
        std::vector<std::shared_ptr<HeGraphVertex>> neighbors;
        std::unordered_set<int> seenIds;

        for (const auto& halfedge : m_outgoingHalfedges) {
            if (!halfedge) {
                continue;
            }

            auto vertex = halfedge->getVertex();
            if (vertex && seenIds.insert(vertex->getId()).second) {
                neighbors.push_back(vertex);
            }
        }

        return neighbors;
    }

    int HeGraphVertex::getValency() const {
        return static_cast<int>(getNeighbors().size());
    }

    HeGraphHalfedge::HeGraphHalfedge(int id)
        : m_id(id)
        , m_targetVertex(nullptr)
        , m_parentEdge(nullptr)
        , m_twin(nullptr) {
    }

    std::shared_ptr<HeGraphVertex> HeGraphHalfedge::getStartVertex() const {
        if (m_twin) {
            return m_twin->getVertex();
        }
        return nullptr;
    }

    Vec3 HeGraphHalfedge::getVector() const {
        auto start = getStartVertex();
        auto end = getVertex();

        if (start && end) {
            return end->getPosition() - start->getPosition();
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    HeGraphEdge::HeGraphEdge(int id)
        : m_id(id)
        , m_forwardHalfedge(nullptr)
        , m_backwardHalfedge(nullptr) {
    }

    std::pair<std::shared_ptr<HeGraphVertex>, std::shared_ptr<HeGraphVertex>> HeGraphEdge::getVertices() const {
        if (m_forwardHalfedge) {
            return std::make_pair(m_forwardHalfedge->getStartVertex(), m_forwardHalfedge->getVertex());
        }
        if (m_backwardHalfedge) {
            return std::make_pair(m_backwardHalfedge->getStartVertex(), m_backwardHalfedge->getVertex());
        }
        return std::make_pair(nullptr, nullptr);
    }

    std::pair<std::shared_ptr<HeGraphHalfedge>, std::shared_ptr<HeGraphHalfedge>> HeGraphEdge::getHalfedges() const {
        return std::make_pair(m_forwardHalfedge, m_backwardHalfedge);
    }

    void HeGraphEdge::setHalfedges(const std::shared_ptr<HeGraphHalfedge>& forward,
                                   const std::shared_ptr<HeGraphHalfedge>& backward) {
        m_forwardHalfedge = forward;
        m_backwardHalfedge = backward;

        auto self = shared_from_this();
        if (forward) {
            forward->setParentEdge(self);
        }
        if (backward) {
            backward->setParentEdge(self);
        }
    }

    void HeGraphData::clear() {
        vertices.clear();
        halfedges.clear();
        edges.clear();
    }

    ComputeGraph::ComputeGraph(const std::string& name)
        : GraphObject(name) {
    }

    ComputeGraph::ComputeGraph(const std::string& name, const GraphData& graphData, bool buildHalfEdge)
        : GraphObject(name) {
        setGraphData(std::make_shared<GraphData>(graphData));
        if (buildHalfEdge) {
            createHalfEdgeGraph(graphData);
        }
    }

    void ComputeGraph::createHalfEdgeGraph(const GraphData& graphData) {
        buildHalfEdgeStructure(graphData);
    }

    void ComputeGraph::updateHalfEdgeData() {
        auto data = getGraphData();
        if (data) {
            createHalfEdgeGraph(*data);
        }
    }

    std::shared_ptr<HeGraphVertex> ComputeGraph::getVertex(int id) const {
        if (id < 0 || id >= static_cast<int>(m_heGraphData.vertices.size())) {
            return nullptr;
        }
        return m_heGraphData.vertices[id];
    }

    std::shared_ptr<HeGraphEdge> ComputeGraph::getEdge(int id) const {
        if (id < 0 || id >= static_cast<int>(m_heGraphData.edges.size())) {
            return nullptr;
        }
        return m_heGraphData.edges[id];
    }

    std::shared_ptr<HeGraphHalfedge> ComputeGraph::getHalfedge(int id) const {
        if (id < 0 || id >= static_cast<int>(m_heGraphData.halfedges.size())) {
            return nullptr;
        }
        return m_heGraphData.halfedges[id];
    }

    void ComputeGraph::buildHalfEdgeStructure(const GraphData& graphData) {
        m_heGraphData.clear();

        m_heGraphData.vertices.reserve(graphData.vertices.size());
        for (size_t i = 0; i < graphData.vertices.size(); ++i) {
            const auto& vertex = graphData.vertices[i];
            auto heVertex = std::make_shared<HeGraphVertex>(static_cast<int>(i), vertex.position, vertex.color);
            m_heGraphData.vertices.push_back(heVertex);
        }

        int edgeId = 0;
        int halfedgeId = 0;
        m_heGraphData.edges.reserve(graphData.edges.size());
        m_heGraphData.halfedges.reserve(graphData.edges.size() * 2);

        for (const auto& edge : graphData.edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(graphData.vertices.size()) ||
                edge.vertexB >= static_cast<int>(graphData.vertices.size())) {
                std::cout << "ComputeGraph::buildHalfEdgeStructure: Skipping invalid edge (" << edge.vertexA
                          << ", " << edge.vertexB << ")" << std::endl;
                continue;
            }

            auto heEdge = std::make_shared<HeGraphEdge>(edgeId++);

            auto forward = std::make_shared<HeGraphHalfedge>(halfedgeId++);
            auto backward = std::make_shared<HeGraphHalfedge>(halfedgeId++);

            auto vertexA = m_heGraphData.vertices[edge.vertexA];
            auto vertexB = m_heGraphData.vertices[edge.vertexB];

            forward->setTargetVertex(vertexB);
            backward->setTargetVertex(vertexA);

            forward->setTwin(backward);
            backward->setTwin(forward);

            heEdge->setHalfedges(forward, backward);

            vertexA->addOutgoingHalfedge(forward);
            vertexB->addOutgoingHalfedge(backward);

            m_heGraphData.halfedges.push_back(forward);
            m_heGraphData.halfedges.push_back(backward);
            m_heGraphData.edges.push_back(heEdge);
        }
    }

} // namespace alice2
