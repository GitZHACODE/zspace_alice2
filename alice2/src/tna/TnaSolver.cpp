#include "TnaSolver.h"

#include "../computeGeom/ComputeMesh.h"

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace alice2 {
namespace {

bool appendBoundaryFace(MeshData& mesh, const std::vector<int>& group) {
    // Adjacent supports define an existing boundary edge, not a valid n-gon.
    if (group.size() < 3) return false;

    MeshFace face;
    face.vertices = group;
    face.normal = mesh.calculateFaceNormal(face);
    face.color = Color(0.78f, 0.84f, 1.0f, 1.0f);
    mesh.faces.push_back(std::move(face));
    return true;
}

std::vector<int> walkBoundaryLoop(const std::shared_ptr<HeMeshHalfedge>& first) {
    std::vector<int> vertices;
    if (!first) return vertices;

    auto current = first;
    do {
        const auto start = current->getStartVertex();
        if (!start) return {};
        vertices.push_back(start->getId());
        current = current->getNext();
    } while (current && current != first);

    return current == first ? vertices : std::vector<int>{};
}

Vec3 faceCentroid(const MeshData& mesh, const MeshFace& face) {
    Vec3 centroid(0.0f, 0.0f, 0.0f);
    if (face.vertices.empty()) return centroid;
    for (const int vertex : face.vertices) centroid = centroid + mesh.vertices[vertex].position;
    return centroid * (1.0f / static_cast<float>(face.vertices.size()));
}

float reciprocalAngleDegrees(const Vec3& formVector, const Vec3& forceVector) {
    const float formLength = formVector.length();
    const float forceLength = forceVector.length();
    if (formLength <= 1e-6f || forceLength <= 1e-6f) return 0.0f;
    const float cosine = std::clamp(std::abs(formVector.dot(forceVector)) /
                                        (formLength * forceLength),
                                    0.0f, 1.0f);
    return std::acos(cosine) * 180.0f / std::numbers::pi_v<float>;
}

bool validVertex(const MeshData& mesh, int vertex) {
    return vertex >= 0 && vertex < static_cast<int>(mesh.vertices.size());
}

std::vector<Vec3> rotateForceCounterClockwise(const MeshData& forceDiagram) {
    std::vector<Vec3> rotated;
    rotated.reserve(forceDiagram.vertices.size());
    for (const MeshVertex& vertex : forceDiagram.vertices) {
        rotated.emplace_back(-vertex.position.y, vertex.position.x, 0.0f);
    }
    return rotated;
}

void rotateForceClockwise(MeshData& forceDiagram, const std::vector<Vec3>& rotated) {
    for (int vertex = 0; vertex < static_cast<int>(forceDiagram.vertices.size()) &&
                          vertex < static_cast<int>(rotated.size()); ++vertex) {
        forceDiagram.vertices[vertex].position = Vec3(rotated[vertex].y, -rotated[vertex].x, 0.0f);
    }
}

// RhinoVAULT does not display the raw geometric dual produced from face
// centroids. After building its dual topology it integrates a force diagram
// from the form with the current q values (one by default). In the rotated
// force coordinate system this is the least-squares system
//
//     C_force * xy_force = q * C_form * xy_form .
//
// The translation of every connected force component is indeterminate, so
// retain one initial vertex per component as an anchor. This is equivalent to
// RhinoVAULT's single known vertex for its usual connected force diagram and
// also keeps the construction usable for meshes with multiple components.
bool initialiseForceFromUnitDensities(MeshData& forceDiagram,
                                      const MeshData& formDiagram,
                                      const std::vector<TnaEdge>& formEdges) {
    if (forceDiagram.vertices.empty() || forceDiagram.edges.empty() ||
        forceDiagram.edges.size() != formEdges.size()) return false;

    const int vertexCount = static_cast<int>(forceDiagram.vertices.size());
    const int edgeCount = static_cast<int>(forceDiagram.edges.size());
    std::vector<std::vector<int>> adjacency(vertexCount);
    std::vector<Eigen::Triplet<double>> laplacianEntries;
    laplacianEntries.reserve(edgeCount * 4);
    Eigen::VectorXd rhsX = Eigen::VectorXd::Zero(vertexCount);
    Eigen::VectorXd rhsY = Eigen::VectorXd::Zero(vertexCount);

    for (int edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex) {
        const MeshEdge& forceEdge = forceDiagram.edges[edgeIndex];
        const TnaEdge& formEdge = formEdges[edgeIndex];
        if (!validVertex(forceDiagram, forceEdge.vertexA) ||
            !validVertex(forceDiagram, forceEdge.vertexB) ||
            !validVertex(formDiagram, formEdge.vertexA) ||
            !validVertex(formDiagram, formEdge.vertexB)) return false;

        const int first = forceEdge.vertexA;
        const int second = forceEdge.vertexB;
        const Vec3 formVector = formDiagram.vertices[formEdge.vertexB].position -
                                formDiagram.vertices[formEdge.vertexA].position;
        // C^T (C_form * xy_form), for an oriented force edge first -> second.
        rhsX[first] -= formVector.x;
        rhsX[second] += formVector.x;
        rhsY[first] -= formVector.y;
        rhsY[second] += formVector.y;
        laplacianEntries.emplace_back(first, first, 1.0);
        laplacianEntries.emplace_back(second, second, 1.0);
        laplacianEntries.emplace_back(first, second, -1.0);
        laplacianEntries.emplace_back(second, first, -1.0);
        adjacency[first].push_back(second);
        adjacency[second].push_back(first);
    }

    Eigen::SparseMatrix<double> laplacian(vertexCount, vertexCount);
    laplacian.setFromTriplets(laplacianEntries.begin(), laplacianEntries.end());

    // Work in the same +90-degree coordinate system as horizontal_nodal.
    std::vector<Vec3> rotated = rotateForceCounterClockwise(forceDiagram);
    std::vector<bool> visited(vertexCount, false);
    std::vector<int> anchors;
    for (int start = 0; start < vertexCount; ++start) {
        if (visited[start]) continue;
        anchors.push_back(start);
        std::vector<int> pending{start};
        visited[start] = true;
        while (!pending.empty()) {
            const int vertex = pending.back();
            pending.pop_back();
            for (const int neighbour : adjacency[vertex]) {
                if (!visited[neighbour]) {
                    visited[neighbour] = true;
                    pending.push_back(neighbour);
                }
            }
        }
    }

    std::vector<bool> known(vertexCount, false);
    for (const int anchor : anchors) known[anchor] = true;
    std::vector<int> freeIndex(vertexCount, -1);
    int freeCount = 0;
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        if (!known[vertex]) freeIndex[vertex] = freeCount++;
    }
    if (freeCount == 0) return true;

    std::vector<Eigen::Triplet<double>> reducedEntries;
    reducedEntries.reserve(laplacian.nonZeros());
    Eigen::VectorXd reducedX = Eigen::VectorXd::Zero(freeCount);
    Eigen::VectorXd reducedY = Eigen::VectorXd::Zero(freeCount);
    for (int row = 0; row < vertexCount; ++row) {
        if (!known[row]) {
            reducedX[freeIndex[row]] = rhsX[row];
            reducedY[freeIndex[row]] = rhsY[row];
        }
    }
    for (int outer = 0; outer < laplacian.outerSize(); ++outer) {
        for (Eigen::SparseMatrix<double>::InnerIterator entry(laplacian, outer); entry; ++entry) {
            const int row = entry.row();
            const int column = entry.col();
            if (known[row]) continue;
            const int reducedRow = freeIndex[row];
            if (known[column]) {
                reducedX[reducedRow] -= entry.value() * rotated[column].x;
                reducedY[reducedRow] -= entry.value() * rotated[column].y;
            } else {
                reducedEntries.emplace_back(reducedRow, freeIndex[column], entry.value());
            }
        }
    }

    Eigen::SparseMatrix<double> reduced(freeCount, freeCount);
    reduced.setFromTriplets(reducedEntries.begin(), reducedEntries.end());
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(reduced);
    if (solver.info() != Eigen::Success) return false;
    const Eigen::VectorXd solvedX = solver.solve(reducedX);
    const Eigen::VectorXd solvedY = solver.solve(reducedY);
    if (solver.info() != Eigen::Success) return false;
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        if (!known[vertex]) {
            rotated[vertex].x = static_cast<float>(solvedX[freeIndex[vertex]]);
            rotated[vertex].y = static_cast<float>(solvedY[freeIndex[vertex]]);
        }
    }
    rotateForceClockwise(forceDiagram, rotated);
    forceDiagram.calculateNormals();
    forceDiagram.triangulationDirty = true;
    return true;
}

