#include "MiqFieldRemesher.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <sstream>

#ifndef ALICE2_WITH_MIQ
#define ALICE2_WITH_MIQ 0
#endif

#if ALICE2_WITH_MIQ
#include <Eigen/Core>
#include <igl/copyleft/comiso/miq.h>
#endif

namespace alice2 {
namespace {
constexpr float kEpsilon = 1e-6f;
constexpr float kTangentTolerance = 1e-3f;

struct EdgeUse { int face; int start; int end; };
using EdgeMap = std::map<std::pair<int, int>, std::vector<EdgeUse>>;

std::pair<int, int> sortedEdge(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }
bool validVertex(const MeshData& mesh, int id) { return id >= 0 && id < static_cast<int>(mesh.vertices.size()); }
float uvComponent(const Vec2& uv, int axis) { return axis == 0 ? uv.x : uv.y; }

EdgeMap collectEdges(const MeshData& mesh) {
    EdgeMap edges;
    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        const auto& face = mesh.faces[fi];
        for (int i = 0; i < 3; ++i) {
            const int a = face.vertices[i];
            const int b = face.vertices[(i + 1) % 3];
            edges[sortedEdge(a, b)].push_back({fi, a, b});
        }
    }
    return edges;
}

bool validateTopology(const MeshData& mesh, std::string& message) {
    if (mesh.vertices.empty() || mesh.faces.empty()) { message = "input mesh is empty"; return false; }
    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        const auto& face = mesh.faces[fi];
        if (face.vertices.size() != 3) { message = "MIQ requires triangle-only input (face " + std::to_string(fi) + ")"; return false; }
        for (int id : face.vertices) {
            if (!validVertex(mesh, id)) { message = "face " + std::to_string(fi) + " references an invalid vertex"; return false; }
        }
        if (mesh.calculateFaceNormal(face).lengthSquared() <= kEpsilon * kEpsilon) {
            message = "face " + std::to_string(fi) + " is degenerate"; return false;
        }
    }

    const EdgeMap edges = collectEdges(mesh);
    std::vector<std::vector<int>> faceNeighbors(mesh.faces.size());
    std::map<int, std::vector<int>> boundary;
    for (const auto& [edge, uses] : edges) {
        if (uses.size() > 2) { message = "input mesh is non-manifold"; return false; }
        if (uses.size() == 2) {
            if (uses[0].start == uses[1].start && uses[0].end == uses[1].end) {
                message = "input mesh has inconsistent face winding"; return false;
            }
            faceNeighbors[uses[0].face].push_back(uses[1].face);
            faceNeighbors[uses[1].face].push_back(uses[0].face);
        } else if (uses.size() == 1) {
            boundary[edge.first].push_back(edge.second);
            boundary[edge.second].push_back(edge.first);
        }
    }
    if (boundary.empty()) { message = "MIQ requires an open surface patch; the input mesh is closed"; return false; }
    for (const auto& [vertex, neighbors] : boundary) {
        if (neighbors.size() != 2) { message = "input mesh boundary is not a manifold loop"; return false; }
    }
    // Multiple manifold boundary loops are valid (for example a slab with a
    // hole). libigl constructs the cuts needed for the MIQ parameterization.

    std::vector<char> visitedFaces(mesh.faces.size(), 0);
    std::queue<int> faceQueue;
    faceQueue.push(0); visitedFaces[0] = 1;
    int count = 0;
    while (!faceQueue.empty()) {
        const int current = faceQueue.front(); faceQueue.pop(); ++count;
        for (int next : faceNeighbors[current]) if (!visitedFaces[next]) { visitedFaces[next] = 1; faceQueue.push(next); }
    }
    if (count != static_cast<int>(mesh.faces.size())) { message = "input mesh must be one connected surface patch"; return false; }
    return true;
}

