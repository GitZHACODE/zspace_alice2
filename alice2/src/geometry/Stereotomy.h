#pragma once

#include <alice2.h>
#include <computeGeom/ComputeMesh.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace alice2::geometry {

// A contiguous face run found by following the stereotomy centre graph.
// walkEdges contains the directed edge used to enter each source face.
struct StereotomyFaceBlock {
    std::vector<int> sourceFaces;
    std::vector<std::pair<int, int>> walkEdges;
};

// An independent, closed-ready portion of the source mesh.  The top and
// bottom arrays share the same local indexing; faces refer to that indexing.
struct StereotomySolid {
    int blockIndex = -1;
    std::vector<int> sourceVertexIds;
    std::vector<Vec3> topVertices;
    std::vector<Vec3> bottomVertices;
    std::vector<std::vector<int>> faces;
    std::vector<std::pair<int, int>> rulingEdges;
};

// Partitions the stereotomy quad mesh and turns every partition into an
// independently offset, closed solid.  Per-block normals intentionally avoid
// contaminating a solid at vertices shared with neighbouring blocks.
class Stereotomy {
public:
    bool rebuild(const ComputeMesh& mesh, int facesPerBlock, float thickness,
                 std::string* diagnostic = nullptr);

    const std::vector<StereotomyFaceBlock>& blocks() const { return m_blocks; }
    const std::vector<StereotomySolid>& solids() const { return m_solids; }

    static bool walkFaceBlocks(const ComputeMesh& mesh, int facesPerBlock,
                               std::vector<StereotomyFaceBlock>& blocks,
                               std::string* diagnostic = nullptr);
    static std::vector<std::pair<int, int>> blockRulingEdges(const MeshData& mesh,
                                                               const StereotomyFaceBlock& block);
    static std::vector<Vec3> offsetBlockAlongVertexNormals(const MeshData& mesh,
                                                            const StereotomyFaceBlock& block,
                                                            const std::vector<Vec3>& topPositions,
                                                            float thickness);
    static std::shared_ptr<MeshObject> makeSolidMesh(const std::string& name,
                                                     const StereotomySolid& solid,
                                                     const Color& colour);

private:
    static Vec3 faceNormal(const MeshData& mesh, int faceIndex, const std::vector<Vec3>& positions);
    static bool sharedFaceEdge(const MeshData& mesh, int firstFace, int secondFace,
                               std::pair<int, int>& edge);
    static bool oppositeQuadEdge(const MeshData& mesh, int faceIndex, const std::pair<int, int>& edge,
                                 std::pair<int, int>& opposite);

    std::vector<StereotomyFaceBlock> m_blocks;
    std::vector<StereotomySolid> m_solids;
};

} // namespace alice2::geometry
