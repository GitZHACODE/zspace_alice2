# Offset Cable Network (3D)

`sketch_bestfit_tna_v2.cpp` registers a separate sketch named **Offset Cable Network (3D)**. It jointly fits spatial node coordinates and signed force densities, without a reciprocal TNA stage. Every node has horizontal equilibrium equations, including fixed supports.

Input: `<working directory>/data/tna.obj`, with RGB vertex colors in the OBJ. Export with consistent face winding. The sketch welds duplicate face-corner vertices on a 0.001-unit coordinate grid before detecting boundaries. Black/red/yellow labels are preserved over unmarked copies; conflicting constraint colors at one welded position are rejected. Exported source IDs refer to the welded mesh.

- Black (RGB <= 0.02): fixed XYZ.
- Red (R >= 0.8, G/B <= 0.2): fixed Z, free XY, no horizontal penalty spring.
- Yellow (R/G >= 0.8, B <= 0.2): cable selection. Every original face edge with two yellow endpoints produces a tension cable segment. Branches of any valence are allowed.
- Other colors: ordinary compression mesh nodes.

Every original face edge remains a compression member. Each selected interior yellow vertex gets one extra cable node, displaced along the recomputed vertex normal by the signed offset, and one tension-only connector back to its original vertex. Boundary yellow vertices keep Z fixed, slide in XY, and are shared by the compression and cable networks, with no duplicate node or zero-length connector. Boundary detection includes holes. Two boundary yellow endpoints therefore create a coincident compression member and separate cable member.

Offset is starting geometry, not a maintained separation or prescribed cable length. The sketch uses a negative offset (default -0.5, slider -10 to -0.001), opposite the mesh normal; zero is excluded to avoid zero-length connectors. All-tension joints must have directions that permit force balance; a chosen offset geometry is not guaranteed to be feasible. Original mesh faces receive surface self-weight, and only original mesh vertices receive the nodal load. Added cable nodes receive neither by default.

Controls: `q` fit, `c` cancel, `r` reload, `f` frame, `e` export, `i` toggle the original gray input wireframe without resetting the fit. Sliders control negative-normal offset, iterations, attraction, nodal load, surface weight density, and sliding movement penalty (0–500, default 50). Higher movement penalty favors the original XY positions of red supports. It is a normalized squared-distance objective, not a horizontal spring or physical reaction; red Z stays fixed. Fitting runs on a background worker. Changing a slider resets the fit.

Display: blue compression, pink cable segments, orange tension connectors. Fixed and red support nodes retain their input colors. The HUD reports full equilibrium residual (including support XY) and maximum horizontal reaction at fixed supports. A result is accepted as equilibrated only when this full residual meets the tolerance. Black coordinates stay fixed; yellow boundary anchors keep Z fixed and can move in XY. Cable nodes and unconstrained mesh coordinates can move. `d` toggles signed density labels and green support-reaction arrows; the support force scale changes their display size. Unfinished iterations are previews explicitly marked NOT equilibrated. An equilibrium result is also not a structural stability check.

Export: `<working directory>/data/offset_cable_network.json` includes positions, reference positions, source-to-cable mapping, faces, typed members, support constraints, and solved densities/reactions when available. `equilibrated: false` identifies an unsolved network export.

Validation: `alice2.exe --check-bestfit-tna` includes offset topology, branching, signed offset, shared anchors, red support classification, surface-only loading, OBJ face-corner welding, and zero-offset rejection checks. Run `alice2.exe --check-offset-cable path/to/tna.obj` to verify offset motion and a short spatial fit on an input file.



The coupled solver uses damped sparse Gauss–Newton steps with increasing equilibrium weight, sign-preserving density variables, and line search. Position preferences select among shapes but never subtract from measured physical imbalance. Iteration limits and local stagnation do not prove that an input network is infeasible. A valid topology is not guaranteed to have a vertical-reaction solution.



Yellow support penalty is a separate 0–500 slider (default 50). Higher values discourage XY movement of yellow boundary anchors; it does not create a physical horizontal spring or guarantee a maximum travel distance. The existing sliding movement penalty applies to red supports only. Black supports remain fixed XYZ. Both penalties trigger a fresh fit setup when changed.


Pressing `e` also writes `data/offset_cable_network.obj` using the same current node positions as JSON. This single OBJ contains a `surface` face group, a `cables` line group, and a `tension_connectors` line group, with shared vertex indices. The OBJ comment records whether the exported state is equilibrated.

Every JSON member includes `force_density` for the current iterate, even before convergence (null when unavailable or nonfinite). Positive values mean compression and negative values mean tension. Check `equilibrated` before treating these as solved forces; `fit_iteration` identifies the iteration, with 0 representing initialization.

The JSON `support_forces` array lists fixed, red sliding, and yellow sliding supports by `node_id`, with position, reaction vector (matching green arrows), opposite `force_on_support`, total and horizontal magnitudes, and raw `nodal_residual`. Unavailable values are null. Sliding-support horizontal reactions are constrained to zero; inspect the residual and `equilibrated` flag to assess unfinished fits.
