# Mesh Bricks: Stage 1

Build/run from `alice2`: `build.bat --bricks` / `run.bat --bricks`.
Place your mesh at `userSrc/bricks/data/input.obj`; see [input format](data/README.md).
Select Height Z, Exact geodesic, or Exact blend and adjust Contour spacing.
Exact geodesic is selected on startup. R reloads; F frames
the mesh. Reload preserves the camera and contour spacing; only the first successful
load frames automatically. Spacing ranges from 0.01 to 1.00 mesh units (default 0.50).
Field and Wire toggle display layers. No input mesh is bundled.

`alice2::bricks::BrickGenerator` lives in `src/bricks`, links as `alice2::bricks`,
and has no viewer dependency. Call `loadObj(path)` or `setMesh(V,F)` followed by
`setBoundaryVertices(startIds,endIds)`, then `computeField(method)` and
`extractContours(spacing)`. Eigen matrices and explicit vertex IDs make it callable
from future tools. Exceptions report invalid inputs. Seed changes invalidate fields;
mesh changes invalidate the distance maps. Spacing only re-extracts contours.

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
vertices, indexed edges, and per-edge level IDs, retained for Stage 2. There is no
splitting or brick generation yet. Flat height fields produce no contours. Missing
seed colors leave height usable and show an error for the geodesic methods.

Verification: build `alice2_bricks_checks`, then run `ctest --test-dir build -C Release
-R bricks_fields --output-on-failure`. Checks cover fields, seed constraints, contour
placement/spacing, OBJ colors and invalid inputs. Viewer interaction and the actual
input mesh still need user testing.
