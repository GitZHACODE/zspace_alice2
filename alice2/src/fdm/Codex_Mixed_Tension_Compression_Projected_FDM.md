# Codex implementation specification: Projected Force-Density Equilibrium for Mixed Compression–Tension Networks

## 0. Mission

Implement a **new, separate solver** for form-finding an arbitrary 3D bar/cable graph with both compression and tension, retaining the supplied geometry as closely as possible while satisfying static equilibrium.

Use the central workflow of Bhooshan, Van Mele & Block (2015), *Discrete funicular structures with curve-folded moulds*: **project geometry toward design targets → compute residual nodal forces → correct force densities by linear least squares → recover geometry with an FDM equilibrium solve → repeat**.

**Do not simply reproduce the paper's compression-only implementation.** Its positive-density hypothesis and the resulting invertibility/stability assumptions do not extend automatically to signed force-density matrices. The mixed-sign extension, shape retention, sparse numerical safeguards, and acceptance logic below are a **proposed new implementation**, not claims made in the paper.

Keep the current solver untouched for A/B comparison. Implement the new solver in independent classes/files with a clean public API. If working in an existing C++ repository, reuse existing mesh visualization, test geometries, slider UI and infrastructure where reasonable, but do not let the old nonlinear solver dictate the new algorithm.

### Success criteria

1. User provides nodes, edges, supports, per-edge **Tension** or **Compression**, external loads, and an input geometry.
2. Cable/tension edges may be **offset from the compression mesh** and linked by discrete connector edges. Do **not** assume all edges belong to a manifold mesh, a common surface, or a triangle topology.
3. Return free-node coordinates `X`, per-edge signed densities `q`, nodal residual forces, validity diagnostics and convergence history.
4. Prioritize actual force equilibrium, exact edge sign assignments, stable numerical solves, then proximity to the input geometry.
5. Every accepted iterate must be assessed using **actual residuals recomputed after the full FDM solve**, not just the projected least-squares residual.
6. Demonstrate whether this approach improves scalability relative to coupled Gauss–Newton/SparseQR; do not assert a speedup without benchmarks.

---

## 1. Conventions and physics (make these explicit in code)

Let `n` be the number of nodes and `m` the number of **structural** edges (compression edges, tension cables and connectors all included).

- `X ∈ R^(n×3)` is the current 3D node position matrix; `X0` is the immutable input geometry.
- `C ∈ R^(m×n)` is the oriented edge-node incidence matrix. Edge `e=(i,j)` has one `+1` and one `-1`; either orientation is fine provided it is consistent.
- `q ∈ R^m` contains signed force densities, each equal to axial force divided by current edge length.
- Adopt **`q > 0` for tension** and **`q < 0` for compression**. If existing repository conventions differ, convert at the API boundary; do not silently mix them.
- `Q = diag(q)`; `L(q) = Cᵀ Q C`.
- Define structural equilibrium as `L(q) X = P`, where `P` denotes applied nodal loads in the matching sign convention. Reaction forces at fixed degrees of freedom are not prescribed.
- For each free node, the equilibrium residual is `r = p_f − A_f(X) q`, equivalently `r = P_f − [L(q)X]_f`. Here `A_f(X)` is the geometry-dependent equilibrium matrix defined so that `A_f(X)q = [L(q)X]_f` with the free DOF rows stacked across x, y, z.
- Support constraints are **per coordinate DOF**, not necessarily whole fixed nodes. Some support nodes may be fixed in x/y and free in z, etc.

**Important:** For zero external load, setting all `q=0` trivially satisfies equilibrium but produces no meaningful self-stress. Prevent this degeneracy using strict nonzero sign-specific bounds and force-scale normalization/anchoring. For zero-load self-stress also use an explicit force scale, such as the mean absolute density, a reference cable density, or a selected edge density.

Edge definitions:

```cpp
enum class MemberType { Tension, Compression };
struct Edge { int i, j; MemberType type; double targetDensityMagnitude; };
struct Node { Eigen::Vector3d x0; Eigen::Vector3d load; bool fixed[3]; };
```

Treat vertical connectors as ordinary, independently typed edges. Do not infer connector type from geometric orientation. If the application needs zero-force or bi-directional bars later, design that as a separate explicit member mode; **do not accidentally relax sign constraints**.

## 2. Two essential operators

### 2.1 Equilibrium matrix at fixed geometry

For edge `e=(i,j)` and `d_e = X_i − X_j`, its contribution to `L(q)X` is:

- at node `i`: `+q_e d_e`;
- at node `j`: `−q_e d_e`.

Only include the free coordinate rows. Assemble `A_f(X)` as a sparse `(number_of_free_DOF × m)` matrix. Then the residual is exactly

