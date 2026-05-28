#pragma once

#ifndef ALICE2_STRESS_ALIGNED_REMESHER_H
#define ALICE2_STRESS_ALIGNED_REMESHER_H

#include "../computeGeom/TensorField.h"
#include "../objects/MeshObject.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace alice2 {

    class StressAlignedRemesher {
    public:
        void setTargetEdgeLength(double length) { targetEdgeLength_ = length; }
        void setUseStressAdaptiveDensity(bool enabled) { useStressAdaptiveDensity_ = enabled; }
        void setStressDensityScale(double scale) { stressDensityScale_ = scale; }

        double targetEdgeLength() const { return targetEdgeLength_; }
        bool useStressAdaptiveDensity() const { return useStressAdaptiveDensity_; }
        double stressDensityScale() const { return stressDensityScale_; }

        std::shared_ptr<MeshData> remesh(const MeshData& mesh, const TensorField& crossField) const;

    private:
        double targetEdgeLength_{0.2};
        bool useStressAdaptiveDensity_{false};
        double stressDensityScale_{1.0};
    };

    namespace stress_aligned_remesher_detail {
        struct EdgeUse {
            int face{-1};
            int local{-1};
        };

        inline std::pair<int, int> sortedEdge(int a, int b) {
            return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
        }

        inline Vec3 faceCenter(const MeshData& mesh, const MeshFace& face) {
            Vec3 c;
            if (face.vertices.empty()) return c;
            for (int id : face.vertices) {
                if (id >= 0 && id < static_cast<int>(mesh.vertices.size())) c += mesh.vertices[id].position;
            }
            return c / static_cast<float>(face.vertices.size());
        }

        inline double alignmentScore(const MeshData& mesh, const TensorField& field, int faceA, int faceB, int a, int b) {
            if (faceA < 0 || faceA >= static_cast<int>(field.size())) return 0.0;
            if (faceB < 0 || faceB >= static_cast<int>(field.size())) return 0.0;
            if (a < 0 || b < 0 || a >= static_cast<int>(mesh.vertices.size()) || b >= static_cast<int>(mesh.vertices.size())) return 0.0;

            Vec3 edge = (mesh.vertices[b].position - mesh.vertices[a].position).normalized();
            const FaceStressTensor& ta = field[faceA];
            const FaceStressTensor& tb = field[faceB];
            Vec3 major = (ta.majorDirection + tb.majorDirection).normalized();
            Vec3 minor = (ta.minorDirection + tb.minorDirection).normalized();
            if (major.lengthSquared() < 1e-8f) major = ta.majorDirection;
            if (minor.lengthSquared() < 1e-8f) minor = ta.minorDirection;

            double edgeOnField = std::max(std::abs(edge.dot(major)), std::abs(edge.dot(minor)));
            double magnitude = 0.5 * (ta.magnitude + tb.magnitude);
            return edgeOnField + 1e-6 * magnitude;
        }

        inline MeshFace makeQuadFromPair(const MeshData& mesh, const MeshFace& fa, const MeshFace& fb, int sharedA, int sharedB) {
            int oppA = -1;
            int oppB = -1;
            for (int id : fa.vertices) {
                if (id != sharedA && id != sharedB) {
                    oppA = id;
                    break;
                }
            }
            for (int id : fb.vertices) {
                if (id != sharedA && id != sharedB) {
                    oppB = id;
                    break;
                }
            }

            std::vector<int> ids{oppA, sharedA, oppB, sharedB};
            Vec3 c = faceCenter(mesh, MeshFace(ids));
            Vec3 n = mesh.calculateFaceNormal(MeshFace(ids));
            std::sort(ids.begin(), ids.end(), [&](int lhs, int rhs) {
                Vec3 dl = mesh.vertices[lhs].position - c;
                Vec3 dr = mesh.vertices[rhs].position - c;
                return std::atan2(dl.y, dl.x) < std::atan2(dr.y, dr.x);
            });
            return MeshFace(ids, n, Color(0.82f, 0.82f, 0.82f, 1.0f));
        }
    }

    inline std::shared_ptr<MeshData> StressAlignedRemesher::remesh(const MeshData& mesh, const TensorField& crossField) const {
        using namespace stress_aligned_remesher_detail;

        auto out = std::make_shared<MeshData>();
        out->vertices = mesh.vertices;

        std::map<std::pair<int, int>, std::vector<EdgeUse>> edgeFaces;
        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            const auto& face = mesh.faces[fi];
            if (face.vertices.size() != 3) continue;
            for (int i = 0; i < 3; ++i) {
                int a = face.vertices[i];
                int b = face.vertices[(i + 1) % 3];
                edgeFaces[sortedEdge(a, b)].push_back({fi, i});
            }
        }

        struct Candidate {
            double score{0.0};
            int faceA{-1};
            int faceB{-1};
            int a{-1};
            int b{-1};
        };

        std::vector<Candidate> candidates;
        for (const auto& item : edgeFaces) {
            const auto& uses = item.second;
            if (uses.size() != 2) continue;
            candidates.push_back({
                alignmentScore(mesh, crossField, uses[0].face, uses[1].face, item.first.first, item.first.second),
                uses[0].face,
                uses[1].face,
                item.first.first,
                item.first.second
            });
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.score < b.score;
        });

        std::set<int> usedFaces;
        for (const auto& c : candidates) {
            if (usedFaces.count(c.faceA) || usedFaces.count(c.faceB)) continue;
            out->faces.push_back(makeQuadFromPair(mesh, mesh.faces[c.faceA], mesh.faces[c.faceB], c.a, c.b));
            usedFaces.insert(c.faceA);
            usedFaces.insert(c.faceB);
        }

        for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
            if (!usedFaces.count(fi)) out->faces.push_back(mesh.faces[fi]);
        }

        std::set<std::pair<int, int>> edges;
        for (const auto& face : out->faces) {
            for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
                edges.insert(sortedEdge(face.vertices[i], face.vertices[(i + 1) % face.vertices.size()]));
            }
        }
        for (const auto& e : edges) out->edges.emplace_back(e.first, e.second, Color(0.05f, 0.05f, 0.05f, 1.0f));

        out->calculateNormals();
        out->triangulationDirty = true;
        return out;
    }

} // namespace alice2

#endif // ALICE2_STRESS_ALIGNED_REMESHER_H
