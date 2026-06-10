#include "StressAlignedRemesher.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace alice2 {

namespace {
    constexpr float kEpsilon = 1e-6f;

    std::pair<int, int> sortedEdge(int a, int b) {
        return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    Vec3 faceCenter(const MeshData& mesh, const MeshFace& face) {
        Vec3 center;
        if (face.vertices.empty()) return center;
        for (int id : face.vertices) {
            if (id >= 0 && id < static_cast<int>(mesh.vertices.size())) {
                center += mesh.vertices[id].position;
            }
        }
        return center / static_cast<float>(face.vertices.size());
    }

    bool barycentricXY(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c, float& u, float& v, float& w) {
        const float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(den) <= kEpsilon) return false;

        u = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / den;
        v = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / den;
        w = 1.0f - u - v;
        return u >= -1e-4f && v >= -1e-4f && w >= -1e-4f;
    }

    int findFaceAtXY(const MeshData& mesh, const Vec3& p, Vec3& projected) {
        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() != 3) continue;

            const int i0 = face.vertices[0];
            const int i1 = face.vertices[1];
            const int i2 = face.vertices[2];
            if (i0 < 0 || i1 < 0 || i2 < 0 ||
                i0 >= static_cast<int>(mesh.vertices.size()) ||
                i1 >= static_cast<int>(mesh.vertices.size()) ||
                i2 >= static_cast<int>(mesh.vertices.size())) {
                continue;
            }

            const Vec3& a = mesh.vertices[i0].position;
            const Vec3& b = mesh.vertices[i1].position;
            const Vec3& c = mesh.vertices[i2].position;
            float u = 0.0f, v = 0.0f, w = 0.0f;
            if (!barycentricXY(p, a, b, c, u, v, w)) continue;