`r(X,q) = p_f − A_f(X) q`.

Make the ordering of rows unambiguous and test `A_f(X)q == pack_free(L(q)X)` numerically for many random instances.

### 2.2 FDM geometry recovery for fixed signed q

Partition `L(q)` by free and fixed coordinate DOFs. Since `L` acts identically on x/y/z but supports may be partial, use a general sparse constrained system or solve each coordinate with its own free/fixed partition:

`L_FF(q) X_F = P_F − L_FC(q) X_C`.

For fixed `q` and geometry-independent `P`, this is linear in free positions. A signed density matrix need not be positive definite; **do not use a Cholesky factorization that assumes SPD**. Prefer sparse pivoting LU (e.g. Eigen `SparseLU`) for the initial implementation and explicitly handle numerical failure / near-singularity. A verified symmetric-indefinite solver is optional.

Use residual checks after factorization. If singular, near singular or severely ill-conditioned, **reject the candidate** rather than silently treating a damped solution as exact equilibrium. Optional regularized solves can be used for diagnostics or warm starts, but mark them as approximate; `L+λI` is not the original equilibrium equation.

When loads depend on the current geometry (for example tributary-area self-weight), run an **inner fixed-point loop**: recompute `P(X)` and solve `L(q)X = P(X)` until both geometry and force residual stabilize. Use relaxation if needed; document that convergence is not guaranteed.

---

## 3. Algorithm: projection-guided signed-density correction

This section adapts the *structure* of Bhooshan's method. Be very strict about residual signs and units.

### 3.1 Initialization

- Validate topology, duplicate and zero-length edges, finite inputs, connectivity to supports and obvious unconstrained rigid motions.
- Build explicit free-DOF mapping.
- Initialize `q` with correct signs and nonzero magnitudes. Either use input values or solve a **sign-constrained, regularized** fixed-geometry least-squares problem:

  `min_q  1/2 ||W_r (A_f(X0)q − p_f)||² + (λ_q/2)||W_q(q−q_ref)||²`

  subject to `q_e ≥ qMin_e` for tension and `q_e ≤ −qMin_e` for compression; apply upper bounds if supplied.

- `qMin_e > 0` is chosen with a physically meaningful scale. Consider scale-normalized variables `s_e = q_e / qScale_e` for numerical stability.
- Preserve `X0` as the immutable design reference. If zero load, ensure normalization eliminates the zero-density collapse; optionally allow a fixed reference density as an equality constraint.
- Attempt an FDM solve for the initial `q`. If it fails, try alternate feasible initial densities/continuation; **report infeasibility if no valid state is found** rather than fabricating convergence.

### 3.2 Project geometry toward the design intent

At each outer iteration, start from an equilibrium position `Xeq` obtained in the previous FDM solve.

Compute a **temporary projected geometry** `Xp` that gently moves *free* coordinates toward `X0`:

`Xp = Xeq + α_i * W_position * (X0 − Xeq)`

with fixed DOFs unchanged and `0 < α_i ≤ 1`. `W_position` is a diagonal or per-DOF weighting (clamp in `[0,1]` initially). Other objectives such as planarisation, reference-surface adherence or cable clearance can be separate projector modules, but start with reference-geometry attraction only.

`Xp` is **not claimed to be in equilibrium**; it is a candidate design state used to determine the force-density correction.

### 3.3 Compute projected residual and target force

Assemble `A = A_f(Xp)` and the new projected load `p = pack_free(P(Xp))`. Define

`r = p − A q`  (units: force).

Bhooshan additionally combines residual force and a per-node design target vector. For our first implementation, choose **one**, not two simultaneously overlapping geometry attractions:

- **Default/simple mode:** Set `t = 0`, because `Xp` already incorporates shape attraction. Solve for densities that best equilibrate `Xp`.
- **Optional guided-force mode:** Define `t = K_target * pack_free(X0 − Xp)` in force units, with a documented user-controlled stiffness and appropriate sign. This is a *new design heuristic*, not a derived physical load or an equation guaranteed by the paper. Do not enable it by default; measure its effect separately.

Let `g = r + t`. Notice that with `t=0`, solving `A Δq ≈ g` means `A(q+Δq) ≈ p`. Using `g=Aq−p` with the same update sign would be incorrect.

### 3.4 Sparse sign-constrained correction solve

Compute a small, regularized correction in **density space only**:

`min_Δq  1/2 ||W_r(A Δq − g)||² + (λ_step/2)||W_step Δq||²`

subject to:

