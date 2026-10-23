# Compression best fit, TNA, then offset-cable best fit

Branch: **experiment/tna-then-offset-bestfit**. Active sketch: [sketch_tna_offset_bestfit.cpp](../../alice2/userSrc/bestfit/sketch_tna_offset_bestfit.cpp), enabled with its first-line main define.

This three-stage, manually triggered experiment replaces the earlier automatic two-stage procedure. It uses the same OBJ reader, colour thresholds, welding, negative offsets, cable branching, tension-only connectors, surface loads, and final spatial best-fit solver as v2.

## Input and keys

Input remains **working-directory/data/tna.obj**, with no absolute-path fallback. The lowpoly test uses alice2/build/bin/Release/data/tna.obj: 169 welded vertices and 144 faces. The separate alice2/build/Release/bestfit/data/tna.obj has 472 welded vertices and 416 faces. The sketch does not overwrite or swap these inputs.

| Key | Action |
|---|---|
| q | Restart from input; fit compression geometry and positive densities; stop for inspection |
| t | Use the completed compression fit/densities for horizontal and vertical TNA; stop for inspection |
| c | Build normal-offset cables from verified TNA and run the mixed-sign best fit |
| Esc | Cancel the active stage |
| v | Toggle the compression-TNA force diagram |
| g | Toggle the compression reference in green |
| i | Toggle the original input |
| d | Toggle density labels, support arrows, and reciprocal direction-error labels |
| f | Frame the form and visible force diagram |
| r | Reload the input |
| e | Export the displayed stage as JSON and OBJ |

No stage automatically starts the next. TNA requires a completed fit with a valid equilibrium candidate. Cable fitting requires verified TNA. A best fit can stagnate in design-distance improvement while retaining a valid equilibrium shape; that shape can proceed to TNA.

Repeating q clears downstream results. Repeating t starts from the saved compression best fit. Repeating c starts from the saved TNA surface, not the last cable-fit geometry. A failed TNA stage has no best-fit fallback.

## Supports, topology, and defaults

Black means fixed XYZ. Red and boundary-yellow supports retain Z and slide in XY during the final mixed fit. Selected interior yellow vertices produce separate cable nodes and tension-only connectors to the source vertex. Yellow-yellow face edges produce cables, including branches.

**During stages 1 and 2**, black, red, and boundary-yellow supports all hold XYZ and can carry horizontal thrust. Cables are absent. Stage 3 restores the original support roles and targets vertical-only reactions, including at black supports.

Stage 1 builds the existing TNA-compatible compression topology. Exterior faces close the reciprocal topology but carry no self-weight. Direct boundary edges between two thrust supports are omitted from active equilibrium members, following TnaSolver::makeFormDiagram. Stage 3 restores every physical surface edge, including these boundary edges. Original vertex IDs and colours are preserved.

Defaults: negative offset -0.05; fit budget 50 for each fit stage; attraction 1; nodal load 0.001; surface weight density 1; red movement penalty 0; yellow movement penalty 50; horizontal TNA budget 10,000; TNA form weight 1; force scale 1. These settings do not guarantee convergence.

Changing nodal load, density, or attraction invalidates the compression fit and requires q. TNA-control changes retain the compression fit and require t. Offset, fit-budget, and support-penalty changes retain verified TNA so c can rerun. Changes during work cancel the active stage before reconfiguration. The support-force arrow scale is display-only.

## Stage 1: compression best fit

This stage uses **BestFitForceDensitySolver**, not the all-support vertical-reaction fit. It fits positive densities on the spatial input and resolves the equilibrium geometry, then searches for an equilibrium shape closer to the input. Horizontal support reactions are allowed.

With coordinate-dependent equilibrium matrix \(A(\mathbf X)\), densities \(q\), and signed vertical loads \(p(\mathbf X)\), the density subproblem is

\[
\min_{q\ge q_{\min}}\|A(\mathbf X)q-p(\mathbf X)\|_2^2,
\qquad q_{\min}=10^{-4}.
\]

The existing sparse bounded fit, density scaling trials, equilibrium-shape solve, self-weight updates, design attraction, and equilibrium/design-distance acceptance are retained. Positive-density graph Laplacians resolve geometry with fixed XYZ thrust anchors. Design attraction guides density updates and is not an added physical reaction.

The completed stage saves its best valid geometry, densities, edge ordering, and equilibrium report. Its design reference is the original input. Detailed equations for this existing method are in [BestFitTna.md](BestFitTna.md).

## Stage 2: fitted-density TNA

The saved compression best fit is projected to XY for reciprocal construction. The density-seeded makeForceDiagram overload builds the reciprocal from the fitted densities, replacing the earlier unseeded centroid initialisation. The force-scale slider multiplies these seeds once.

The existing TNA solver parallelises form/force edges with frozen-neighbour Jacobi updates. In rotated force coordinates its shared target is

\[
\mathbf t_e=\alpha\widehat{\mathbf d}^{form}_e+
(1-\alpha)\widehat{\mathbf d}^{force,rot}_e.
\]

Targets are formed once and the blend is not normalised. Form weight 1, the default, holds fitted XY and adjusts the force diagram. Weight 0 adjusts the plan towards the seeded force diagram. Intermediate values adjust both. Densities follow

\[
q_e=\ell^{force}_e/\ell^{form,XY}_e.
\]

Force-edge length bounds are 0.01 to 100 times each initial seeded length, with numerical floors. Support XY remains fixed. Zero-length XY reciprocal pairs are rejected. Advancement requires finite free-node H residual at most 0.001 and correctly oriented reciprocal pairs within 3 degrees; reaching the iteration count is insufficient.

