# Failed support-balance trials

This records the attempts made to reduce horizontal support force in `sketch_projected_mixed_fdm.cpp`. The experimental sliding-support and coupled solvers were removed after these trials. The sketch and solver remain at the last tested implementation from before the sliding-support experiment.

## Reference problem we need to match

`sketch_bestfit_tna_v2.cpp` welds `data/tna.obj` and builds the offset cable network. On this source mesh, the yellow boundary supports are fixed in **Z** and free in **XY**. The fit penalizes their XY movement; it does not pin their XY coordinates. It seeks horizontal equilibrium at **every node**, including supports, while geometry and signed force densities change together. It starts with a normal cable offset of `-0.05` and updates tributary-area gravity loads as the geometry changes. Its force-density convention is positive compression and negative tension.

The existing projected FDM sketch instead pins the six yellow supports in XYZ, uses a vertical cable offset initially set to `-0.5`, and freezes gravity loads at the input geometry. Consequently, minimizing reactions on these pinned supports is a different problem from the v2 fit. A small free-node force residual in this model says nothing about horizontal reaction at its pinned supports.

## Trials and measured outcomes

| Trial | Source-mesh outcome | Why it was insufficient |
| --- | --- | --- |
| Fixed-geometry support penalty during projected FDM | At penalty 10, horizontal support-force norm fell only modestly while shape error grew from the shape-only result of about `18` to about `393`. | The penalty was evaluated at a projected geometry; the recovered equilibrium moved elsewhere. |
| Reaction-Jacobian correction after a shape-only solve, with fixed XYZ supports | With the shape-error allowance at 3 times the fitted value and compression changes capped at 10%, norm fell from `12.95` to `10.07`; shape error rose from `17.98` to `53.94`. Average compression-density change was about `4.8%`, and cable tension increased. | It improved the reaction honestly, but remained far from zero because it solved the pinned-support problem. |
| Same correction with an effectively unrestricted shape allowance | Norm reached `0.315`, but shape error reached about `2458` and the largest tension density was about `-30.9`. | Near cancellation came with a severe shape change; this is not evidence that the v2 sliding-support problem requires such a change. |
| Change yellow supports to horizontal sliders, retaining a few hard XY gauge pins and the old exact-recovery solve | Shape error reached about `8446`; horizontal force at the gauge pins was about `1.44`. | Hard gauge pins and exact FDM recovery did not reproduce v2's soft position preferences or joint fit. |
| Alternating linear geometry and bounded-density solves with yellow supports fixed only in Z, normal offset `-0.05`, and frozen loads | After 50 iterations, all-node horizontal residual was about `0.00044`, but shape error was about `6093`. | It found force balance by moving the geometry excessively. Blockwise minimization did not preserve the coupled geometry-density tradeoff. |
| Analytic coupled linearization solved with sparse QR | A 50-iteration source-mesh probe exceeded 180 seconds without a result. | This was too slow for an interactive sketch; it was not a completed convergence test. |
| Analytic coupled linearization solved with least-squares conjugate gradients | With global step scaling, after 50 iterations total force residual stayed about `1.11`, horizontal residual about `0.356`, and shape error about `0.337`. Capping density and geometry corrections separately stagnated after 22 iterations with total residual about `1.31`. | Density variables near their lower bounds made the log-density step poorly scaled. The iterative linear solve and ad hoc step caps did not produce a useful coupled update. |

The source-mesh probes used frozen fan-triangulated tributary areas, so their numbers are diagnostic measurements, not a side-by-side comparison with v2's updating load model. The diagnostic four-node test was easier: its loads were constructed from a known equilibrium, and its assertion required only a *decrease* in support-force norm. It did not establish that the source mesh would balance with pinned supports.

## Conclusion of this attempt

The fixed-support results do **not** establish infeasibility for the v2 problem. The experimental replacements either solved a different boundary-value problem, moved the mesh too far, or failed to converge at useful speed. No measured v2 solution was available for a numerical side-by-side comparison during these trials. Further solver changes are closed here; the failed experimental code was removed.