            projected = a * u + b * v + c * w;
            return fi;
        }
        return -1;
    }

    bool projectPointToFaceXY(const MeshData& mesh, int faceIndex, Vec3& point) {
        if (faceIndex < 0 || faceIndex >= static_cast<int>(mesh.faces.size())) return false;
        const MeshFace& face = mesh.faces[faceIndex];
        if (face.vertices.size() != 3) return false;

        const int i0 = face.vertices[0];
        const int i1 = face.vertices[1];
        const int i2 = face.vertices[2];
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            i0 >= static_cast<int>(mesh.vertices.size()) ||
            i1 >= static_cast<int>(mesh.vertices.size()) ||
            i2 >= static_cast<int>(mesh.vertices.size())) {
            return false;
        }

        const Vec3& a = mesh.vertices[i0].position;
        const Vec3& b = mesh.vertices[i1].position;
        const Vec3& c = mesh.vertices[i2].position;
        float u = 0.0f;
        float v = 0.0f;
        float w = 0.0f;
        if (!barycentricXY(point, a, b, c, u, v, w)) return false;

        point = a * u + b * v + c * w;
        return true;
    }

    bool raySegmentIntersectionXY(const Vec3& origin,
                                  const Vec3& direction,
                                  const Vec3& a,
                                  const Vec3& b,
                                  float& rayT,
                                  float& segmentT) {
        const Vec3 edge = b - a;
        const float det = direction.x * (-edge.y) - direction.y * (-edge.x);
        if (std::abs(det) <= 1e-8f) return false;

        const Vec3 delta = a - origin;
        const float t = (delta.x * (-edge.y) - delta.y * (-edge.x)) / det;
        const float u = (direction.x * delta.y - direction.y * delta.x) / det;
        if (t <= 1e-5f || u < -1e-4f || u > 1.0001f) return false;
        rayT = t;
        segmentT = std::clamp(u, 0.0f, 1.0f);
        return true;
    }

    std::map<std::pair<int, int>, std::vector<int>> buildEdgeFaces(const MeshData& mesh) {
        std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            const MeshFace& face = mesh.faces[fi];
            if (face.vertices.size() != 3) continue;
            for (int i = 0; i < 3; ++i) {
                edgeFaces[sortedEdge(face.vertices[i], face.vertices[(i + 1) % 3])].push_back(fi);
            }
        }
        return edgeFaces;
    }

    TensorStreamline traceOneWay(const MeshData& mesh,
                                 const TensorField& field,
                                 const std::map<std::pair<int, int>, std::vector<int>>& edgeFaces,
                                 int seedFace,
                                 Vec3 seed,
                                 Vec3 initialDirection,
                                 bool usePrimary,
                                 float spacing,
                                 int maxSteps) {
        TensorStreamline line;
        line.push_back(seed);

        int faceIndex = seedFace;
        Vec3 current = seed;
        Vec3 direction = initialDirection.normalized();
        const float nudge = std::max(1e-5f, spacing * 1e-4f);

        for (int step = 0; step < maxSteps; ++step) {
            if (faceIndex < 0 || faceIndex >= static_cast<int>(mesh.faces.size())) break;
            if (faceIndex >= static_cast<int>(field.size())) break;

            Vec3 fieldDir = usePrimary ? field[faceIndex].majorDirection : field[faceIndex].minorDirection;
            if (fieldDir.lengthSquared() <= 1e-8f) break;
            fieldDir.normalize();
            if (fieldDir.dot(direction) < 0.0f) fieldDir = -fieldDir;
            direction = fieldDir;

            const MeshFace& face = mesh.faces[faceIndex];
            if (face.vertices.size() != 3) break;

            float bestT = std::numeric_limits<float>::max();
            float bestSegmentT = 0.0f;
            int bestEdge = -1;
            for (int ei = 0; ei < 3; ++ei) {
                const int aId = face.vertices[ei];
                const int bId = face.vertices[(ei + 1) % 3];
                if (aId < 0 || bId < 0 ||
                    aId >= static_cast<int>(mesh.vertices.size()) ||
                    bId >= static_cast<int>(mesh.vertices.size())) {
                    continue;
                }

                float t = 0.0f;
                float segmentT = 0.0f;
                if (raySegmentIntersectionXY(current, direction, mesh.vertices[aId].position, mesh.vertices[bId].position, t, segmentT) && t < bestT) {
                    bestT = t;
                    bestSegmentT = segmentT;
                    bestEdge = ei;
                }
            }

            if (bestEdge < 0 || !std::isfinite(bestT)) break;
            const int aId = face.vertices[bestEdge];
            const int bId = face.vertices[(bestEdge + 1) % 3];
            Vec3 next = mesh.vertices[aId].position + (mesh.vertices[bId].position - mesh.vertices[aId].position) * bestSegmentT;
            line.push_back(next);

            const auto it = edgeFaces.find(sortedEdge(aId, bId));
            if (it == edgeFaces.end() || it->second.size() < 2) break;

            int nextFace = -1;
            for (int candidate : it->second) {
                if (candidate != faceIndex) {
                    nextFace = candidate;
                    break;
                }
            }
            if (nextFace < 0) break;

            faceIndex = nextFace;
            current = next + direction * nudge;
            projectPointToFaceXY(mesh, faceIndex, current);
        }

        return line;
    }

    TensorStreamline traceBothWays(const MeshData& mesh,
                                   const TensorField& field,
                                   const std::map<std::pair<int, int>, std::vector<int>>& edgeFaces,
                                   int seedFace,
                                   const Vec3& seed,
                                   bool usePrimary,
                                   float spacing,
                                   int maxSteps) {
        Vec3 dir = usePrimary ? field[seedFace].majorDirection : field[seedFace].minorDirection;
        if (dir.lengthSquared() <= 1e-8f) return {};
        dir.normalize();

        TensorStreamline negative = traceOneWay(mesh, field, edgeFaces, seedFace, seed, -dir, usePrimary, spacing, maxSteps);
        std::reverse(negative.begin(), negative.end());
        TensorStreamline positive = traceOneWay(mesh, field, edgeFaces, seedFace, seed, dir, usePrimary, spacing, maxSteps);

        TensorStreamline line;
        line.reserve(negative.size() + positive.size());
        line.insert(line.end(), negative.begin(), negative.end());
        if (!line.empty() && !positive.empty()) positive.erase(positive.begin());
        line.insert(line.end(), positive.begin(), positive.end());
        return line;
    }

    void appendPolylineSamples(const TensorStreamline& line, float stepLength, std::vector<Vec3>& samples) {
        if (line.size() < 2) return;
        const float step = std::max(1e-4f, stepLength);

        for (size_t i = 0; i + 1 < line.size(); ++i) {
            const Vec3 a = line[i];
            const Vec3 b = line[i + 1];
            const Vec3 edge = b - a;
            const float length = edge.length();
            if (length <= 1e-6f) continue;

            const int count = std::max(1, static_cast<int>(std::ceil(length / step)));
            for (int j = 0; j <= count; ++j) {
                samples.push_back(a + edge * (static_cast<float>(j) / static_cast<float>(count)));
            }
        }
    }

    bool overlapsAcceptedLines(const TensorStreamline& line,
                               const std::vector<Vec3>& acceptedSamples,
                               float minDistance,
                               float sampleStep) {
        if (acceptedSamples.empty()) return false;

        std::vector<Vec3> candidateSamples;
        appendPolylineSamples(line, sampleStep, candidateSamples);
        if (candidateSamples.empty()) return false;

        const float minDistance2 = minDistance * minDistance;
        int closeCount = 0;
        for (const Vec3& sample : candidateSamples) {
            for (const Vec3& accepted : acceptedSamples) {
                const float dx = sample.x - accepted.x;
                const float dy = sample.y - accepted.y;
                if (dx * dx + dy * dy < minDistance2) {
                    ++closeCount;
                    break;
                }
            }
        }

        return closeCount > std::max(2, static_cast<int>(candidateSamples.size() * 0.18f));
    }
}

    void StressAlignedRemesher::setSpacing(float spacing) {
        spacing_ = std::max(1e-3f, spacing);
    }

    void StressAlignedRemesher::setPrimaryOffset(float offset) {
        primaryOffset_ = std::clamp(offset, -1.0f, 1.0f);
    }

    void StressAlignedRemesher::setSecondaryOffset(float offset) {
        secondaryOffset_ = std::clamp(offset, -1.0f, 1.0f);
    }

    void StressAlignedRemesher::setMaxSteps(int steps) {
        maxSteps_ = std::max(1, steps);
    }

    void StressAlignedRemesher::clear() {
        streamlines_.primary.clear();
        streamlines_.secondary.clear();
    }

    bool StressAlignedRemesher::extractStreamlines(const MeshObject& mesh, const StressAnalyzer& analyzer) {
        auto data = mesh.getMeshData();
        if (!data) {
            clear();
            return false;
        }
        return extractStreamlines(*data, analyzer.getSmoothedCrossField());
    }

    bool StressAlignedRemesher::extractStreamlines(const MeshData& mesh, const TensorField& field) {
        clear();
        if (mesh.vertices.empty() || mesh.faces.empty() || field.empty()) return false;

        streamlines_.primary = extractDirection(mesh, field, true);
        streamlines_.secondary = extractDirection(mesh, field, false);
        return !streamlines_.primary.empty() || !streamlines_.secondary.empty();
    }

    std::vector<TensorStreamline> StressAlignedRemesher::extractDirection(const MeshData& mesh,
                                                                          const TensorField& field,
                                                                          bool usePrimary) const {
        std::vector<TensorStreamline> lines;
        if (mesh.vertices.empty()) return lines;

        Vec3 minB = mesh.vertices.front().position;
        Vec3 maxB = minB;
        for (const MeshVertex& vertex : mesh.vertices) {
            minB.x = std::min(minB.x, vertex.position.x);
            minB.y = std::min(minB.y, vertex.position.y);
            minB.z = std::min(minB.z, vertex.position.z);
            maxB.x = std::max(maxB.x, vertex.position.x);
            maxB.y = std::max(maxB.y, vertex.position.y);
            maxB.z = std::max(maxB.z, vertex.position.z);
        }

        const auto edgeFaces = buildEdgeFaces(mesh);
        const float minSeedDistance2 = spacing_ * spacing_ * 0.56f;
        const float minLineDistance = spacing_ * 0.70f;
        const float lineSampleStep = spacing_ * 0.35f;
        std::vector<Vec3> acceptedSeeds;
        std::vector<Vec3> acceptedLineSamples;

        for (float y = minB.y; y <= maxB.y + spacing_ * 0.5f; y += spacing_) {
            for (float x = minB.x; x <= maxB.x + spacing_ * 0.5f; x += spacing_) {
                Vec3 seed(x, y, 0.0f);
                int faceIndex = findFaceAtXY(mesh, seed, seed);
                if (faceIndex < 0 || faceIndex >= static_cast<int>(field.size())) continue;

                const float offset = usePrimary ? primaryOffset_ : secondaryOffset_;
                if (std::abs(offset) > 1e-6f) {
                    Vec3 shift = usePrimary ? field[faceIndex].minorDirection : field[faceIndex].majorDirection;
                    shift.z = 0.0f;
                    if (shift.lengthSquared() > 1e-8f) {
                        shift.normalize();
                        seed += shift * (spacing_ * offset);
                        faceIndex = findFaceAtXY(mesh, seed, seed);
                        if (faceIndex < 0 || faceIndex >= static_cast<int>(field.size())) continue;
                    }
                }

                bool tooClose = false;
                for (const Vec3& other : acceptedSeeds) {
                    const Vec3 delta = seed - other;
                    if (delta.x * delta.x + delta.y * delta.y < minSeedDistance2) {
                        tooClose = true;
                        break;
                    }
                }
                if (tooClose) continue;

                TensorStreamline line = traceBothWays(mesh, field, edgeFaces, faceIndex, seed, usePrimary, spacing_, maxSteps_);
                if (line.size() < 2) continue;
                if (overlapsAcceptedLines(line, acceptedLineSamples, minLineDistance, lineSampleStep)) continue;

                acceptedSeeds.push_back(seed);
                appendPolylineSamples(line, lineSampleStep, acceptedLineSamples);
                lines.push_back(std::move(line));
            }
        }

        if (lines.empty()) {
            const int count = static_cast<int>(std::min(mesh.faces.size(), field.size()));
            for (int fi = 0; fi < count; ++fi) {
                if (mesh.faces[fi].vertices.size() != 3) continue;
                TensorStreamline line = traceBothWays(mesh, field, edgeFaces, fi, faceCenter(mesh, mesh.faces[fi]), usePrimary, spacing_, maxSteps_);
                if (line.size() > 1) lines.push_back(std::move(line));
            }
        }

        return lines;
    }

} // namespace alice2
