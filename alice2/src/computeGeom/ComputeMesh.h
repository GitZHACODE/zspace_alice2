#pragma once

#ifndef ALICE2_COMPUTE_MESH_H
#define ALICE2_COMPUTE_MESH_H

#include "../objects/MeshObject.h"
#include "../utils/Math.h"
#include <vector>
#include <memory>

namespace alice2 {

    // Forward declarations
    class HeMeshVertex;
    class HeMeshHalfedge;
    class HeMeshEdge;
    class HeMeshFace;

    // Half-edge mesh data structures
    class HeMeshVertex {
    public:
        HeMeshVertex(int id, const Vec3& position);
        
        // Basic properties
        int getId() const { return m_id; }
        Vec3 getPosition() const { return m_position; }
        
        // Connectivity queries
        std::vector<std::shared_ptr<HeMeshHalfedge>> getHalfedges() const;
        std::vector<std::shared_ptr<HeMeshEdge>> getEdges() const;
        std::vector<std::shared_ptr<HeMeshVertex>> getConnectedVertices() const;
        int getValency() const;
        bool onBoundary() const;
        
        // Internal connectivity management
        void addOutgoingHalfedge(std::shared_ptr<HeMeshHalfedge> halfedge);
        std::shared_ptr<HeMeshHalfedge> getOutgoingHalfedge() const { return m_outgoingHalfedge; }
        
    private:
        int m_id;
        Vec3 m_position;
        std::shared_ptr<HeMeshHalfedge> m_outgoingHalfedge;  // One outgoing half-edge
    };

    class HeMeshHalfedge {
    public:
        HeMeshHalfedge(int id);
        
        // Basic properties
        int getId() const { return m_id; }
        
        // Connectivity queries
        std::shared_ptr<HeMeshVertex> getVertex() const { return m_targetVertex; }
        std::shared_ptr<HeMeshVertex> getStartVertex() const;
        std::shared_ptr<HeMeshEdge> getEdge() const { return m_parentEdge; }
        std::shared_ptr<HeMeshFace> getFace() const { return m_face; }
        Vec3 getVector() const;
        bool onBoundary() const { return m_face == nullptr; }
        
        // Navigation
        std::shared_ptr<HeMeshHalfedge> getNext() const { return m_next; }
        std::shared_ptr<HeMeshHalfedge> getPrev() const { return m_prev; }
        std::shared_ptr<HeMeshHalfedge> getSymmetry() const { return m_twin; }
        
        // Internal connectivity management
        void setTargetVertex(std::shared_ptr<HeMeshVertex> vertex) { m_targetVertex = vertex; }
        void setParentEdge(std::shared_ptr<HeMeshEdge> edge) { m_parentEdge = edge; }
        void setFace(std::shared_ptr<HeMeshFace> face) { m_face = face; }
        void setNext(std::shared_ptr<HeMeshHalfedge> next) { m_next = next; }
        void setPrev(std::shared_ptr<HeMeshHalfedge> prev) { m_prev = prev; }
        void setTwin(std::shared_ptr<HeMeshHalfedge> twin) { m_twin = twin; }
        
    private:
        int m_id;
        std::shared_ptr<HeMeshVertex> m_targetVertex;
        std::shared_ptr<HeMeshEdge> m_parentEdge;
        std::shared_ptr<HeMeshFace> m_face;
        std::shared_ptr<HeMeshHalfedge> m_next;
        std::shared_ptr<HeMeshHalfedge> m_prev;
        std::shared_ptr<HeMeshHalfedge> m_twin;
    };

    class HeMeshEdge : public std::enable_shared_from_this<HeMeshEdge> {
    public:
        HeMeshEdge(int id);
        
        // Basic properties
        int getId() const { return m_id; }
        
        // Connectivity queries
        std::pair<std::shared_ptr<HeMeshVertex>, std::shared_ptr<HeMeshVertex>> getVertices() const;
        std::pair<std::shared_ptr<HeMeshHalfedge>, std::shared_ptr<HeMeshHalfedge>> getHalfedges() const;
        std::vector<std::shared_ptr<HeMeshFace>> getFaces() const;
        bool onBoundary() const;
        
