# Current Agent Plan

## F1 4K screenshot request (26 October 2026)

- User requests immediate capture: saved F1, 3840x2160 (16:9), 1x UI scaling.
- Coordinator handles this narrow capture; retain the existing slicer work below.
- Add an optional native offscreen PNG render target in `Application.cpp` and
  `ScreenshotTarget.h`; document the resolution option in `build_agent.md`.
- Reuse the existing `build_surface_height` configuration and current example
  print-mesh display, preserve camera JSON, and verify PNG dimensions and contents.
- Build: incremental Release `alice2` target in `alice2/build_surface_height`.
- Status: complete. Native capture target and web viewer baseline build passed;
  the PNG is 3840x2160, visually checked, with UI at the original pixel positions
  and the saved F1 camera JSON SHA-256 unchanged.
- The live toolsets runtime currently fails `computeContours` before capture
  (both 8 and 0 relaxation iterations). Do not alter that concurrent slicer work.
  The delivered preview uses the existing validated performance-audit runtime,
  retaining its original sketch objects/DLLs and linking only the new capture
  application object. Core rendering interfaces and camera JSON match exactly.
- Output: `alice2/build_surface_height/capture-f1-4k/F1-3840x2160-ui-1x.png`.
  Capture scripts, logs and a provenance manifest are in the same build folder.

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

## 2026-10-28: Build Alice2 with updated printmesh sections

- User intent: build the native Alice2 application against the current sibling
  Toolsets checkout, including the requested Rhino-style printmesh sections.
- Participating agents: current agent in the build-agent role.
- Proposed files: generated build outputs only, plus this appended build record;
  preserve existing source changes and earlier plans.
- Build command: `alice2\build_with_zspace.bat -Jobs 4`, using the existing native
  OpenUSD SDK and TetGen library from `alice2/build_zspace_v3`.
- Acceptance checks: successful Release build, fresh Toolsets printmesh object,
  and executable plus required zSpace DLLs in `build_zspace_v3/bin/Release`.
- Implementation status: the default build exposed pre-existing slicer API
  differences. Reuse the configured `alice2/build_surface_height` build with its
  TASK-008 Toolsets worktree, preserving its existing changes. The original
  printmesh source/test blobs match the main checkout exactly; carry only those
  verified printmesh updates into the desktop worktree. Build with `-Test`.
- Run step after success: `alice2\run_with_zspace.bat build_surface_height`.
- Verification: Release build succeeded in `alice2/build_surface_height` with
  the matching desktop Toolsets worktree and the Rhino-style printmesh update.
  All 22 native integration/Toolsets checks passed, including printmesh.
  Updated the desktop worktree's printmesh API comments and documentation too;
  preserved its pre-existing harmonic/source and documentation work.
- Implementation status: complete; application not launched (build-only request).

## 2026-10-28: Final printpath targets and method timing

- User intent: show actual postprocessed printpath targets as black vertices,
  retain yellow feature points, and print method timings to diagnose slow
  slicing/postprocessing in Alice2.
- Participating agents: coordinator handles Alice2 display/caches and outer
  timings; postprocess_timing agent handles opt-in Toolsets method aggregation.
- Proposed files: active slicer sketch, focused display/timing helpers and
  verification, CMake test wiring, and timing scopes in the existing TASK-008
  Toolsets worktree. Preserve earlier printmesh and harmonic edits.
- Build: existing `alice2/build_surface_height` Release configuration with
  TASK-008 Toolsets, native OpenUSD, TetGen and `-Test`.
- Acceptance: markers come from final processed 3D path vertices and recorded
  feature metadata; black targets and yellow features appear in the actual path
  view and sampled-layer inspector. Report import, pipeline stages, PrintSyn
  submethods, printmesh and display-cache times; distinguish wall elapsed from
  inclusive summed parallel worker times, and avoid per-frame timing spam.
- Verification: focused marker/timing checks plus the native build/test suite;
  confirm numerical geometry is unchanged by instrumentation.