- `q_min_e ≤ q_e + Δq_e ≤ q_max_e` (tension),
- `−q_max_e ≤ q_e + Δq_e ≤ −q_min_e` (compression),
- optional `|Δq_e| ≤ trust_e`,
- optional density-scale normalization / reference-edge anchoring.

For ease of bounds handling, optimize over `q_new` instead:

`min_qnew  1/2 ||W_r(A qnew − (p+t))||² + (λ_step/2)||W_step(qnew−q)||²`.

This is a **convex box-constrained quadratic program in q_new at fixed Xp**, even though the full mixed-sign form-finding problem is nonconvex. Implement a sparse bound-constrained least-squares method (projected gradient with line search, L-BFGS-B, or active set); avoid dense matrix inverse and avoid assuming a particular QR backend. For a first robust baseline, an active-set / projected-gradient method with sparse `A*v` and `Aᵀ*w` is sufficient.

Do not form `(AᵀA)^{-1}`. Bhooshan writes the unbounded normal-equation form, but explicit inversion is numerically undesirable, and we require sign bounds.

### 3.5 Force-density continuation and FDM recovery

Do not blindly replace densities with the full optimization output. Use a backtracking fraction `β ∈ (0,1]`:

`q_trial = q + β*(q_new − q)`.

Every candidate remains feasible under convex box constraints. For each candidate:

1. Check sign constraints and density bounds.
2. Solve the **full signed FDM** equilibrium for `Xtrial` with **original support coordinates** and physical loads.
3. Recompute loads if geometry dependent.
4. Compute the actual force residual `Rtrial = ||pack_free(P(Xtrial) − L(q_trial)Xtrial)||`.
5. Evaluate `Eshape = Σ_i w_i ||Xtrial_i − X0_i||²`, plus optional penalties for extreme edge lengths, force magnitudes and geometric collisions. Check edge collapse and invertibility.
6. Accept only if the equilibrium solve is numerically valid AND the shape/regularization merit function shows meaningful improvement within the prescribed acceptance rule; otherwise reduce `β` and retry. If no step is accepted, reduce the projection step `α_i`, strengthen regularization or report stagnation.

Crucial: since a *valid FDM solve already drives the force residual nearly to zero*, the outer merit is predominantly shape proximity and optional design objectives. Do not use equilibrium residual decrease alone as your outer acceptance criterion: that cannot distinguish good geometries among equilibrated configurations.

For signed networks, FDM may have mathematically valid yet unstable/undesirable equilibrium solutions. Report and optionally flag negative eigenvalue directions of the constrained tangent/stiffness matrix `L_FF(q)` as a **stability heuristic only**; this graph-level linearization is not a full material/geometric buckling analysis. Do not claim physical stability from small equilibrium residual alone.

### 3.6 Outer convergence

Stop when all applicable tests pass:

- actual equilibrium residual `||r|| / max(1,||p||,Fscale) < tolForce`;
- accepted relative shape-energy improvement below `tolShape` for several steps;
- density step below `tolQ` or legitimate constrained optimum / stalled trust region;
- no bounds, sign or solver validity violations.

Also stop on iteration/time limit with an honest reason (`converged`, `stagnated`, `singular`, `nonfinite`, `infeasible`, `max_iterations`).

**If a proposed q adjustment changes nothing:** do not assume convergence. The attraction direction may be inaccessible due to equilibrium geometry or active bounds. Report the projected residual, density-gradient norm, active constraints and shape discrepancy.

---

## 4. Critical issue: feasibility and self-stress in mixed-sign systems

A mixed compression/tension network can have `L_FF(q)` singular, indefinite or poorly conditioned. The presence of supports does not automatically prevent this. In zero-load situations, nontrivial self-stress at the exact input geometry requires

`A_f(X0) q = 0`

with nonzero `q` obeying specified signs and bounds. This might be impossible. In general, **there may not exist a near-input equilibrium geometry for the chosen topology and support conditions**.

Build a diagnostic *fixed-geometry* feasibility mode before starting outer iterations:

`min_q ||A_f(X0)q−p_f||`, with sign constraints and scale anchoring.

Return the minimum residual, feasible magnitudes, number of active bounds and the rank/conditioning estimate. This gives the user a physically meaningful indication of whether the given geometry is even near an admissible force-density self-stress. Do not conflate this result with full spatial form-finding.

If the graph has multiple independent components or floppy mechanisms, either handle them explicitly with support-aware component checks or reject them with a detailed diagnostic. **Never** add artificial support constraints without informing the caller.

### Recommended continuation (optional second milestone)