bool validateField(const MeshData& mesh, const TensorField& field, std::string& message) {
    if (field.size() != mesh.faces.size()) { message = "field must contain exactly one tensor per input face"; return false; }
    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        const auto& tensor = field[fi];
        const float majorLength = tensor.majorDirection.length();
        const float minorLength = tensor.minorDirection.length();
        if (majorLength <= kEpsilon || minorLength <= kEpsilon) { message = "field contains a zero direction on face " + std::to_string(fi); return false; }
        const Vec3 normal = mesh.calculateFaceNormal(mesh.faces[fi]).normalized();
        const Vec3 major = tensor.majorDirection / majorLength;
        const Vec3 minor = tensor.minorDirection / minorLength;
        if (std::abs(major.dot(normal)) > kTangentTolerance || std::abs(minor.dot(normal)) > kTangentTolerance) {
            message = "field directions must be tangent to face " + std::to_string(fi); return false;
        }
        if (std::abs(major.dot(minor)) > kTangentTolerance) {
            message = "field directions must be orthogonal on face " + std::to_string(fi); return false;
        }
    }
    return true;
}

void appendIsoSegments(const Vec3 p[3], const Vec2 uv[3], int axis, std::vector<std::vector<Vec3>>& lines) {
    float minimum = uvComponent(uv[0], axis), maximum = minimum;
    for (int i = 1; i < 3; ++i) {
        minimum = std::min(minimum, uvComponent(uv[i], axis));
        maximum = std::max(maximum, uvComponent(uv[i], axis));
    }
    for (int level = static_cast<int>(std::ceil(minimum - 1e-5f)); level <= static_cast<int>(std::floor(maximum + 1e-5f)); ++level) {
        std::vector<Vec3> hits;
        for (int edge = 0; edge < 3; ++edge) {
            const int a = edge, b = (edge + 1) % 3;
            const float av = uvComponent(uv[a], axis) - static_cast<float>(level);
            const float bv = uvComponent(uv[b], axis) - static_cast<float>(level);
            if (std::abs(av) <= 1e-5f && std::abs(bv) <= 1e-5f) continue;
            if ((av < -1e-5f && bv < -1e-5f) || (av > 1e-5f && bv > 1e-5f)) continue;
            const float denominator = av - bv;
            if (std::abs(denominator) <= kEpsilon) continue;
            const Vec3 point = p[a] + (p[b] - p[a]) * std::clamp(av / denominator, 0.0f, 1.0f);
            bool duplicate = false;
            for (const Vec3& hit : hits) if ((point - hit).lengthSquared() < 1e-10f) { duplicate = true; break; }
            if (!duplicate) hits.push_back(point);
        }
        if (hits.size() == 2) lines.push_back({hits[0], hits[1]});
    }
}

void buildGridLines(const MeshData& mesh, MiqRemeshResult& result) {
    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        if (fi >= static_cast<int>(result.uvFaces.size())) break;
        Vec3 p[3]; Vec2 uv[3]; bool valid = true;
        for (int corner = 0; corner < 3; ++corner) {
            const int vertexId = mesh.faces[fi].vertices[corner];
            const int uvId = result.uvFaces[fi][corner];
            if (!validVertex(mesh, vertexId) || uvId < 0 || uvId >= static_cast<int>(result.uv.size())) { valid = false; break; }
            p[corner] = mesh.vertices[vertexId].position;
            uv[corner] = result.uv[uvId];
        }
        if (!valid) continue;
        appendIsoSegments(p, uv, 0, result.gridLines.u);
        appendIsoSegments(p, uv, 1, result.gridLines.v);
    }
}

struct SurfacePoint {
    Vec3 position;
    Vec3 normal;
};

struct UvSurfacePoint {
    Vec2 uv;
    Vec3 position;
};

struct UvBoundarySegment {
    UvSurfacePoint a;
    UvSurfacePoint b;
};