std::pair<int, int> undirectedEdgeKey(int first, int second) {
    return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
}

std::vector<float> reciprocalAngles(const MeshData& formDiagram,
                                    const MeshData& forceDiagram,
                                    const std::vector<TnaEdge>& formEdges) {
    std::vector<float> angles;
    angles.reserve(forceDiagram.edges.size());
    for (int edgeIndex = 0; edgeIndex < static_cast<int>(forceDiagram.edges.size()); ++edgeIndex) {
        const MeshEdge& forceEdge = forceDiagram.edges[edgeIndex];
        const TnaEdge formEdge = edgeIndex < static_cast<int>(formEdges.size())
                                     ? formEdges[edgeIndex]
                                     : TnaEdge{};
        if (!validVertex(forceDiagram, forceEdge.vertexA) ||
            !validVertex(forceDiagram, forceEdge.vertexB) ||
            !validVertex(formDiagram, formEdge.vertexA) ||
            !validVertex(formDiagram, formEdge.vertexB)) {
            angles.push_back(0.0f);
            continue;
        }
        const Vec3 formVector = formDiagram.vertices[formEdge.vertexB].position -
                                formDiagram.vertices[formEdge.vertexA].position;
        const Vec3 forceVector = forceDiagram.vertices[forceEdge.vertexB].position -
                                 forceDiagram.vertices[forceEdge.vertexA].position;
        angles.push_back(reciprocalAngleDegrees(formVector, forceVector));
    }
    return angles;
}

std::vector<float> reciprocalDirectionDeviations(
    const MeshData& form, const MeshData& force, const std::vector<TnaEdge>& edges,
    const std::vector<TnaHorizontalSettings::EdgeConstraint>& constraints = {}) {
    std::vector<float> deviations(force.edges.size(), std::numeric_limits<float>::infinity());
    for (std::size_t i = 0; i < force.edges.size() && i < edges.size(); ++i) {
        const auto& a = edges[i];
        const auto& b = force.edges[i];
        if (!validVertex(form, a.vertexA) || !validVertex(form, a.vertexB) ||
            !validVertex(force, b.vertexA) || !validVertex(force, b.vertexB)) continue;
        const Vec3 u = form.vertices[a.vertexB].position - form.vertices[a.vertexA].position;
        const Vec3 v = force.vertices[b.vertexB].position - force.vertices[b.vertexA].position;
        const double lu = std::hypot(double(u.x), double(u.y));
        const double lv = std::hypot(double(v.x), double(v.y));
        if (!std::isfinite(lu) || !std::isfinite(lv) || lu <= 1e-6 || lv <= 1e-6) continue;
        const bool tension = a.isTension || (i < constraints.size() && constraints[i].isTension);
        // Rotate force +90 degrees to compare with the signed form direction.
        const double cosine = (tension ? -1.0 : 1.0) *
                              (-double(u.x) * v.y + double(u.y) * v.x) / (lu * lv);
        deviations[i] = static_cast<float>(std::acos(std::clamp(cosine, -1.0, 1.0)) *
                                           180.0 / std::numbers::pi);
    }
    return deviations;
}

void updateHorizontalAngles(TnaHorizontalEquilibrium& state) {
    state.edgeAnglesDegrees = reciprocalAngles(state.formDiagram, state.forceDiagram,
                                                state.reciprocalFormEdges);
    state.maximumAngleDeviation = 0.0f;
    for (const float angle : state.edgeAnglesDegrees) {
        state.maximumAngleDeviation = std::max(state.maximumAngleDeviation, std::abs(90.0f - angle));
    }
    state.edgeDirectionDeviationsDegrees = reciprocalDirectionDeviations(
        state.formDiagram, state.forceDiagram, state.reciprocalFormEdges, state.edgeConstraints);
    state.maximumDirectionDeviation = 0.0f;
    for (const float deviation : state.edgeDirectionDeviationsDegrees) {
        state.maximumDirectionDeviation = std::max(state.maximumDirectionDeviation, deviation);
    }
}

std::vector<float> tributaryAreas(const MeshData& mesh,
                                  const std::unordered_set<int>& unloadedFaces) {
    std::vector<float> areas(mesh.vertices.size(), 0.0f);
    for (int faceIndex = 0; faceIndex < static_cast<int>(mesh.faces.size()); ++faceIndex) {
        if (unloadedFaces.contains(faceIndex)) continue;
        const MeshFace& face = mesh.faces[faceIndex];
        if (face.vertices.size() < 3) continue;

        Vec3 centroid(0.0f, 0.0f, 0.0f);
        bool valid = true;
        for (const int vertex : face.vertices) {
            if (!validVertex(mesh, vertex)) {
                valid = false;
                break;
            }
            centroid += mesh.vertices[vertex].position;
        }
        if (!valid) continue;
        centroid /= static_cast<float>(face.vertices.size());

        // Same vertex tributary-area construction as COMPAS LoadUpdater:
        // each adjacent face-side contributes one quarter of its centroid fan
        // triangle area to the endpoint vertex.
        for (int local = 0; local < static_cast<int>(face.vertices.size()); ++local) {
            const int vertex = face.vertices[local];
            const int previous = face.vertices[(local - 1 + static_cast<int>(face.vertices.size())) %
                                               static_cast<int>(face.vertices.size())];
            const int next = face.vertices[(local + 1) % static_cast<int>(face.vertices.size())];
            const Vec3& position = mesh.vertices[vertex].position;
            areas[vertex] += 0.25f * (mesh.vertices[next].position - position).cross(centroid - position).length();
            areas[vertex] += 0.25f * (mesh.vertices[previous].position - position).cross(centroid - position).length();
        }
    }
    return areas;
}

} // namespace