- Implementation status: directly requested changes in progress.
- Result: complete. Final path views and sampled-layer inspectors use cached
  black ordinary targets and yellow recorded features; sampled 2D minimaps show
  matching black/yellow markers. Raw/FaceTag diagnostic views are retained.
- Timings: Alice2 prints import, setup, public solve stages, postprocess,
  printmesh and cache wall times. Toolsets aggregates inclusive submethod times
  with calls, maximum and explicit wall-sum/worker-sum labels, largest first.
  Collection is enabled only around computation and disabled during rendering.
- Verification: Release build and all 23 native checks passed. Added final-marker
  partition/unchanged-geometry checks; profiled/unprofiled native fixture output
  remains identical. Collector concurrency, reset, exceptions and PrintSyn stage
  coverage pass. Read-only review found no remaining issue.
- Output: `alice2/build_surface_height/bin/Release/alice2.exe`; launch with
  `alice2\run_with_zspace.bat build_surface_height`. No commit/push or GUI launch.

## 2026-10-28: Remove second resampling and explain measured costs

- User intent: remove the second (mapped 3D) resampling stage and explain the
  supplied harmonic/FaceTag timing breakdown.
- Participating agents: coordinator owns pipeline/test changes and native build;
  postprocess_timing performs read-only hotspot/source analysis.
- Files: TASK-008 PrintSyn pipeline, API comments, focused regression checks,
  architecture explanation and this build record. Preserve fixed FaceTag counts,
  mandatory feature positions, attributes and all concurrent work.
- Build: existing `build_surface_height` Release configuration with `-Test`.
- Acceptance: pipeline goes directly from projection to linking, with no second
  resampling timing row; distorted chart mapping retains the 2D target count.
  Explain inclusive/parallel times and the serial FaceTag preparation cost using
  the user's measurements. No unrelated performance changes or benchmarks.
- Implementation status: directly requested work in progress.
- Result: removed the mapped-3D resampling call from the production pipeline;
  mapping now proceeds directly to linking in both routes. Kept the standalone
  public utility for API compatibility. Updated pipeline comments/docs and added
  checks for absent second-pass timing and unchanged targets on a stretched map.
- Verification: `build_surface_height` Release build and all 23 native checks
  passed. The supplied tagged run already skipped the removed stage, so no
  speedup is claimed for that run.
- Width-field comparison: width assignment reuses segment distance arrays and
  stores a winning width-group index. Groups merge source sides sharing width
  IDs, so that index alone cannot recover FaceTag identity. A fast shared field
  approach can carry source-edge/tag ownership too; the existing FaceTag query
  path still repeats signed distances, per-side ownership and transition search.
- Timing explanation: serial FaceTag preparation accounts for 18.510 s of
  18.540 s in fixed-count sampling. Its projected-anchor welding includes
  2.321 s of dense remapping. Aggregate 46.134 s classification includes parallel
  extraction work and overlaps parent rows; it is not additional wall time.
- Implementation status: complete for removal and source-backed explanation;
  classifier optimization has not been applied. Launch remains
  `alice2\run_with_zspace.bat build_surface_height`.

## 2026-10-28: Shared FaceTag distance queries and responsive layer inspection

- User intent: implement the efficient shared/batched field approach for FaceTag
  classification, preserving side identity, and remove pauses when switching
  layers in single-layer views.
- Participating agents: backend agent owns prepared distance/owner queries and
  parity checks; coordinator owns Alice2 inspector/field presentation caching,
  baseline measurement and final integration/build.
- Files: TASK-008 field/query implementation and focused correctness checks;
  Alice2 inspector cache helpers, active sketch and verification wiring.
- Acceptance: original width/FaceTag ownership, transition positions, feature
  masks and equal per-tag counts survive. No second resampling is reintroduced.
  Revisited layers reuse display data; drawing performs no geometry queries or
  repeated full-grid color/topology rebuilding. Invalidate on new input/solve.
