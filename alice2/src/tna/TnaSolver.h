#pragma once

#ifndef ALICE2_TNA_SOLVER_H
#define ALICE2_TNA_SOLVER_H

#include "../objects/MeshObject.h"

#include <string>
#include <vector>

namespace alice2 {

struct TnaEdge {
    int vertexA{-1};
    int vertexB{-1};
    // COMPAS FormDiagram edge attribute _is_tension.
    bool isTension{false};
};

// Stage 1 of TNA: the planar input mesh with one exterior polygon appended
// for every support-delimited boundary chain. The recorded chains will later
// provide the exterior faces needed by the dual force diagram.
struct TnaFormDiagram {
    bool success{false};
    std::string diagnostic;

    MeshData mesh;
    // Source vertex IDs accepted as supports. If the caller supplies none,
    // all topological boundary vertices are used.
    std::vector<int> supportVertices;
    // One chain per support-to-support boundary walk, inclusive of both
    // support endpoints. A boundary loop without supports is one group.
    std::vector<std::vector<int>> boundaryGroups;
    // Original boundary edges, including hole loops and their closing edges,
    // recorded before exterior faces change the boundary topology.
    std::vector<TnaEdge> boundaryEdges;
    // Original form edges participating in equilibrium. The closing chord of
    // every appended outside face is intentionally excluded.
    std::vector<TnaEdge> activeFormEdges;
    std::vector<int> exteriorFormFaces;
    int appendedBoundaryFaces{0};
    int skippedDegenerateGroups{0};
};

// Stage 2 of TNA: the reciprocal dual of the completed form topology.
struct TnaForceDiagram {
    bool success{false};
    std::string diagnostic;

    MeshData mesh;
    // Maps force vertex ID to its source form-face ID.
    std::vector<int> forceVertexFormFaces;
    // Form endpoints corresponding to each force edge. This avoids relying on
    // the implementation-specific ordering of the form mesh edge list.
    std::vector<TnaEdge> reciprocalFormEdges;
    // Optional force-diagram constraints. COMPAS leaves this empty unless a
    // caller explicitly adds force constraints.
    std::vector<int> fixedForceVertices;
    // Undirected angle between each force edge and its corresponding form
    // edge, in degrees. Displayed reciprocal diagrams target 90 degrees.
    std::vector<float> edgeAnglesDegrees;
    // Signed reciprocal-direction error, 0 = correct, 180 = reversed.
    // Infinity denotes a degenerate or invalid reciprocal pair.
    std::vector<float> edgeDirectionDeviationsDegrees;
};

struct TnaHorizontalSettings {
    // COMPAS-TNA alpha remapped to [0, 1]. 0 uses force-edge directions as
    // targets, 1 uses form-edge directions, and intermediate values blend
    // the two unit directions before parallelisation.
    float formWeight{1.0f};
    // Reciprocity diagnostic threshold. Like COMPAS horizontal_nodal, the
    // solve still completes its configured iteration count.
    float angleToleranceDegrees{3.0f};
    // This is kmax in COMPAS-TNA. Each applicable diagram receives this
    // many Jacobi parallelisation iterations.
    int maximumIterations{100};
    // COMPAS ForceDiagram.attributes["scale"]. It converts force-diagram
    // edge length into horizontal force for q = scale * l_force / l_form.
    float forceScale{1.0f};

    struct EdgeConstraint {
        // FormDiagram edge defaults in COMPAS-TNA.
        float formLengthMinimum{0.0f};
        float formLengthMaximum{1e7f};
        float horizontalForceMinimum{0.0f};
        float horizontalForceMaximum{1e7f};
        // ForceDiagram edge defaults in COMPAS-TNA.
        float forceLengthMinimum{0.0f};
        float forceLengthMaximum{1e7f};
        bool isTension{false};
    };
    // One item per ordered reciprocal pair. Empty means the COMPAS defaults
    // above are applied to every pair. Tension is enabled when either the
    // form edge or this explicit constraint marks the pair as tension.
    std::vector<EdgeConstraint> edgeConstraints;
};

struct TnaHorizontalEquilibrium {
    bool success{false};
    // The configured form/force passes have completed.
    bool converged{false};
    // Every nondegenerate reciprocal pair meets angleToleranceDegrees with
    // the correct compression/tension direction.
    bool satisfiesAngleTarget{false};
    std::string diagnostic;

