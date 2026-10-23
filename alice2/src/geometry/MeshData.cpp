#include "MeshData.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>

namespace alice2 {

    // MeshData implementation
    void MeshData::clear() {
        vertices.clear();
        edges.clear();
        faces.clear();
        triangleIndices.clear();
        triangulationDirty = true;
    }

    void MeshData::calculateNormals() {
        // Reset vertex normals
        for (auto& vertex : vertices) {
            vertex.normal = Vec3(0, 0, 0);
        }

        // Calculate face normals and accumulate vertex normals
        for (auto& face : faces) {
            face.normal = calculateFaceNormal(face);
            
            // Add face normal to each vertex normal
            for (int vertexIndex : face.vertices) {
                if (vertexIndex >= 0 && vertexIndex < static_cast<int>(vertices.size())) {
                    vertices[vertexIndex].normal = vertices[vertexIndex].normal + face.normal;
                }
            }
        }

        // Normalize vertex normals
        for (auto& vertex : vertices) {
            float length = std::sqrt(vertex.normal.x * vertex.normal.x + 
                                   vertex.normal.y * vertex.normal.y + 
                                   vertex.normal.z * vertex.normal.z);
            if (length > 0.0001f) {
                vertex.normal = vertex.normal * (1.0f / length);
            } else {
                vertex.normal = Vec3(0, 0, 1); // Default up normal
            }
        }
    }

    Vec3 MeshData::calculateFaceNormal(const MeshFace& face) const {
        if (face.vertices.size() < 3) {
            return Vec3(0, 0, 1); // Default up normal
        }

        // Use first three vertices to calculate normal
        int i0 = face.vertices[0];
        int i1 = face.vertices[1];
        int i2 = face.vertices[2];

        if (i0 < 0 || i0 >= static_cast<int>(vertices.size()) ||
            i1 < 0 || i1 >= static_cast<int>(vertices.size()) ||
            i2 < 0 || i2 >= static_cast<int>(vertices.size())) {
            return Vec3(0, 0, 1);
        }

        Vec3 v0 = vertices[i0].position;
        Vec3 v1 = vertices[i1].position;
        Vec3 v2 = vertices[i2].position;

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        
        // Cross product
        Vec3 normal = Vec3(
            edge1.y * edge2.z - edge1.z * edge2.y,
            edge1.z * edge2.x - edge1.x * edge2.z,
            edge1.x * edge2.y - edge1.y * edge2.x
        );

        // Normalize
        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0.0001f) {
            normal = normal * (1.0f / length);
        } else {
            normal = Vec3(0, 0, 1);
        }

