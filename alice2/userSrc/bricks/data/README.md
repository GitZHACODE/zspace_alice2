Place `input.obj` here, then run `run.bat --bricks` from `alice2`.
Press R or click Reload OBJ after replacing the file.

Mark start vertices red and end vertices blue using OBJ vertex colors:

```
v 0 0 0 1 0 0
v 0 0 10 0 0 1
```

Neutral vertices may omit colors or use white. RGB values can be 0..1 or 0..255.
Materials/texture colors do not define seeds. Mark every vertex along each desired
boundary. No automatic seed selection is performed. Height works without colors;
Exact geodesic needs red vertices; Exact blend needs disjoint red and blue sets.

Use one connected surface with no unused vertices or degenerate triangles.
Maya polygon-corner exports are supported: identical positions are welded on import,
and red/blue marks are combined across copies. A position marked both red and blue
is rejected. Welding uses exact coordinates and does not modify the source OBJ.
Triangles are preferred; convex planar polygons are fan-triangulated on import.
Runtime `input.obj` or `data/input.obj` overrides this source folder.
