# Rectangle packing sketch

Build from the repository root:

```powershell
cmake --build alice2/build --config Release --target sketch_rectangle_packing rectangle_packing_checks
ctest --test-dir alice2/build -C Release -R '^rectangle_packing$' --output-on-failure
& ./alice2/build/Release/rectangle_packing/sketch_rectangle_packing.exe
```

The sketch reads `data/packing.json` beside its executable. The build seeds that file
from the source example if it is missing, preserving existing edits on subsequent
builds. Existing root-level input is copied into `data/` on the first build if needed.
Edit the JSON in `alice2/build/Release/rectangle_packing/data/` and press **R**
to reload. There is no fallback to the source folder. For standalone use, copy
the whole executable folder, including its DLLs, `usd` resources and `data/packing.json`.
alice2 sets its working directory to that folder even when launched elsewhere.
**Show active field** toggles the coloured grid dots. **S** exports `packing_result.json` into the working
directory. The view frames the polygon at startup; **R** preserves the current
camera position, orientation and zoom. Use the normal orbit controls.

The viewport has a white background and dark UI controls. The region and mask
share thick dark outlines; the mask is filled medium grey. Rectangle types use
a vibrant categorical colour palette. The inner rectangle
is used for computation but is not drawn. The **Inset** slider (0–10
model units, default 0.2) moves every displayed rectangle edge inwards. Rectangles that
collapse at the selected inset are hidden. This display setting does not change
packing, coverage statistics, or exported placement geometry.

Input points are `[x,y]` in the XY plane. Polygon and optional mask must be simple,
with a mask strictly inside the outer polygon. An empty mask is allowed. Repeating
the first point to close a loop is optional. Rectangles have positive fixed `w`
and `h`, a unique `id`, and `targetShare` between 0 and 1; shares must sum to 1.
Types repeat freely unless an optional integer `maxCount` is supplied.

Percentages are fractions: `targetCoverage: 0.85` means 85% of outer area minus
mask area. `targetShare: 0.6` means 60% of the total placed rectangle area, rather
than 60% of rectangle count. `shareTolerance: 0.05` allows five percentage points
of deviation per type. Coverage is maximised within this tolerance; if the search
finds no compliant layout, it returns its best mix approximation and flags it.
The coverage target is not a cap on additional packing; Type mode also uses it
for the coverage safeguard described below.

The **Targets** panel provides a coverage target slider and an area-share slider
for each type. Moving one type's slider keeps that value and redistributes the
remaining percentage proportionally across the other types, always totalling
100%. If all other shares are zero, the remainder is divided equally. A single
type stays at 100%. Target edits repack automatically and apply in either mode.
**W** saves the edited coverage and shares along with the current configuration.
Targets and controls share one column, with targets at the top and mode controls
below. Scroll over the panel to reach lower controls on smaller windows; scrolling
over the scene keeps the usual camera zoom behaviour.

`gridSpacing` controls the point lattice. `gridAngleDegrees` sets its orientation.
A histogram search finds the largest rectangle made from entirely usable grid
cells at this orientation and resolution. This is an approximation to the
continuous largest inscribed rectangle. Its corner anchors the same lattice,
which extends across the polygon. The sketch does not yet search grid angles
or offsets automatically.

Only the footprint's lower-left corner in the grid frame snaps to a point.
Rectangle dimensions remain exact and need not be multiples of grid spacing.
`allowRotation90` allows swapping width and height. Full footprint checks reject
outer-boundary crossings, concave notches, and enclosed or crossing masks.
Boundary contact between footprints and with polygon/mask edges is allowed;
anchors on the mask boundary are filtered out.

The solver uses structured sweeps, shuffled candidate orders and area-share deficits. Every third
attempt removes a quarter of the current best layout and repacks it. `attempts`
controls effort and `seed` gives repeatable results. Grid generation is limited
to 100,000 cells and 500,000 candidates; increase spacing for larger inputs.
Computation runs synchronously when loading; dense inputs can take time.

Output includes coverage, target and share status, per-type counts and shares,
and world-space corners for every placement. `anchor` is the world-space grid
point at the footprint's lower-left. `footprintWidth` and `footprintHeight` are
measured in the frame rotated by `gridAngleDegrees`; `typeRotated90` indicates
whether the original type dimensions were swapped. `optimalityProven` is false:
this heuristic cannot prove infeasibility or global optimality.

## Modes

The **Area mode / Type mode** buttons switch the solver as well as the preview.
Only controls belonging to the active mode are shown. In Area mode, type
attractors have no effect. In Type mode, boundary settings have no effect.
Both modes retain the polygon, mask, grid, inventory and requested type mix.
Inset and field visibility are shared display controls. Switching modes preserves
the inactive settings for later use and triggers repacking.

Set `settings.mode` to `"area"` or `"type"`. Omitted mode defaults to Area.
The sample starts in Type mode, with initial positions, radii and strengths for
A, B and C in the top-level `attractors` array. Type selection automatically
selects that type's first point, and the sliders display its loaded values.

## Area mode: boundary value field

