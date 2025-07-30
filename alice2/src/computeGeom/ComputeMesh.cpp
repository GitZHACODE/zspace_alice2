#include "ComputeMesh.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace alice2 {

    // HeMeshVertex implementation
    HeMeshVertex::HeMeshVertex(int id, const Vec3& position)
        : m_id(id), m_position(position), m_outgoingHalfedge(nullptr) {
    }

    std::vector<std::shared_ptr<HeMeshHalfedge>> HeMeshVertex::getHalfedges() const {
        std::vector<std::shared_ptr<HeMeshHalfedge>> halfedges;
        if (!m_outgoingHalfedge) return halfedges;

        // Start with the stored outgoing half-edge
        auto current = m_outgoingHalfedge;
        halfedges.push_back(current);

        // Traverse around the vertex by following twin->next pattern
        while (true) {
            auto twin = current->getSymmetry();
            if (!twin) {
                // Hit boundary - can't continue traversal
                break;
            }

            current = twin->getNext();
            if (!current || current == m_outgoingHalfedge) {
                // Either null or completed the loop
                break;
            }

            halfedges.push_back(current);
        }

        return halfedges;
    }

    std::vector<std::shared_ptr<HeMeshEdge>> HeMeshVertex::getEdges() const {
        std::vector<std::shared_ptr<HeMeshEdge>> edges;

        // Get edges from outgoing half-edges
        auto halfedges = getHalfedges();
        for (auto he : halfedges) {
            if (he->getEdge()) {
                edges.push_back(he->getEdge());
            }
        }

        // For boundary vertices, we also need to find incoming half-edges
        // The incoming half-edge is the one whose target vertex is this vertex
        if (m_outgoingHalfedge) {
            auto prevHE = m_outgoingHalfedge->getPrev();
            if (prevHE && prevHE->getEdge()) {
                // Check if this edge is already in our list
                bool found = false;
                for (auto edge : edges) {
                    if (edge == prevHE->getEdge()) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    edges.push_back(prevHE->getEdge());
                }
            }
        }

        return edges;
    }

    std::vector<std::shared_ptr<HeMeshVertex>> HeMeshVertex::getConnectedVertices() const {
        std::vector<std::shared_ptr<HeMeshVertex>> vertices;
        auto halfedges = getHalfedges();
        for (auto he : halfedges) {
            if (he->getVertex()) {
                vertices.push_back(he->getVertex());
            }
        }
        return vertices;
    }

    int HeMeshVertex::getValency() const {
        return static_cast<int>(getEdges().size());
    }

    bool HeMeshVertex::onBoundary() const {
        auto halfedges = getHalfedges();
        for (auto he : halfedges) {
            if (he->onBoundary()) {
                return true;
            }
        }
        return false;
    }

    void HeMeshVertex::addOutgoingHalfedge(std::shared_ptr<HeMeshHalfedge> halfedge) {
        if (!m_outgoingHalfedge) {
            m_outgoingHalfedge = halfedge;
        }
    }

    // HeMeshHalfedge implementation
    HeMeshHalfedge::HeMeshHalfedge(int id)
        : m_id(id), m_targetVertex(nullptr), m_parentEdge(nullptr), 
          m_face(nullptr), m_next(nullptr), m_prev(nullptr), m_twin(nullptr) {
    }

    std::shared_ptr<HeMeshVertex> HeMeshHalfedge::getStartVertex() const {
        if (m_twin) {
            return m_twin->getVertex();
        }
        return nullptr;
    }

    Vec3 HeMeshHalfedge::getVector() const {
        auto start = getStartVertex();
        if (start && m_targetVertex) {
            return m_targetVertex->getPosition() - start->getPosition();
        }
        return Vec3(0, 0, 0);
    }

    // HeMeshEdge implementation
    HeMeshEdge::HeMeshEdge(int id)
        : m_id(id), m_halfedge1(nullptr), m_halfedge2(nullptr) {
    }

    std::pair<std::shared_ptr<HeMeshVertex>, std::shared_ptr<HeMeshVertex>> HeMeshEdge::getVertices() const {
        if (m_halfedge1 && m_halfedge2) {
            return std::make_pair(m_halfedge1->getStartVertex(), m_halfedge1->getVertex());
        }
        return std::make_pair(nullptr, nullptr);
    }

    std::pair<std::shared_ptr<HeMeshHalfedge>, std::shared_ptr<HeMeshHalfedge>> HeMeshEdge::getHalfedges() const {
        return std::make_pair(m_halfedge1, m_halfedge2);
    }

    std::vector<std::shared_ptr<HeMeshFace>> HeMeshEdge::getFaces() const {
        std::vector<std::shared_ptr<HeMeshFace>> faces;
        if (m_halfedge1 && m_halfedge1->getFace()) {
            faces.push_back(m_halfedge1->getFace());
        }
        if (m_halfedge2 && m_halfedge2->getFace()) {
            faces.push_back(m_halfedge2->getFace());
        }
        return faces;
    }

    bool HeMeshEdge::onBoundary() const {
        return (m_halfedge1 && m_halfedge1->onBoundary()) || 
               (m_halfedge2 && m_halfedge2->onBoundary());
    }

    void HeMeshEdge::setHalfedges(std::shared_ptr<HeMeshHalfedge> he1, std::shared_ptr<HeMeshHalfedge> he2) {
        m_halfedge1 = he1;
        m_halfedge2 = he2;
        if (he1) he1->setParentEdge(shared_from_this());
        if (he2) he2->setParentEdge(shared_from_this());
    }

    // HeMeshFace implementation
    HeMeshFace::HeMeshFace(int id)
        : m_id(id), m_halfedge(nullptr) {
    }

    std::vector<std::shared_ptr<HeMeshVertex>> HeMeshFace::getVertices() const {
        std::vector<std::shared_ptr<HeMeshVertex>> vertices;
        if (!m_halfedge) return vertices;
        
        auto current = m_halfedge;
        do {
            if (current->getVertex()) {
                vertices.push_back(current->getVertex());
            }
            current = current->getNext();
        } while (current && current != m_halfedge);
        
        return vertices;
    }

    std::vector<std::shared_ptr<HeMeshHalfedge>> HeMeshFace::getHalfedges() const {
        std::vector<std::shared_ptr<HeMeshHalfedge>> halfedges;
        if (!m_halfedge) return halfedges;
        
        auto current = m_halfedge;
        do {
            halfedges.push_back(current);
            current = current->getNext();
        } while (current && current != m_halfedge);
        
        return halfedges;
    }

    std::vector<std::shared_ptr<HeMeshEdge>> HeMeshFace::getEdges() const {
        std::vector<std::shared_ptr<HeMeshEdge>> edges;
        auto halfedges = getHalfedges();
        for (auto he : halfedges) {
            if (he->getEdge()) {
                edges.push_back(he->getEdge());
            }
        }
        return edges;
    }

    bool HeMeshFace::onBoundary() const {
        auto halfedges = getHalfedges();
        for (auto he : halfedges) {
            if (he->getSymmetry() && he->getSymmetry()->onBoundary()) {
                return true;
            }
        }
        return false;
    }

    // HeMeshData implementation
    void HeMeshData::clear() {
        vertices.clear();
        halfedges.clear();
        edges.clear();
        faces.clear();
    }

    // ComputeMesh implementation
    ComputeMesh::ComputeMesh(const std::string& name)
        : MeshObject(name) {
    }

    ComputeMesh::ComputeMesh(const std::string& name, const MeshData& meshData, bool enableHalfEdge)
        : MeshObject(name) {

        if(meshData.vertices.size() == 0 || meshData.faces.size() == 0) {
            return;
        }

        // Set the mesh data first (needed for generateEdgesFromFaces)
        setMeshData(std::make_shared<MeshData>(meshData));

        if(meshData.edges.size() == 0) {
            // Generate edges from faces if not provided
            generateEdgesFromFaces();
        }

        // Build half-edge structure using the updated mesh data
        if(enableHalfEdge) createHalfEdgeMesh(*getMeshData());

        calculateBounds();
    }

    // Mesh operations


    std::shared_ptr<HeMeshVertex> ComputeMesh::getVertex(int id) const {
        if (id >= 0 && id < static_cast<int>(m_heMeshData.vertices.size())) {
            return m_heMeshData.vertices[id];
        }
        return nullptr;
    }

    std::shared_ptr<HeMeshHalfedge> ComputeMesh::getHalfedge(int id) const {
        if (id >= 0 && id < static_cast<int>(m_heMeshData.halfedges.size())) {
            return m_heMeshData.halfedges[id];
        }
        return nullptr;
    }

    std::shared_ptr<HeMeshEdge> ComputeMesh::getEdge(int id) const {
        if (id >= 0 && id < static_cast<int>(m_heMeshData.edges.size())) {
            return m_heMeshData.edges[id];
        }
        return nullptr;
    }

    std::shared_ptr<HeMeshFace> ComputeMesh::getFace(int id) const {
        if (id >= 0 && id < static_cast<int>(m_heMeshData.faces.size())) {
            return m_heMeshData.faces[id];
        }
        return nullptr;
    }

    void ComputeMesh::createHalfEdgeMesh(const MeshData& meshData) {
        m_heMeshData.clear();

        // Step 1: Create vertices
        createVertices(meshData);

        // Step 2: Create faces and interior half-edges
        createFacesAndHalfedges(meshData);

        // Step 3: Create edges and boundary half-edges (twins)
        createEdges();

        // Step 4: Link boundary half-edges in a loop
        linkBoundaryHalfedges();

        // Step 5: Link vertex outgoing half-edges
        linkVertexHalfedges();

        // Count boundary vs interior half-edges for debugging
        int interiorHalfedges = 0;
        int boundaryHalfedges = 0;
        for (auto& he : m_heMeshData.halfedges) {
            if (he->getFace() == nullptr) {
                boundaryHalfedges++;
            } else {
                interiorHalfedges++;
            }
        }

        std::cout << "Half-edge mesh built: "
                  << m_heMeshData.vertices.size() << " vertices, "
                  << m_heMeshData.halfedges.size() << " half-edges ("
                  << interiorHalfedges << " interior + " << boundaryHalfedges << " boundary), "
                  << m_heMeshData.edges.size() << " edges, "
                  << m_heMeshData.faces.size() << " faces" << std::endl;
    }

    void ComputeMesh::updateHalfEdgeData(){
        createHalfEdgeMesh(*getMeshData());
    }

    void ComputeMesh::createVertices(const MeshData& meshData) {
        m_heMeshData.vertices.reserve(meshData.vertices.size());

        for (size_t i = 0; i < meshData.vertices.size(); ++i) {
            auto vertex = std::make_shared<HeMeshVertex>(static_cast<int>(i), meshData.vertices[i].position);
            m_heMeshData.vertices.push_back(vertex);
        }
    }

    void ComputeMesh::createFacesAndHalfedges(const MeshData& meshData) {
        int halfedgeId = 0;

        for (size_t faceIdx = 0; faceIdx < meshData.faces.size(); ++faceIdx) {
            const auto& meshFace = meshData.faces[faceIdx];

            // Create face
            auto face = std::make_shared<HeMeshFace>(static_cast<int>(faceIdx));
            m_heMeshData.faces.push_back(face);

            // Create half-edges for this face
            std::vector<std::shared_ptr<HeMeshHalfedge>> faceHalfedges;

            for (size_t i = 0; i < meshFace.vertices.size(); ++i) {
                int currentVertex = meshFace.vertices[i];
                int nextVertex = meshFace.vertices[(i + 1) % meshFace.vertices.size()];

                // Create half-edge from currentVertex to nextVertex
                auto halfedge = std::make_shared<HeMeshHalfedge>(halfedgeId++);
                halfedge->setTargetVertex(m_heMeshData.vertices[nextVertex]);
                halfedge->setFace(face);

                faceHalfedges.push_back(halfedge);
                m_heMeshData.halfedges.push_back(halfedge);
            }

            // Link next/prev for this face
            for (size_t i = 0; i < faceHalfedges.size(); ++i) {
                size_t nextIdx = (i + 1) % faceHalfedges.size();
                size_t prevIdx = (i + faceHalfedges.size() - 1) % faceHalfedges.size();

                faceHalfedges[i]->setNext(faceHalfedges[nextIdx]);
                faceHalfedges[i]->setPrev(faceHalfedges[prevIdx]);
            }

            // Set face's representative half-edge
            if (!faceHalfedges.empty()) {
                face->setHalfedge(faceHalfedges[0]);
            }
        }
    }

    void ComputeMesh::createEdges() {
        int edgeId = 0;
        int halfedgeId = static_cast<int>(m_heMeshData.halfedges.size()); // Continue ID from existing half-edges

        // Group interior half-edges by the edge they represent (vertex pair)
        std::unordered_map<EdgeKey, std::shared_ptr<HeMeshHalfedge>, EdgeKeyHash> edgeToHalfedge;
        std::unordered_set<EdgeKey, EdgeKeyHash> processedEdges;

        // First pass: collect all interior half-edges by their edge key
        // Store a copy to avoid iterator invalidation when we add boundary half-edges
        std::vector<std::shared_ptr<HeMeshHalfedge>> interiorHalfedges = m_heMeshData.halfedges;

        for (auto& halfedge : interiorHalfedges) {
            auto endVertex = halfedge->getVertex();
            auto prevHalfedge = halfedge->getPrev();

            if (endVertex && prevHalfedge && prevHalfedge->getVertex()) {
                auto startVertex = prevHalfedge->getVertex();
                EdgeKey key(startVertex->getId(), endVertex->getId());
                edgeToHalfedge[key] = halfedge;
            }
        }

        // Second pass: create edges and link twins
        for (auto& halfedge : interiorHalfedges) {
            auto endVertex = halfedge->getVertex();
            auto prevHalfedge = halfedge->getPrev();

            if (!endVertex || !prevHalfedge || !prevHalfedge->getVertex()) continue;

            auto startVertex = prevHalfedge->getVertex();
            EdgeKey key(startVertex->getId(), endVertex->getId());
            EdgeKey twinKey(endVertex->getId(), startVertex->getId());

            // Skip if we already processed this edge
            if (processedEdges.find(key) != processedEdges.end()) continue;

            // Create edge
            auto edge = std::make_shared<HeMeshEdge>(edgeId++);
            m_heMeshData.edges.push_back(edge);

            // Look for twin interior half-edge
            auto twinIt = edgeToHalfedge.find(twinKey);

            if (twinIt != edgeToHalfedge.end()) {
                // Found twin interior half-edge - this is a shared edge between two faces
                auto twinHalfedge = twinIt->second;

                // Link twins
                halfedge->setTwin(twinHalfedge);
                twinHalfedge->setTwin(halfedge);

                // Set edge's half-edges (both are interior)
                edge->setHalfedges(halfedge, twinHalfedge);

                // Mark both directions as processed
                processedEdges.insert(key);
                processedEdges.insert(twinKey);
            } else {
                // No twin found - this is a boundary edge
                // Create boundary half-edge (twin) - goes in opposite direction
                auto boundaryHE = std::make_shared<HeMeshHalfedge>(halfedgeId++);
                boundaryHE->setTargetVertex(startVertex); // Opposite direction
                boundaryHE->setFace(nullptr); // Boundary half-edge has no face

                // Link twins
                halfedge->setTwin(boundaryHE);
                boundaryHE->setTwin(halfedge);

                // Set edge's half-edges (interior + boundary)
                edge->setHalfedges(halfedge, boundaryHE);

                // Add boundary half-edge to mesh
                m_heMeshData.halfedges.push_back(boundaryHE);

                // Mark this direction as processed
                processedEdges.insert(key);
            }
        }
    }

    void ComputeMesh::linkBoundaryHalfedges() {
        // Link boundary half-edges in a loop around the mesh boundary
        // Only process half-edges that actually have getFace() == nullptr

        std::vector<std::shared_ptr<HeMeshHalfedge>> boundaryHalfedges;

        // Collect all actual boundary half-edges (those with no face)
        for (auto& halfedge : m_heMeshData.halfedges) {
            if (halfedge->getFace() == nullptr) {
                boundaryHalfedges.push_back(halfedge);
            }
        }

        // If no boundary half-edges exist (closed mesh), nothing to link
        if (boundaryHalfedges.empty()) {
            return;
        }

        // Link boundary half-edges in a loop
        // For each boundary half-edge, find the next one by following the boundary
        for (auto& boundaryHE : boundaryHalfedges) {
            auto targetVertex = boundaryHE->getVertex();

            // Find the boundary half-edge that starts from this target vertex
            std::shared_ptr<HeMeshHalfedge> nextBoundaryHE = nullptr;
            for (auto& candidate : boundaryHalfedges) {
                if (candidate != boundaryHE) {
                    // Check if this candidate starts from our target vertex
                    auto candidateTwin = candidate->getSymmetry();
                    if (candidateTwin && candidateTwin->getVertex() == targetVertex) {
                        nextBoundaryHE = candidate;
                        break;
                    }
                }
            }

            if (nextBoundaryHE) {
                boundaryHE->setNext(nextBoundaryHE);
                nextBoundaryHE->setPrev(boundaryHE);
            }
        }
    }

    void ComputeMesh::linkVertexHalfedges() {
        // Set outgoing half-edge for each vertex
        // Only process interior half-edges (those with faces)
        for (auto& halfedge : m_heMeshData.halfedges) {
            if (halfedge->getFace() != nullptr) {
                // This is an interior half-edge
                auto prevHalfedge = halfedge->getPrev();
                if (prevHalfedge && prevHalfedge->getVertex()) {
                    auto startVertex = prevHalfedge->getVertex();
                    startVertex->addOutgoingHalfedge(halfedge);
                }
            }
        }
    }

} // namespace alice2