- Verification: capture baseline with existing deployed native DLLs, compare
  outputs/timings on the same input/options, run native correctness suite and
  Release build, check cache invalidation and reuse separately from benchmarks.
- Build/run: existing `build_surface_height`, native OpenUSD/TetGen and TASK-008
  source worktree; `alice2\run_with_zspace.bat build_surface_height` after success.
- Implementation status: complete and verified.
- Result: complete. Shared prepared directed-segment distances now serve width
  and FaceTag queries; source-side ownership and tie order are retained. Batched
  grid queries and independent tagged-layer preparation run in parallel when
  requested. Fixed per-FaceTag budgets/features remain unchanged; no second 3D
  resampling was reintroduced.
- Alice2: owned path/feature snapshots remove per-frame graph/feature queries.
  Full-resolution path/width/FaceTag textures replace per-cell immediate drawing.
  Layer textures are reused on return, bounded to 128 layers and 64 MiB, and
  invalidated on input/recompute/cleanup. Existing black targets and yellow
  features are retained. Categorical FaceTag textures use nearest filtering.
- Verification: Release build and all 26 native checks passed, including scalar
  oracle/query parity, serial/parallel parity and rollback, inspector grid and
  marker snapshots, actual GL upload bytes/state, reuse/eviction/invalidation.
  Both repositories pass git diff --check. No commit/push or application launch.
- Matched local benchmark: 61 harmonic layers, 78,210 grid vertices, four OpenMP
  workers, iso relaxation disabled and height generation disabled consistently
  before/after. Postprocess 24,094.9782 -> 5,324.6349 ms; grid FaceTag query
  778.3936 -> 66.2558 ms. Whole final geometry/attributes/grid tags serialized
  identically (SHA256 B10B820E73935C130296CC9CF99E85A225EA4D0E00C3752CC8B0DB423B4A06B7).
  Default relaxation was not benchmarked because the original example failed its
  nearly-tangent print-height-ray check at layer 54 before postprocessing.
- Hidden-context layer check: cold preview 81.7596 ms; 10,000 hits rebuilt no
  textures (mean lookup 0.00016935 ms); switching to another layer and back reused
  the original texture IDs. These are cache timings, not application frame rates.
  Benchmark artifacts remain local in build_surface_height; no tracked report.
- Output: alice2/build_surface_height/bin/Release/alice2.exe. Launch using
  alice2\run_with_zspace.bat build_surface_height.

## 2026-10-28: Check first/second-layer normal orientation

- User intent: check apparent normal reversal between the first two layers.
- Participating agents: coordinator inspects numeric output and display/loft;
  backend agent independently traces cap and projection normal conventions.
- Scope: read-only source/output diagnosis; no geometry changes or rebuild.
- Acceptance: distinguish path normals from loft face winding; identify which
  layer is reversed and the source of its orientation, including whether the
  latest performance optimization changed the normals.
- Result: confirmed in the matched 61-layer example. First displayed layer
  (index 0) has 214 downward (0,0,-1) path normals; second layer (index 1)
  averages (-0.002054,0.000820,0.999652). The before/after serialized outputs
  are byte-identical, so the performance work did not introduce the mismatch.
- Cause: extractCapMesh copies the volume's outward bottom-cap winding, while
  interior extractIsoMesh orients triangles low-to-high. No cap normal override
  is supplied, so realizeParameterGraph uses its downward face cross-product.
  PrintMesh projects that normal into its section and places height toward it.
- Status: read-only check complete; production code unchanged. A correction
  should align fabrication normals with increasing layer direction, retaining
  the source solid's outward cap convention and avoiding any global-Z rule.

## 2026-10-28: Compare feature handling with Toolsets 2db24962

- User intent: investigate extra vertices near corners and duplicate feature
  markers compared with commit 2db24962da71e15b8285084d0ae412d35bf36ecd.
- Participating agents: coordinator checks historical/current sampling and
  numeric output/display; backend agent independently reviews feature anchors.
