#include "Stereotomy.h"

#include <algorithm>
#include <unordered_map>

namespace alice2::geometry {

bool Stereotomy::walkFaceBlocks(const ComputeMesh& mesh, int facesPerBlock,
                                 std::vector<StereotomyFaceBlock>& blocks, std::string* diagnostic) {
    const std::shared_ptr<MeshData> data = mesh.getMeshData();
    if (!data || data->faces.empty()) {
        if (diagnostic) *diagnostic = "Stereotomy mesh contains no faces.";
        return false;
    }
    if (facesPerBlock < 1) {
        if (diagnostic) *diagnostic = "Faces per block must be positive.";
        return false;
    }
    blocks.clear();
    std::vector<std::shared_ptr<HeMeshVertex>> starts;
    for (const auto& vertex : mesh.getVertices()) {
        if (!vertex || vertex->getValency() != 3) continue;
        int valencyTwo = 0, valencyFour = 0;
        for (const auto& neighbour : vertex->getConnectedVertices()) {
            if (!neighbour) continue;
            valencyTwo += neighbour->getValency() == 2;
            valencyFour += neighbour->getValency() == 4;
        }
        if (valencyTwo == 2 && valencyFour == 1) starts.push_back(vertex);
    }
    if (starts.empty()) {
        if (diagnostic) *diagnostic = "No valency-3 centre-graph endpoint with two valency-2 neighbours was found.";
        return false;
    }

    std::vector<bool> assigned(data->faces.size(), false);
    int assignedCount = 0;
    auto flush = [&](StereotomyFaceBlock& pending) {
        if (!pending.sourceFaces.empty()) {
            blocks.push_back(std::move(pending));
            pending = StereotomyFaceBlock{};
        }
    };
    auto walkFrom = [&](const std::shared_ptr<HeMeshVertex>& start) {
        std::shared_ptr<HeMeshHalfedge> startHalfedge;
        for (const auto& halfedge : start->getHalfedges()) {
            if (halfedge && halfedge->getVertex() && halfedge->getVertex()->getValency() == 4) {
                startHalfedge = halfedge;
                break;
            }
        }
        if (!startHalfedge) return false;

        StereotomyFaceBlock pending;
        std::shared_ptr<HeMeshHalfedge> current = startHalfedge;
        bool firstStep = true;
        const int guardLimit = std::max(1, static_cast<int>(mesh.getHalfedges().size()) * 2);
        for (int step = 0; step < guardLimit; ++step) {
            if (!firstStep && current == startHalfedge) {
                flush(pending);
                return true;
            }
            firstStep = false;
            if (!current || !current->getFace() || !current->getStartVertex() || !current->getVertex()) return false;
            const int face = current->getFace()->getId();
            if (face < 0 || face >= static_cast<int>(assigned.size())) return false;
            if (!assigned[face]) {
                assigned[face] = true;
                ++assignedCount;
                pending.sourceFaces.push_back(face);
                pending.walkEdges.push_back({current->getStartVertex()->getId(), current->getVertex()->getId()});
                if (static_cast<int>(pending.sourceFaces.size()) == facesPerBlock) flush(pending);
            }
            if (current->getVertex()->getValency() == 3) {
                flush(pending);
                current = current->getSymmetry();
                if (!current || current->onBoundary()) return false;
                continue;
            }
            const auto next = current->getNext();
            const auto twin = next ? next->getSymmetry() : nullptr;
            current = twin ? twin->getNext() : nullptr;
            if (!current || current->onBoundary()) return false;
        }
        return false;
    };

    for (const auto& start : starts) {
        if (assignedCount == static_cast<int>(data->faces.size())) break;
        if (!walkFrom(start)) {
            if (diagnostic) *diagnostic = "Centre-graph next/twin/next walk did not close.";
            return false;
        }
    }
    if (assignedCount != static_cast<int>(data->faces.size())) {
        if (diagnostic) *diagnostic = "Centre-graph walks assigned " + std::to_string(assignedCount) + " of " +
                                      std::to_string(data->faces.size()) + " faces.";
        return false;
    }
    if (diagnostic) *diagnostic = "Stereotomy walk: " + std::to_string(blocks.size()) + " blocks from " +
                                  std::to_string(data->faces.size()) + " faces.";
    return true;
}

Vec3 Stereotomy::faceNormal(const MeshData& mesh, int faceIndex, const std::vector<Vec3>& positions) {
    const std::vector<int>& face = mesh.faces[faceIndex].vertices;
    if (face.size() < 3) return {};
    return (positions[face[1]] - positions[face[0]]).cross(positions[face[2]] - positions[face[0]]).normalized();
}

std::vector<Vec3> Stereotomy::offsetBlockAlongVertexNormals(const MeshData& mesh,
                                                              const StereotomyFaceBlock& block,
                                                              const std::vector<Vec3>& topPositions,
                                                              float thickness) {
    std::vector<Vec3> result = topPositions;
    std::vector<Vec3> normalSums(topPositions.size());
    for (int faceIndex : block.sourceFaces) {
        if (faceIndex < 0 || faceIndex >= static_cast<int>(mesh.faces.size())) continue;
        const Vec3 normal = faceNormal(mesh, faceIndex, topPositions);
        for (int vertex : mesh.faces[faceIndex].vertices) {
            if (vertex >= 0 && vertex < static_cast<int>(normalSums.size())) normalSums[vertex] += normal;
        }
    }
    for (size_t vertex = 0; vertex < result.size(); ++vertex) {
        if (normalSums[vertex].lengthSquared() > 1e-8f) result[vertex] -= normalSums[vertex].normalized() * thickness;
    }
    return result;
}

bool Stereotomy::sharedFaceEdge(const MeshData& mesh, int firstFace, int secondFace, std::pair<int, int>& edge) {
    if (firstFace < 0 || secondFace < 0 || firstFace >= static_cast<int>(mesh.faces.size()) ||
        secondFace >= static_cast<int>(mesh.faces.size())) return false;
    const auto& first = mesh.faces[firstFace].vertices;
    const auto& second = mesh.faces[secondFace].vertices;
    for (size_t i = 0; i < first.size(); ++i) for (size_t j = 0; j < second.size(); ++j) {
        const int a = first[i], b = first[(i + 1) % first.size()];
        const int c = second[j], d = second[(j + 1) % second.size()];
        if ((a == c && b == d) || (a == d && b == c)) { edge = {a, b}; return true; }
    }
    return false;
}

bool Stereotomy::oppositeQuadEdge(const MeshData& mesh, int faceIndex, const std::pair<int, int>& edge,
                                  std::pair<int, int>& opposite) {
    if (faceIndex < 0 || faceIndex >= static_cast<int>(mesh.faces.size())) return false;
    const auto& face = mesh.faces[faceIndex].vertices;
    if (face.size() != 4) return false;
    for (size_t i = 0; i < face.size(); ++i) {
        const int a = face[i], b = face[(i + 1) % face.size()];
        if ((a == edge.first && b == edge.second) || (a == edge.second && b == edge.first)) {
            opposite = {face[(i + 2) % face.size()], face[(i + 3) % face.size()]};
            return true;
        }
    }
    return false;
}

std::vector<std::pair<int, int>> Stereotomy::blockRulingEdges(const MeshData& mesh,
                                                                const StereotomyFaceBlock& block) {
    std::vector<std::pair<int, int>> result, interior;
    if (block.sourceFaces.size() < 2) return result;
    interior.reserve(block.sourceFaces.size() - 1);
    for (size_t face = 1; face < block.sourceFaces.size(); ++face) {
        std::pair<int, int> edge;
        if (sharedFaceEdge(mesh, block.sourceFaces[face - 1], block.sourceFaces[face], edge)) interior.push_back(edge);
    }
    if (interior.empty()) return result;
    std::pair<int, int> boundary;
    if (oppositeQuadEdge(mesh, block.sourceFaces.front(), interior.front(), boundary)) result.push_back(boundary);
    result.insert(result.end(), interior.begin(), interior.end());
    if (oppositeQuadEdge(mesh, block.sourceFaces.back(), interior.back(), boundary)) result.push_back(boundary);
    return result;
}

bool Stereotomy::rebuild(const ComputeMesh& mesh, int facesPerBlock, float thickness, std::string* diagnostic) {
    m_blocks.clear();
    m_solids.clear();
    std::vector<StereotomyFaceBlock> blocks;
    if (!walkFaceBlocks(mesh, facesPerBlock, blocks, diagnostic)) return false;
    const std::shared_ptr<MeshData> data = mesh.getMeshData();
    std::vector<Vec3> topPositions;
    topPositions.reserve(data->vertices.size());
    for (const MeshVertex& vertex : data->vertices) topPositions.push_back(vertex.position);

    std::vector<StereotomySolid> solids;
    solids.reserve(blocks.size());
    for (int blockIndex = 0; blockIndex < static_cast<int>(blocks.size()); ++blockIndex) {
        const auto& block = blocks[blockIndex];
        const std::vector<Vec3> bottomPositions = offsetBlockAlongVertexNormals(*data, block, topPositions, thickness);
        StereotomySolid solid;
        solid.blockIndex = blockIndex;
        std::unordered_map<int, int> localBySource;
        for (int faceIndex : block.sourceFaces) {
            if (faceIndex < 0 || faceIndex >= static_cast<int>(data->faces.size())) continue;
            std::vector<int> face;
            for (int sourceVertex : data->faces[faceIndex].vertices) {
                if (sourceVertex < 0 || sourceVertex >= static_cast<int>(topPositions.size())) continue;
                auto [it, inserted] = localBySource.emplace(sourceVertex, static_cast<int>(solid.topVertices.size()));
                if (inserted) {
                    solid.sourceVertexIds.push_back(sourceVertex);
                    solid.topVertices.push_back(topPositions[sourceVertex]);
                    solid.bottomVertices.push_back(bottomPositions[sourceVertex]);
                }
                face.push_back(it->second);
            }
            if (face.size() >= 3) solid.faces.push_back(std::move(face));
        }
        for (const auto& [a, b] : blockRulingEdges(*data, block)) {
            const auto aIt = localBySource.find(a), bIt = localBySource.find(b);
            if (aIt != localBySource.end() && bIt != localBySource.end()) solid.rulingEdges.push_back({aIt->second, bIt->second});
        }
        solids.push_back(std::move(solid));
    }
    m_blocks = std::move(blocks);
    m_solids = std::move(solids);
    return true;
}

std::shared_ptr<MeshObject> Stereotomy::makeSolidMesh(const std::string& name, const StereotomySolid& solid,
                                                       const Color& colour) {
    if (solid.topVertices.empty() || solid.topVertices.size() != solid.bottomVertices.size() || solid.faces.empty()) return nullptr;
    MeshObject bottom(name + "_bottom");
    bottom.createFromVerticesAndFaces(solid.bottomVertices, solid.faces);
    std::vector<Vec3> offsets;
    offsets.reserve(solid.topVertices.size());
    for (size_t i = 0; i < solid.topVertices.size(); ++i) offsets.push_back(solid.topVertices[i] - solid.bottomVertices[i]);
    auto mesh = std::make_shared<MeshObject>(bottom.extrudeMesh(0.0f, MeshExtrudeMode::SmoothSolid, offsets));
    mesh->setUseFaceColors(true);
    mesh->setShowEdges(true);
    mesh->setEdgeWidth(2.0f);
    const auto data = mesh->getMeshData();
    for (MeshFace& face : data->faces) face.color = colour;
    for (MeshEdge& edge : data->edges) edge.color = Color(0, 0, 0, 1);
    return mesh;
}

} // namespace alice2::geometry
