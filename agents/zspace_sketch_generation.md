# zSpace Sketch Generation Notes

Use zSpace for geometry construction and alice2 `scene().draw(...)` for display.

Use lowercase `z` for zSpace-related sketch names, wrapper types, and filenames. Prefer names like `zSpaceHalfedgeTraversalSketch`, `zDisplaySetting`, `zDisplayMeshSetting`, `zSpaceDraw.h`, and `zSpaceObject.cpp` rather than leading `ZSpace...` or `ZDisplay...` names.

When creating a `zSpace::zObjectMesh` with `zSpace::zFnMesh::create`, keep face winding consistent so normals point outward. For a pyramid with base vertices `0, 1, 2, 3` and apex `4`, the base face should be wound opposite to the side faces:

```cpp
zSpace::zIntArray faceCounts = {4, 3, 3, 3, 3};
zSpace::zIntArray faceConnects = {
    0, 3, 2, 1,
    0, 1, 4,
    1, 2, 4,
    2, 3, 4,
    3, 0, 4
};
```

Before finalizing generated mesh code, check each face winding against the intended outward normal direction. Do not assume vertex order is correct just because the mesh displays.

## Halfedge Traversal Display

For halfedge traversal sketches, draw each highlighted halfedge as a short directed arrow rather than the full edge:

- arrow segment length: about `0.1 * edgeLength`
- arrow position: centered on the corresponding edge
- arrow offset: slightly toward the face that owns that halfedge
- avoid artificial z stepping; keep arrows at the level of their edge/face

Use this color mapping:

- current: black
- next: cyan
- previous: green
- symmetry/twin: magenta