If direct signed FDM repeatedly fails, introduce a temporary anchor to a known geometrical state or a homotopy in loading/force magnitudes, reducing that anchor only once unregularized equilibrium is solvable. Such temporary regularization is a numerical path-finding device and must not be reported as genuine force equilibrium until removed. A generic interpolation from compression-only to mixed signs is **not** guaranteed to preserve nonsingularity.

---

## 5. Architecture (C++/Eigen proposal)

Implement small testable modules rather than one long solver function:

- `StructuralGraph`: arbitrary vertex/edge data, member type, fixed DOF masks, and loads.
- `DofMap`: free/fixed coordinate indexing, scatter/gather.
- `EquilibriumAssembly`: build signed sparse `L(q)`, sparse `A_f(X)`, load packs and residuals.
- `SignedFDMSolver`: sparse linear equilibrium recovery, factorization and residual validation.
- `DensityCorrectionSolver`: bounded sparse least-squares solve for `q_new`.
- `GeometryProjector`: reference attraction, support preservation; future pluggable constraints.
- `ProjectedMixedFDM`: outer iteration, line search, acceptance and history.
- `SolverDiagnostics`: residual components, conditioning, active bounds, geometry distance, runtime and failure codes.

Suggested main API:

```cpp
struct SolverOptions {
    int maxOuterIterations = 150;
    int maxBacktracks = 12;
    double positionProjection = 0.15;
    double minDensityMagnitude = 1e-3; // scaled to problem units; not universal
    double maxDensityMagnitude = 1e3; // scaled to problem units; not universal
    double correctionRegularization = 1e-3;
    double initialDensityTrustFraction = 0.25;
    double relativeForceTolerance = 1e-8;
    double relativeShapeTolerance = 1e-7;
    double relativeDensityTolerance = 1e-7;
    bool useTargetForce = false;  // default OFF
};
struct IterationRecord {
    int iteration;
    double shapeError, equilibriumResidual, relativeDensityStep;
    double minAbsDensity, maxAbsDensity, fdmSolveMillis, correctionMillis;
    int activeBounds, rejectedTrials;
};
struct SolverResult {
    Eigen::MatrixXd X; // n x 3
    Eigen::VectorXd q; // m
    std::vector<IterationRecord> history;
    std::string terminationReason;
    bool equilibriumValidated;
};
SolverResult solveProjectedMixedFDM(const StructuralGraph&, const SolverOptions&);
```

Names/paths can fit the repository style; keep semantics.

### Algorithm pseudocode

```text
validate_graph_supports_and_signs()
q = initialize_signed_bounded_densities(X0, loads)
X = signed_fdm_recover(q)       // must succeed, or emit a clear failure
record_actual_metrics(X, q)

for iteration = 0 .. maxOuterIterations-1:
    alpha = current_projection_amount
    accepted = false

    for projection_retry:
        Xp = project_toward_X0(X, alpha)     // temporary, not equilibrium
        A = equilibrium_matrix_free(Xp)
        p = loads_packed_for_free_dofs(Xp)
        r = p - A*q
        t = optional_target_force_or_zero(Xp, X0)
        q_candidate = solve_box_constrained_sparse_ls(A, p+t, q, trust, reg)

        for beta in [1, 1/2, 1/4, ...]:
            q_trial = q + beta*(q_candidate - q)
            if not signed_bounds_valid(q_trial): continue
            X_trial, valid = signed_fdm_recover(q_trial)
            if not valid: continue
            measure actual force residual and shape merit
            if physically_valid_and_acceptable(X_trial, q_trial, X, q):
                X, q = X_trial, q_trial
                accepted = true
                break

        if accepted: break
        alpha *= 0.5

    record iteration, convergence and failure diagnostics
    if accepted and stopping_criteria(): return result
    if not accepted: return stagnation/infeasibility result

return max_iterations result
```

If the starting FDM state is far from `X0`, trial shape objectives should be compared against that starting *equilibrium* state, not against the temporary projected geometry.

---

## 6. Test plan (must implement before calling this done)

### Assembly identity tests

1. Three-node chain with explicit supports: verify `A_f(X)q` equals the free rows of `L(q)X` for both tension and compression.
2. Random 3D graph with random signed densities and partial support masks: validate the same identity within numerical tolerance.
3. Check force dimensions, sign conventions, and invariance to reversing edge orientation.
4. Verify all accepted `q` strictly obey declared signs and magnitude bounds.

### Physical / numerical test cases

**T1 — Positive-density baseline:** Small tension network with fixed boundaries and loads; verify FDM recovery and compare to direct force-density equations.

**T2 — Mixed-sign offset-cable structure:** Compression network nodes on an arch/canopy, a *separate set of tension-cable nodes* offset from it, connected by discrete (e.g. vertical) members with explicit signs. Confirm every connector participates in `C`, `A`, `L` and force residual calculations; do not generate cable edges from the compression mesh or merge coincident-but-distinct nodes.

