# Projected mixed FDM

Build with `build.bat` from `alice2/`, then run `build/bin/Release/alice2.exe --check-projected-mixed-fdm`. The active sketch is `userSrc/geometry/sketch_projected_mixed_fdm.cpp` (`#define __MAIN__`); it shows the original compression and cable layout with the cable tier below the compression tier and vertical tension connectors between them.

`ProjectedMixedFDM.h` accepts arbitrary graph edges, constant nodal loads, and fixed coordinate masks. Positive density means compression; negative means tension. The equilibrium operator internally uses `-q` so that the physical geometry and loads keep the same meaning. The solver first reports a fixed-input-geometry residual, then alternates reference projection, bounded density correction, and full signed SparseLU recovery. Every accepted state is checked against the actual equilibrium residual. A zero-load solve anchors the first edge's density to its declared target magnitude.

`supportHorizontalWeight` adds a soft target of zero force in fixed horizontal support coordinates. The sketch exposes this weight with a slider and reports the actual horizontal support-force norm after FDM recovery. Larger weights can trade shape fidelity for lower horizontal thrust; an exact zero balance is not guaranteed for every graph and sign pattern.

The current implementation handles constant nodal loads only. It does not test material buckling, detect every nearly singular but solvable matrix, or claim a speedup over the existing solver. A `stagnated` status means no further admissible shape-improving step was found.