        return normal;
    }

    void MeshData::triangulate() {
        triangleIndices.clear();

        for (const auto& face : faces) {
            if (face.vertices.size() < 3) continue;

            // Simple fan triangulation for n-gons
            // This works well for convex polygons
            for (size_t i = 1; i < face.vertices.size() - 1; i++) {
                triangleIndices.push_back(face.vertices[0]);
                triangleIndices.push_back(face.vertices[i]);
                triangleIndices.push_back(face.vertices[i + 1]);
            }
        }

        triangulationDirty = false;
    }

    void MeshData::updateBounds(Vec3& minBounds, Vec3& maxBounds) const {
        if (vertices.empty()) {
            minBounds = Vec3(-0.5f, -0.5f, -0.5f);
            maxBounds = Vec3(0.5f, 0.5f, 0.5f);
            return;
        }

        minBounds = vertices[0].position;
        maxBounds = vertices[0].position;

        for (const auto& vertex : vertices) {
            minBounds.x = std::min(minBounds.x, vertex.position.x);
            minBounds.y = std::min(minBounds.y, vertex.position.y);
            minBounds.z = std::min(minBounds.z, vertex.position.z);
            
            maxBounds.x = std::max(maxBounds.x, vertex.position.x);
            maxBounds.y = std::max(maxBounds.y, vertex.position.y);
            maxBounds.z = std::max(maxBounds.z, vertex.position.z);
        }
    }

    namespace {
        void rebuildEdgesFromFaces(MeshData& mesh) {
            mesh.edges.clear();
            std::set<std::pair<int, int>> edgeSet;
            for (const auto& face : mesh.faces) {
                for (size_t i = 0; i < face.vertices.size(); ++i) {
                    int a = face.vertices[i];
                    int b = face.vertices[(i + 1) % face.vertices.size()];
                    if (a < 0 || b < 0 || a == b) continue;
                    if (a > b) std::swap(a, b);
                    edgeSet.insert({a, b});
                }
            }
            for (const auto& edge : edgeSet) {
                mesh.edges.emplace_back(edge.first, edge.second, Color(0, 0, 0, 1));
            }
        }

        std::vector<int> orderedChamferNeighbors(const MeshData& mesh,
                                                 int vertexId,
                                                 const std::vector<int>& neighbors,
                                                 const std::set<int>& boundaryNeighbors) {
            std::vector<int> ordered = neighbors;
            const Vec3& center = mesh.vertices[vertexId].position;
            std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
                Vec3 da = mesh.vertices[a].position - center;
                Vec3 db = mesh.vertices[b].position - center;
                return std::atan2(da.y, da.x) < std::atan2(db.y, db.x);
            });

            if (boundaryNeighbors.size() != 2 || ordered.size() < 3) return ordered;

            int b0 = *boundaryNeighbors.begin();
            int b1 = *std::next(boundaryNeighbors.begin());
            auto it0 = std::find(ordered.begin(), ordered.end(), b0);
            auto it1 = std::find(ordered.begin(), ordered.end(), b1);
            if (it0 == ordered.end() || it1 == ordered.end()) return ordered;

            int i0 = static_cast<int>(std::distance(ordered.begin(), it0));
            int i1 = static_cast<int>(std::distance(ordered.begin(), it1));
            int n = static_cast<int>(ordered.size());

            auto path = [&](int start, int end) {
                std::vector<int> result;
                int index = start;
                while (true) {
                    result.push_back(ordered[index]);
                    if (index == end) break;
                    index = (index + 1) % n;
                }
                return result;
            };

            std::vector<int> pathA = path(i0, i1);
            std::vector<int> pathB = path(i1, i0);

            auto internalCount = [&](const std::vector<int>& pathIds) {
                int count = 0;
                for (int id : pathIds) {
                    if (!boundaryNeighbors.count(id)) ++count;
                }
                return count;
            };

            int internalA = internalCount(pathA);
            int internalB = internalCount(pathB);
            if (internalA != internalB) return internalA > internalB ? pathA : pathB;
            return pathA.size() <= pathB.size() ? pathA : pathB;
        }

        void compactMeshData(MeshData& mesh) {
            std::vector<int> remap(mesh.vertices.size(), -1);
            std::vector<MeshVertex> vertices;
            for (const auto& face : mesh.faces) {
                for (int id : face.vertices) {
                    if (id >= 0 && id < static_cast<int>(remap.size()) && remap[id] < 0) {
                        remap[id] = static_cast<int>(vertices.size());
                        vertices.push_back(mesh.vertices[id]);
                    }
                }
            }

            for (auto& face : mesh.faces) {
                for (int& id : face.vertices) {
                    if (id >= 0 && id < static_cast<int>(remap.size())) id = remap[id];
                }
            }
            for (auto& edge : mesh.edges) {
                if (edge.vertexA >= 0 && edge.vertexA < static_cast<int>(remap.size())) edge.vertexA = remap[edge.vertexA];
                if (edge.vertexB >= 0 && edge.vertexB < static_cast<int>(remap.size())) edge.vertexB = remap[edge.vertexB];
            }

            mesh.vertices.swap(vertices);
            mesh.edges.erase(std::remove_if(mesh.edges.begin(), mesh.edges.end(), [&](const MeshEdge& edge) {
                return edge.vertexA < 0 || edge.vertexB < 0 || edge.vertexA == edge.vertexB;
            }), mesh.edges.end());
        }
    }

    MeshData chamferMeshVertices(const MeshData& mesh,
                                 const std::vector<int>& vertexIds,
                                 float edgePercentage,
                                 bool removeInternalEdges) {
        MeshData result = mesh;
        edgePercentage = std::clamp(edgePercentage, 0.0f, 1.0f);
        if (edgePercentage <= 1e-6f || vertexIds.empty() || mesh.vertices.empty() || mesh.faces.empty()) return result;

        std::set<int> selected;
        for (int id : vertexIds) {
            if (id >= 0 && id < static_cast<int>(mesh.vertices.size())) selected.insert(id);
        }
        if (selected.empty()) return result;

        std::map<std::pair<int, int>, int> edgeUseCount;
        std::map<int, std::set<int>> neighborSets;
        for (const auto& face : mesh.faces) {
            for (size_t i = 0; i < face.vertices.size(); ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % face.vertices.size()];
                if (a < 0 || b < 0 ||
                    a >= static_cast<int>(mesh.vertices.size()) ||
                    b >= static_cast<int>(mesh.vertices.size()) ||
                    a == b) continue;
                int lo = std::min(a, b);
                int hi = std::max(a, b);
                edgeUseCount[{lo, hi}]++;
                neighborSets[a].insert(b);
                neighborSets[b].insert(a);
            }
        }

        for (auto it = selected.begin(); it != selected.end();) {
            auto found = neighborSets.find(*it);
            if (found == neighborSets.end() || found->second.size() < 3) {
                it = selected.erase(it);
            } else {
                ++it;
            }
        }
        if (selected.empty()) return result;

        std::map<std::pair<int, int>, int> chamferVertexByDirectedEdge;
        auto chamferVertex = [&](int center, int neighbor) {
            std::pair<int, int> key{center, neighbor};
            auto found = chamferVertexByDirectedEdge.find(key);
            if (found != chamferVertexByDirectedEdge.end()) return found->second;

            const Vec3& c = result.vertices[center].position;
            const Vec3& n = result.vertices[neighbor].position;
            Vec3 dir = n - c;
            float length = dir.length();
            if (length <= 1e-6f) return center;
            dir = dir / length;
            float offset = length * edgePercentage;
            MeshVertex vertex(c + dir * offset,
                              result.vertices[center].normal,
                              result.vertices[center].color);
            int id = static_cast<int>(result.vertices.size());
            result.vertices.push_back(vertex);
            chamferVertexByDirectedEdge[key] = id;
            return id;
        };

        std::vector<MeshFace> rewrittenFaces;
        rewrittenFaces.reserve(result.faces.size() + selected.size());
        for (const auto& face : result.faces) {
            std::vector<int> rewritten;
            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                int id = face.vertices[i];
                if (!selected.count(id)) {
                    rewritten.push_back(id);
                    continue;
                }

                int prev = face.vertices[(i - 1 + static_cast<int>(face.vertices.size())) % static_cast<int>(face.vertices.size())];
                int next = face.vertices[(i + 1) % static_cast<int>(face.vertices.size())];
                rewritten.push_back(chamferVertex(id, prev));
                rewritten.push_back(chamferVertex(id, next));
            }

            rewritten.erase(std::unique(rewritten.begin(), rewritten.end()), rewritten.end());
            if (rewritten.size() > 1 && rewritten.front() == rewritten.back()) rewritten.pop_back();
            if (rewritten.size() >= 3) rewrittenFaces.emplace_back(rewritten, face.normal, face.color);
        }

        for (int selectedVertex : selected) {
            std::vector<int> neighbors(neighborSets[selectedVertex].begin(), neighborSets[selectedVertex].end());
            std::set<int> boundaryNeighbors;
            for (int neighbor : neighbors) {
                int lo = std::min(selectedVertex, neighbor);
                int hi = std::max(selectedVertex, neighbor);
                if (edgeUseCount[{lo, hi}] == 1) boundaryNeighbors.insert(neighbor);
            }

            std::vector<int> ordered = orderedChamferNeighbors(result, selectedVertex, neighbors, boundaryNeighbors);
            if (ordered.size() < 3) continue;

            std::vector<int> chamferFace;
            bool isBoundaryVertex = boundaryNeighbors.size() == 2;
            chamferFace.reserve(ordered.size() + (isBoundaryVertex ? 1 : 0));
            for (int neighbor : ordered) chamferFace.push_back(chamferVertex(selectedVertex, neighbor));
            if (isBoundaryVertex) chamferFace.push_back(selectedVertex);
            if (chamferFace.size() >= 3) {
                rewrittenFaces.emplace_back(chamferFace, Vec3(0, 0, 1), result.vertices[selectedVertex].color);
            }
        }

        result.faces.swap(rewrittenFaces);
        rebuildEdgesFromFaces(result);

        if (!removeInternalEdges) {
            for (const auto& item : chamferVertexByDirectedEdge) {
                result.edges.emplace_back(item.first.first, item.second, Color(0, 0, 0, 1));
            }
        } else {
            compactMeshData(result);
            rebuildEdgesFromFaces(result);
        }

        result.calculateNormals();
        result.triangulationDirty = true;
        return result;
    }

    void MeshData::chamferVertices(const std::vector<int>& vertexIds,
                                   float edgePercentage,
                                   bool removeInternalEdges) {
        *this = chamferMeshVertices(*this, vertexIds, edgePercentage, removeInternalEdges);
    }

    MeshData catmullClarkSmoothMesh(const MeshData& mesh,
                                    int levels,
                                    const std::vector<int>& fixedVertices,
                                    bool fixBoundaryCorners) {
        if (levels <= 0 || mesh.vertices.empty() || mesh.faces.empty()) return mesh;

        MeshData current = mesh;
        for (int level = 0; level < levels; ++level) {
            std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
            std::vector<std::vector<int>> vertexFaces(current.vertices.size());
            std::vector<std::set<int>> vertexNeighbors(current.vertices.size());
            for (int fi = 0; fi < static_cast<int>(current.faces.size()); ++fi) {
                const auto& face = current.faces[fi];
                for (int id : face.vertices) {
                    if (id >= 0 && id < static_cast<int>(vertexFaces.size())) vertexFaces[id].push_back(fi);
                }
                for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                    int a = face.vertices[i];
                    int b = face.vertices[(i + 1) % static_cast<int>(face.vertices.size())];
                    if (a < 0 || b < 0 ||
                        a >= static_cast<int>(current.vertices.size()) ||
                        b >= static_cast<int>(current.vertices.size()) ||
                        a == b) continue;
                    edgeFaces[{std::min(a, b), std::max(a, b)}].push_back(fi);
                    vertexNeighbors[a].insert(b);
                    vertexNeighbors[b].insert(a);
                }
            }

            std::set<int> fixed(fixedVertices.begin(), fixedVertices.end());
            std::vector<std::vector<int>> boundaryNeighbors(current.vertices.size());
            for (const auto& item : edgeFaces) {
                if (item.second.size() == 1) {
                    boundaryNeighbors[item.first.first].push_back(item.first.second);
                    boundaryNeighbors[item.first.second].push_back(item.first.first);
                }
            }
            if (fixBoundaryCorners) {
                for (int vi = 0; vi < static_cast<int>(boundaryNeighbors.size()); ++vi) {
                    if (boundaryNeighbors[vi].size() == 2 && vertexNeighbors[vi].size() == 2) {
                        fixed.insert(vi);
                    }
                }
            }

            std::vector<Vec3> facePoints(current.faces.size());
            for (int fi = 0; fi < static_cast<int>(current.faces.size()); ++fi) {
                const auto& face = current.faces[fi];
                Vec3 center;
                int count = 0;
                for (int id : face.vertices) {
                    if (id >= 0 && id < static_cast<int>(current.vertices.size())) {
                        center += current.vertices[id].position;
                        ++count;
                    }
                }
                facePoints[fi] = count > 0 ? center / static_cast<float>(count) : Vec3();
            }

            MeshData next;
            std::vector<int> vertexPointIds(current.vertices.size(), -1);
            for (int vi = 0; vi < static_cast<int>(current.vertices.size()); ++vi) {
                Vec3 p = current.vertices[vi].position;
                Vec3 newPosition = p;

                if (!fixed.count(vi)) {
                    if (boundaryNeighbors[vi].size() == 2) {
                        Vec3 a = current.vertices[boundaryNeighbors[vi][0]].position;
                        Vec3 b = current.vertices[boundaryNeighbors[vi][1]].position;
                        newPosition = p * 0.75f + (a + b) * 0.125f;
                    } else {
                        int n = static_cast<int>(vertexNeighbors[vi].size());
                        if (n > 0 && !vertexFaces[vi].empty()) {
                            Vec3 f;
                            for (int faceId : vertexFaces[vi]) f += facePoints[faceId];
                            f = f / static_cast<float>(vertexFaces[vi].size());

                            Vec3 r;
                            for (int neighbor : vertexNeighbors[vi]) {
                                r += (p + current.vertices[neighbor].position) * 0.5f;
                            }
                            r = r / static_cast<float>(n);
                            newPosition = (f + r * 2.0f + p * static_cast<float>(n - 3)) / static_cast<float>(n);
                        }
                    }
                }

                vertexPointIds[vi] = static_cast<int>(next.vertices.size());
                next.vertices.emplace_back(newPosition, current.vertices[vi].normal, current.vertices[vi].color);
            }

            std::map<std::pair<int, int>, int> edgePointIds;
            for (const auto& item : edgeFaces) {
                int a = item.first.first;
                int b = item.first.second;
                Vec3 point = (current.vertices[a].position + current.vertices[b].position) * 0.5f;
                if (item.second.size() == 2) {
                    point = (current.vertices[a].position +
                             current.vertices[b].position +
                             facePoints[item.second[0]] +
                             facePoints[item.second[1]]) * 0.25f;
                }
                int id = static_cast<int>(next.vertices.size());
                next.vertices.emplace_back(point, Vec3(0, 0, 1), Color(0.8f, 0.8f, 0.82f, 1.0f));
                edgePointIds[item.first] = id;
            }

            std::vector<int> facePointIds(current.faces.size(), -1);
            for (int fi = 0; fi < static_cast<int>(current.faces.size()); ++fi) {
                facePointIds[fi] = static_cast<int>(next.vertices.size());
                next.vertices.emplace_back(facePoints[fi], Vec3(0, 0, 1), current.faces[fi].color);
            }

            for (int fi = 0; fi < static_cast<int>(current.faces.size()); ++fi) {
                const auto& face = current.faces[fi];
                if (face.vertices.size() < 3) continue;
                for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                    int v = face.vertices[i];
                    int prev = face.vertices[(i - 1 + static_cast<int>(face.vertices.size())) % static_cast<int>(face.vertices.size())];
                    int nextV = face.vertices[(i + 1) % static_cast<int>(face.vertices.size())];
                    auto prevEdge = std::make_pair(std::min(prev, v), std::max(prev, v));
                    auto nextEdge = std::make_pair(std::min(v, nextV), std::max(v, nextV));
                    if (!edgePointIds.count(prevEdge) || !edgePointIds.count(nextEdge)) continue;
                    next.faces.emplace_back(std::vector<int>{
                                                vertexPointIds[v],
                                                edgePointIds[nextEdge],
                                                facePointIds[fi],
                                                edgePointIds[prevEdge]
                                            },
                                            Vec3(0, 0, 1),
                                            face.color);
                }
            }

            rebuildEdgesFromFaces(next);
            next.calculateNormals();
            next.triangulationDirty = true;
            current = next;
        }

        return current;
    }

    void MeshData::catmullClarkSmooth(int levels,
                                      const std::vector<int>& fixedVertices,
                                      bool fixBoundaryCorners) {
        *this = catmullClarkSmoothMesh(*this, levels, fixedVertices, fixBoundaryCorners);
    }

    // MeshObject implementation
} // namespace alice2
