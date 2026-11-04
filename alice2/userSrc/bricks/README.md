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
| Depth | 0.01 | 0.10 | 0.05 |
| Length A | 0.05 | 0.20 | 0.20 |
| Length B | 0.05 | 0.20 | 0.10 |

Gap must be smaller than contour spacing. Brick dimensions update the population
without recomputing fields or contours. Reload preserves the parameters and camera.

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

Call `generateBricks(BrickOptions{gap, depth, lengthA, lengthB})` after extracting
contours. Read `bricks()` and `contourPaths()` to access solids and ordered paths.
Each brick stores eight corners, its orthonormal frame, actual dimensions, A/B
type, trimmed flag, course/path IDs, arc-length interval, closest triangle and
barycentric weights. No Rhino or Grasshopper types are used.

Contour edges are chained independently per level and connected component.
Open paths begin at the lexicographically smaller endpoint (X, then Y, then Z),
so courses align to one side without vertical splits or planes. Closed paths use
the lexicographically smallest contour vertex as their seam; branching paths break
at junctions. Seam alignment on complicated surfaces may need the future planes.

Lengths A and B alternate along each course. Placement pitches are `length + gap`,
following the visible pattern setup in `Components/DrawBrickPattern.cs`. Odd levels
shift the repeat by half of `lengthA + lengthB + 2*gap`, placing the short-brick
center below the adjacent course's long-brick center. End bricks are shortened to
fit the contour span, with a half-gap margin at both ends (including closed seams).
The shared `Utils.ComputeBricks` / `InstantiateBricks` implementations referenced by
the C# components are absent; this C++ implementation recreates the layout directly.

Each base rectangle is centered on its contour course, with length along a sampled
contour tangent and width `contour spacing - gap`. An AABB query finds the closest
mesh triangle at the brick center. Area-weighted vertex normals are interpolated
using that point's barycentric weights and normalized. The tangent is projected
into the normal plane; the width axis is their cross product. All four base corners
extrude by `depth * interpolatedNormal`, following mesh winding. The base remains
a simple planar rectangle; it does not deform to the mesh. A/B colors distinguish
the two nominal lengths.

There is no boundary trimming, collision resolution, vertical splitting, or plane
alignment in this stage. Tight curvature and nonuniform blended-field spacing can
cause local overlap; closed paths can have a shortened brick at their seam.
Generation rejects invalid dimensions and caps output at 250,000 bricks.

Verification: build `alice2_bricks_checks`, then run `ctest --test-dir build -C Release
-R bricks_fields --output-on-failure`. Checks cover fields, seed constraints, contour
placement/spacing, OBJ colors and invalid inputs, plus Flemish course staggering,
gaps, box dimensions, smooth barycentric normals, closed contours and population
invalidation. Pass an OBJ path to `alice2_bricks_checks.exe` to exercise all three
field methods and populate that mesh. The Stage 2 preview has also been checked
on the current sandbox OBJ.