    MeshData formDiagram;
    MeshData forceDiagram;
    std::vector<TnaEdge> reciprocalFormEdges;
    std::vector<int> fixedFormVertices;
    std::vector<int> fixedForceVertices;
    std::vector<float> edgeAnglesDegrees;
    std::vector<float> edgeDirectionDeviationsDegrees;
    std::vector<float> forceDensities;
    // The COMPAS horizontal solver forms these weighted directions once,
    // then parallelises both diagrams towards the same targets.
    std::vector<Vec3> horizontalTargets;
    std::vector<TnaHorizontalSettings::EdgeConstraint> edgeConstraints;
    float targetFormWeight{-1.0f};
    float forceScale{1.0f};
    int formIterations{0};
    int forceIterations{0};
    // Cumulative iteration limit. Each press of the sketch's h key extends
    // this limit by one user-selected batch.
    int iterationLimit{0};
    // COMPAS completes the form pass before starting the force pass.
    bool solvingForceDiagram{false};
    float maximumAngleDeviation{0.0f};
    float maximumDirectionDeviation{0.0f};
};

struct TnaVerticalSettings {
    // Matches COMPAS vertical_from_q. q is already derived from the solved
    // form/force pair and is multiplied by this horizontal-force scale.
    float forceScale{1.0f};
    // Extra vertical load at every vertex when nodalLoads is empty. The sign
    // follows the input coordinate convention.
    float nodalLoad{0.0f};
    std::vector<float> nodalLoads;
    // COMPAS self-weight is tributaryArea * thickness * density. Supply a
    // zero density to solve only the specified nodal loads.
    float density{1.0f};
    float thickness{1.0f};
    std::vector<float> thicknesses;
    // Original vertex Z values. Only entries at support vertices are used;
    // this keeps the supports at their pre-flattening elevations.
    std::vector<float> supportHeights;
    // Vertices constrained only during vertical equilibrium. Horizontal
    // fixed vertices are always included as vertical supports as well.
    std::vector<int> additionalFixedVertices;
    // Added outside faces are topology-only and must not contribute load.
    std::vector<int> unloadedFaces;
    int maximumIterations{100};
    float residualTolerance{1e-3f};
};

struct TnaVerticalEquilibrium {
    // True only when equilibrium converges with finite results.
    bool success{false};
    bool converged{false};
    std::string diagnostic;

    MeshData formDiagram;
    std::vector<float> forceDensities;
    std::vector<float> verticalLoads;
    std::vector<float> verticalReactions;
    int iteration{0};
    float residual{0.0f};
};

class TnaSolver {
public:
    TnaFormDiagram makeFormDiagram(const MeshData& input,
                                   const std::vector<int>& supportVertices = {}) const;

    TnaForceDiagram makeForceDiagram(const TnaFormDiagram& formDiagram) const;

    // Call after makeFormDiagram and before makeForceDiagram. Match active
    // edges by unordered endpoints; reject invalid selections atomically.
    // Rebuild the force diagram and reset equilibrium after changing marks.
    bool setTensionEdges(TnaFormDiagram& form,
                         const std::vector<TnaEdge>& edges,
                         bool isTension = true) const;
    // Mark active original boundary edges on every loop, including holes.
    // Inactive support-to-support edges and appended chords are excluded.
    bool setBoundaryEdgesTension(TnaFormDiagram& form,
                                 bool isTension = true) const;
    // Walk the original boundary loop containing an outer-ring support.
    // -1 uses the first support; the caller must choose an outer-ring support
    // if supports also lie on holes. Other loops and interior marks are kept.
    bool setOuterBoundaryEdgesTension(TnaFormDiagram& form,
                                      int supportVertex = -1,
                                      bool isTension = true) const;

    bool resetHorizontalEquilibrium(const MeshData& formDiagram,
                                    const TnaForceDiagram& forceDiagram,
                                    const std::vector<int>& fixedFormVertices);
    bool continueHorizontalEquilibrium(int additionalIterations);
    void stepHorizontalEquilibrium(const TnaHorizontalSettings& settings);
    const TnaHorizontalEquilibrium& horizontalEquilibrium() const { return m_horizontal; }

    bool solveVerticalEquilibrium(const TnaVerticalSettings& settings);
    const TnaVerticalEquilibrium& verticalEquilibrium() const { return m_vertical; }

private:
    TnaHorizontalEquilibrium m_horizontal;
    TnaVerticalEquilibrium m_vertical;
};

} // namespace alice2

#endif // ALICE2_TNA_SOLVER_H