bool barycentricUv(const Vec2& point, const Vec2& a, const Vec2& b, const Vec2& c, float& u, float& v, float& w) {
    const float denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(denominator) <= kEpsilon) return false;
    u = ((b.y - c.y) * (point.x - c.x) + (c.x - b.x) * (point.y - c.y)) / denominator;
    v = ((c.y - a.y) * (point.x - c.x) + (a.x - c.x) * (point.y - c.y)) / denominator;
    w = 1.0f - u - v;
    return u >= -1e-4f && v >= -1e-4f && w >= -1e-4f;
}

bool locateUvPoint(const MeshData& mesh, const MiqRemeshResult& result, const Vec2& point, SurfacePoint& sample) {
    const int faceCount = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(result.uvFaces.size()));
    for (int fi = 0; fi < faceCount; ++fi) {
        const MeshFace& sourceFace = mesh.faces[fi];
        const std::array<int, 3>& uvFace = result.uvFaces[fi];
        if (sourceFace.vertices.size() != 3) continue;
        const Vec2& a = result.uv[uvFace[0]];
        const Vec2& b = result.uv[uvFace[1]];
        const Vec2& c = result.uv[uvFace[2]];
        float u = 0.0f, v = 0.0f, w = 0.0f;
        if (!barycentricUv(point, a, b, c, u, v, w)) continue;
        const Vec3& pa = mesh.vertices[sourceFace.vertices[0]].position;
        const Vec3& pb = mesh.vertices[sourceFace.vertices[1]].position;
        const Vec3& pc = mesh.vertices[sourceFace.vertices[2]].position;
        sample.position = pa * u + pb * v + pc * w;
        sample.normal = mesh.calculateFaceNormal(sourceFace).normalized();
        return true;
    }
    return false;
}

std::vector<UvBoundarySegment> collectUvBoundarySegments(const MeshData& mesh, const MiqRemeshResult& result) {
    struct Use { int face; int edge; };
    std::map<std::pair<int, int>, std::vector<Use>> uses;
    const int faceCount = std::min(static_cast<int>(mesh.faces.size()), static_cast<int>(result.uvFaces.size()));
    for (int fi = 0; fi < faceCount; ++fi) {
        if (mesh.faces[fi].vertices.size() != 3) continue;
        const auto& uvFace = result.uvFaces[fi];
        for (int edge = 0; edge < 3; ++edge)
            uses[sortedEdge(uvFace[edge], uvFace[(edge + 1) % 3])].push_back({fi, edge});
    }

    std::vector<UvBoundarySegment> boundary;
    for (const auto& [edge, edgeUses] : uses) {
        if (edgeUses.size() != 1) continue;
        const Use use = edgeUses.front();
        const MeshFace& face = mesh.faces[use.face];
        const auto& uvFace = result.uvFaces[use.face];
        const int next = (use.edge + 1) % 3;
        boundary.push_back({{result.uv[uvFace[use.edge]], mesh.vertices[face.vertices[use.edge]].position},
                            {result.uv[uvFace[next]], mesh.vertices[face.vertices[next]].position}});
    }
    return boundary;
}