- Scope: read-only algorithm comparison and output diagnostics; no rebuild.
- Acceptance: distinguish mandatory FaceTag transition targets from geometric
  features; identify changed sampling/merging tolerances and actual duplicates.
- Verification: local diagnostic compiled the exact historical detect/reconstruct/
  resample methods alongside current code, using the same 61 current raw
  contours (relaxation disabled). Historical/current reconstruction on identical
  untagged inputs produced identical geometry on all 61 layers. This isolates
  feature behavior; it is not a full historical slicer rebuild.
- Findings: old feature detection was transient; current flags preserve both
  angle/intersection identities. On untagged reconstructed contours this retains
  2,241 flagged features vs 2,212 corners re-detected by the historical method.
  Current tagged extraction additionally splits categorical boundaries before
  reconstruction; tagged fixed-count sampling preserves all transition/feature
  anchors instead of ordinary fixed-spacing sampling.
- Near-duplicate handling differs: old append rejected points closer than 0.01
  mm (closed tail 0.1 mm); tagged append/welding only merges near exact 1e-9 mm
  coincidences, with a separate float-scale feature/transition snap. There is no
  ordinary-sample clearance near required anchors in the tagged sampler.
- Saved final output: 12,205 points, no exact within-path coordinate duplicates;
  56 edges shorter than 0.01 mm, 55 adjoining a FaceTag transition and 9 adjoining
  a feature (categories overlap). Two feature pairs are below 0.1 mm: displayed
  layer 43 ~0.034024 mm and layer 48 ~0.095046 mm. The former also survives the
  historical reconstruction/sampling; the latter is preserved by persistent flags
  though historical corner re-detection does not retain that pair as features.
- Status: read-only comparison complete; no production source changes/rebuild.
  A future cleanup must coalesce compatible nearby transition/feature anchors
  before computing budgets, retaining FaceTag ownership and equal per-tag counts;
  indiscriminate post-sampling deletion would violate those counts.

## 2026-10-28: Restore geometric features and 0.01 mm merging

- User intent: revert feature identification to re-detected geometric corners
  and merge nearby vertices at 0.01 mm, retaining current FaceTag sampling.
- Participating agents: backend agent owns Toolsets behavior/regression checks;
  coordinator owns Alice2 display expectations, integration build and example QA.
- Proposed files: feature helpers and tagged sampler/tests in TASK-008; Alice2
  marker/inspector verification and this plan. Preserve all other dirty work.
- Acceptance: stale feature masks do not create yellow features or protected
  corner anchors. Merge compatible consecutive near anchors before budgeting;
  preserve categorical ownership, equal per-tag counts, serial/parallel parity,
  attributes and no second resampling. Final markers use geometric corners.
- Build: existing build_surface_height with TASK-008 Toolsets, native OpenUSD,
  TetGen and -Test. Recheck the same 61-layer example for short edges/features.
- Result: complete. Toolsets featureVertexIds always re-detects current graph
  corners; reconstruction and production samplers refresh angle flags and no
  longer use old angle/intersection masks as anchors. Alice2 still computes its
  marker/inspector lists once per cache build, keeping the layer-switch cache.
- Nearby consecutive protected anchors coalesce at 0.01 mm along the chart or
  mapped section before budgets. Exact corner coordinates win over nearby tag
  crossings; ordinary dense contour vertices do not transitively collapse.
  The current fixed-count FaceTag sampler, parallel preparation and no second
  3D resampling are retained. Interval allocation redistributes around spacing
  caps; output clearance and equal point-label totals are checked transactionally.
  Generic 2D sampling restores the same 0.01 mm weld, including both seam
  windings. Exact endpoint copies avoid float-generated duplicate targets.
- Changed Toolsets files for this request: printsyn/zPrintSyn.cpp,
  printsyn/FaceTagSampling.cpp, utils/FaceTagQuery.cpp, printsyn/zPrintSyn.h,
  tagged-slicing architecture notes and focused smoke tests/reference endpoints.
  Alice2 changes: marker/inspector helper comments and verification expectations,
  agents/alice2_agent.md and this plan. Prior unrelated changes remain intact.
