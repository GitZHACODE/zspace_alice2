# Mesh Bricks: Stages 1 and 2

Build/run from `alice2`: `build.bat --bricks` / `run.bat --bricks`.
Place your mesh at `userSrc/bricks/data/input.obj`; see [input format](data/README.md).
Select Height Z, Exact geodesic, or Exact blend and adjust Contour spacing.
Exact geodesic is selected on startup. R reloads; F frames
the mesh. Reload preserves the camera and contour spacing; only the first successful
load frames automatically. Spacing ranges from 0.01 to 1.00 mesh units (default 0.50).
Field and Wire toggle the source mesh. Bricks, Brick edges, and Contours toggle
the population and course preview. Brick parameters have these ranges in mesh units:

| Parameter | Minimum | Maximum | Default |
| --- | --- | --- | --- |
| Contour spacing | 0.01 | 1.00 | 0.50 |
| Gap | 0.01 | 0.10 | 0.01 |
| Depth A | 0.00 | 0.10 | 0.05 |
| Depth B | 0.00 | 0.10 | 0.05 |
| Length A | 0.05 | 0.20 | 0.20 |
| Length B | 0.05 | 0.20 | 0.10 |

Gap must be smaller than contour spacing. Brick dimensions update the population
without recomputing fields or contours. Reload preserves the parameters and camera.
Depth A and Depth B apply independently to their corresponding brick types. Zero
depth omits that type's solids while retaining its layout cells, spacing and plane
joins, so the remaining type is not repacked. Both zero removes all brick solids.
Depth sliders snap to their displayed 0.001 precision.
The Brick Maker controls sit on an opaque white panel, grouped by field,
dimensions, alignment and display. Status text wraps within the control area. Geodesic and Blend select the exact
methods. Boundary vertices toggles the red/blue source marks. Input mesh boundary
edges are always drawn as thick black lines. Bricks use two dark brown shades.

`alice2::bricks::BrickGenerator` lives in `src/bricks`, links as `alice2::bricks`,
and has no viewer dependency. Call `loadObj(path)` or `setMesh(V,F)` followed by
`setBoundaryVertices(startIds,endIds)`, then `computeField(method)` and
`extractContours(spacing)`. Eigen matrices and explicit vertex IDs make it callable
from future tools. Exceptions report invalid inputs. Seed changes invalidate fields;
mesh changes invalidate the distance maps. Spacing re-extracts contours and
invalidates the population. Field and seed changes also invalidate the population.

- Height: `z - min(z)` in mesh units.
- Exact geodesic: libigl's exact polyhedral surface geodesic distance to the nearest
  red source vertex, evaluated at every mesh vertex. Its distance map is cached
  until the mesh or seeds change.
  Contours still interpolate the vertex field linearly within each triangle.
- Exact blend: solve exact geodesic maps `dStart` and `dEnd`, then `L*dStart/(dStart+dEnd)`.
  `L` averages mean `dStart` on the end set and mean `dEnd` on the start set.
  Red is explicitly zero; blue is explicitly L. Mark whole boundary chains to
  constrain whole boundaries. This is a boundary-adapted scalar interpolation:
  contour spacing is approximate physical spacing, not a constant-width guarantee.

Both geodesic modes use `igl::exact_geodesic`, replacing the former heat solver.

Interior levels are `min(field) + k*spacing`, strictly below the maximum. Seed
vertices are shown separately in red/blue. libigl isolines returns shared contour
vertices, indexed edges, and per-edge level IDs. Flat height fields produce no contours. Missing
seed colors leave height usable and show an error for the geodesic methods.

## Brick population

Call `generateBricks(BrickOptions{gap, depthA, depthB, lengthA, lengthB})` after extracting
contours. Read `bricks()` and `contourPaths()` to access solids and ordered paths.
Each brick stores eight corners, its orthonormal frame, actual dimensions, A/B
type, trimmed flag, course/path IDs, arc-length interval, closest triangle and
barycentric weights. No Rhino or Grasshopper types are used.

Contour edges are chained independently per level and connected component.
Open paths begin at the lexicographically smaller endpoint (X, then Y, then Z),
so courses align to one side when plane divisions are disabled. Closed paths use
the lexicographically smallest contour vertex as their seam; branching paths break
at junctions. Plane divisions can override the course seam alignment.

Lengths A and B alternate along each course. Placement pitches are `length + gap`,
following the visible pattern setup in `Components/DrawBrickPattern.cs`. Odd levels
shift the repeat by half of `lengthA + lengthB + 2*gap`, placing the short-brick
center below the adjacent course's long-brick center. End bricks are shortened to
fit the contour span, with a half-gap margin at both ends (including closed seams).
With retained plane cuts, the layout follows the supplied `Shared/Utils.cs`
implementations described below. The no-plane layout retains the original nominal pitches.

