#pragma once

#ifndef ALICE2_STRESS_ALIGNED_REMESHER_H
#define ALICE2_STRESS_ALIGNED_REMESHER_H

#include "../computeGeom/TensorField.h"
#include "../objects/GraphObject.h"
#include "../objects/MeshObject.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace alice2 {

    class StressAlignedRemesher {
    public:
        void setTargetEdgeLength(double length) { targetEdgeLength_ = length; }
        void setSnapVertices(const std::vector<int>& vertexIds) { snapVertices_ = vertexIds; }
        void setAngleSimplificationTolerance(float radians) { angleSimplificationTolerance_ = radians; }
        void setChamferPercentage(float percentage) { chamferPercentage_ = std::clamp(percentage, 0.0f, 1.0f); }
        void setSmoothLevels(int levels) { smoothLevels_ = std::max(0, levels); }

        double targetEdgeLength() const { return targetEdgeLength_; }
        float angleSimplificationTolerance() const { return angleSimplificationTolerance_; }
        float chamferPercentage() const { return chamferPercentage_; }
        int smoothLevels() const { return smoothLevels_; }

        std::shared_ptr<MeshData> remesh(const MeshData& mesh, const TensorField& crossField) const;

    private:
        double targetEdgeLength_{0.2};
        std::vector<int> snapVertices_;
        float angleSimplificationTolerance_{0.38f};
        float chamferPercentage_{0.20f};
        int smoothLevels_{1};
    };

    namespace stress_aligned_remesher_detail {
        static constexpr float kPi = 3.14159265358979323846f;

        struct Singularity {
            int vertex{-1};
            float index{0.0f};
            std::vector<int> faces;
        };

        struct SingularityCluster {
            Vec3 center;
            std::vector<int> rawIds;
            std::vector<int> vertices;
            std::vector<int> faces;
            float indexSum{0.0f};
        };

        inline float wrapPi(float value) {
            while (value <= -kPi) value += 2.0f * kPi;
            while (value > kPi) value -= 2.0f * kPi;
            return value;
        }

        inline float crossXY(const Vec3& origin, const Vec3& a, const Vec3& b) {
            return (a.x - origin.x) * (b.y - origin.y) - (a.y - origin.y) * (b.x - origin.x);
        }

        inline std::pair<int, int> sortedEdge(int a, int b) {
            return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
        }

        inline Vec3 faceCenter(const MeshData& mesh, const MeshFace& face) {
            Vec3 center;
            if (face.vertices.empty()) return center;
            for (int id : face.vertices) {
                if (id >= 0 && id < static_cast<int>(mesh.vertices.size())) {
                    center += mesh.vertices[id].position;
                }
            }
            return center / static_cast<float>(face.vertices.size());
        }

        inline std::vector<Vec3> convexHullXY(std::vector<Vec3> points) {
            if (points.size() <= 3) return points;

            std::sort(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) {
                if (std::abs(a.x - b.x) > 1e-6f) return a.x < b.x;
                return a.y < b.y;
            });
            points.erase(std::unique(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) {
                return std::abs(a.x - b.x) <= 1e-6f && std::abs(a.y - b.y) <= 1e-6f;
            }), points.end());
            if (points.size() <= 3) return points;

            std::vector<Vec3> hull;
            hull.reserve(points.size() * 2);
            for (const Vec3& p : points) {
                while (hull.size() >= 2 && crossXY(hull[hull.size() - 2], hull.back(), p) <= 1e-6f) {
                    hull.pop_back();
                }
                hull.push_back(p);
            }

            size_t lowerSize = hull.size();
            for (int i = static_cast<int>(points.size()) - 2; i >= 0; --i) {
                const Vec3& p = points[i];
                while (hull.size() > lowerSize && crossXY(hull[hull.size() - 2], hull.back(), p) <= 1e-6f) {
                    hull.pop_back();
                }
                hull.push_back(p);
            }
            if (!hull.empty()) hull.pop_back();
            return hull;
        }

        inline std::vector<Vec3> edgeMidpointDualPolygon(const std::vector<Vec3>& polygon) {
            std::vector<Vec3> result;
            result.reserve(polygon.size());
            if (polygon.size() < 3) return result;
            for (size_t i = 0; i < polygon.size(); ++i) {
                result.push_back((polygon[i] + polygon[(i + 1) % polygon.size()]) * 0.5f);
            }
            return result;
        }

        inline std::vector<std::vector<int>> vertexFaces(const MeshData& mesh) {
            std::vector<std::vector<int>> result(mesh.vertices.size());
            for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
                for (int id : mesh.faces[fi].vertices) {
                    if (id >= 0 && id < static_cast<int>(result.size())) result[id].push_back(fi);
                }
            }
            return result;
        }

        inline std::set<int> boundaryVertices(const MeshData& mesh) {
            std::map<std::pair<int, int>, int> uses;
            for (const auto& face : mesh.faces) {
                for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                    int a = face.vertices[i];
                    int b = face.vertices[(i + 1) % face.vertices.size()];
                    uses[sortedEdge(a, b)]++;
                }
            }

            std::set<int> result;
            for (const auto& item : uses) {
                if (item.second == 1) {
                    result.insert(item.first.first);
                    result.insert(item.first.second);
                }
            }
            return result;
        }

        inline std::vector<std::pair<int, int>> boundaryEdges(const MeshData& mesh) {
            std::map<std::pair<int, int>, int> uses;
            for (const auto& face : mesh.faces) {
                for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                    int a = face.vertices[i];
                    int b = face.vertices[(i + 1) % face.vertices.size()];
                    uses[sortedEdge(a, b)]++;
                }
            }

            std::vector<std::pair<int, int>> result;
            for (const auto& item : uses) {
                if (item.second == 1) result.push_back(item.first);
            }
            return result;
        }

        inline std::vector<int> sortedIncidentFaces(const MeshData& mesh, int vertexId, const std::vector<int>& faces) {
            std::vector<int> sorted = faces;
            const Vec3& p = mesh.vertices[vertexId].position;
            std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
                Vec3 ca = faceCenter(mesh, mesh.faces[a]) - p;
                Vec3 cb = faceCenter(mesh, mesh.faces[b]) - p;
                return std::atan2(ca.y, ca.x) < std::atan2(cb.y, cb.x);
            });
            return sorted;
        }

        inline std::vector<Singularity> findSingularities(const MeshData& mesh, const TensorField& field) {
            std::vector<Singularity> result;
            auto facesByVertex = vertexFaces(mesh);
            auto boundary = boundaryVertices(mesh);

            for (int vi = 0; vi < static_cast<int>(facesByVertex.size()); ++vi) {
                if (boundary.count(vi) || facesByVertex[vi].size() < 3) continue;
                std::vector<int> faces = sortedIncidentFaces(mesh, vi, facesByVertex[vi]);

                float total = 0.0f;
                bool valid = true;
                for (size_t i = 0; i < faces.size(); ++i) {
                    int fa = faces[i];
                    int fb = faces[(i + 1) % faces.size()];
                    if (fa < 0 || fb < 0 || fa >= static_cast<int>(field.size()) || fb >= static_cast<int>(field.size())) {
                        valid = false;
                        break;
                    }

                    Vec3 da = field[fa].majorDirection;
                    Vec3 db = field[fb].majorDirection;
                    da.z = 0.0f;
                    db.z = 0.0f;
                    if (da.lengthSquared() <= 1e-10f || db.lengthSquared() <= 1e-10f) {
                        valid = false;
                        break;
                    }
                    da.normalize();
                    db.normalize();

                    float aa = std::atan2(da.y, da.x);
                    float ab = std::atan2(db.y, db.x);
                    total += wrapPi(4.0f * ab - 4.0f * aa);
                }
                if (!valid) continue;

                float index = total / (8.0f * kPi);
                if (std::abs(index) >= 0.12f) {
                    result.push_back({vi, index, faces});
                }
            }

            std::sort(result.begin(), result.end(), [](const Singularity& a, const Singularity& b) {
                return std::abs(a.index) > std::abs(b.index);
            });
            return result;
        }

        inline std::vector<SingularityCluster> clusterSingularities(const MeshData& mesh,
                                                                   const std::vector<Singularity>& singularities,
                                                                   float clusterDistance) {
            std::vector<SingularityCluster> clusters;
            float clusterDistanceSq = clusterDistance * clusterDistance;

            for (int si = 0; si < static_cast<int>(singularities.size()); ++si) {
                const Singularity& s = singularities[si];
                if (s.vertex < 0 || s.vertex >= static_cast<int>(mesh.vertices.size())) continue;
                const Vec3& p = mesh.vertices[s.vertex].position;

                int target = -1;
                float bestDistance = clusterDistanceSq;
                for (int ci = 0; ci < static_cast<int>(clusters.size()); ++ci) {
                    float d = (clusters[ci].center - p).lengthSquared();
                    if (d < bestDistance) {
                        bestDistance = d;
                        target = ci;
                    }
                }

                if (target < 0) {
                    SingularityCluster cluster;
                    cluster.center = p;
                    clusters.push_back(cluster);
                    target = static_cast<int>(clusters.size()) - 1;
                }

                SingularityCluster& cluster = clusters[target];
                cluster.rawIds.push_back(si);
                cluster.vertices.push_back(s.vertex);
                cluster.indexSum += s.index;
                cluster.faces.insert(cluster.faces.end(), s.faces.begin(), s.faces.end());

                Vec3 center;
                for (int vertex : cluster.vertices) center += mesh.vertices[vertex].position;
                cluster.center = center / static_cast<float>(cluster.vertices.size());
            }

            for (auto& cluster : clusters) {
                std::sort(cluster.vertices.begin(), cluster.vertices.end());
                cluster.vertices.erase(std::unique(cluster.vertices.begin(), cluster.vertices.end()), cluster.vertices.end());
                std::sort(cluster.faces.begin(), cluster.faces.end());
                cluster.faces.erase(std::unique(cluster.faces.begin(), cluster.faces.end()), cluster.faces.end());
            }
            return clusters;
        }

        inline int graphVertex(GraphData& graph, const Vec3& point, const Color& color, float mergeDistance) {
            float mergeDistanceSq = mergeDistance * mergeDistance;
            for (int i = 0; i < static_cast<int>(graph.vertices.size()); ++i) {
                if ((graph.vertices[i].position - point).lengthSquared() <= mergeDistanceSq) return i;
            }
            return graph.addVertex(point, color);
        }

        inline void graphEdge(GraphData& graph, int a, int b) {
            if (a < 0 || b < 0 || a == b) return;
            for (const auto& edge : graph.edges) {
                if ((edge.vertexA == a && edge.vertexB == b) || (edge.vertexA == b && edge.vertexB == a)) return;
            }
            graph.addEdge(a, b);
        }

        inline void addPolylineToGraph(GraphData& graph, const std::vector<Vec3>& line, const Color& color, float mergeDistance) {
            if (line.size() < 2) return;
            int previous = graphVertex(graph, line.front(), color, mergeDistance);
            for (size_t i = 1; i < line.size(); ++i) {
                int current = graphVertex(graph, line[i], color, mergeDistance);
                graphEdge(graph, previous, current);
                previous = current;
            }
        }

        inline void addSimplifiedBoundaryToGraph(GraphData& graph,
                                                 const MeshData& mesh,
                                                 const Color& color,
                                                 float mergeDistance,
                                                 float angleTolerance) {
            std::map<int, std::vector<int>> adjacency;
            for (const auto& edge : boundaryEdges(mesh)) {
                adjacency[edge.first].push_back(edge.second);
                adjacency[edge.second].push_back(edge.first);
            }

            std::set<std::pair<int, int>> visitedDirected;
            for (const auto& item : adjacency) {
                int start = item.first;
                for (int firstNext : item.second) {
                    if (visitedDirected.count({start, firstNext})) continue;

                    std::vector<int> loop;
                    loop.push_back(start);

                    int previous = start;
                    int current = firstNext;
                    visitedDirected.insert({start, firstNext});

                    int guard = 0;
                    int maxGuard = static_cast<int>(adjacency.size()) + 8;
                    while (current != start && guard++ < maxGuard) {
                        loop.push_back(current);

                        const auto& neighbors = adjacency[current];
                        if (neighbors.empty()) break;

                        int next = -1;
                        if (neighbors.size() == 1) {
                            next = neighbors.front();
                        } else {
                            next = (neighbors[0] == previous) ? neighbors[1] : neighbors[0];
                        }

                        if (next < 0 || visitedDirected.count({current, next})) break;
                        visitedDirected.insert({current, next});
                        previous = current;
                        current = next;
                    }

                    if (current == start) {
                        visitedDirected.insert({previous, start});
                    }
                    if (loop.size() < 3) continue;

                    float signedArea = 0.0f;
                    for (size_t i = 0; i < loop.size(); ++i) {
                        const Vec3& a = mesh.vertices[loop[i]].position;
                        const Vec3& b = mesh.vertices[loop[(i + 1) % loop.size()]].position;
                        signedArea += a.x * b.y - b.x * a.y;
                    }
                    if (std::abs(signedArea) <= 1e-8f) continue;

                    std::vector<Vec3> simplified;
                    for (int i = 0; i < static_cast<int>(loop.size()); ++i) {
                        int prevId = loop[(i - 1 + static_cast<int>(loop.size())) % static_cast<int>(loop.size())];
                        int id = loop[i];
                        int nextId = loop[(i + 1) % static_cast<int>(loop.size())];
                        if (prevId < 0 || id < 0 || nextId < 0 ||
                            prevId >= static_cast<int>(mesh.vertices.size()) ||
                            id >= static_cast<int>(mesh.vertices.size()) ||
                            nextId >= static_cast<int>(mesh.vertices.size())) continue;

                        Vec3 a = mesh.vertices[prevId].position - mesh.vertices[id].position;
                        Vec3 b = mesh.vertices[nextId].position - mesh.vertices[id].position;
                        a.z = 0.0f;
                        b.z = 0.0f;
                        if (a.lengthSquared() <= 1e-8f || b.lengthSquared() <= 1e-8f) continue;
                        a.normalize();
                        b.normalize();
                        float angle = std::acos(std::clamp(a.dot(b), -1.0f, 1.0f));
                        if (std::abs(kPi - angle) > angleTolerance || simplified.empty()) {
                            simplified.push_back(mesh.vertices[id].position);
                        }
                    }

                    if (simplified.size() < 3) {
                        simplified.clear();
                        for (int id : loop) {
                            if (id >= 0 && id < static_cast<int>(mesh.vertices.size())) {
                                simplified.push_back(mesh.vertices[id].position);
                            }
                        }
                    }
                    if (simplified.size() < 3) continue;

                    for (size_t i = 0; i < simplified.size(); ++i) {
                        addPolylineToGraph(graph,
                                           {simplified[i], simplified[(i + 1) % simplified.size()]},
                                           color,
                                           mergeDistance);
                    }
                }
            }
        }

        struct ClosestGraphPoint {
            Vec3 point;
            float distanceSq{std::numeric_limits<float>::max()};
            int edgeA{-1};
            int edgeB{-1};
            int vertex{-1};
            bool valid{false};
        };

        inline ClosestGraphPoint closestPointOnGraph(const GraphData& graph, const Vec3& query) {
            ClosestGraphPoint result;
            for (const auto& vertex : graph.vertices) {
                float d = (vertex.position - query).lengthSquared();
                if (d < result.distanceSq) {
                    result.point = vertex.position;
                    result.distanceSq = d;
                    result.vertex = static_cast<int>(&vertex - graph.vertices.data());
                    result.edgeA = -1;
                    result.edgeB = -1;
                    result.valid = true;
                }
            }

            for (const auto& edge : graph.edges) {
                if (edge.vertexA < 0 || edge.vertexB < 0 ||
                    edge.vertexA >= static_cast<int>(graph.vertices.size()) ||
                    edge.vertexB >= static_cast<int>(graph.vertices.size())) continue;

                const Vec3& a = graph.vertices[edge.vertexA].position;
                const Vec3& b = graph.vertices[edge.vertexB].position;
                Vec3 ab = b - a;
                ab.z = 0.0f;
                float lengthSq = ab.lengthSquared();
                if (lengthSq <= 1e-10f) continue;

                Vec3 aq = query - a;
                aq.z = 0.0f;
                float t = std::clamp(aq.dot(ab) / lengthSq, 0.0f, 1.0f);
                Vec3 p = a + ab * t;
                float d = (p - query).lengthSquared();
                if (d < result.distanceSq) {
                    result.point = p;
                    result.distanceSq = d;
                    result.vertex = -1;
                    result.edgeA = edge.vertexA;
                    result.edgeB = edge.vertexB;
                    result.valid = true;
                }
            }
            return result;
        }

        inline void splitGraphEdgeAtVertex(GraphData& graph, int edgeA, int edgeB, int splitVertex) {
            if (edgeA < 0 || edgeB < 0 || splitVertex < 0 || splitVertex == edgeA || splitVertex == edgeB) return;
            graph.edges.erase(std::remove_if(graph.edges.begin(), graph.edges.end(), [&](const GraphEdge& edge) {
                return (edge.vertexA == edgeA && edge.vertexB == edgeB) || (edge.vertexA == edgeB && edge.vertexB == edgeA);
            }), graph.edges.end());
            graphEdge(graph, edgeA, splitVertex);
            graphEdge(graph, splitVertex, edgeB);
        }

        inline std::vector<int> graphNeighbors(const GraphData& graph, int vertex) {
            std::vector<int> result;
            for (const auto& edge : graph.edges) {
                if (edge.vertexA == vertex) result.push_back(edge.vertexB);
                if (edge.vertexB == vertex) result.push_back(edge.vertexA);
            }
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        inline void compactGraph(GraphData& graph) {
            std::vector<int> remap(graph.vertices.size(), -1);
            GraphData compact;
            for (const auto& edge : graph.edges) {
                if (edge.vertexA >= 0 && edge.vertexA < static_cast<int>(remap.size()) && remap[edge.vertexA] < 0) {
                    remap[edge.vertexA] = compact.addVertex(graph.vertices[edge.vertexA].position, graph.vertices[edge.vertexA].color);
                }
                if (edge.vertexB >= 0 && edge.vertexB < static_cast<int>(remap.size()) && remap[edge.vertexB] < 0) {
                    remap[edge.vertexB] = compact.addVertex(graph.vertices[edge.vertexB].position, graph.vertices[edge.vertexB].color);
                }
            }
            for (const auto& edge : graph.edges) {
                if (edge.vertexA < 0 || edge.vertexB < 0 ||
                    edge.vertexA >= static_cast<int>(remap.size()) ||
                    edge.vertexB >= static_cast<int>(remap.size())) continue;
                graphEdge(compact, remap[edge.vertexA], remap[edge.vertexB]);
            }
            graph = compact;
        }

        inline int collapseGraphVerticesToPoint(GraphData& graph,
                                                const std::vector<int>& verticesToCollapse,
                                                const Vec3& point,
                                                const Color& color,
                                                float mergeDistance) {
            if (verticesToCollapse.empty()) return 0;

            std::set<int> collapseSet;
            for (int id : verticesToCollapse) {
                if (id >= 0 && id < static_cast<int>(graph.vertices.size())) collapseSet.insert(id);
            }
            if (collapseSet.empty()) return 0;

            int centerVertex = graphVertex(graph, point, color, mergeDistance);
            std::vector<GraphEdge> oldEdges = graph.edges;
            graph.edges.clear();

            for (const auto& edge : oldEdges) {
                int a = collapseSet.count(edge.vertexA) ? centerVertex : edge.vertexA;
                int b = collapseSet.count(edge.vertexB) ? centerVertex : edge.vertexB;
                graphEdge(graph, a, b);
            }

            compactGraph(graph);
            return static_cast<int>(collapseSet.size());
        }

        inline void simplifyGraphByAngles(GraphData& graph, float angleTolerance) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (int vi = 0; vi < static_cast<int>(graph.vertices.size()); ++vi) {
                    std::vector<int> neighbors = graphNeighbors(graph, vi);
                    if (neighbors.size() != 2) continue;

                    Vec3 a = graph.vertices[neighbors[0]].position - graph.vertices[vi].position;
                    Vec3 b = graph.vertices[neighbors[1]].position - graph.vertices[vi].position;
                    a.z = 0.0f;
                    b.z = 0.0f;
                    if (a.lengthSquared() <= 1e-8f || b.lengthSquared() <= 1e-8f) continue;
                    a.normalize();
                    b.normalize();
                    float angle = std::acos(std::clamp(a.dot(b), -1.0f, 1.0f));
                    if (std::abs(kPi - angle) > angleTolerance) continue;

                    graph.edges.erase(std::remove_if(graph.edges.begin(), graph.edges.end(), [&](const GraphEdge& edge) {
                        return edge.vertexA == vi || edge.vertexB == vi;
                    }), graph.edges.end());
                    graphEdge(graph, neighbors[0], neighbors[1]);
                    changed = true;
                    break;
                }
            }
            compactGraph(graph);
        }

        inline float polygonAreaXY(const std::vector<int>& cycle, const GraphData& graph) {
            float area = 0.0f;
            for (size_t i = 0; i < cycle.size(); ++i) {
                const Vec3& a = graph.vertices[cycle[i]].position;
                const Vec3& b = graph.vertices[cycle[(i + 1) % cycle.size()]].position;
                area += a.x * b.y - b.x * a.y;
            }
            return area * 0.5f;
        }

        inline std::vector<std::vector<int>> extractGraphFacesAroundZ(const GraphData& graph) {
            std::vector<std::vector<int>> sortedNeighbors(graph.vertices.size());
            for (int vi = 0; vi < static_cast<int>(graph.vertices.size()); ++vi) {
                sortedNeighbors[vi] = graphNeighbors(graph, vi);
                std::sort(sortedNeighbors[vi].begin(), sortedNeighbors[vi].end(), [&](int a, int b) {
                    Vec3 da = graph.vertices[a].position - graph.vertices[vi].position;
                    Vec3 db = graph.vertices[b].position - graph.vertices[vi].position;
                    return std::atan2(da.y, da.x) < std::atan2(db.y, db.x);
                });
            }

            std::set<std::pair<int, int>> visited;
            std::vector<std::vector<int>> faces;
            for (const auto& edge : graph.edges) {
                for (const auto& directed : {std::make_pair(edge.vertexA, edge.vertexB), std::make_pair(edge.vertexB, edge.vertexA)}) {
                    int startA = directed.first;
                    int startB = directed.second;
                    if (visited.count(directed)) continue;

                    std::vector<int> cycle;
                    int a = startA;
                    int b = startB;
                    bool closed = false;
                    for (int guard = 0; guard < static_cast<int>(graph.edges.size()) * 4 + 16; ++guard) {
                        visited.insert({a, b});
                        cycle.push_back(a);

                        const auto& neighbors = sortedNeighbors[b];
                        auto it = std::find(neighbors.begin(), neighbors.end(), a);
                        if (it == neighbors.end() || neighbors.empty()) break;
                        int index = static_cast<int>(std::distance(neighbors.begin(), it));
                        int next = neighbors[(index - 1 + static_cast<int>(neighbors.size())) % static_cast<int>(neighbors.size())];

                        a = b;
                        b = next;
                        if (a == startA && b == startB) {
                            closed = true;
                            break;
                        }
                    }

                    if (!closed || cycle.size() < 3) continue;
                    float area = polygonAreaXY(cycle, graph);
                    if (area <= 1e-6f) continue;

                    std::vector<int> canonical = cycle;
                    auto minIt = std::min_element(canonical.begin(), canonical.end());
                    std::rotate(canonical.begin(), minIt, canonical.end());
                    bool duplicate = false;
                    for (const auto& existing : faces) {
                        if (existing == canonical) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) faces.push_back(canonical);
                }
            }
            return faces;
        }

        inline void connectFacesToVertices(MeshData& mesh, const std::vector<int>& centerVertices) {
            if (centerVertices.empty() || mesh.faces.empty()) return;

            std::set<int> centers(centerVertices.begin(), centerVertices.end());
            std::vector<MeshFace> faces;
            faces.reserve(mesh.faces.size());

            for (const auto& face : mesh.faces) {
                int center = -1;
                int centerLocal = -1;
                for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                    int id = face.vertices[i];
                    if (centers.count(id)) {
                        center = id;
                        centerLocal = i;
                        break;
                    }
                }

                if (center < 0 || face.vertices.size() < 4) {
                    faces.push_back(face);
                    continue;
                }

                std::vector<int> ring;
                ring.reserve(face.vertices.size() - 1);
                for (int step = 1; step < static_cast<int>(face.vertices.size()); ++step) {
                    ring.push_back(face.vertices[(centerLocal + step) % static_cast<int>(face.vertices.size())]);
                }

                if (ring.size() < 2) {
                    faces.push_back(face);
                    continue;
                }

                for (int i = 0; i + 1 < static_cast<int>(ring.size()); ++i) {
                    std::vector<int> tri{center, ring[i], ring[i + 1]};
                    faces.emplace_back(tri, face.normal, face.color);
                }
            }

            mesh.faces.swap(faces);

            std::set<std::pair<int, int>> edgeSet;
            mesh.edges.clear();
            for (const auto& face : mesh.faces) {
                for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                    int a = face.vertices[i];
                    int b = face.vertices[(i + 1) % static_cast<int>(face.vertices.size())];
                    if (a < 0 || b < 0 || a == b) continue;
                    edgeSet.insert(sortedEdge(a, b));
                }
            }
            for (const auto& edge : edgeSet) mesh.edges.emplace_back(edge.first, edge.second, Color(0, 0, 0, 1));
            mesh.calculateNormals();
            mesh.triangulationDirty = true;
        }
    }

    inline std::shared_ptr<MeshData> StressAlignedRemesher::remesh(const MeshData& mesh, const TensorField& crossField) const {
        using namespace stress_aligned_remesher_detail;

        auto out = std::make_shared<MeshData>();
        if (mesh.vertices.empty() || mesh.faces.empty() || crossField.empty()) return out;

        float target = static_cast<float>(targetEdgeLength_);
        if (target <= 1e-6f) return out;

        std::vector<Singularity> singularities = findSingularities(mesh, crossField);
        std::vector<SingularityCluster> clusters = clusterSingularities(mesh, singularities, target);

        std::cout << "[StressAlignedRemesher] raw singularities: " << singularities.size()
                  << " | clusters: " << clusters.size()
                  << " | clusterDistance: " << target
                  << std::endl;

        GraphData graph;
        const Color graphColor(0.0f, 0.0f, 0.0f, 1.0f);
        const float mergeDistance = target * 0.025f;
        const int boundaryVertexCountBefore = static_cast<int>(graph.vertices.size());
        addSimplifiedBoundaryToGraph(graph, mesh, graphColor, mergeDistance, angleSimplificationTolerance_);
        const int boundaryVertexCount = static_cast<int>(graph.vertices.size());

        struct DualVertexRef {
            Vec3 position;
            int graphVertex{-1};
            int cluster{-1};
        };
        std::vector<DualVertexRef> dualVertices;
        std::vector<std::vector<int>> dualVerticesByCluster(clusters.size());

        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            const SingularityCluster& cluster = clusters[ci];

            std::vector<Vec3> rawPoints;
            for (int vertex : cluster.vertices) {
                if (vertex >= 0 && vertex < static_cast<int>(mesh.vertices.size())) {
                    rawPoints.push_back(mesh.vertices[vertex].position);
                }
            }
            std::vector<Vec3> singularityHull = convexHullXY(rawPoints);
            std::vector<Vec3> dualPolygon = edgeMidpointDualPolygon(singularityHull);

            std::cout << "  cluster[" << ci << "] raw=" << cluster.rawIds.size()
                      << " vertices=" << cluster.vertices.size()
                      << " faces=" << cluster.faces.size()
                      << " singularityHull=" << singularityHull.size()
                      << " dualPolygon=" << dualPolygon.size()
                      << " indexSum=" << cluster.indexSum
                      << " center=(" << cluster.center.x << ", " << cluster.center.y << ", " << cluster.center.z << ")"
                      << std::endl;

            if (dualPolygon.size() >= 3) {
                for (size_t i = 0; i < dualPolygon.size(); ++i) {
                    addPolylineToGraph(graph, {dualPolygon[i], dualPolygon[(i + 1) % dualPolygon.size()]}, graphColor, mergeDistance);
                }
                for (const Vec3& p : dualPolygon) {
                    int id = graphVertex(graph, p, graphColor, mergeDistance);
                    dualVertices.push_back({p, id, static_cast<int>(ci)});
                    dualVerticesByCluster[ci].push_back(id);
                }
            }
        }

        int boundaryConnections = 0;
        int clusterConnections = 0;
        for (const DualVertexRef& source : dualVertices) {
            if (source.graphVertex < 0) continue;

            GraphData boundaryGraph;
            for (int i = 0; i < boundaryVertexCount; ++i) {
                boundaryGraph.addVertex(graph.vertices[i].position, graph.vertices[i].color);
            }
            for (const auto& edge : graph.edges) {
                if (edge.vertexA >= 0 && edge.vertexA < boundaryVertexCount &&
                    edge.vertexB >= 0 && edge.vertexB < boundaryVertexCount) {
                    graphEdge(boundaryGraph, edge.vertexA, edge.vertexB);
                }
            }

            ClosestGraphPoint boundaryTarget = closestPointOnGraph(boundaryGraph, source.position);

            int otherDualVertex = -1;
            float otherDistanceSq = std::numeric_limits<float>::max();
            for (const DualVertexRef& other : dualVertices) {
                if (other.cluster == source.cluster || other.graphVertex == source.graphVertex) continue;
                float d = (other.position - source.position).lengthSquared();
                if (d < otherDistanceSq) {
                    otherDistanceSq = d;
                    otherDualVertex = other.graphVertex;
                }
            }

            if (otherDualVertex >= 0 && (!boundaryTarget.valid || otherDistanceSq < boundaryTarget.distanceSq)) {
                graphEdge(graph, source.graphVertex, otherDualVertex);
                ++clusterConnections;
            } else if (boundaryTarget.valid) {
                int targetVertex = graphVertex(graph, boundaryTarget.point, graphColor, mergeDistance);
                splitGraphEdgeAtVertex(graph, boundaryTarget.edgeA, boundaryTarget.edgeB, targetVertex);
                graphEdge(graph, source.graphVertex, targetVertex);
                ++boundaryConnections;
            }
        }

        int collapsedDualVertices = 0;
        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            collapsedDualVertices += collapseGraphVerticesToPoint(graph,
                                                                  dualVerticesByCluster[ci],
                                                                  clusters[ci].center,
                                                                  graphColor,
                                                                  mergeDistance);
        }
        compactGraph(graph);
        simplifyGraphByAngles(graph, angleSimplificationTolerance_);
        std::vector<std::vector<int>> graphFaces = extractGraphFacesAroundZ(graph);

        std::cout << "[StressAlignedRemesher] boundary vertices: " << (boundaryVertexCount - boundaryVertexCountBefore)
                  << " | dual vertices: " << dualVertices.size()
                  << " | boundary connections: " << boundaryConnections
                  << " | cluster connections: " << clusterConnections
                  << " | collapsed dual vertices: " << collapsedDualVertices
                  << " | graph vertices: " << graph.vertices.size()
                  << " | graph edges: " << graph.edges.size()
                  << " | graph faces: " << graphFaces.size()
                  << std::endl;

        out->vertices.clear();
        out->edges.clear();
        out->faces.clear();
        out->vertices.reserve(graph.vertices.size());
        for (const auto& vertex : graph.vertices) {
            out->vertices.emplace_back(vertex.position, Vec3(0, 0, 1), Color(0.8f, 0.8f, 0.82f, 1.0f));
        }
        for (const auto& face : graphFaces) {
            out->faces.emplace_back(face, Vec3(0, 0, 1), Color(0.72f, 0.76f, 0.78f, 1.0f));
        }

        std::set<std::pair<int, int>> edges;
        for (const auto& edge : graph.edges) edges.insert(sortedEdge(edge.vertexA, edge.vertexB));
        for (const auto& edge : edges) out->edges.emplace_back(edge.first, edge.second, Color(0, 0, 0, 1));

        std::vector<int> chamferVertices;
        const float columnSearchDistanceSq = target * target;
        for (int sourceVertex : snapVertices_) {
            if (sourceVertex < 0 || sourceVertex >= static_cast<int>(mesh.vertices.size())) continue;

            int closest = -1;
            float closestDistanceSq = columnSearchDistanceSq;
            const Vec3& sourcePosition = mesh.vertices[sourceVertex].position;
            for (int i = 0; i < static_cast<int>(out->vertices.size()); ++i) {
                float distanceSq = (out->vertices[i].position - sourcePosition).lengthSquared();
                if (distanceSq < closestDistanceSq) {
                    closestDistanceSq = distanceSq;
                    closest = i;
                }
            }
            if (closest >= 0 && std::find(chamferVertices.begin(), chamferVertices.end(), closest) == chamferVertices.end()) {
                chamferVertices.push_back(closest);
            }
        }

        if (!chamferVertices.empty()) {
            connectFacesToVertices(*out, chamferVertices);
            out->chamferVertices(chamferVertices, chamferPercentage_, true);
            out->catmullClarkSmooth(smoothLevels_);
        }

        out->calculateNormals();
        out->triangulationDirty = true;
        return out;
    }
}

#endif