bool clipBoundarySegmentToCell(const UvBoundarySegment& segment, int u, int v,
                               UvSurfacePoint& start, UvSurfacePoint& end) {
    const float minU = static_cast<float>(u), maxU = minU + 1.0f;
    const float minV = static_cast<float>(v), maxV = minV + 1.0f;
    const Vec2 delta{segment.b.uv.x - segment.a.uv.x, segment.b.uv.y - segment.a.uv.y};
    std::vector<float> hits;
    const auto inside = [=](const Vec2& point) {
        return point.x >= minU - kEpsilon && point.x <= maxU + kEpsilon &&
               point.y >= minV - kEpsilon && point.y <= maxV + kEpsilon;
    };
    const auto addHit = [&](float t) {
        if (t < -kEpsilon || t > 1.0f + kEpsilon) return;
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const Vec2 point{segment.a.uv.x + delta.x * clamped, segment.a.uv.y + delta.y * clamped};
        if (!inside(point)) return;
        for (float existing : hits) if (std::abs(existing - clamped) <= 1e-5f) return;
        hits.push_back(clamped);
    };
    if (inside(segment.a.uv)) addHit(0.0f);
    if (inside(segment.b.uv)) addHit(1.0f);
    if (std::abs(delta.x) > kEpsilon) { addHit((minU - segment.a.uv.x) / delta.x); addHit((maxU - segment.a.uv.x) / delta.x); }
    if (std::abs(delta.y) > kEpsilon) { addHit((minV - segment.a.uv.y) / delta.y); addHit((maxV - segment.a.uv.y) / delta.y); }
    if (hits.size() < 2) return false;
    std::sort(hits.begin(), hits.end());
    const auto sample = [&](float t) {
        return UvSurfacePoint{{segment.a.uv.x + delta.x * t, segment.a.uv.y + delta.y * t},
                              segment.a.position + (segment.b.position - segment.a.position) * t};
    };
    start = sample(hits.front());
    end = sample(hits.back());
    return (start.position - end.position).lengthSquared() > kEpsilon * kEpsilon;
}

