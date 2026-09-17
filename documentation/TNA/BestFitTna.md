# Best-fit shape and fixed-plan TNA

The enabled sketch is `alice2/userSrc/geometry/sketch_bestfit_tna.cpp`.
It loads and welds `alice2/data/tna.obj`, retaining the original as a grey reference.
The force diagram is scaled and translated for side-by-side display only;
underlying geometry, densities and numerical checks always use solver units.

- **1** fits signed force densities and moves the shape in XYZ.
- **2** transfers the fitted shape and densities to TNA. All XY coordinates from
  step 1 stay fixed; forces and unconstrained heights may change.
- **f** frames both diagrams; **i**, **a**, **d** toggle the reference, reciprocal
  angle labels and density labels.
- **t**, **b**, **g**, **o** retain the original boundary-tension controls and toggle near-zero vertical member constraint.
- **r** reloads; **e** exports a valid completed stage to
  `data/bestfit_tna_formfound.obj`, without topology-only exterior faces.

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