        // Internal connectivity management
        void setHalfedges(std::shared_ptr<HeMeshHalfedge> he1, std::shared_ptr<HeMeshHalfedge> he2);
        std::shared_ptr<HeMeshHalfedge> getHalfedge1() const { return m_halfedge1; }
        std::shared_ptr<HeMeshHalfedge> getHalfedge2() const { return m_halfedge2; }
        
    private:
        int m_id;
        std::shared_ptr<HeMeshHalfedge> m_halfedge1;
        std::shared_ptr<HeMeshHalfedge> m_halfedge2;
    };

    class HeMeshFace {
    public:
        HeMeshFace(int id);
        
        // Basic properties
        int getId() const { return m_id; }
        
        // Connectivity queries
        std::vector<std::shared_ptr<HeMeshVertex>> getVertices() const;
        std::vector<std::shared_ptr<HeMeshHalfedge>> getHalfedges() const;
        std::vector<std::shared_ptr<HeMeshEdge>> getEdges() const;
        bool onBoundary() const;
        
        // Internal connectivity management
        void setHalfedge(std::shared_ptr<HeMeshHalfedge> halfedge) { m_halfedge = halfedge; }
        std::shared_ptr<HeMeshHalfedge> getHalfedge() const { return m_halfedge; }
        
    private:
        int m_id;
        std::shared_ptr<HeMeshHalfedge> m_halfedge;  // One half-edge of this face
    };

    // Half-edge mesh data container
    struct HeMeshData {
        std::vector<std::shared_ptr<HeMeshVertex>> vertices;
        std::vector<std::shared_ptr<HeMeshHalfedge>> halfedges;
        std::vector<std::shared_ptr<HeMeshEdge>> edges;
        std::vector<std::shared_ptr<HeMeshFace>> faces;
        
        void clear();
    };

    // Main ComputeMesh class inheriting from MeshObject
    class ComputeMesh : public MeshObject {
    public:
        ComputeMesh(const std::string& name = "ComputeMesh");
        ComputeMesh(const std::string& name, const MeshData& meshData, bool enableHalfEdge = true);
        
        void createHalfEdgeMesh(const MeshData& meshData);

        // Update Halfedge data from mesh data
        void updateHalfEdgeData();

        // Mesh operations

        // Override object type
        ObjectType getType() const override { return ObjectType::Mesh; }
        
        // Half-edge mesh access
        const HeMeshData& getHeMeshData() const { return m_heMeshData; }
        
        // Vertex access
        std::shared_ptr<HeMeshVertex> getVertex(int id) const;
        const std::vector<std::shared_ptr<HeMeshVertex>>& getVertices() const { return m_heMeshData.vertices; }
        
        // Half-edge access
        std::shared_ptr<HeMeshHalfedge> getHalfedge(int id) const;
        const std::vector<std::shared_ptr<HeMeshHalfedge>>& getHalfedges() const { return m_heMeshData.halfedges; }
        
        // Edge access
        std::shared_ptr<HeMeshEdge> getEdge(int id) const;
        const std::vector<std::shared_ptr<HeMeshEdge>>& getEdges() const { return m_heMeshData.edges; }
        
        // Face access
        std::shared_ptr<HeMeshFace> getFace(int id) const;
        const std::vector<std::shared_ptr<HeMeshFace>>& getFaces() const { return m_heMeshData.faces; }

    private:
        HeMeshData m_heMeshData;
        
        // Construction helpers
        void createVertices(const MeshData& meshData);
        void createFacesAndHalfedges(const MeshData& meshData);
        void createEdges();
        void linkBoundaryHalfedges();
        void linkVertexHalfedges();
        
        // Utility structures for construction
        struct EdgeKey {
            int v1, v2;
            EdgeKey(int a, int b) : v1(a), v2(b) {} // Keep direction - don't normalize!
            bool operator==(const EdgeKey& other) const { return v1 == other.v1 && v2 == other.v2; }
        };
        
        struct EdgeKeyHash {
            std::size_t operator()(const EdgeKey& key) const {
                return std::hash<int>()(key.v1) ^ (std::hash<int>()(key.v2) << 1);
            }
        };
    };

} // namespace alice2

#endif // ALICE2_COMPUTE_MESH_H