TnaFormDiagram TnaSolver::makeFormDiagram(const MeshData& input,
                                          const std::vector<int>& requestedSupports) const {
    TnaFormDiagram result;
    if (input.vertices.empty() || input.faces.empty()) {
        result.diagnostic = "TNA form diagram needs a mesh with vertices and faces";
        return result;
    }

    ComputeMesh topology("tna_form_topology", input, true);
    std::vector<std::shared_ptr<HeMeshHalfedge>> boundaryStarts;
    std::unordered_set<int> boundaryVertexIds;
    std::unordered_set<int> visitedBoundaryHalfedges;
    for (const auto& halfedge : topology.getHalfedges()) {
        if (!halfedge || !halfedge->onBoundary() ||
            visitedBoundaryHalfedges.contains(halfedge->getId())) continue;

        const std::vector<int> loop = walkBoundaryLoop(halfedge);
        if (loop.empty()) {
            result.diagnostic = "Could not walk a closed boundary loop; check mesh face winding";
            return result;
        }

        auto current = halfedge;
        do {
            visitedBoundaryHalfedges.insert(current->getId());
            current = current->getNext();
        } while (current && current != halfedge);

        boundaryStarts.push_back(halfedge);
        boundaryVertexIds.insert(loop.begin(), loop.end());
        for (int i = 0; i < static_cast<int>(loop.size()); ++i) {
            result.boundaryEdges.push_back({loop[i], loop[(i + 1) % loop.size()]});
        }
    }

    if (boundaryStarts.empty()) {
        result.diagnostic = "TNA form diagram needs at least one boundary loop";
        return result;
    }

    std::unordered_set<int> supportSet;
    if (requestedSupports.empty()) {
        // Ordinary OBJ files have no vertex-colour support tags.
        supportSet = boundaryVertexIds;
    } else {
        for (const int vertex : requestedSupports) {
            if (vertex >= 0 && vertex < static_cast<int>(input.vertices.size()) &&
                boundaryVertexIds.contains(vertex)) {
                supportSet.insert(vertex);
            }
        }
        if (supportSet.empty()) {
            result.diagnostic = "Explicit support vertices must lie on a mesh boundary";
            return result;
        }
    }
    result.supportVertices.assign(supportSet.begin(), supportSet.end());
    std::sort(result.supportVertices.begin(), result.supportVertices.end());

    // update_boundaries in COMPAS marks a boundary edge joining two supports
    // as non-active before adding the exterior faces. It has no reciprocal
    // force edge and must not be treated as part of the equilibrium form.
    std::set<std::pair<int, int>> supportedBoundaryEdges;
    for (const auto& start : boundaryStarts) {
        const std::vector<int> loop = walkBoundaryLoop(start);
        for (int i = 0; i < static_cast<int>(loop.size()); ++i) {
            const int first = loop[i];
            const int second = loop[(i + 1) % loop.size()];
            if (supportSet.contains(first) && supportSet.contains(second)) {
                supportedBoundaryEdges.insert(undirectedEdgeKey(first, second));
            }
        }
    }

    std::set<std::pair<int, int>> activeInputEdges;
    for (const MeshFace& face : input.faces) {
        for (int i = 0; i < static_cast<int>(face.vertices.size()); ++i) {
            const std::pair<int, int> edge = undirectedEdgeKey(face.vertices[i],
                                                                 face.vertices[(i + 1) % face.vertices.size()]);
            if (!supportedBoundaryEdges.contains(edge)) activeInputEdges.insert(edge);
        }
    }

    result.mesh = input;
    for (const auto& start : boundaryStarts) {
        const std::vector<int> loop = walkBoundaryLoop(start);
        std::vector<int> supportPositions;
        for (int i = 0; i < static_cast<int>(loop.size()); ++i) {
            if (supportSet.contains(loop[i])) supportPositions.push_back(i);
        }

        if (supportPositions.size() < 2) {
            // An opening with no supports still needs one exterior face for
            // the later dual construction.
            result.boundaryGroups.push_back(loop);
            if (appendBoundaryFace(result.mesh, loop)) {
                result.exteriorFormFaces.push_back(static_cast<int>(result.mesh.faces.size()) - 1);
                ++result.appendedBoundaryFaces;
            } else ++result.skippedDegenerateGroups;
            continue;
        }

        for (int groupIndex = 0; groupIndex < static_cast<int>(supportPositions.size()); ++groupIndex) {
            const int first = supportPositions[groupIndex];
            const int last = supportPositions[(groupIndex + 1) % supportPositions.size()];
            std::vector<int> group;
            for (int i = first;; i = (i + 1) % static_cast<int>(loop.size())) {
                group.push_back(loop[i]);
                if (i == last) break;
            }
            result.boundaryGroups.push_back(group);
            if (appendBoundaryFace(result.mesh, group)) {
                result.exteriorFormFaces.push_back(static_cast<int>(result.mesh.faces.size()) - 1);
                ++result.appendedBoundaryFaces;
            } else ++result.skippedDegenerateGroups;
        }
    }

    // Recreate the edge list so the support-to-support closing edge of each
    // exterior n-gon is visible and available to the next topology stage.
    MeshObject formObject("tna_form_diagram");
    formObject.setMeshData(std::make_shared<MeshData>(result.mesh));
    formObject.generateEdgesFromFaces();
    formObject.getMeshData()->calculateNormals();
    formObject.getMeshData()->triangulationDirty = true;
    result.mesh = *formObject.getMeshData();
    for (const MeshEdge& edge : result.mesh.edges) {
        if (activeInputEdges.contains(undirectedEdgeKey(edge.vertexA, edge.vertexB))) {
            result.activeFormEdges.push_back({edge.vertexA, edge.vertexB});
        }
    }

    std::ostringstream status;
    status << "Form diagram: " << result.mesh.vertices.size() << " vertices, "
           << result.mesh.faces.size() << " faces, " << result.supportVertices.size()
           << " supports, " << result.boundaryGroups.size() << " boundary groups, "
           << result.appendedBoundaryFaces << " exterior n-gons";
    if (result.skippedDegenerateGroups > 0) {
        status << " (" << result.skippedDegenerateGroups << " adjacent-support groups kept as edges)";
    }
    result.diagnostic = status.str();
    result.success = true;
    return result;
}

bool TnaSolver::setTensionEdges(TnaFormDiagram& form,
                               const std::vector<TnaEdge>& edges,
                               bool isTension) const {
    if (!form.success) {
        form.diagnostic = "Tension marking needs a valid form diagram";
        return false;
    }

    std::map<std::pair<int, int>, std::size_t> activeEdges;
    for (std::size_t i = 0; i < form.activeFormEdges.size(); ++i) {
        const TnaEdge& edge = form.activeFormEdges[i];
        activeEdges.emplace(undirectedEdgeKey(edge.vertexA, edge.vertexB), i);
    }
    std::set<std::size_t> selected;
    for (const TnaEdge& edge : edges) {
        const auto found = activeEdges.find(undirectedEdgeKey(edge.vertexA, edge.vertexB));
        if (!validVertex(form.mesh, edge.vertexA) || !validVertex(form.mesh, edge.vertexB) ||
            edge.vertexA == edge.vertexB || found == activeEdges.end()) {
            std::ostringstream status;
            status << "Cannot mark inactive or invalid form edge (" << edge.vertexA
                   << ", " << edge.vertexB << ")";
            form.diagnostic = status.str();
            return false;
        }
        selected.insert(found->second);
    }
    for (const std::size_t index : selected) form.activeFormEdges[index].isTension = isTension;
    std::ostringstream status;
    status << "Marked " << selected.size() << " active form edges as "
           << (isTension ? "tension" : "compression");
    form.diagnostic = status.str();
    return true;
}

bool TnaSolver::setBoundaryEdgesTension(TnaFormDiagram& form, bool isTension) const {
    std::set<std::pair<int, int>> boundaryKeys;
    for (const TnaEdge& edge : form.boundaryEdges) {
        boundaryKeys.insert(undirectedEdgeKey(edge.vertexA, edge.vertexB));
    }
    std::vector<TnaEdge> selected;
    for (const TnaEdge& edge : form.activeFormEdges) {
        if (boundaryKeys.contains(undirectedEdgeKey(edge.vertexA, edge.vertexB))) {
            selected.push_back(edge);
        }
    }
    return setTensionEdges(form, selected, isTension);
}