Vertical TNA holds the plan/densities, restores original support heights, and solves

\[
L_{ff}(q)z_f=p_f(\mathbf X)-L_{fs}(q)z_s.
\]

Compression uses sparse Cholesky. Loads retain v2's signed scalar convention and combine nodal load with tributary area times density and thickness. Exterior faces are unloaded. Areas are recalculated after height updates. The wrapper runs one self-weight update per step, up to 100, and requires combined free-node horizontal/vertical residual at most 0.001. A fitted state already close to equilibrium may need little vertical change.

The form display retains fitted heights during the horizontal calculation; projection to Z=0 is not displayed as a structural height change.

## Force-diagram display

Pressing t shows the compression reciprocal beside the form and frames both. Display-only scaling fits its XY size to the form; translation places it to the right. Solver coordinates/densities and exported raw reciprocal positions are unchanged.

Blue means small reciprocal direction error; larger errors approach red. Press d for degree labels. During cable fitting this diagram remains the **stage-2 compression reciprocal**, not a reciprocal of the current mixed spatial network. Stage 3 does not construct a 3D force polyhedron. Press v to hide the diagram.

## Stage 3: offset-cable best fit

After verified TNA, normals are recalculated on the physical surface without exterior faces. Interior cable nodes start at

\[
\mathbf x_i^{cable}=\mathbf x_i^{TNA}+\delta\widehat{\mathbf n}_i^{TNA},
\qquad\delta<0.
\]

Compression densities transfer by unordered endpoint IDs. Restored support-to-support edges use the median TNA density. New cable/connector tension magnitudes start at 0.1 times the median. Seeds have a lower floor of 1.01 times the fitting minimum, making log parameters finite; TNA values near/below that floor are raised slightly.

VerticalReactionFit jointly varies compression/cable geometry and density magnitudes. Black XYZ remains fixed; red and boundary-yellow Z remains fixed. The TNA surface plus offset cable nodes becomes the positional reference. Signs are preserved by

\[
q_e=\sigma_e(q_{\min}+e^{\eta_e}),\qquad
\sigma_e=+1\text{ compression},\ -1\text{ tension}.
\]

All-node XY residuals, including black supports, enter the equilibrium objective; only free-Z residuals enter vertically. Red/yellow movement preferences are not physical reactions. The existing finite-difference Jacobian, sparse QR, damping, bounded line search, and increasing equilibrium-weight schedule remain. Explicit density seeds skip the old first-step initialisation; unseeded v2 callers are unchanged.

Offset is initial geometry, not a permanent separation constraint. Final success requires finite nondegenerate members and all-node XY plus free-Z residual at most 0.001. Compression success does not guarantee cable convergence. See [OffsetCableComputationalReference.md](OffsetCableComputationalReference.md) for full spatial-fitting equations.

## Exports and execution

Press e to write the displayed stage to working-directory data/tna_offset_bestfit.json and .obj. JSON includes stage, current densities/support forces, original positions, saved compression best-fit positions, compression/TNA positions, and raw reciprocal geometry. Before c, red/yellow supports are exported as temporary thrust supports with XYZ fixed, and cables are absent. OBJ contains physical surface faces and, when present, cable/connector lines in one file.

Node reference means original input for compression fitting, saved compression best fit for TNA, and TNA surface/offset network for the cable fit. compression_equilibrated identifies a completed valid compression result. Top-level equilibrated is reserved for a verified completed mixed network; vertical_only_reactions_required is true after the mixed network is built. Support-force signs retain v2's reporting convention. The reciprocal is labelled as compression TNA and remains frozen during stage 3.

From the repository root:

    cmake --build alice2/build --config Release --parallel 4
    .\alice2\build\bin\Release\alice2_checks.exe --check-tna-offset
    .\alice2\build\bin\Release\alice2_checks.exe --check-bestfit-tna
    .\alice2\build\bin\Release\alice2_checks.exe --check-tna-offset alice2/build/bin/Release/data/tna.obj 50

The command-line check explicitly starts/waits for each stage, following q, t, c. Optional arguments: OBJ path, fit budget, form weight, horizontal budget. A real-input run returns nonzero for failed compression/TNA or an unfinished final cable equilibrium.

## Measured lowpoly results

Release build and existing regressions pass. Synthetic checks pass for manual stage stops, out-of-order rejection, reciprocal generation, density transfer, negative offset, valence-four branching, support restoration, cancellation, and stage-specific parameter invalidation. Synthetic mixed equilibrium converged in 10 iterations at residual about 5.94e-8.

On the 169-vertex lowpoly input, stage 1 stagnated in design-distance improvement while retaining valid equilibrium. Compression fit plus 10,000 TNA horizontal iterations and one vertical update took about 1.1 seconds. Verified compression residual was 8.84e-6 and RMS displacement from input 0.0201, compared with 0.519 in the earlier unseeded experiment. TNA densities ranged from 8.66e-5 to 8.98.

After 50 cable-fit iterations, H residual was 1.930 and free-Z residual 0.316: total about 1.956, **not equilibrated**. Runtime was approximately 30 seconds in that run. At 100 iterations, total residual improved to approximately 0.784 (H 0.455, free Z 0.639) in about 50 seconds, also **not equilibrated**. These times are machine/run dependent.

The schedule improves the compression starting shape, but the tested cable-fit budgets do not produce final equilibrium. Capacity, stability, clearance, crossings, and face inversion are not validated by the residual checks.