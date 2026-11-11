# GPU mesh rendering

`MeshObject` uses a renderer-owned GPU cache by default. Shared vertex attributes
and triangulated indices are uploaded once and drawn with `glDrawElements`.
Normal shading runs in a GLSL shader, including the normal transform and
front/back colour interpolation. Camera navigation and object transforms change
matrices rather than rebuilding triangle arrays or uploading geometry.

Regular vertex colours, polygon face colours, normal shading, global mesh view
modes, and vertex/edge overlays are supported. Polygon boundary edges are kept;
wireframe does not expose triangulation diagonals. Face colours and coloured
edges need expanded attribute streams, created only when those modes are used.
Normal shading and vertex-colour faces use the compact indexed buffers.

The existing compatibility OpenGL context, matrix stack, UI, text, and primitive
drawing remain available. GPU meshes use GLSL 1.20 and restore program, buffer,
client-array and drawing state after each object. Unsupported contexts or shader
failures fall back to the previous rendering path. Vector export always uses
that path so gl2ps receives the geometry.

## Editing mesh data

`MeshObject` editing methods invalidate the buffers, as do `MeshData::clear()`,
`calculateNormals()` and `triangulate()`. `setMeshData()` and recalculating object
bounds also invalidate the associated data. Existing `triangulationDirty` users
continue to regenerate topology before rendering.

Code that edits public arrays directly must mark the change:

```cpp
auto data = mesh.getMeshData();
data->vertices[0].color = alice2::Color(1, 0, 0);
data->markRenderDirty();
```

For topology edits, also set `data->triangulationDirty = true`. Recalculate
normals and bounds when those need updating. Merely obtaining a mesh pointer
does not invalidate it, and the renderer does not scan all vertices every frame.

Object transforms and shading parameters require no invalidation:

```cpp
mesh.getTransform().translate({1, 0, 0});
mesh.setNormalShadingColors({0.9f, 0.9f, 0.9f}, {0.2f, 0.2f, 0.2f});
```

Buffers belong to the renderer's context, are reused by objects sharing the same
`MeshData`, and are removed when the data expires, is cleared, or the renderer
shuts down. Call `Renderer::shutdown()` before destroying its OpenGL context.

## Checks and benchmark

From `alice2/`, in a prepared viewer build:

```powershell
cmake -S . -B build -DALICE2_BUILD_GPU_CHECKS=ON
cmake --build build --config Release --target alice2_gpu_mesh_checks
.\build\Release\checks\alice2_gpu_mesh_checks.exe --large
```

For the Techlab embedding build, configure `ALICE2_BUILD_GPU_CHECKS=ON` in the
parent and build the target from `build/msvc/alice2`; its executable is placed
in the parent's `build/msvc/Release/checks` folder.

The check requires a working OpenGL display, creates a hidden 512x512 window,
compares framebuffer pixels with the compatibility path, checks edits and cache
lifetime, and measures normal-shaded grids during camera navigation. V-sync is
disabled and `glFinish()` includes GPU completion in the reported frame times.
Initial upload and warmup are excluded. Benchmarks cover 0.52, 2.10, and 4.72
million triangles, plus eight instances totalling 16.78 million. `--large` adds
one mesh containing 8.39 million triangles. Foreground pixel checks reject empty
or offscreen scenes. These are synthetic render timings, not full application
FPS or a guarantee for all GPUs, resolutions, and scene contents.

`Renderer::getMeshRenderStats()` reports cumulative uploads, uploaded bytes,
draw calls, submitted triangles and the live cache size. The navigation checks
require zero additional uploads after warmup. For comparisons or compatibility
troubleshooting, `setBufferedMeshesEnabled(false)` selects the old path.

This cache removes repeated CPU submission work. It does not simplify meshes,
accelerate USD import/welding, add spatial culling, sort transparent surfaces, or
automatically batch separate objects into instanced draw calls. Those remain
separate opportunities as scene complexity grows.