bool TnaSolver::setOuterBoundaryEdgesTension(TnaFormDiagram& form,
                                            int supportVertex,
                                            bool isTension) const {
    if (!form.success || form.supportVertices.empty()) {
        form.diagnostic = "Outer boundary tension marking needs a valid form diagram with supports";
        return false;
    }
    if (supportVertex == -1) supportVertex = form.supportVertices.front();
    if (!validVertex(form.mesh, supportVertex) ||
        std::find(form.supportVertices.begin(), form.supportVertices.end(), supportVertex) ==
            form.supportVertices.end()) {
        form.diagnostic = "Outer boundary tension marking needs a support vertex on the requested ring";
        return false;
    }

    // Exterior faces close the original boundaries. Remove them from a copy
    // before walking halfedges, retaining the original vertex IDs.
    MeshData original = form.mesh;
    original.faces.clear();
    original.edges.clear();
    const std::unordered_set<int> exteriorFaces(form.exteriorFormFaces.begin(),
                                                form.exteriorFormFaces.end());
    for (int i = 0; i < static_cast<int>(form.mesh.faces.size()); ++i) {
        if (!exteriorFaces.contains(i)) original.faces.push_back(form.mesh.faces[i]);
    }
    ComputeMesh topology("tna_outer_boundary", original, true);
    for (const auto& halfedge : topology.getHalfedges()) {
        if (!halfedge || !halfedge->onBoundary()) continue;
        const auto start = halfedge->getStartVertex();
        if (!start || start->getId() != supportVertex) continue;
        const std::vector<int> loop = walkBoundaryLoop(halfedge);
        if (loop.empty()) {
            form.diagnostic = "Could not walk a closed boundary loop from the support vertex";
            return false;
        }
        std::set<std::pair<int, int>> ringKeys;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            ringKeys.insert(undirectedEdgeKey(loop[i], loop[(i + 1) % loop.size()]));
        }
        std::vector<TnaEdge> selected;
        for (const TnaEdge& edge : form.activeFormEdges) {
            if (ringKeys.contains(undirectedEdgeKey(edge.vertexA, edge.vertexB))) {
                selected.push_back(edge);
            }
        }
        return setTensionEdges(form, selected, isTension);
    }
    form.diagnostic = "The selected support vertex is not on an original mesh boundary";
    return false;
}

TnaForceDiagram TnaSolver::makeForceDiagram(const TnaFormDiagram& form) const {
    TnaForceDiagram result;
    const MeshData& formDiagram = form.mesh;
    if (formDiagram.vertices.empty() || formDiagram.faces.empty()) {
        result.diagnostic = "TNA force diagram needs a form mesh with vertices and faces";
        return result;
    }
    if (form.activeFormEdges.empty()) {
        result.diagnostic = "TNA force diagram needs active form edges";
        return result;
    }

    ComputeMesh topology("tna_force_topology", formDiagram, true);
    if (topology.getFaces().size() != formDiagram.faces.size()) {
        result.diagnostic = "Could not construct form half-edge topology for the force dual";
        return result;
    }

    // This matches FormDiagram.dual_diagram: only faces around inner free
    // form vertices belong to the force diagram. Added outside faces appear
    // only when a free boundary-chain vertex makes them part of such a cycle.
    std::vector<bool> boundary(formDiagram.vertices.size(), false);
    for (const auto& edge : topology.getEdges()) {
        if (!edge->onBoundary()) continue;
        const auto [first, second] = edge->getVertices();
        if (first) boundary[first->getId()] = true;
        if (second) boundary[second->getId()] = true;
    }
    std::vector<bool> support(formDiagram.vertices.size(), false);
    for (const int vertex : form.supportVertices) {
        if (validVertex(formDiagram, vertex)) support[vertex] = true;
    }
    std::vector<bool> inner(formDiagram.vertices.size(), false);
    for (int vertex = 0; vertex < static_cast<int>(formDiagram.vertices.size()); ++vertex) {
        inner[vertex] = !boundary[vertex] && !support[vertex];
    }

    std::unordered_map<int, int> formFaceToForceVertex;
    for (const auto& vertex : topology.getVertices()) {
        const int formVertex = vertex->getId();
        if (!inner[formVertex]) continue;
        std::vector<int> forceFace;
        for (const auto& halfedge : vertex->getHalfedges()) {
            if (!halfedge || !halfedge->getFace()) continue;
            const int formFace = halfedge->getFace()->getId();
            auto found = formFaceToForceVertex.find(formFace);
            if (found == formFaceToForceVertex.end()) {
                const int forceVertex = static_cast<int>(result.mesh.vertices.size());
                formFaceToForceVertex.emplace(formFace, forceVertex);
                result.mesh.vertices.emplace_back(faceCentroid(formDiagram, formDiagram.faces[formFace]),
                                                  Vec3(0.0f, 0.0f, 1.0f),
                                                  Color(0.86f, 0.18f, 0.05f, 1.0f));
                result.forceVertexFormFaces.push_back(formFace);
                forceFace.push_back(forceVertex);
            } else {
                forceFace.push_back(found->second);
            }
        }
        if (forceFace.size() >= 3) result.mesh.faces.emplace_back(forceFace);
    }

    // ForceDiagram.ordered_edges(form) in COMPAS keys each force edge by the
    // two form faces on either side of one *oriented* active form edge. Keep
    // precisely that ordering here. It is essential for a weighted target to
    // have the same direction in both diagrams.
    struct TopologyEdge {
        std::shared_ptr<HeMeshHalfedge> first;
        std::shared_ptr<HeMeshHalfedge> second;
    };
    std::map<std::pair<int, int>, TopologyEdge> topologyEdges;
    for (const auto& edge : topology.getEdges()) {
        const auto [first, second] = edge->getHalfedges();
        if (!first || !second) continue;
        const auto firstVertex = first->getStartVertex();
        const auto secondVertex = first->getVertex();
        if (!firstVertex || !secondVertex) continue;
        topologyEdges.emplace(undirectedEdgeKey(firstVertex->getId(), secondVertex->getId()),
                              TopologyEdge{first, second});
    }

    int missingReciprocalEdges = 0;
    for (const TnaEdge& formEdge : form.activeFormEdges) {
        const auto foundTopology = topologyEdges.find(undirectedEdgeKey(formEdge.vertexA, formEdge.vertexB));
        if (foundTopology == topologyEdges.end()) {
            ++missingReciprocalEdges;
            continue;
        }

        std::shared_ptr<HeMeshHalfedge> formHalfedge = foundTopology->second.first;
        std::shared_ptr<HeMeshHalfedge> oppositeHalfedge = foundTopology->second.second;
        const auto halfedgeStart = formHalfedge->getStartVertex();
        const auto halfedgeEnd = formHalfedge->getVertex();
        if (!halfedgeStart || !halfedgeEnd ||
            (halfedgeStart->getId() != formEdge.vertexA || halfedgeEnd->getId() != formEdge.vertexB)) {
            std::swap(formHalfedge, oppositeHalfedge);
        }
        if (!formHalfedge || !oppositeHalfedge || !formHalfedge->getFace() || !oppositeHalfedge->getFace()) {
            ++missingReciprocalEdges;
            continue;
        }

        const auto formA = formHalfedge->getStartVertex();
        const auto formB = formHalfedge->getVertex();
        if (!formA || !formB || formA->getId() != formEdge.vertexA || formB->getId() != formEdge.vertexB) {
            ++missingReciprocalEdges;
            continue;
        }
        const int firstFace = formHalfedge->getFace()->getId();
        const int secondFace = oppositeHalfedge->getFace()->getId();
        const auto first = formFaceToForceVertex.find(firstFace);
        const auto second = formFaceToForceVertex.find(secondFace);
        if (first == formFaceToForceVertex.end() || second == formFaceToForceVertex.end()) {
            ++missingReciprocalEdges;
            continue;
        }
        result.mesh.edges.emplace_back(first->second, second->second, Color(0.86f, 0.18f, 0.05f, 1.0f));
        result.reciprocalFormEdges.push_back(formEdge);
    }

    const bool forceInitialised = missingReciprocalEdges == 0 &&
                                  initialiseForceFromUnitDensities(result.mesh, formDiagram,
                                                                   result.reciprocalFormEdges);
    if (forceInitialised) {
        result.edgeAnglesDegrees = reciprocalAngles(formDiagram, result.mesh,
                                                     result.reciprocalFormEdges);
        result.edgeDirectionDeviationsDegrees = reciprocalDirectionDeviations(
            formDiagram, result.mesh, result.reciprocalFormEdges);
    }

    result.mesh.triangulationDirty = true;
    std::ostringstream status;
    status << "TNA force diagram: " << result.mesh.vertices.size() << " dual vertices, "
           << result.mesh.edges.size() << " dual edges, " << result.mesh.faces.size() << " dual faces, "
           << result.fixedForceVertices.size() << " fixed force vertices";
    if (missingReciprocalEdges > 0) {
        status << " (" << missingReciprocalEdges << " active form edges have no dual edge)";
    }
    if (!forceInitialised) {
        status << " (could not initialise reciprocal unit-density force geometry)";
    }
    result.diagnostic = status.str();
    result.success = !result.mesh.vertices.empty() &&
                     result.mesh.edges.size() == form.activeFormEdges.size() &&
                     missingReciprocalEdges == 0 && forceInitialised;
    return result;
}

