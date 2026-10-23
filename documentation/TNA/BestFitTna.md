# Best-fit shape and fixed-plan TNA

This document describes the v1 sketch `alice2/userSrc/geometry/sketch_bestfit_tna.cpp`.
Its `__MAIN__` define is currently commented out; the enabled offset-cable v2 sketch
is documented in [OffsetCableNetwork.md](OffsetCableNetwork.md), with the mathematical
reference in [OffsetCableComputationalReference.md](OffsetCableComputationalReference.md).
It loads and welds `<working directory>/data/tna.obj`, retaining the original as a grey reference.
All exports also go to `<working directory>/data/`; there is no source-tree fallback.
The force diagram is scaled and translated for side-by-side display only;
underlying geometry, densities and numerical checks always use solver units.

- **q** fits signed force densities and moves the shape in XYZ.
- **c** cancels fitting, retaining the last accepted equilibrium shape if one exists.
- **w** transfers the fitted shape and densities to TNA. All XY coordinates from
  step 1 stay fixed; forces and unconstrained heights may change.
- **f** frames both diagrams; **i**, **a**, **d** toggle the reference, reciprocal
  angle labels and density labels.
- **t**, **b**, **g**, **o** retain the original boundary-tension controls and toggle near-zero vertical member constraint.
- **r** reloads; **e** exports a valid completed stage to
  `data/bestfit_tna_formfound.obj`, without topology-only exterior faces.

In this sketch, OUTER is the boundary ring passing through the black supports;
all other boundary rings are inner. Selection starts at a black support and does
not depend on face order or which loop the mesh traversal encounters first.
The ALL boundary-tension toggle still includes both outer and inner rings.

Black vertices remain fixed in XYZ in both stages, except when a black support is
connected to tension members: in step 1 it is allowed to slide in XY with a high
penalty stiffness towards its reference position to prevent massive displacement,
allowing compression and tension member forces to equilibrate in XY so the support
load becomes purely vertical. Red vertices retain their original Z but can move
horizontally in step 1. Self-weight excludes exterior faces. Editing fitting controls
invalidates both stages; editing TNA controls restores the fitted result without
repeating step 1. A nonzero tension-force distance moves and fixes exclusively
tension-connected dual vertices for step 2. Support reaction forces are rendered as
green lines and arrowheads.

## Numerical method

Step 1 adapts section 3.2 of *Discrete funicular structures with curve-folded
moulds*, Bhooshan, Van Mele and Block, IASS 2015 (`PAPBhooshan.pdf`). It uses the
least-squares initial densities and residual-plus-target density updates, without
the paper's fabrication objective or distance-from-plane projection.

The equilibrium equations use `A(X) q = p`, with positive compression and
negative tension, matching the existing TNA convention. The implementation
builds rows only for unconstrained coordinate directions. An active-set bounded
least-squares solve uses rank-revealing QR rather than inverting normal equations.
Density magnitudes have a minimum of `1e-4`.

For current geometry X, reference X0 and attraction weight w, density updates fit
`A(X) dq = (p - A(X)q) - w (A(X0) - A(X))q`. This converts attraction displacements
to target forces using the current stiffness. Each trial then solves XYZ force
density equilibrium and iterates self-weight. Backtracking retains only valid
equilibrium candidates that improve RMS distance to the reference.

An initial least-squares solution can approach a mechanism with signed densities.
Initialization therefore also tests blends toward uniformly sized signed
densities and force scales, retaining the closest valid equilibrium candidate.
This is a local search, not a guarantee of the globally closest shape. Convergence,
stagnation, iteration exhaustion and failure are reported separately.

Density updates warm-start the bounded least-squares solve from the accepted
densities. Equilibrium matrices use sparse storage (at most six coefficients per
edge). The default density solver uses block principal pivots and sparse QR,
swapping multiple infeasible variables together rather than refactoring after
every individual change. It verifies feasibility and KKT optimality before
returning. Rank-deficient subproblems use the original minimum-norm COD solve;
failed or cycling sparse fits retry the dense Lawson-Hanson solver. The reference
matrix, sliding support list and near-zero edge mask are cached until reset.
Each equilibrium candidate shares its X/Y factorization and solves XY only
once; subsequent self-weight iterations update Z and reuse the loads already
computed by the preceding residual check. The full 64-candidate initialization
and 12-trial backtracking limits are retained.

Step 1 runs on an owning worker thread. The viewer consumes independent solver
snapshots after outer updates; camera input and rendering continue during a fit.
Cancellation is checked in both density solvers, between candidate trials and
inside self-weight iterations. Reload, restart and sketch cleanup stop/join the
old job and discard its snapshots before starting another. An individual matrix
factorization cannot be interrupted, so cancellation completes at its next check.

Step 2 integrates the reciprocal force diagram from the fitted horizontal edge
forces, preserving edge ownership and sign. It uses TNA form weight 1 (fixed plan)
and the existing 0.1-to-5-times initial force-edge length bounds. It checks both
reciprocity and actual force residuals. Default tolerances are 3 degrees and an
absolute free-coordinate force norm of `1e-3`. Support reactions do not contribute
to that norm. Already equilibrated inputs are verified without changing them;
incompatible constraints are reported, not silently relaxed.

## Verification

From the repository root:

```powershell
cmake -S alice2 -B alice2/build
cmake --build alice2/build --config Release --parallel
./alice2/build/bin/Release/alice2.exe --check-bestfit-tna ./alice2/data/tna.obj
```

The check runs without an OpenGL context. It covers known equilibrium, mixed
signs, rank-deficient fitting, support constraints, invalid inputs, transfer,
active horizontal/vertical refinement, fixed XY and honest iteration-limit
failure. Omit the OBJ argument to run only the synthetic checks.

The checks also cover repeated density updates, sliding versus fixed supports,
red Z constraints, near-zero bounds, monotone fit distance, cache reset,
sparse-versus-dense fit quality, rank-deficient fallback, cancellation and
background-worker restart without stale results.

To time step 1 using the current sketch defaults (including sliding penalty 50):

```powershell
./alice2/build/bin/Release/alice2.exe --benchmark-bestfit-tna ./alice2/data/tna.obj
# Optional reference comparison with the previous dense algorithm:
./alice2/build/bin/Release/alice2.exe --benchmark-bestfit-tna ./alice2/data/tna.obj --dense
```

The benchmark reports each outer iteration, total solve time, cumulative density
fit/equilibrium time, candidate count and dense fallback count. It checks equilibrium, support
constraints, density signs and non-increasing design RMS. It excludes loading,
reset and rendering. On the updated OBJ (308 welded vertices, 545 active edges),
the Release comparison measured 80.05 s for the previous dense solver and 9.78 s
for sparse block pivots. Both took 15 outer iterations and 195 candidate trials,
with matching printed residual/RMS values at every step. The sparse run used no
dense fallbacks: density fitting took 5.44 s and equilibrium trials 4.33 s.
Timings depend on hardware and build configuration. Nonunique density solutions
can differ between backends on other meshes; final equilibrium, signs, supports
and design fit must be checked rather than assuming identical coordinates.