- Verification: build_surface_height Release and all 26 native checks passed;
  both repositories pass git diff --check. Regressions cover stale/zero masks,
  threshold semantics, exact corner/attribute preference, cyclic merges both
  windings, dense contours/long detours, chart/world scale differences, budget
  capacity/ownership failures and rollback, serial/parallel results, malformed
  optional attributes, and cached marker classification.
- Same 61-layer harmonic example (relaxation and height generation disabled):
  12,205 -> 12,174 final points; 56 -> 0 edges below 0.01 mm. Endpoint-inclusive
  budgets stay {3:27,4:11,5:27,6:11} on every layer and unique point labels stay
  {3:27,4:10,5:26,6:9}. The stale ~0.095 mm feature pair disappears; historical
  layer-43 ~0.034 mm geometric pair remains, above the requested merge tolerance.
  Postprocess 6.562 s in this check while compiling; no new speed claim.
- Output: alice2/build_surface_height/bin/Release/alice2.exe, launched with
  alice2\run_with_zspace.bat build_surface_height. Application was not launched;
  user closed their existing Alice2 window to release the DLL for linking.
  No commit or push.

## 2026-10-28: Correct harmonic first-layer fabrication normals

- User intent: implement the annotated normal correction: print normals point
  toward increasing layers while source solid/cap normals remain outward.
- Participating agents: backend agent implements and checks normal export;
  coordinator owns integration build and the same 61-layer numeric verification.
- Proposed files: harmonic fabrication-normal boundary and focused Toolsets
  regressions; this plan. Preserve feature/sampling/performance changes.
- Acceptance: first/second print normals agree with increasing layer direction,
  including rotated geometry; bottom mesh winding/source geometry and solver
  conventions remain unchanged. Cover raw/final paths, relaxation disabled and
  enabled, serial/parallel, and printmesh positive height placement.
- Build: existing build_surface_height with TASK-008, OpenUSD, TetGen and -Test.
- Result: complete. Local prepareBottomPrintNormals in harmonic/Relax.cpp
  computes a fabrication-only inward area-weighted bottom-cap normal table from
  the unchanged outward mesh. It runs after successful zero/nonzero relaxation
  and after surface-height optimization. Repeated calls rebuild from geometry
  and do not double-flip. Interior/top normal tables and solver layer conversion
  remain unchanged; existing feedback best-state copies retain the correction.
- Changed files: harmonic/Relax.cpp, HarmonicSlicer.h projection-normal getter
  comments and tests/smoke/zspace_toolsets_slicing.cpp in TASK-008; Alice2
  agents/zspace_agent.md and this plan. No unrelated source changes.
- Verification: build_surface_height Release and all 26 native tests passed.
  Normal regression covers rotated/tilted and negative-world-Z layer direction,
  moderate nonplanar bottom, relaxation 0/2, serial/parallel, raw/final/combined
  paths, one-sided printmesh height, repeats/recompute and enabled surface-height
  optimization/count feedback. Source cap coordinates and outward winding are
  checked unchanged. Both repositories pass git diff --check; independent review
  found no blockers. Rotated fixture uses 1/3 and 2/3 iso values to avoid an
  unrelated exact-mid-source-ring degeneracy in contour extraction.
- Same 61-layer example: all 214 first-layer normals changed from (0,0,-1) to
  (0,0,1); second-layer mean stays (-0.002054,0.000820,0.999652). Full serialized
  comparison confirms every coordinate, width, height, feature flag, FaceTag,
  connectivity, all other-layer normals and all grid labels are bit-identical.
  12,174 targets, zero edges below 0.01 mm and per-tag budgets stay unchanged.
- Output: alice2/build_surface_height/bin/Release/alice2.exe. Run using
  alice2\run_with_zspace.bat build_surface_height. No GUI launch, commit or push.
