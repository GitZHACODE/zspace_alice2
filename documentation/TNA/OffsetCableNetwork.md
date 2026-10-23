# Offset Cable Network (3D)

For paper preparation, see the [computational and mathematical reference](OffsetCableComputationalReference.md), including numbered equations, derivative derivations, seed details, algorithm pseudocode, numerical limitations and attribution boundaries.

Checked against the current sources on 2026-10-06, after restoring commit `1fffc12` ("before sparseQR"). The commit name is historical: this baseline already uses Eigen SparseQR for its coupled linear steps. It has a finite-difference Jacobian and rebuilds the QR factorization every iteration. The later analytic Jacobian, cached symbolic analysis, LSMR method, solver switch and offset timing command are absent from this revision.

The enabled sketch is `alice2/userSrc/bestfit/sketch_bestfit_tna_v2.cpp`, registered as **Offset Cable Network (3D)**. Its method is spatial equilibrium form-finding using signed force densities: it jointly adjusts member densities and free node coordinates. The older v1 two-stage fitting/TNA workflow is documented separately in [BestFitTna.md](BestFitTna.md); v1 is currently disabled in its source.

This method is related to 3D graphic statics through spatial force equilibrium. It does not construct a reciprocal force diagram or polyhedral force cells, and does not run a reciprocal TNA stage. Those diagram relationships distinguish the polyhedral graphic-statics approach; see the [Block Research Group overview](https://blockresearchgroup.github.io/compas_3gs/latest/overview/00_graphic_statics.html).

## Input and network construction

Input is `<working directory>/data/tna.obj`, with RGB vertex colors. Paths use `std::filesystem::current_path()`, so the application's launch directory determines the data folder. There is no hard-coded source-tree fallback. Export consistent face winding because winding determines normal direction.

Before building the network, `weldOffsetCableMesh` maps positions to a 0.001-unit grid using rounded coordinates. This is grid-key welding, rather than a nearest-neighbour distance search. The first position in a grid cell is retained; positions are not averaged. Black/red/yellow labels take precedence over unmarked copies, and conflicting constraint colors at one welded location are rejected. Faces are remapped and checked for collapse; IDs in the exported network refer to this welded mesh.

| Color / node role | Classification | Coordinate constraint | Members |
| --- | --- | --- | --- |
| Black | R/G/B <= 0.02 | Fixed XYZ | Original compression mesh |
| Red | R >= 0.8, G/B <= 0.2 | Fixed Z, free XY | Original compression mesh |
| Interior yellow | R/G >= 0.8, B <= 0.2 | Original and added nodes free XYZ | Compression at the original node; cables at its offset copy; tension connector between them |
| Boundary yellow | Same yellow threshold | Fixed Z, free XY in this sketch | Shared compression/cable anchor |
| Other | Unmarked mesh node | Free XYZ | Compression mesh |

The yellow classification alone does not create an offset node: the vertex must be an endpoint of at least one original face edge whose two endpoints are yellow. An isolated interior yellow vertex has no cable copy or connector. Boundary yellow vertices are classified as supports even if isolated from the selected cable edges.

`makeOffsetCableNetwork` counts undirected face edges. Incidence one marks a boundary, including boundaries around holes; incidence above two is rejected. Every distinct face edge becomes a compression member. Each edge with two yellow endpoints also creates a separate tension cable member. Branches of any valence are allowed. Repeated face vertices, invalid indices and degenerate face normals are rejected.

Face normals use the sum of cross products around all polygon corners. Vertex normals accumulate these unnormalized face vectors and are then normalized. Each selected interior yellow vertex gets a new node at `X_original + offset * normal`, plus a tension-only connector. An undefined interior normal is rejected. A selected boundary yellow vertex uses its original node as the cable endpoint, without a duplicate or zero-length connector. Two boundary yellow endpoints therefore have both a compression member and a separate, coincident cable member.

Member order is compression face edges, interior connectors, then cable segments. Original surface faces stay attached to original mesh nodes; cable copies do not acquire surface faces. The sketch passes `slideYellow=true`; the topology helper's default argument is false and is not the setting used by the viewer.

Offset is initial geometry. The solver does not enforce that separation, the initial normal direction, a connector orientation or any member's initial length during fitting. A connector labelled "vertical tension" in discussion is a normal-offset member initially and can become inclined.

## Controls and current defaults

| Control | Default | Slider range | Effect |
| --- | --- | --- | --- |
| Cable offset | -0.05 | -10 to -0.001 | Opposite the input vertex normal; resets network |
| Fit iterations | 50 | 1 to 1000 | Maximum coupled outer iterations, rounded to an integer |
| Design attraction | 1 | 0 to 2 | Position preference for ordinary free coordinates |
| Surface nodal load | 0.001 | 0 to 10 | Base vertical load on each original surface node |
| Surface weight density | 1 | 0 to 10 | Multiplier for tributary-area self-weight |
| Support force scale | 0.4 | 0 to 5 | Arrow display scale only |
| Sliding movement penalty | 0 | 0 to 500 | XY position preference at red supports |
| Yellow support penalty | 50 | 0 to 500 | XY position preference at yellow boundary supports |

Keys: `q` starts a fresh fit, `c` requests cancellation, `r` reloads, `f` frames, `e` exports JSON and OBJ, `d` toggles density labels/reaction arrows, and `i` toggles the gray original wireframe. There is no `m` solver switch in this revision. Pressing `q` again starts from a newly reset solver, rather than resuming the displayed iterate.

Changing any solver slider cancels the worker and resets the network/fit. Support force scale and the display toggles do not reset it. Blue lines show compression, pink show cable segments and orange show tension connectors. Node markers retain the support colors.

## Unknowns, signs and normalization

Let `N` be the node count (surface plus cable copies), `E` the member count, and `K` the number of free coordinate components. The unknown vector has `E + K` entries:

```text
x = [u_0 ... u_(E-1), y_0 ... y_(K-1)]
q_e = s_e * (q_min + exp(u_e))
s_e = +1 for compression, -1 for tension
q_min = 1e-4
X_(node,axis) = S * y_(node,axis)
S = max(1, maximum initial distance from the first node)
```

Thus member signs and the minimum density magnitude are maintained by parameterization, not by clipping after a solve. Fixed coordinates are copied from the reference geometry and excluded from the unknown vector. The initial log variables are zero, so densities start at approximately +1.0001 or -1.0001. Free coordinate variables initially reproduce the reference geometry.

Density is signed axial force divided by member length. Under this sketch's convention, `F_e = q_e * length_e`, with positive compression and negative tension. Density is not stress or cross-sectional area. The code has no built-in physical unit system: coordinates, loads, thickness, density and tolerances must be interpreted consistently. The minimum density and the normalization floor at one introduce numerical scales, so changing model units can change numerical behaviour.

## Loads, residuals and support reactions

For every evaluation, including finite differences and line-search trials, the current surface geometry determines tributary areas. For a face centroid `c`, a vertex receives a quarter of the cross-product magnitude from each of its two adjacent centroid-fan triangles:

```text
A_i += 0.25 * ||(X_next - X_i) cross (c - X_i)||
     + 0.25 * ||(X_previous - X_i) cross (c - X_i)||
p_i = base_i + density * thickness_i * A_i
```

These are 3D surface-area contributions, not projected XY areas. Thickness defaults to one; programmatic per-vertex thicknesses and unloaded-face exclusions are supported by the load settings. The v2 builder supplies the base nodal load only to original surface vertices, including supports. Cable copies have zero base load and no incident surface faces, so they have zero self-weight. No cable/bar material weight is added separately. Positive slider values enter the solver's Z-load term; the sign follows the existing coordinate/load convention.

The shared reporter constructs the algebraic residual at every node as:

```text
r_i = (0, 0, p_i) - sum_over_incident_members q_ij * (X_i - X_j)
```

The coupled residual vector `g(x)` contains XY components at every node, including black supports, and Z components only at nodes with free Z. With `B` black supports and `T` distinct fixed-Z sliding supports, this gives `R = 3*N - B - T` equilibrium rows and `K = 3*N - 3*B - T` free coordinate components for the viewer's disjoint support sets.

Black nodes stay fixed XYZ, but their horizontal residuals are explicitly required to vanish. Their vertical residual is carried as a support reaction. Red and boundary-yellow supports have free XY equilibrium equations and fixed Z; only their Z reaction is available. There is no physical horizontal spring at either type of sliding support. Setting a movement penalty changes a design objective; it never subtracts that penalty from the measured physical residual.

The exported/reported support reaction is `-r_i` on constrained components; unconstrained reaction components are zero. This is the code's convention used by the green arrows. `force_on_support` is its opposite. At an unfinished sliding-support iterate, the horizontal reaction still displays as zero by definition: its unresolved horizontal balance is in `nodal_residual`. Check residuals as well as arrow directions.

For Z-up coordinates, downward gravity and positive compression forces pushing nodes away from their neighbours, the physical support-on-network vector is `r_i` on constrained components. The exporter labels its negative as `reaction`. Reconcile that naming/sign convention before treating exported vectors as physical reactions; see the mathematical reference's support-force sign audit. This documentation update does not alter the solver or exporter.

## Objective and initial seed

The coupled fit minimizes a weighted equilibrium residual plus normalized position preferences:

```text
Phi(x) = W / P^2 * ||g(x)||^2
       + sum_over_free_coordinates alpha_j * (y_j - y_reference_j)^2
P = max(1, maximum absolute initial nodal load)
W starts at 100
```

`P` is fixed after initial setup; it is not recomputed as geometry and self-weight change. `alpha_j` is design attraction for ordinary free coordinates, red movement penalty for red-support XY, or yellow movement penalty for boundary-yellow XY. These preferences have no physical force interpretation and impose no hard displacement bound. There is no density-magnitude or member-length penalty in this objective.

On the first step, the solver attempts one step of the existing `BestFitForceDensitySolver` as a seed. That helper can perform its bounded density fit and up to 64 initialization trials with self-weight equilibrium. A successful candidate is converted into log densities and normalized coordinates, then adopted only if it lowers the full coupled objective. The helper's free-node equilibrium alone does not certify vertical-only black-support reactions.

Validation uses black supports as graph anchors; when there are no black nodes it temporarily uses the fixed-Z supports for connectivity validation. The actual coupled constraints remain fixed Z in that case. Every active graph component needs a validation anchor. With no black supports, the helper seed reset fails its own black-support requirement and the coupled solver proceeds from the original initialization. Zero attraction/penalties can leave unconstrained motions or nonunique solutions; damping is numerical regularization, not an added physical support.

## Coupled Gauss-Newton step

The implementation in `VerticalReactionFit.h` uses forward finite differences. For each unknown component `c`, it copies the unknown vector and evaluates the complete geometry, densities, loads and residual report:

```text
h_c = 1e-4 * max(1, abs(x_c))
J(:,c) = (g(x + h_c * unit_c) - g(x)) / h_c
```

Coefficients with absolute unweighted derivative at most `1e-12` are omitted. Moving self-weight is included implicitly because loads are recomputed for each perturbed geometry. The Jacobian uses double-precision sparse coefficients, while evaluated positions, densities, loads and residual vectors use the existing float mesh/report types. Double QR does not make these float evaluations or their finite differences exact.

The damped least-squares system has `R + K + (E + K)` rows and `E + K` columns:

```text
             [ sqrt(W)/P * J                     ]
J_augmented =[ position rows: sqrt(alpha_j)       ]
             [ sqrt(lambda) * identity           ]

rhs         =[ -sqrt(W)/P * g(x)                  ]
             [ -sqrt(alpha_j)*(y_j-y_reference_j)]
             [ 0                                 ]

delta = argmin ||J_augmented * delta - rhs||^2
```

Each position row has a coefficient only in its corresponding coordinate column. Damping applies to both log-density and coordinate increments. It regularizes the step; the nonlinear objective `Phi` does not include a damping penalty on the absolute state.

The code builds triplets and a new sparse matrix, then calls `Eigen::SparseQR<..., Eigen::COLAMDOrdering<int>>::compute` followed by `solve`. It performs symbolic ordering and numeric QR anew each iteration. There is no explicit normal-equation matrix/inverse, analytic Jacobian cache, symbolic-factorization cache, LSMR iteration limit or automatic LSMR fallback in this coupled baseline.

## Step limits, line search and schedule

The step is divided by a common factor:

```text
divisor = max(1, max_abs(log_density_delta), max_abs(coordinate_delta)/0.2)
delta /= divisor
```

This caps each proposed log-density increment at one and each normalized coordinate increment at 0.2, without changing the direction of the entire step. Actual coordinate increments are multiplied by `S`.

The solver tries at most 14 trial step lengths `1, 1/2, ..., 1/8192`. Trials with any absolute log-density variable above 25 are skipped. The first finite trial that strictly lowers `Phi` is accepted. Physical residual alone and design RMS alone need not decrease on every accepted trial; acceptance uses their weighted objective. The fit can trade position accuracy for force balance as `W` increases.

| Quantity | Initial value | Update after an outer iteration |
| --- | --- | --- |
| Damping `lambda` | 1e-4 | Accepted: halve, floor 1e-8; rejected: multiply by 10, ceiling 1e6 |
| Equilibrium weight `W` | 100 | If not equilibrated, multiply by 10 after rejection or every eighth iteration; ceiling 1e12 |
| Outer iteration count | 0 | Increment once per completed step, including rejected steps |

The Jacobian and line search use the current weight/damping. Updates affect the next iteration. Successful equilibrium prevents that iteration's weight increase, but does not necessarily terminate optimization. Continuing iterations may change a previously equilibrated state; the viewer stores the current iterate, not a separate best-equilibrated archive.

## Residual, RMS and termination

The v2 solver replaces the shared reporter's horizontal norm with the norm of raw XY residuals at all nodes, including black supports. Its vertical norm contains only free-Z residuals:

```text
H = sqrt(sum_all_nodes (r_ix^2 + r_iy^2))
V = sqrt(sum_free_Z_nodes r_iz^2)
residual = sqrt(H^2 + V^2)
RMS = sqrt(sum_all_nodes ||X_i - X_reference_i||^2 / N)
```

Residual has force units and is a global Euclidean norm, not a per-node average or maximum. RMS has length units and measures design displacement, not force imbalance. Both original surface nodes and cable copies enter RMS; black nodes contribute zero displacement. Fixed-Z support vertical reactions are excluded from residual because the support carries them. The reference for a cable copy is its initial offset position, not its source surface position.

`success` / exported `equilibrated` is true when the report is finite and the full residual is at most `loads.residualTolerance`, default `1e-3`. A member shorter than `1e-6` model units makes the iterate invalid. Small solver increments do not prove equilibrium.

Termination rules are evaluated in this order:

1. `Converged`: current state is equilibrated and either no line-search step was accepted or the capped full step norm is below `1e-5`.
2. `IterationLimit`: maximum outer iterations reached. The current state may or may not be equilibrated; inspect `success`.
3. `Stagnated`: no step accepted, damping has reached `1e6`, and weight has reached `1e12`.
4. Otherwise continue. QR failure or a nonfinite increment reports `Failed`; a stop request reports `Cancelled`.

The `1e-5` termination check uses the capped direction before line-search halving, not the actual final accepted displacement. Iteration exhaustion/stagnation does not establish infeasibility. There is no global optimum guarantee or structural-stability guarantee.

## Worker and computation cost

A `std::jthread` runs the fit; the viewer polls the latest complete snapshot under a mutex. Rendering can refresh between snapshots, but geometry changes only when a solver step publishes a result. The loop has no deliberate sleep or frame-rate throttle. Running on a background worker keeps the UI responsive; it does not itself parallelize QR.

Cancellation is checked during seed work, Jacobian column evaluations and line-search trials. The coupled QR compute/solve calls have no stop-token checkpoint inside them, so cancellation/reload may wait for a factorization to finish. The viewer joins the old worker and clears pending snapshots before starting a new fit.

Each Jacobian requires one complete residual evaluation per unknown, in addition to the base and trial evaluations. These evaluations copy mesh/result data. Sparse QR factorization can also create fill-in much larger than the original matrix. A low-poly input can therefore still have expensive iterations once cable copies and density unknowns are included. This restored baseline exposes no coupled phase timings; old analytic-Jacobian/LSMR benchmark logs do not describe this version's runtime.

## JSON and OBJ export

Press `e` to write both files beneath `<working directory>/data/`. Exports are allowed before equilibrium, so always inspect `equilibrated`.

| JSON field | Meaning |
| --- | --- |
| `nodes[].id`, `position` | Zero-based node ID and current XYZ |
| `nodes[].reference` | Initial built-network XYZ, including normal offset for cable copies |
| `nodes[].reaction`, `vertical_load` | Current reporter reaction and load, present when the report is finite |
| `members[].a`, `b`, `kind`, `tension` | Endpoints and compression/cable/connector role |
| `members[].force_density` | Current signed q, including unfinished iterates; null when unavailable/nonfinite |
| `faces` | Original surface face connectivity on original nodes |
| `surface_vertex_count` | Number of welded original surface nodes |
| `source_to_cable_node` | Mapping per welded surface node: -1 if unselected, itself for a selected boundary anchor, added node ID for an interior copy |
| `fixed_xyz`, `fixed_z`, `yellow_supports` | Black anchors, red/yellow sliding anchors and yellow subset |
| `initial_offset`, movement penalties | Initial network offset and red/yellow design preferences |
| `fit_iteration`, `diagnostic`, `equilibrated` | Current iteration and solution-status information |
| `support_forces` | Support IDs/types, positions, reactions, opposite forces, magnitudes and raw nodal residuals |

`vertical_only_reactions_required` describes the target constraint, not a claim that an unfinished export satisfies it. At sliding supports, horizontal reaction magnitude can be zero while horizontal nodal residual is nonzero. At fixed supports, horizontal reaction magnitude measures that imbalance directly. `reference` is geometry, not a reference force or a previous solver iterate.

`offset_cable_network.obj` uses exactly the current JSON positions, with one shared 1-based vertex list. It contains `surface` faces, `cables` lines and `tension_connectors` lines. Compression members are represented through surface edges; densities and support forces are carried by JSON. The OBJ comment records equilibrium status. This revision does not export solver choice, slider settings for all loads/attraction, or numeric residual/RMS totals as separate top-level JSON fields; the diagnostic carries H/V information and consumers can recompute the quantities.

## Verification and limitations

```powershell
cmake --build alice2/build --config Release --parallel 4
./alice2/build/Release/checks/alice2_checks.exe --check-bestfit-tna
./alice2/build/Release/checks/alice2_checks.exe --check-offset-cable path/to/tna.obj
```

The synthetic suite checks the tied-arch coupled fit and vertical-only black reactions, red/yellow movement preferences, fixed heights, all-sliding anchors, cancellation, and honest failure on an incompatible network. Topology checks cover branching, offset movement, boundary anchors, red classification, surface-only loads, face-corner welding and invalid input. It also includes the older fitting/TNA regression checks.

The strict OBJ check welds the input, compares offsets -0.5/-1, then allows up to 100 coupled iterations with nodal load 0.001, density 1 and both movement penalties 50. These are test settings, not the viewer's offset/red-penalty defaults. It fails if the final fit or horizontal balance is not verified. `--benchmark-offset-cable` and `--qr` selection belong to the later rolled-back implementation and are unavailable here. `--benchmark-bestfit-tna` benchmarks the separate v1 helper workflow.

All-tension joints need incident force directions that can balance the applied load; valid connectivity alone does not ensure feasibility. Large opposing cable/compression forces can cancel, particularly for coincident members, and the current objective has no upper-density constraint or force-magnitude regularization. Compare axial forces `abs(q)*length`, the geometry, and equilibrium residual rather than requiring equal tension/compression densities. A low residual certifies the implemented static equations only; it does not check buckling, stiffness/stability, material stress, elastic compatibility, cable slack under other loads, collision, fabrication or a minimum member length beyond the numerical rejection threshold.

## Source map

- [Sketch controls, worker, display and exporters](../../alice2/userSrc/bestfit/sketch_bestfit_tna_v2.cpp)
- [Welding, offset topology and load settings](../../alice2/src/tna/OffsetCableNetwork.h)
- [Coupled variables, objective, finite differences, QR and schedule](../../alice2/src/tna/VerticalReactionFit.h)
- [Shared load/residual reporting and initial seed solver](../../alice2/src/tna/BestFitForceDensitySolver.cpp)
- [Settings and report types](../../alice2/src/tna/BestFitForceDensitySolver.h)
- [Tributary areas and vertical settings](../../alice2/src/tna/TnaSolver.cpp)
- [Headless regression checks](../../alice2/src/tna/BestFitTnaChecks.cpp)