Each base rectangle is centered on its contour course, with length along a sampled
contour tangent and width `contour spacing - gap`. An AABB query finds the closest
mesh triangle at the layout reference point. Area-weighted vertex normals are interpolated
using that point's barycentric weights and normalized. The tangent is projected
into the normal plane; the width axis is their cross product. All four base corners
extrude by their type's `depthA` or `depthB` times the interpolated normal, following mesh winding. The base remains
a simple planar rectangle; it does not deform to the mesh. A/B colors distinguish
the two nominal lengths.

There is no surface boundary trimming or collision resolution. Tight curvature
and nonuniform blended-field spacing can
cause local overlap; closed paths can have a shortened brick at their seam.
Generation rejects invalid dimensions and caps output at 250,000 bricks.

## Plane divisions and alignment

Place `planes.obj` beside the selected `input.obj`. R / Reload OBJ reads both files,
preserving camera and dimensions. Use planes enables the divisions; Mesh cuts
shows only the plane/triangle intersection segments as thin hot-pink lines on
the input mesh. Plane outlines and reference-point markers are not rendered.
The black boundary and enabled mesh annotations remain visible over the bricks.
Missing plane files preserve the original course layout. Invalid plane files show
an error in the status and leave the mesh available without plane alignment.

OBJ `l` polylines and Rhino degree-1, two-point `curv` records are supported. Each
line defines the plane X axis, world Z defines its Y axis, and their cross product
defines its normal. The planes extend infinitely; the exported line length does
not restrict intersections. Lines parallel to Z are invalid. Planar face records
also define planes. Coincident planes, including opposite normal orientations,
are deduplicated. `setAlignmentPlanes()` accepts origin/normal pairs directly;
`loadPlanesObj()` imports the file without Rhino dependencies.

Every genuine contour/plane crossing is collected in arc-length order. Tangencies
and contours lying within a plane do not split. Multiple intersections with one
plane are handled independently. Before placement, adjacent fragments of length
**less than or equal to `lengthA + 2*lengthB`** merge iteratively: remove the cut
at their right end, except a terminal open-path fragment removes its left cut.
Closed paths apply the same rule cyclically, retaining at least one whole-loop
segment. Removed cuts no longer align bricks. Changing lengths recomputes merging.

Plane-split paths now use the actual sequence from `Shared/Utils.cs`:

1. `BrickCount`: choose the count from the mean A/B pitch, using floor when the
   fractional remainder is at most 0.1, otherwise ceiling. Enforce an even count
   so each segment has identical types at its two endpoints.
2. `ComputeBricks`: equally divide the segment into center intervals of length h.
   B uses pitch `lengthB + gap`; A uses `2*h - (lengthB + gap)`. Endpoint records
   contain only the half facing into the segment.
3. `CombineBrickes`: extend the previous segment's ending half by the next
   segment's starting half, then delete the duplicate starting record. Closed
   paths also combine their first/last halves across the seam.
4. `InstantiateBricks`: apply gap/2 at the two external ends of the combined width,
   never at the plane passing through it. Build one rectangle and one solid.

A references the even courses, B the odd courses, preserving the sandbox's initial
course convention. B's material length remains fixed; A adapts to each segment.
When A and B are equal, aligned bond mode applies the segment fitting equally to
both types. Matching ordered plane pairs share an even subdivision count across
courses, using their mean span to select the count. Corresponding centers retain
the same arc-length fraction through each section as the surface bends, instead
of changing phase or jumping counts. Actual fitted lengths can vary by course.
The length sliders snap to their displayed 0.001 precision so matching displayed
values select this mode reliably.
The joined A can be asymmetric around the plane when neighboring segment lengths
are different. Its layout reference stays on the plane, as in the C# source;
its geometric box center can move along the tangent. The midpoint closure-brick
heuristic has been removed. Without retained cuts the existing nominal-pitch
layout is used. The source's application-specific rounding to 10 mm is omitted:
this class preserves the input mesh units and the exact gap after combining.

`contourSegments()` exposes the merged polylines, source path/course, arc-length
interval and start/end plane IDs. Each brick includes `segmentIndex` and
`alignmentPlaneIndex` (-1 for ordinary bricks). `referencePoint` and
`referenceDistance` retain the placement reference used for its tangent and smooth
normal; `origin` is the geometric center of its base rectangle. Barycentric weights
are evaluated at `referencePoint`, which lies on the closest triangle.

Verification: build `alice2_bricks_checks`, then run `ctest --test-dir build -C Release
-R bricks_fields --output-on-failure`. Checks cover fields, seed constraints, contour
placement/spacing, OBJ colors and invalid inputs, plus Flemish course staggering,
gaps, box dimensions, smooth barycentric normals, closed contours and population
invalidation. Pass an OBJ path to `alice2_bricks_checks.exe` to exercise all three
field methods and populate that mesh. The Stage 2 preview has also been checked
on the current sandbox OBJ.