namespace {

// Horizontal equilibrium with fixed edge lengths l_e and target directions
// t_e is the least-squares problem
//
//     min_x  sum_(e=(i,j)) || (x_j - x_i) - l_e t_e ||^2 .
//
// Its normal equations have a graph-Laplacian left-hand side. Rather than
// assembling Lx=b, each Jacobi iteration applies the row update directly:
//
//     x_i^(k+1) = (1 / deg(i)) sum_(e=(i,j)) (x_j^k - s_ie l_e t_e),
//
// where s_ie is +1 when the edge is oriented i -> j and -1 otherwise. All
// x_j values come from the frozen previous iterate, so this is Jacobi rather
// than in-place Gauss-Seidel relaxation.
void paralleliseEdges(std::vector<Vec3>& coordinates,
                      const std::vector<TnaEdge>& edges,
                      const std::vector<Vec3>& targets,
                      const std::vector<bool>& fixed,
                      const std::vector<float>& minimumLengths,
                      const std::vector<float>& maximumLengths) {
    std::vector<std::vector<int>> incidentEdges(coordinates.size());
    for (int edge = 0; edge < static_cast<int>(edges.size()); ++edge) {
        if (edges[edge].vertexA >= 0 && edges[edge].vertexB >= 0 &&
            edges[edge].vertexA < static_cast<int>(coordinates.size()) &&
            edges[edge].vertexB < static_cast<int>(coordinates.size())) {
            incidentEdges[edges[edge].vertexA].push_back(edge);
            incidentEdges[edges[edge].vertexB].push_back(edge);
        }
    }

    // Match COMPAS parallelise_edges: one Jacobi step reads a frozen snapshot
    // of positions and edge lengths, and writes every movable vertex from
    // that same snapshot. In-place Gauss-Seidel updates give a different
    // result, particularly for intermediate form/force weights.
    const std::vector<Vec3> previous = coordinates;
    std::vector<float> lengths(edges.size(), 0.0f);
    for (int edge = 0; edge < static_cast<int>(edges.size()); ++edge) {
        const TnaEdge& endpoints = edges[edge];
        if (endpoints.vertexA < 0 || endpoints.vertexB < 0 ||
            endpoints.vertexA >= static_cast<int>(previous.size()) ||
            endpoints.vertexB >= static_cast<int>(previous.size())) continue;
        lengths[edge] = (previous[endpoints.vertexB] - previous[endpoints.vertexA]).length();
        if (edge < static_cast<int>(minimumLengths.size())) {
            lengths[edge] = std::max(lengths[edge], minimumLengths[edge]);
        }
        if (edge < static_cast<int>(maximumLengths.size())) {
            lengths[edge] = std::min(lengths[edge], maximumLengths[edge]);
        }
    }

    for (int vertex = 0; vertex < static_cast<int>(coordinates.size()); ++vertex) {
        if (fixed[vertex]) continue;
        Vec3 targetPosition(0.0f, 0.0f, 0.0f);
        int count = 0;
        for (const int edgeIndex : incidentEdges[vertex]) {
            const TnaEdge& edge = edges[edgeIndex];
            const int other = edge.vertexA == vertex ? edge.vertexB : edge.vertexA;
            const Vec3& target = targets[edgeIndex];
            const float length = lengths[edgeIndex];
            targetPosition += edge.vertexA == vertex
                                  ? previous[other] - target * length
                                  : previous[other] + target * length;
            ++count;
        }
        if (count > 0) coordinates[vertex] = targetPosition / static_cast<float>(count);
        coordinates[vertex].z = 0.0f;
    }

    // COMPAS collapses exactly-zero-length edges to their midpoint after the
    // Jacobi update. Keep this rare but important degeneracy behaviour.
    for (int edge = 0; edge < static_cast<int>(edges.size()); ++edge) {
        if (lengths[edge] != 0.0f) continue;
        const TnaEdge& endpoints = edges[edge];
        if (endpoints.vertexA < 0 || endpoints.vertexB < 0 ||
            endpoints.vertexA >= static_cast<int>(coordinates.size()) ||
            endpoints.vertexB >= static_cast<int>(coordinates.size())) continue;
        const Vec3 midpoint = (coordinates[endpoints.vertexA] + coordinates[endpoints.vertexB]) * 0.5f;
        coordinates[endpoints.vertexA] = midpoint;
        coordinates[endpoints.vertexB] = midpoint;
    }
}

std::vector<Vec3> makeHorizontalTargets(const TnaHorizontalEquilibrium& state,
                                        float alpha) {
    std::vector<Vec3> targets(state.forceDiagram.edges.size(), Vec3(0.0f, 0.0f, 0.0f));
    const std::vector<Vec3> rotatedForce = rotateForceCounterClockwise(state.forceDiagram);
    for (int edgeIndex = 0; edgeIndex < static_cast<int>(state.forceDiagram.edges.size()); ++edgeIndex) {
        const MeshEdge& forceEdge = state.forceDiagram.edges[edgeIndex];
        const TnaEdge formEdge = state.reciprocalFormEdges[edgeIndex];
        if (!validVertex(state.forceDiagram, forceEdge.vertexA) ||
            !validVertex(state.forceDiagram, forceEdge.vertexB) ||
            !validVertex(state.formDiagram, formEdge.vertexA) ||
            !validVertex(state.formDiagram, formEdge.vertexB)) continue;

        Vec3 formDirection = state.formDiagram.vertices[formEdge.vertexB].position -
                             state.formDiagram.vertices[formEdge.vertexA].position;
        Vec3 forceDirection = rotatedForce[forceEdge.vertexB] - rotatedForce[forceEdge.vertexA];
        if (formDirection.lengthSquared() <= 1e-12f || forceDirection.lengthSquared() <= 1e-12f) continue;

        if (edgeIndex < static_cast<int>(state.edgeConstraints.size()) &&
            state.edgeConstraints[edgeIndex].isTension) {
            formDirection = -formDirection;
        }

        // The force edge was explicitly ordered from the form edge's left
        // face to its right face, just as ForceDiagram.ordered_edges(form).
        // In the rotated force coordinate system, the shared target is
        //
        //     t_e = alpha * u_form + (1 - alpha) * u_force .
        //
        // Do not normalise the blend: COMPAS uses its magnitude as part of
        // the Jacobi projection, particularly for intermediate weights.
        targets[edgeIndex] = formDirection.normalized() * alpha +
                             forceDirection.normalized() * (1.0f - alpha);
    }
    return targets;
}

} // namespace