**T3 — Shape preservation:** Perturb known equilibrium geometry. With admissible signed `q`, test whether final shape error is improved relative to initialization while force residual remains below tolerance.

**T4 — Impossible sign pattern:** Construct a graph and loads with no sign-feasible self-stress within bounds. Verify informative failure/stagnation rather than NaN, silent sign changes or false convergence.

**T5 — Singular or mechanism case:** Test disconnected or inadequately supported geometry and intentionally singular `L_FF`. Verify explicit numerical failure diagnostics.

**T6 — Zero load:** Ensure the solver does not converge by allowing densities to go to zero; check fixed density scale/reference constraint.

**T7 — Geometry-dependent self-weight:** Verify inner load/geometry fixed-point loop against recomputed final nodal loads.

**T8 — Scalability:** Benchmark sparse assembly, density correction solve, signed FDM factorization, line-search attempts, memory, and total runtime for increasing network sizes. Compare final actual force residual, relative shape error and runtime against the existing coupled Gauss–Newton/SparseQR implementation on the *same* meshes, signs, loads and support constraints.

### Numerical safeguards

- Check all inputs/outputs for NaN/Inf.
- Check minimum member length and reject collapse.
- Check signed support-aware matrix invertibility, factorization success, numerical residual.
- Log exact reason for every failed candidate; never silently fall back to another physical model.
- Avoid dense normal equations and explicit inverse in production code.
- Distinguish force residual `||p−Aq||` from correction residual `||AΔq−g||` and final actual residual after FDM.
- Unit-scale tests: rescale input length/load/density consistently and verify stable behavior.

---

## 7. Milestones and Codex deliverables

**Milestone A — Fully testable kernels:** StructuralGraph, DofMap, signed force-density assembly, equilibrium matrix and direct signed FDM solve; unit tests for `Aq = pack(LX)`. No UI needed.

**Milestone B — Box-constrained density solver:** Iterative sparse solver for signed bounded `q`, scale/anchor mechanism and fixed-geometry feasibility diagnostic; tests for constraint satisfaction and optimality.

**Milestone C — Outer projected solver:** Projection toward `X0`, projected residual, `q` correction, backtracking candidate validation, shape objective and convergence history. Keep target-force mode disabled by default.

**Milestone D — Offset cable integration:** Connect arbitrary compression and tension subgraphs with connector edges; render per-edge densities and per-node actual residual vectors.

**Milestone E — Robustness and benchmarks:** Singular-system detection, scale normalization, geometry-dependent loads if already needed, benchmark versus current solver.

For each milestone: write compilation instructions and runnable tests, show actual performance numbers when available, and document unsolved cases. **Do not** mark a test as passed without running it.

## 8. Scientific framing / warnings for implementation

1. **From Bhooshan (2015):** project nodes to a geometry constraint, compute residual forces, add design target vectors, derive a least-squares force-density increment, update `q`, recover equilibrium positions via FDM, repeat; the reported application uses positive densities for compression-only structures (note the paper's sign convention need not match ours).
2. **This specification's extension:** opposite signs for tension/compression, an arbitrary graph including offset cables, box-constrained density updates, force-scale anchoring, sparse pivoting FDM solves, regularization, accepted-step checks and convergence diagnostics.
3. The **local correction subproblem** at fixed projected geometry is convex under the described bounds, but the overall geometry/density equilibrium design problem is still nonconvex.
4. **Equilibrium is not structural stability.** An indefinite `L_FF` can signal problems with the direct solver or quadratic energy stability; no assertion of full buckling resistance should be made from form finding alone.
5. The primary bottleneck may move from coupled SparseQR to repeated signed FDM factorizations and density optimization. Benchmark before optimizing further.

## 9. Relevant source sections

Bhooshan, Van Mele & Block, *Discrete funicular structures with curve-folded moulds*, IASS 2015:
- **Page 4:** FDM equilibrium solve and compression-only density formulation.
- **Pages 5–6, Section 3.2:** initial least-squares `q`; geometry projection; residual forces; target vectors; normal-equation density correction; density update; FDM equilibrium recovery.
- **Page 8, Section 3.4:** residual-force vs. target-vector negotiation.
- **Page 11, Section 4.3:** Forward Euler density updates can be unstable for larger networks.

Use the attached paper as the historical method description; follow the explicit numerical decisions above for this mixed-sign adaptation. When any ambiguity arises, prefer correctness, strict sign constraints, explicit failure reporting and a minimal working test over added solver complexity.
