# Current Agent Plan

Status: TASK-008 implemented and locally verified. Actual Alice2 F1 evidence saved; Tracker review-pending progress published and read back at version 56. Awaiting Lin's acceptance; code remains local/uncommitted.

## Current User Intent (22 October 2026)

Optimise constrained harmonic iso relaxation against a no-relaxation baseline;
validate the shared example USD; preserve the current print-height method;
omit the top-cap print layer;
resample in 3D between mandatory features after intersection reconstruction and
mapping. Validate and capture evidence in the actual Alice2 application using
the user's saved F1 camera. Publish progress/results to the existing Tracker leaf.

## Current Contributor and Source State

- Lin / Codex root coordinates the current work; no concurrent agent edits started.
- Authoritative toolsets: `C:/Users/Wo.Lin/source/repos/zspace_toolsets/build/worktrees/task-008-print-path-post-processing`, branch `codex/task-008-print-path-post-processing`; previous uncommitted work retained.
- Alice: current `codex/task-006-print-mesh` checkout; dirty CMake/sketch/verification edits retained.
- Existing TASK-008 Tracker leaf: `subtask-fix-print-path-feature-point-sampling-2h9zl4`, project 4163, UAT, owner Wo L.
- Validation input found at `//zaha-hadid.com/Data/Projects/4163_Carbcomn/4163_Codex/shared-assets/slicer_inputs/example.usda` (not directly under shared-assets).
- F1 slot 0 exists in `alice2/build_task008/bin/Release/src/cameras/camera_saves.json`; preserve the saved camera file.

## Proposed Implementation and Files

1. Profile the current native pipeline and relaxation stages on the example;
   compare identical Release/thread/input/settings runs with relaxation at 0
   and the normal iteration count. Separate setup, fairing/checks, UV, unroll,
   path sampling and height costs. Use warm-up and repeated median/range timings.
2. Optimise measured hotspots in toolsets `zSlicer/harmonic/Relaxation.cpp`,
   `Relax.cpp` and related geometry query helpers. Retain geometry, cap,
   boundary, feature and nonintersection checks; do not reduce quality merely
   to claim speedup.
3. Add feature-span 3D resampling in PrintSyn/mapping helpers, with new points
   on the current layer geometry and aligned normal/height attributes using
   existing mesh interpolation. Keep the current height formulation unchanged.
4. Keep the top cap as a fixed solve/reference mesh, but omit its printed paths
   and associated print statistics/export layer. Apply to the confirmed slicer scope.
5. Update focused native correctness tests separately from the benchmark harness.
   Rebuild Alice against this exact source worktree, verify deployed DLLs,
   load the shared example and restore F1 for actual application screenshots.
6. Update `docs/architecture/tagged-slicing.md`, shared task history/evidence and
   Tracker. No screenshots from a substitute browser report for this validation.

## Build and Acceptance

- Reuse `alice2/build_task008` and the existing native toolsets verification build;
  avoid a new cold build directory. Alice wrapper supports `-ToolsetsDir` and
  `-BuildDir`; verify the cached paths before incremental builds.
- Baseline and optimised benchmark must use identical functional options apart
  from relaxation, and report both total and stage timings honestly.
- Exact fixed caps, sliding boundary support, preserved mandatory features,
  final-point attribute alignment, unchanged existing mesh-height tables,
  top print-layer omission, short feature spans and both enabled/disabled
  relaxation require focused checks.
- Build/tests and actual Alice2/F1 render checks must pass; keep real screenshots
  and measured output in a follow-up history record and publish Tracker progress.
- Completion remains subject to the shared task-end review/acceptance gate.

## Decisions and Verification

Resolved by Lin: reuse ResampleLength with half-spacing feature clearance and
apply top print-layer omission / second 3D sampling to both slicers. A later
instruction revokes the requested height change: keep the CURRENT print-height
method unchanged, including its enabled/disabled-relaxation behavior. No height
behavior edit had been made at the time of that correction. The earlier proposed
strict previous-mesh ray method is superseded and must not be implemented.

- Five warmed alternating Release runs at four OpenMP threads: median full
  pipeline 6.340 s disabled / 9.484 s enabled; relaxation 3.170 s versus the
  pre-optimization 27.905 s profile. Constraints/iterations are unchanged.
- All 2,339 retained example features survive exactly; ordinary clearance is
  at least 5.214 mm for a 10 mm target. Relaxed geometry and section height
  tables are bit-identical to the pre-optimization capture; caps remain fixed.
- Native checks: 10/10. Alice2 incremental native build and checks: 20/20.
- Actual shared USD imported in Alice2, F1 restored, and native framebuffer
  screenshots captured. Camera file SHA-256 remains unchanged.
- Existing disabled-relaxation gradient heights are extreme near the example's
  top and distort that print-mesh view. Preserve them per Lin's instruction;
  use actual path views to compare relaxation, and record this limitation.
- Code remains local/uncommitted. Publish Tracker progress/evidence location;
  retain the leaf in progress until the shared task-end acceptance gate passes.

## Earlier Completed Plan (retained for context)

## User Intent

Create a standalone sketch that tests zSpace SDK polygon SDF field creation.

## Participating Agents

- `zspace_agent`: use `zFnMeshScalarField`, `getScalars_Polygon`, `setFieldValues`, and `getIsocontour` correctly.
- `alice2_agent`: add the sketch under `alice2/userSrc/zspace/` and keep display through `scene().draw(...)`.
- `code_agent`: keep the sketch focused and self-contained.
- `build_agent`: run the zSpace build and fix compile/link errors.
- `document_agent`: not triggered.

## Proposed Changes

- Add `alice2/userSrc/zspace/sdf/sketch_zspace_sdf_polygon_field.cpp`.
- Switch the active `__MAIN__` from the slicer sketch to this SDF test sketch.
- Draw the scalar field, input polygon graph, and extracted zero isocontour.

## Build Command

```bat
alice2\build_with_zspace.bat
```

## Acceptance Checks

- Build succeeds with zSpace enabled.
- Sketch creates a closed polygon graph.
- Sketch computes raw polygon SDF values on a 2D mesh scalar field.
- Sketch extracts and draws the `0.0` isocontour.

## Implementation Status

- [x] Inspect existing zSpace sketch and field draw patterns.
- [x] Add polygon SDF test sketch.
- [x] Switch active `__MAIN__`.
- [x] Build and fix any compile/link errors.