bool TnaSolver::resetHorizontalEquilibrium(const MeshData& formDiagram,
                                           const TnaForceDiagram& forceDiagram,
                                           const std::vector<int>& fixedFormVertices) {
    m_horizontal = {};
    m_vertical = {};
    TnaHorizontalEquilibrium& result = m_horizontal;
    if (!forceDiagram.success || formDiagram.vertices.empty() || forceDiagram.mesh.vertices.empty() ||
        forceDiagram.mesh.edges.empty()) {
        result.diagnostic = "Horizontal equilibrium needs a valid form diagram and force dual";
        return false;
    }
    if (forceDiagram.mesh.edges.size() != forceDiagram.reciprocalFormEdges.size()) {
        result.diagnostic = "Force dual is missing reciprocal form-edge mappings";
        return false;
    }

    result.formDiagram = formDiagram;
    result.forceDiagram = forceDiagram.mesh;
    result.reciprocalFormEdges = forceDiagram.reciprocalFormEdges;
    result.fixedForceVertices = forceDiagram.fixedForceVertices;
    for (const int vertex : fixedFormVertices) {
        if (validVertex(result.formDiagram, vertex) &&
            std::find(result.fixedFormVertices.begin(), result.fixedFormVertices.end(), vertex) ==
                result.fixedFormVertices.end()) {
            result.fixedFormVertices.push_back(vertex);
        }
    }
    updateHorizontalAngles(result);
    std::ostringstream status;
    status << "TNA horizontal equilibrium reset: " << result.forceDiagram.edges.size()
           << " reciprocal pairs, initial max direction deviation " << result.maximumDirectionDeviation << " deg";
    result.diagnostic = status.str();
    result.success = true;
    return true;
}

bool TnaSolver::continueHorizontalEquilibrium(int additionalIterations) {
    if (!m_horizontal.success) return false;

    const bool completedBatch = m_horizontal.converged;
    const int currentLimit = std::max({m_horizontal.iterationLimit,
                                       m_horizontal.formIterations,
                                       m_horizontal.forceIterations});
    m_horizontal.iterationLimit = currentLimit + std::max(1, additionalIterations);
    // A new batch follows the same COMPAS ordering: extend the form pass
    // first, then extend the force pass. The diagrams themselves remain in
    // their current solved positions.
    if (completedBatch) m_horizontal.solvingForceDiagram = false;
    m_horizontal.converged = false;
    m_horizontal.satisfiesAngleTarget = false;
    return true;
}