bool buildQuadMesh(const MeshData& mesh, MiqRemeshResult& result, std::string& diagnostic) {
    if (result.uv.empty() || result.uvFaces.empty()) return false;
    float minU = result.uv.front().x, maxU = minU, minV = result.uv.front().y, maxV = minV;
    for (const Vec2& uv : result.uv) {
        minU = std::min(minU, uv.x); maxU = std::max(maxU, uv.x);
        minV = std::min(minV, uv.y); maxV = std::max(maxV, uv.y);
    }
    const int firstU = static_cast<int>(std::floor(minU));
    const int lastU = static_cast<int>(std::ceil(maxU));
    const int firstV = static_cast<int>(std::floor(minV));
    const int lastV = static_cast<int>(std::ceil(maxV));
    const long long candidateCount = static_cast<long long>(lastU - firstU) * static_cast<long long>(lastV - firstV);
    constexpr long long kMaxCandidateCells = 250000;
    if (candidateCount <= 0 || candidateCount > kMaxCandidateCells) {
        diagnostic = "MIQ UV grid has an unsupported candidate-cell count";
        return false;
    }

    auto quadMesh = std::make_shared<MeshData>();
    std::map<std::pair<int, int>, int> gridVertexIds;
    std::set<std::pair<int, int>> completeCells;
    auto appendVertex = [&](int u, int v, const SurfacePoint& sample) {
        const std::pair<int, int> key{u, v};
        const auto existing = gridVertexIds.find(key);
        if (existing != gridVertexIds.end()) return existing->second;
        const int id = static_cast<int>(quadMesh->vertices.size());
        quadMesh->vertices.emplace_back(sample.position, sample.normal, Color(0.16f, 0.72f, 0.26f, 1.0f));
        gridVertexIds.emplace(key, id);
        return id;
    };

    for (int v = firstV; v < lastV; ++v) {
        for (int u = firstU; u < lastU; ++u) {
            const Vec2 uv[4]{{static_cast<float>(u), static_cast<float>(v)},
                              {static_cast<float>(u + 1), static_cast<float>(v)},
                              {static_cast<float>(u + 1), static_cast<float>(v + 1)},
                              {static_cast<float>(u), static_cast<float>(v + 1)}};
            SurfacePoint corners[4];
            SurfacePoint center;
            bool complete = locateUvPoint(mesh, result, Vec2{static_cast<float>(u) + 0.5f, static_cast<float>(v) + 0.5f}, center);
            for (int corner = 0; corner < 4 && complete; ++corner) complete = locateUvPoint(mesh, result, uv[corner], corners[corner]);
            if (!complete) {
                continue;
            }

            int ids[4]{appendVertex(u, v, corners[0]), appendVertex(u + 1, v, corners[1]),
                       appendVertex(u + 1, v + 1, corners[2]), appendVertex(u, v + 1, corners[3])};
            const Vec3 cellNormal = (corners[1].position - corners[0].position).cross(corners[2].position - corners[0].position);
            if (cellNormal.dot(center.normal) < 0.0f) std::swap(ids[1], ids[3]);
            quadMesh->faces.emplace_back(std::vector<int>{ids[0], ids[1], ids[2], ids[3]}, center.normal, Color(0.16f, 0.72f, 0.26f, 1.0f));
            completeCells.insert({u, v});
        }
    }
    result.quadCount = static_cast<int>(quadMesh->faces.size());

    std::set<std::pair<int, int>> boundaryCells;
    std::map<std::pair<long long, long long>, int> boundaryVertexIds;
    constexpr double kUvKeyScale = 1000000.0;
    const auto appendBoundaryVertex = [&](const UvSurfacePoint& point, const Vec3& normal) {
        const std::pair<long long, long long> key{static_cast<long long>(std::llround(point.uv.x * kUvKeyScale)),
                                                  static_cast<long long>(std::llround(point.uv.y * kUvKeyScale))};
        const int integerU = static_cast<int>(std::lround(point.uv.x));
        const int integerV = static_cast<int>(std::lround(point.uv.y));
        if (std::abs(point.uv.x - static_cast<float>(integerU)) <= 1e-5f &&
            std::abs(point.uv.y - static_cast<float>(integerV)) <= 1e-5f) {
            const auto gridVertex = gridVertexIds.find({integerU, integerV});
            if (gridVertex != gridVertexIds.end()) return gridVertex->second;
        }
        const auto existing = boundaryVertexIds.find(key);
        if (existing != boundaryVertexIds.end()) return existing->second;
        const int id = static_cast<int>(quadMesh->vertices.size());
        quadMesh->vertices.emplace_back(point.position, normal, Color(0.90f, 0.48f, 0.05f, 1.0f));
        boundaryVertexIds.emplace(key, id);
        return id;
    };

    const std::vector<UvBoundarySegment> chartBoundary = collectUvBoundarySegments(mesh, result);
    std::map<std::pair<int, int>, std::vector<int>> boundarySegmentsByCell;
    for (int index = 0; index < static_cast<int>(chartBoundary.size()); ++index) {
        const UvBoundarySegment& segment = chartBoundary[index];
        const int segmentFirstU = static_cast<int>(std::floor(std::min(segment.a.uv.x, segment.b.uv.x) - kEpsilon));
        const int segmentLastU = static_cast<int>(std::ceil(std::max(segment.a.uv.x, segment.b.uv.x) + kEpsilon));
        const int segmentFirstV = static_cast<int>(std::floor(std::min(segment.a.uv.y, segment.b.uv.y) - kEpsilon));
        const int segmentLastV = static_cast<int>(std::ceil(std::max(segment.a.uv.y, segment.b.uv.y) + kEpsilon));
        for (int v = segmentFirstV; v < segmentLastV; ++v)
            for (int u = segmentFirstU; u < segmentLastU; ++u)
                boundarySegmentsByCell[{u, v}].push_back(index);
    }

    for (const auto& [cell, segmentIndices] : boundarySegmentsByCell) {
        const int u = cell.first, v = cell.second;
        if (completeCells.contains(cell)) continue;
        std::map<std::pair<long long, long long>, UvSurfacePoint> polygonPoints;
        const auto addPoint = [&](const UvSurfacePoint& point) {
            const std::pair<long long, long long> key{static_cast<long long>(std::llround(point.uv.x * kUvKeyScale)),
                                                      static_cast<long long>(std::llround(point.uv.y * kUvKeyScale))};
            polygonPoints.try_emplace(key, point);
        };
        // Grid corners inside the chart connect the boundary-isoline hits to one cell polygon.
        for (const Vec2 corner : {Vec2{static_cast<float>(u), static_cast<float>(v)},
                                  Vec2{static_cast<float>(u + 1), static_cast<float>(v)},
                                  Vec2{static_cast<float>(u + 1), static_cast<float>(v + 1)},
                                  Vec2{static_cast<float>(u), static_cast<float>(v + 1)}}) {
            SurfacePoint sample;
            if (locateUvPoint(mesh, result, corner, sample)) addPoint({corner, sample.position});
        }
        for (int index : segmentIndices) {
            UvSurfacePoint start, end;
            if (!clipBoundarySegmentToCell(chartBoundary[index], u, v, start, end)) continue;
            // These include original chart vertices as well as the U/V integer-isoline intersections.
            addPoint(start);
            addPoint(end);
        }
        if (polygonPoints.size() < 3) continue;

        std::vector<UvSurfacePoint> polygon;
        polygon.reserve(polygonPoints.size());
        Vec2 centroid{0.0f, 0.0f};
        for (const auto& [key, point] : polygonPoints) { polygon.push_back(point); centroid.x += point.uv.x; centroid.y += point.uv.y; }
        centroid.x /= static_cast<float>(polygon.size());
        centroid.y /= static_cast<float>(polygon.size());
        std::sort(polygon.begin(), polygon.end(), [&centroid](const UvSurfacePoint& a, const UvSurfacePoint& b) {
            return std::atan2(a.uv.y - centroid.y, a.uv.x - centroid.x) < std::atan2(b.uv.y - centroid.y, b.uv.x - centroid.x);
        });

        SurfacePoint normalSample{};
        if (!locateUvPoint(mesh, result, centroid, normalSample)) {
            for (const UvSurfacePoint& point : polygon) if (locateUvPoint(mesh, result, point.uv, normalSample)) break;
        }
        if (normalSample.normal.lengthSquared() <= kEpsilon * kEpsilon) continue;
        std::vector<int> ids;
        ids.reserve(polygon.size());
        for (const UvSurfacePoint& point : polygon) ids.push_back(appendBoundaryVertex(point, normalSample.normal));
        if (ids.size() < 3) continue;
        // Do not use only the first triangle here: consecutive boundary hits may be collinear.
        Vec3 polygonNormal{0.0f, 0.0f, 0.0f};
        for (int i = 0; i < static_cast<int>(polygon.size()); ++i) {
            const Vec3& a = polygon[i].position;
            const Vec3& b = polygon[(i + 1) % static_cast<int>(polygon.size())].position;
            polygonNormal.x += (a.y - b.y) * (a.z + b.z);
            polygonNormal.y += (a.z - b.z) * (a.x + b.x);
            polygonNormal.z += (a.x - b.x) * (a.y + b.y);
        }
        if (polygonNormal.lengthSquared() <= kEpsilon * kEpsilon) continue;
        if (polygonNormal.dot(normalSample.normal) < 0.0f) std::reverse(ids.begin(), ids.end());
        quadMesh->faces.emplace_back(ids, normalSample.normal, Color(0.90f, 0.48f, 0.05f, 1.0f));
        ++result.boundaryFaceCount;
        boundaryCells.insert(cell);
    }
    result.boundaryCellCount = static_cast<int>(boundaryCells.size());

    std::set<std::pair<int, int>> edgeIds;
    for (const MeshFace& face : quadMesh->faces) {
        for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
            const int a = face.vertices[i];
            const int b = face.vertices[(i + 1) % static_cast<int>(face.vertices.size())];
            edgeIds.insert(sortedEdge(a, b));
        }
    }
    quadMesh->edges.reserve(edgeIds.size());
    for (const auto& [a, b] : edgeIds) quadMesh->edges.emplace_back(a, b, Color(0.08f, 0.38f, 0.12f, 1.0f));
    quadMesh->calculateNormals();
    quadMesh->triangulationDirty = true;
    result.quadMesh = std::move(quadMesh);
    return true;
}
} // namespace