The **Outer / Inner / Both** buttons select which boundary attracts small
rectangles. **Boundary influence width** is a falloff distance in model units;
**Boundary preference strength** ranges from 0 (original coverage-focused search)
to 1 (strong spatial preference). Controls update the heatmap immediately and
repack after a short pause. The **Show active field** toggle shows values on the point
lattice: red favours small types, blue favours large types. There are no separate
grey anchor dots. The heatmap displays the field even at strength 0.
Inset remains a separate display-only control.

Optional input settings, retained in the sample for switching to Area mode:

```json
"boundaryField": {
  "source": "both",
  "influenceWidth": 12,
  "strength": 1.0
}
```

Place this object within `settings`. If omitted, strength defaults to 0 for
compatibility. The field is `exp(-distance / influenceWidth)`, measured to the
nearest selected boundary segment. For Both, use the nearer of the two boundaries.
With no mask, Inner gives zero influence everywhere. Candidates sample the field
at their centres. Type areas are normalised between the smallest and largest
available types; equal-area types receive no size preference.

The solver retains a coverage-focused warm start, biases candidate order and
sampling, and compares compliant layouts using
`coverage - 2 * strength * areaWeightedFieldMismatch`. Mix tolerance remains the
first priority. The field is a soft preference: it does not forbid any otherwise
valid type. Stronger preferences can reduce coverage, including below the
requested coverage target. This heuristic cannot guarantee monotonic changes
while adjusting sliders. Export includes field settings and mismatch statistics.

## Type mode: attractors

Choose **Type mode**, choose a type, enable **Edit attractors**, then click in
the usable region to place a point. Click an existing marker to select it, and
drag it to move it. Left-drag camera orbit is suspended during attractor editing;
disable point editing to orbit normally. Markers and rings use their type's colour.
The ring indicates the half-strength radius, not a hard cutoff. Points can be
placed freely rather than snapping to the anchor lattice.

Radius and strength sliders edit the selected attractor, or set defaults for
the next point when none is selected. Switching type selects its first existing
point. Use **Delete point** or **D** to remove the selection. Multiple
points can belong to one type. **Show active field** shows the selected type's
attraction values on the grid. Area controls and area-field penalties are inactive.

Add an optional top-level array to the input JSON:

```json
"attractors": [
  {"type":"A", "point":[25,65], "radius":20, "strength":0.8},
  {"type":"B", "point":[70,30], "radius":12, "strength":1.0}
]
```

Points use world XY coordinates, including when the grid is rotated, and must
lie in the usable region. Type IDs must match `rectangles`. Radius must be
positive and strength must be between 0 and 1. If omitted, no attractors are
added. **W** saves an editable `packing_config.json` containing the current
attractors, active mode and boundary settings without overwriting the input. To reload it,
copy its contents into your `packing.json` and press R. **S** includes attractors
and an attraction statistic in the placement export. R discards unsaved edits.

For a candidate of the matching type, attraction at its centre is
`strength * exp(-ln(2) * (distance / radius)^2)`. At one radius it is half strength;
at two radii it is 6.25% of strength. Multiple matching attractors combine by
maximum, not summation. Other types get no bonus from that point. This works for
equal-area types as well as different sizes.

Far placements also incur a quadratic cost:
`strength * (distance / radius)^2`, measured from the rectangle centre. At one
radius the cost equals strength; at two radii it is four times strength; at
four radii it is sixteen times strength. Unlike the Gaussian bonus, this cost
keeps increasing far away. With multiple active attractors of the same type,
use the lowest cost as an alternative destination. Points at strength zero are
ignored for penalties; types without active attractors have no distance cost.
The heatmap and half-strength rings continue to show the Gaussian influence.

The Type mode compliant-layout score is
`coverage + 0.75 * attractionScore - 1.5 * attractorDistancePenalty`,
where both type statistics are weighted by placed rectangle area. Candidate
ordering prefers lower distance costs, and partial repacking alternately removes
random rectangles or the most distant ones. A final relocation and pair-exchange
pass improves type positions while preserving counts, coverage and valid geometry.
Type mode also tries moving up to eight blocking rectangles together and
reinserting them with bounded backtracking. It accepts a rearrangement only
when every displaced rectangle fits again and the combined attraction score
improves. This lets large rectangles approach their attractors even when several
smaller rectangles occupy the destination. It preserves rectangle inventory,
mix and coverage; dense or obstructed regions can still limit clustering.
Smaller radii and higher strengths
encourage tighter groups. Export includes `attractorDistancePenalty`.

Mix tolerance remains the first priority. With active type attractors and a
mix-compliant coverage-focused reference layout, the coverage floor is
`min(targetCoverage, referenceCoverage) * typeCoverageRetention`. Set
`settings.typeCoverageRetention` between 0 and 1 (greater than zero). The default
is 1.0; the example uses 0.8 so tighter groups can win even when they cover up to
20% less area than the reference threshold. It does not affect Area mode.
This prevents stronger clustering from selecting just
a handful of rectangles. If a compliant reference cannot be found, the heuristic
continues searching for a valid mix without that coverage floor. Setting every
attractor strength to zero restores the same packing as having no attractors.
These points encourage clusters; they do not constrain the resulting groups'
actual centroids.