bool TnaSolver::solveVerticalEquilibrium(const TnaVerticalSettings& settings) {
    m_vertical = {};
    TnaVerticalEquilibrium& result = m_vertical;
    const TnaHorizontalEquilibrium& horizontal = m_horizontal;
    if (!horizontal.success || !horizontal.converged) {
        result.diagnostic = "Horizontal equilibrium must finish before vertical equilibrium";
        return false;
    }
    if (horizontal.formDiagram.vertices.empty() || horizontal.reciprocalFormEdges.empty() ||
        horizontal.forceDensities.size() != horizontal.reciprocalFormEdges.size()) {
        result.diagnostic = "Vertical equilibrium needs solved reciprocal form/force densities";
        return false;
    }
    const auto finiteValues = [](const std::vector<float>& values) {
        return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
    };
    if (!std::isfinite(settings.forceScale) || settings.forceScale <= 0.0f ||
        !std::isfinite(settings.residualTolerance) || settings.residualTolerance < 0.0f ||
        !std::isfinite(settings.nodalLoad) || !std::isfinite(settings.density) ||
        !std::isfinite(settings.thickness) || !finiteValues(settings.nodalLoads) ||
        !finiteValues(settings.thicknesses) || !finiteValues(settings.supportHeights)) {
        result.diagnostic = "Vertical equilibrium needs finite inputs, a positive force scale and nonnegative tolerance";
        return false;
    }
    for (const MeshVertex& vertex : horizontal.formDiagram.vertices) {
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.position.z)) {
            result.diagnostic = "Vertical equilibrium contains nonfinite coordinates";
            return false;
        }
    }

    result.formDiagram = horizontal.formDiagram;
    const int vertexCount = static_cast<int>(result.formDiagram.vertices.size());
    std::vector<bool> fixed(vertexCount, false);
    for (const int vertex : horizontal.fixedFormVertices) {
        if (validVertex(result.formDiagram, vertex)) fixed[vertex] = true;
    }
    for (const int vertex : settings.additionalFixedVertices) {
        if (!validVertex(result.formDiagram, vertex)) {
            result.diagnostic = "Vertical equilibrium contains an invalid additional fixed vertex";
            return false;
        }
        fixed[vertex] = true;
    }
    const int fixedCount = static_cast<int>(std::count(fixed.begin(), fixed.end(), true));
    if (fixedCount == 0) {
        result.diagnostic = "Vertical equilibrium needs at least one fixed support vertex";
        return false;
    }
    if (!settings.supportHeights.empty() && settings.supportHeights.size() != result.formDiagram.vertices.size()) {
        result.diagnostic = "Support heights must contain one Z value per form vertex";
        return false;
    }
    if (!settings.nodalLoads.empty() && settings.nodalLoads.size() != result.formDiagram.vertices.size()) {
        result.diagnostic = "Nodal loads must contain one value per form vertex";
        return false;
    }
    if (!settings.thicknesses.empty() && settings.thicknesses.size() != result.formDiagram.vertices.size()) {
        result.diagnostic = "Thicknesses must contain one value per form vertex";
        return false;
    }

    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        if (fixed[vertex] && !settings.supportHeights.empty()) {
            result.formDiagram.vertices[vertex].position.z = settings.supportHeights[vertex];
        }
    }

    std::vector<int> freeVertices;
    std::vector<int> freeIndex(vertexCount, -1);
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        if (!fixed[vertex]) {
            freeIndex[vertex] = static_cast<int>(freeVertices.size());
            freeVertices.push_back(vertex);
        }
    }
    const float verticalScale = settings.forceScale;
    bool hasTension = false;
    result.forceDensities.resize(horizontal.forceDensities.size());
    for (int edge = 0; edge < static_cast<int>(horizontal.forceDensities.size()); ++edge) {
        result.forceDensities[edge] = horizontal.forceDensities[edge] * verticalScale;
        if (!std::isfinite(result.forceDensities[edge])) {
            result.diagnostic = "Vertical equilibrium contains nonfinite force densities";
            return false;
        }
        hasTension = hasTension || result.forceDensities[edge] < 0.0f;
    }

    std::vector<Eigen::Triplet<double>> coefficients;
    coefficients.reserve(horizontal.reciprocalFormEdges.size() * 4);
    Eigen::VectorXd fixedContribution = Eigen::VectorXd::Zero(static_cast<int>(freeVertices.size()));
    for (int edgeIndex = 0; edgeIndex < static_cast<int>(horizontal.reciprocalFormEdges.size()); ++edgeIndex) {
        const TnaEdge& edge = horizontal.reciprocalFormEdges[edgeIndex];
        if (!validVertex(result.formDiagram, edge.vertexA) || !validVertex(result.formDiagram, edge.vertexB)) {
            result.diagnostic = "Vertical equilibrium contains an invalid reciprocal form edge";
            return false;
        }
        const double q = result.forceDensities[edgeIndex];
        const int first = edge.vertexA;
        const int second = edge.vertexB;
        if (!fixed[first]) {
            const int row = freeIndex[first];
            coefficients.emplace_back(row, row, q);
            if (!fixed[second]) coefficients.emplace_back(row, freeIndex[second], -q);
            else fixedContribution[row] += q * result.formDiagram.vertices[second].position.z;
        }
        if (!fixed[second]) {
            const int row = freeIndex[second];
            coefficients.emplace_back(row, row, q);
            if (!fixed[first]) coefficients.emplace_back(row, freeIndex[first], -q);
            else fixedContribution[row] += q * result.formDiagram.vertices[first].position.z;
        }
    }

    Eigen::SparseMatrix<double> system(static_cast<int>(freeVertices.size()),
                                        static_cast<int>(freeVertices.size()));
    system.setFromTriplets(coefficients.begin(), coefficients.end());
    system.makeCompressed();
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> compressionSolver;
    Eigen::SparseLU<Eigen::SparseMatrix<double>> signedSolver;
    if (!freeVertices.empty()) {
        if (hasTension) signedSolver.compute(system);
        else compressionSolver.compute(system);
        if ((hasTension ? signedSolver.info() : compressionSolver.info()) != Eigen::Success) {
            result.diagnostic = "Vertical equilibrium factorization failed; check disconnected vertices, supports or cancelling signed densities";
            return false;
        }
    }

    const std::unordered_set<int> unloadedFaces(settings.unloadedFaces.begin(), settings.unloadedFaces.end());
    std::vector<float> baseLoads(vertexCount, settings.nodalLoad);
    if (!settings.nodalLoads.empty()) baseLoads = settings.nodalLoads;
    std::vector<float> thickness(vertexCount, settings.thickness);
    if (!settings.thicknesses.empty()) thickness = settings.thicknesses;

    const int maximumIterations = std::max(1, settings.maximumIterations);
    std::vector<float> loads(vertexCount, 0.0f);
    std::vector<float> residual(vertexCount, 0.0f);
    for (int iteration = 0; iteration < maximumIterations; ++iteration) {
        const std::vector<float> areas = tributaryAreas(result.formDiagram, unloadedFaces);
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            loads[vertex] = baseLoads[vertex] + areas[vertex] * thickness[vertex] * settings.density;
        }
        if (!finiteValues(loads)) {
            result.diagnostic = "Vertical equilibrium produced nonfinite loads";
            return false;
        }

        Eigen::VectorXd rightHandSide = fixedContribution;
        for (int row = 0; row < static_cast<int>(freeVertices.size()); ++row) {
            rightHandSide[row] += loads[freeVertices[row]];
        }
        Eigen::VectorXd freeHeights(freeVertices.size());
        if (!freeVertices.empty()) {
            if (hasTension) freeHeights = signedSolver.solve(rightHandSide);
            else freeHeights = compressionSolver.solve(rightHandSide);
            if ((hasTension ? signedSolver.info() : compressionSolver.info()) != Eigen::Success) {
                result.diagnostic = "Vertical equilibrium sparse solve failed";
                return false;
            }
            if (!freeHeights.allFinite() || !rightHandSide.allFinite()) {
                result.diagnostic = "Vertical equilibrium produced nonfinite heights or loads";
                return false;
            }
            const double relativeResidual = (system * freeHeights - rightHandSide).norm() /
                                            std::max(rightHandSide.norm(), 1e-30);
            if (!std::isfinite(relativeResidual) || relativeResidual > 1e-8) {
                result.diagnostic = "Vertical equilibrium linear residual exceeds 1e-8";
                return false;
            }
        }
        for (int row = 0; row < static_cast<int>(freeVertices.size()); ++row) {
            const float height = static_cast<float>(freeHeights[row]);
            if (!std::isfinite(height)) {
                result.diagnostic = "Vertical equilibrium height exceeds coordinate precision";
                return false;
            }
            result.formDiagram.vertices[freeVertices[row]].position.z = height;
        }

        // COMPAS updates self-weight after the new geometry and measures the
        // free-node residual against those updated loads.
        const std::vector<float> updatedAreas = tributaryAreas(result.formDiagram, unloadedFaces);
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            loads[vertex] = baseLoads[vertex] + updatedAreas[vertex] * thickness[vertex] * settings.density;
            residual[vertex] = -loads[vertex];
        }
        for (int edgeIndex = 0; edgeIndex < static_cast<int>(horizontal.reciprocalFormEdges.size()); ++edgeIndex) {
            const TnaEdge& edge = horizontal.reciprocalFormEdges[edgeIndex];
            const float q = result.forceDensities[edgeIndex];
            const float contribution = q * (result.formDiagram.vertices[edge.vertexA].position.z -
                                            result.formDiagram.vertices[edge.vertexB].position.z);
            residual[edge.vertexA] += contribution;
            residual[edge.vertexB] -= contribution;
        }
        double residualSquared = 0.0;
        if (!finiteValues(loads) || !finiteValues(residual)) {
            result.diagnostic = "Vertical equilibrium produced nonfinite loads or reactions";
            return false;
        }
        for (const int vertex : freeVertices) residualSquared += double(residual[vertex]) * residual[vertex];
        result.residual = static_cast<float>(std::sqrt(residualSquared));
        result.iteration = iteration + 1;
        if (result.residual <= settings.residualTolerance) {
            result.converged = true;
            break;
        }
    }

    result.verticalLoads = std::move(loads);
    result.verticalReactions = std::move(residual);
    result.formDiagram.calculateNormals();
    result.formDiagram.triangulationDirty = true;
    result.success = result.converged;
    std::ostringstream status;
    status << "TNA vertical equilibrium " << (result.converged ? "converged" : "did not converge within iteration limit")
           << ": iteration " << result.iteration << ", free residual " << result.residual;
    result.diagnostic = status.str();
    return result.success;
}

