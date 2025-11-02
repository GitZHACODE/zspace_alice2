// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include "../src/computeGeom/ComputeMesh.h"
#include <iostream>
#include <memory>

using namespace alice2;

class HeMeshSketch : public ISketch {
private:
    std::shared_ptr<ComputeMesh> m_computeMesh;
    
    // Visualization flags
    bool b_showVertices;
    bool b_showEdges;
    bool b_showFaces;
    bool b_showHalfEdges;
    bool d_showVertexInfo;
    bool d_showBoundaryInfo;
    
    // Selected elements for debugging
    int m_selectedVertexId;
    int m_selectedFaceId;
    
public:
    HeMeshSketch() 
        : b_showVertices(true)
        , b_showEdges(true) 
        , b_showFaces(true)
        , b_showHalfEdges(false)
        , d_showVertexInfo(false)
        , d_showBoundaryInfo(false)
        , m_selectedVertexId(0)
        , m_selectedFaceId(0)
    {
    }

    std::string getName() const override {
        return "Half-Edge Mesh Demo";
    }

    std::string getDescription() const override {
        return "Demonstrates half-edge mesh data structure with connectivity queries";
    }

    void setup() override {
        // Set background color
        scene().setBackgroundColor(Color(0.15f, 0.15f, 0.15f));

        // Enable grid and axes
        scene().setShowGrid(true);
        scene().setGridSize(10.0f);
        scene().setGridDivisions(10);
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);

        // Create a simple test mesh (cube)
        createTestMesh();
        