MiqRemeshResult MiqFieldRemesher::parameterize(const MeshData& mesh, const TensorField& field, const MiqRemeshOptions& options) const {
    MiqRemeshResult result;
    if (options.targetSpacing <= kEpsilon) { result.diagnostic = "target spacing must be positive"; return result; }
    if (!validateTopology(mesh, result.diagnostic) || !validateField(mesh, field, result.diagnostic)) return result;
#if !ALICE2_WITH_MIQ
    result.diagnostic = "MIQ support is disabled; configure with -DALICE2_ENABLE_MIQ_REMESH=ON";
    return result;
#else
    Eigen::MatrixXd V(mesh.vertices.size(), 3), PD1(mesh.faces.size(), 3), PD2(mesh.faces.size(), 3);
    Eigen::MatrixXi F(mesh.faces.size(), 3);
    Vec3 minBounds = mesh.vertices.front().position, maxBounds = minBounds;
    for (int vi = 0; vi < static_cast<int>(mesh.vertices.size()); ++vi) {
        const Vec3& p = mesh.vertices[vi].position;
        V.row(vi) << p.x, p.y, p.z;
        minBounds.x = std::min(minBounds.x, p.x); minBounds.y = std::min(minBounds.y, p.y); minBounds.z = std::min(minBounds.z, p.z);
        maxBounds.x = std::max(maxBounds.x, p.x); maxBounds.y = std::max(maxBounds.y, p.y); maxBounds.z = std::max(maxBounds.z, p.z);
    }
    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        for (int corner = 0; corner < 3; ++corner) F(fi, corner) = mesh.faces[fi].vertices[corner];
        const Vec3 major = field[fi].majorDirection.normalized(), minor = field[fi].minorDirection.normalized();
        PD1.row(fi) << major.x, major.y, major.z;
        PD2.row(fi) << minor.x, minor.y, minor.z;
    }
    Eigen::MatrixXd UV;
    Eigen::MatrixXi FUV;
    const float diagonal = std::max((maxBounds - minBounds).length(), kEpsilon);
    try {
        igl::copyleft::comiso::miq(V, F, PD1, PD2, UV, FUV,
                                   static_cast<double>(diagonal / options.targetSpacing), 5.0, true,
                                   options.stiffnessIterations, options.localIterations, true, true);
    } catch (const std::exception& error) {
        result.diagnostic = std::string("MIQ solve failed: ") + error.what(); return result;
    }
    if (UV.rows() == 0 || FUV.rows() != F.rows() || FUV.cols() != 3) { result.diagnostic = "MIQ returned a malformed UV parameterization"; return result; }
    result.uv.reserve(UV.rows());
    for (int i = 0; i < UV.rows(); ++i) result.uv.emplace_back(static_cast<float>(UV(i, 0)), static_cast<float>(UV(i, 1)));
    std::set<int> usedUv;
    for (int fi = 0; fi < FUV.rows(); ++fi) {
        const std::array<int, 3> face{FUV(fi, 0), FUV(fi, 1), FUV(fi, 2)};
        for (int id : face) if (id < 0 || id >= UV.rows()) { result.diagnostic = "MIQ returned an invalid UV face index"; return result; }
        usedUv.insert(face[0]); usedUv.insert(face[1]); usedUv.insert(face[2]); result.uvFaces.push_back(face);
    }
    result.seamVertexCount = std::max(0, static_cast<int>(usedUv.size()) - static_cast<int>(mesh.vertices.size()));
    buildGridLines(mesh, result);
    std::string quadDiagnostic;
    if (!buildQuadMesh(mesh, result, quadDiagnostic)) {
        result.diagnostic = quadDiagnostic;
        return result;
    }
    result.success = true;
    std::ostringstream summary;
    summary << "MIQ UV " << result.uv.size() << " | seams " << result.seamVertexCount
            << " | quads " << result.quadCount << " | boundary faces " << result.boundaryFaceCount;
    result.diagnostic = summary.str();
    return result;
#endif
}
} // namespace alice2