void TnaSolver::stepHorizontalEquilibrium(const TnaHorizontalSettings& settings) {
    TnaHorizontalEquilibrium& state = m_horizontal;
    if (!state.success) return;

    const float requestedAlpha = std::clamp(settings.formWeight, 0.0f, 1.0f);
    const int requestedIterations = std::max(1, settings.maximumIterations);
    if (state.iterationLimit == 0) state.iterationLimit = requestedIterations;
    const int maximumIterations = state.iterationLimit;
    // horizontal_nodal creates targets once, before either diagram moves.
    // The only reset path in the sketch is OBJ reload, so retain that target
    // field if controls change after a solve has begun.
    const float alpha = state.targetFormWeight >= 0.0f ? state.targetFormWeight : requestedAlpha;
    if (state.targetFormWeight >= 0.0f && std::abs(state.targetFormWeight - requestedAlpha) > 1e-6f) {
        state.diagnostic = "Form weight is fixed for the current solve; press r to reload and use a new weight";
        return;
    }
    const bool formCompleteAtLimit = alpha >= 1.0f || state.formIterations >= maximumIterations;
    const bool forceCompleteAtLimit = alpha <= 0.0f || state.forceIterations >= maximumIterations;
    if (state.converged) {
        if (formCompleteAtLimit && forceCompleteAtLimit) return;
        state.converged = false;
        state.satisfiesAngleTarget = false;
    }

    if (state.targetFormWeight < 0.0f) {
        if (!settings.edgeConstraints.empty() &&
            settings.edgeConstraints.size() != state.forceDiagram.edges.size()) {
            state.converged = true;
            state.diagnostic = "Horizontal edge constraints must have one item per reciprocal form/force pair";
            return;
        }
        state.edgeConstraints = settings.edgeConstraints;
        if (state.edgeConstraints.empty()) {
            state.edgeConstraints.resize(state.forceDiagram.edges.size());
        }
        for (std::size_t i = 0; i < state.edgeConstraints.size(); ++i) {
            state.edgeConstraints[i].isTension = state.edgeConstraints[i].isTension ||
                                                state.reciprocalFormEdges[i].isTension;
        }
        state.forceScale = std::max(settings.forceScale, 1e-6f);
        state.horizontalTargets = makeHorizontalTargets(state, alpha);
        state.targetFormWeight = alpha;
    }
    const std::vector<Vec3>& targets = state.horizontalTargets;

    std::vector<float> formMinimumLengths;
    std::vector<float> formMaximumLengths;
    std::vector<float> forceMinimumLengths;
    std::vector<float> forceMaximumLengths;
    formMinimumLengths.reserve(state.edgeConstraints.size());
    formMaximumLengths.reserve(state.edgeConstraints.size());
    forceMinimumLengths.reserve(state.edgeConstraints.size());
    forceMaximumLengths.reserve(state.edgeConstraints.size());
    for (const TnaHorizontalSettings::EdgeConstraint& constraint : state.edgeConstraints) {
        // The sequence is intentional: it is exactly horizontal_nodal's
        // lmin/lmax and hmin/hmax combination before parallelise_edges.
        formMinimumLengths.push_back(constraint.formLengthMinimum);
        formMaximumLengths.push_back(constraint.formLengthMaximum);
        forceMinimumLengths.push_back(
            std::max(constraint.horizontalForceMinimum / state.forceScale,
                     constraint.forceLengthMinimum));
        forceMaximumLengths.push_back(
            std::min(constraint.horizontalForceMaximum / state.forceScale,
                     constraint.forceLengthMaximum));
    }

    std::vector<bool> fixedForm(state.formDiagram.vertices.size(), false);
    for (const int vertex : state.fixedFormVertices) {
        if (validVertex(state.formDiagram, vertex)) fixedForm[vertex] = true;
    }
    std::vector<bool> fixedForce(state.forceDiagram.vertices.size(), false);
    for (const int vertex : state.fixedForceVertices) {
        if (validVertex(state.forceDiagram, vertex)) fixedForce[vertex] = true;
    }
    bool performedIteration = false;
    if (!state.solvingForceDiagram && alpha < 1.0f && state.formIterations < maximumIterations) {
        std::vector<Vec3> formCoordinates;
        formCoordinates.reserve(state.formDiagram.vertices.size());
        for (const MeshVertex& vertex : state.formDiagram.vertices) formCoordinates.push_back(vertex.position);
        paralleliseEdges(formCoordinates, state.reciprocalFormEdges, targets, fixedForm,
                         formMinimumLengths, formMaximumLengths);
        for (int vertex = 0; vertex < static_cast<int>(state.formDiagram.vertices.size()); ++vertex) {
            state.formDiagram.vertices[vertex].position = formCoordinates[vertex];
        }
        ++state.formIterations;
        performedIteration = true;
    }
    if (!state.solvingForceDiagram && (alpha >= 1.0f || state.formIterations >= maximumIterations)) {
        state.solvingForceDiagram = true;
    }
    if (!performedIteration && state.solvingForceDiagram && alpha > 0.0f &&
        state.forceIterations < maximumIterations) {
        std::vector<Vec3> rotatedForce = rotateForceCounterClockwise(state.forceDiagram);
        std::vector<TnaEdge> forceEdges;
        forceEdges.reserve(state.forceDiagram.edges.size());
        for (const MeshEdge& edge : state.forceDiagram.edges) forceEdges.push_back({edge.vertexA, edge.vertexB});
        paralleliseEdges(rotatedForce, forceEdges, targets, fixedForce,
                         forceMinimumLengths, forceMaximumLengths);
        rotateForceClockwise(state.forceDiagram, rotatedForce);
        ++state.forceIterations;
        performedIteration = true;
    }

    updateHorizontalAngles(state);
    state.formDiagram.calculateNormals();
    state.formDiagram.triangulationDirty = true;
    state.forceDensities.clear();
    state.forceDensities.reserve(state.forceDiagram.edges.size());
    for (int edgeIndex = 0; edgeIndex < static_cast<int>(state.forceDiagram.edges.size()); ++edgeIndex) {
        const MeshEdge& forceEdge = state.forceDiagram.edges[edgeIndex];
        const TnaEdge formEdge = state.reciprocalFormEdges[edgeIndex];
        const float forceLength = (state.forceDiagram.vertices[forceEdge.vertexB].position -
                                   state.forceDiagram.vertices[forceEdge.vertexA].position).length();
        const float formLength = (state.formDiagram.vertices[formEdge.vertexB].position -
                                  state.formDiagram.vertices[formEdge.vertexA].position).length();
        const float sign = edgeIndex < static_cast<int>(state.edgeConstraints.size()) &&
                                   state.edgeConstraints[edgeIndex].isTension
                               ? -1.0f
                               : 1.0f;
        // After rotating the force diagram back, reciprocal edge lengths
        // provide the horizontal force density used by vertical equilibrium:
        //
        //     q_e = sign_e * scale * l_force,e / l_form,e .
        state.forceDensities.push_back(formLength > 1e-6f
                                           ? sign * state.forceScale * forceLength / formLength
                                           : 0.0f);
    }
    const bool formComplete = alpha >= 1.0f || state.formIterations >= maximumIterations;
    const bool forceComplete = alpha <= 0.0f || state.forceIterations >= maximumIterations;
    if (formComplete && forceComplete) {
        state.converged = true;
        state.satisfiesAngleTarget =
            std::isfinite(state.maximumDirectionDeviation) &&
            state.maximumDirectionDeviation <= std::max(0.0f, settings.angleToleranceDegrees);
    }

    std::ostringstream status;
    const char* phase = !state.converged ? "running"
                      : state.satisfiesAngleTarget ? "solved"
                                                   : "iteration limit reached";
    status << "TNA horizontal equilibrium " << phase
           << ": form " << state.formIterations << "/" << (alpha < 1.0f ? maximumIterations : 0)
           << ", force " << state.forceIterations << "/" << (alpha > 0.0f ? maximumIterations : 0)
           << ", max direction deviation " << state.maximumDirectionDeviation << " deg";
    state.diagnostic = status.str();
}

} // namespace alice2