        std::cout << "Half-Edge Mesh Sketch initialized" << std::endl;
    }

    void createTestMesh() {
        // Create a simple cube mesh data
        MeshData meshData;
        
        // Cube vertices
        meshData.vertices = {
            MeshVertex(Vec3(-1, -1, -1)), // 0
            MeshVertex(Vec3( 1, -1, -1)), // 1
            MeshVertex(Vec3( 1,  1, -1)), // 2
            MeshVertex(Vec3(-1,  1, -1)), // 3
            MeshVertex(Vec3(-1, -1,  1)), // 4
            MeshVertex(Vec3( 1, -1,  1)), // 5
            MeshVertex(Vec3( 1,  1,  1)), // 6
            MeshVertex(Vec3(-1,  1,  1)),  // 7

            MeshVertex(Vec3(1, 0, 2)),
            MeshVertex(Vec3(1, 1, 2)),
            MeshVertex(Vec3(0, 1, 2)),
            MeshVertex(Vec3(-1, 1, 2)),
            MeshVertex(Vec3(-1, 0, 2)),
            MeshVertex(Vec3(-1, -1, 2)),
            MeshVertex(Vec3(0, -1, 2)),
            MeshVertex(Vec3(1, -1, 2)),
        };
        
        // Cube faces (counter-clockwise when viewed from outside)
        meshData.faces = {
            MeshFace({0, 1, 2, 3}), // Bottom face
            MeshFace({4, 7, 6, 5}), // Top face
            MeshFace({0, 4, 5, 1}), // Front face
            MeshFace({2, 6, 7, 3}), // Back face
            MeshFace({0, 3, 7, 4}), // Left face
            MeshFace({1, 5, 6, 2}),  // Right face

            MeshFace({8, 9, 10, 11, 12, 13, 14, 15})};

        // Create ComputeMesh from MeshData
        m_computeMesh = std::make_shared<ComputeMesh>("TestCube", meshData);
        m_computeMesh->setRenderMode(MeshRenderMode::NormalShaded);
        m_computeMesh->setNormalShadingColors(Color(0.8f, 0.9f, 1.0f), Color(0.3f, 0.4f, 0.6f));
        m_computeMesh->setShowVertices(b_showVertices);
        m_computeMesh->setShowEdges(b_showEdges);
        m_computeMesh->setShowFaces(b_showFaces);
        
        scene().addObject(m_computeMesh);
        
        // Print mesh statistics
        printMeshStatistics();
    }

    void printMeshStatistics() {
        if (!m_computeMesh) return;
        
        const auto& heMeshData = m_computeMesh->getHeMeshData();
        
        std::cout << "\n=== Half-Edge Mesh Statistics ===" << std::endl;
        std::cout << "Vertices: " << heMeshData.vertices.size() << std::endl;
        std::cout << "Half-edges: " << heMeshData.halfedges.size() << std::endl;
        std::cout << "Edges: " << heMeshData.edges.size() << std::endl;
        std::cout << "Faces: " << heMeshData.faces.size() << std::endl;

        const auto& meshData = m_computeMesh->getMeshData();

        std::cout << "\n=== Mesh Statistics ===" << std::endl;
        std::cout << "Vertices: " << meshData->vertices.size() << std::endl;
        std::cout << "Edges: " << meshData->edges.size() << std::endl;
        std::cout << "Faces: " << meshData->faces.size() << std::endl;
        
        
        // Analyze vertex valencies
        std::cout << "\n=== Vertex Valencies ===" << std::endl;
        for (size_t i = 0; i < heMeshData.vertices.size(); ++i) {
            auto vertex = heMeshData.vertices[i];
            std::cout << "Vertex " << i << ": valency = " << vertex->getValency() 
                      << ", boundary = " << (vertex->onBoundary() ? "yes" : "no") << std::endl;
        }
        
        // Analyze boundary edges
        int boundaryEdges = 0;
        for (const auto& edge : heMeshData.edges) {
            if (edge->onBoundary()) {
                boundaryEdges++;
            }
        }
        std::cout << "\nBoundary edges: " << boundaryEdges << std::endl;
    }

    void update(float deltaTime) override {
        // Update logic here if needed
    }

    void draw(Renderer& renderer, Camera& camera) override {
        if (!m_computeMesh) return;
        
        // Draw half-edge specific visualizations
        if (b_showHalfEdges) {
            drawHalfEdges(renderer);
        }
        
        if (d_showVertexInfo) {
            drawVertexConnectivity(renderer);
        }
        
        if (d_showBoundaryInfo) {
            drawBoundaryInfo(renderer);
        }
        
        // Draw UI
        drawUI(renderer);
    }

    void drawHalfEdges(Renderer& renderer) {
        const auto& heMeshData = m_computeMesh->getHeMeshData();
        
        for (const auto& halfedge : heMeshData.halfedges) {
            auto startVertex = halfedge->getStartVertex();
            auto endVertex = halfedge->getVertex();
            
            if (startVertex && endVertex) {
                Vec3 start = startVertex->getPosition();
                Vec3 end = endVertex->getPosition();
                Vec3 direction = (end - start).normalized();
                Vec3 arrowPos = start + direction * 0.8f * (end - start).length();
                
                // Draw half-edge as colored line
                Color color = halfedge->onBoundary() ? Color(1.0f, 0.0f, 0.0f) : Color(0.0f, 1.0f, 0.0f);
                renderer.drawLine(start, arrowPos, color, 2.0f);
                
                // Draw small arrow head
                renderer.drawPoint(arrowPos, color, 4.0f);
            }
        }
    }

    void drawVertexConnectivity(Renderer& renderer) {
        if (m_selectedVertexId < 0 || m_selectedVertexId >= static_cast<int>(m_computeMesh->getVertices().size())) {
            return;
        }
        
        auto vertex = m_computeMesh->getVertex(m_selectedVertexId);
        if (!vertex) return;
        
        // Highlight selected vertex
        renderer.setColor(Color(1.0f, 1.0f, 0.0f));
        renderer.drawPoint(vertex->getPosition(), Color(1.0f, 1.0f, 0.0f), 8.0f);
        
        // Draw connections to neighboring vertices
        auto connectedVertices = vertex->getConnectedVertices();
        for (const auto& neighbor : connectedVertices) {
            Color lineColor(1.0f, 0.5f, 0.0f);
            renderer.drawLine(vertex->getPosition(), neighbor->getPosition(), lineColor, 3.0f);
            renderer.drawPoint(neighbor->getPosition(), Color(1.0f, 0.5f, 0.0f), 6.0f);
        }
    }

    void drawBoundaryInfo(Renderer& renderer) {
        const auto& heMeshData = m_computeMesh->getHeMeshData();
        
        // Highlight boundary vertices
        for (const auto& vertex : heMeshData.vertices) {
            if (vertex->onBoundary()) {
                renderer.setColor(Color(1.0f, 0.0f, 1.0f));
                renderer.drawPoint(vertex->getPosition(), Color(1.0f, 0.0f, 1.0f), 6.0f);
            }
        }
        
        // Highlight boundary edges
        for (const auto& edge : heMeshData.edges) {
            if (edge->onBoundary()) {
                auto vertices = edge->getVertices();
                if (vertices.first && vertices.second) {
                    Color edgeColor(1.0f, 0.0f, 1.0f);
                    renderer.drawLine(vertices.first->getPosition(), vertices.second->getPosition(), edgeColor, 4.0f);
                }
            }
        }
    }

    void drawUI(Renderer& renderer) {
        // 2D text rendering (screen overlay)
        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Color(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 70);

        // Controls
        renderer.setColor(Color(0.75f, 0.75f, 0.75f));
        renderer.drawString("Controls:", 10, 120);
        renderer.drawString("'v' - Toggle vertices", 10, 140);
        renderer.drawString("'e' - Toggle edges", 10, 160);
        renderer.drawString("'f' - Toggle faces", 10, 180);
        renderer.drawString("'h' - Toggle half-edges", 10, 200);
        renderer.drawString("'i' - Toggle vertex info", 10, 220);
        renderer.drawString("'b' - Toggle boundary info", 10, 240);
        renderer.drawString("'1/2' - Select vertex (" + std::to_string(m_selectedVertexId) + ")", 10, 260);
        
        // Mesh info
        if (m_computeMesh) {
            const auto& heMeshData = m_computeMesh->getHeMeshData();
            renderer.setColor(Color(0.9f, 0.9f, 0.5f));
            renderer.drawString("Mesh Info:", 10, 300);
            renderer.drawString("Vertices: " + std::to_string(heMeshData.vertices.size()), 10, 320);
            renderer.drawString("Half-edges: " + std::to_string(heMeshData.halfedges.size()), 10, 340);
            renderer.drawString("Edges: " + std::to_string(heMeshData.edges.size()), 10, 360);
            renderer.drawString("Faces: " + std::to_string(heMeshData.faces.size()), 10, 380);
        }
    }

    void cleanup() override {
        // Clean up resources
    }

    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case 'v':
                b_showVertices = !b_showVertices;
                if (m_computeMesh) m_computeMesh->setShowVertices(b_showVertices);
                return true;
                
            case 'e':
                b_showEdges = !b_showEdges;
                if (m_computeMesh) m_computeMesh->setShowEdges(b_showEdges);
                return true;
                
            case 'f':
                b_showFaces = !b_showFaces;
                if (m_computeMesh) m_computeMesh->setShowFaces(b_showFaces);
                return true;
                
            case 'h':
                b_showHalfEdges = !b_showHalfEdges;
                return true;
                
            case 'i':
                d_showVertexInfo = !d_showVertexInfo;
                return true;
                
            case 'b':
                d_showBoundaryInfo = !d_showBoundaryInfo;
                return true;
                
            case '1':
                if (m_computeMesh && m_selectedVertexId > 0) {
                    m_selectedVertexId--;
                }
                return true;
                
            case '2':
                if (m_computeMesh && m_selectedVertexId < static_cast<int>(m_computeMesh->getVertices().size()) - 1) {
                    m_selectedVertexId++;
                }
                return true;
        }
        return false;
    }

    std::string getAuthor() const override {
        return "alice2 User";
    }
};

// Register the sketch
// ALICE2_REGISTER_SKETCH_AUTO(HeMeshSketch)

#endif // __MAIN__
