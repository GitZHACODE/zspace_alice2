# GPU mesh rendering

Meshes default to `MeshRenderMode::FlatShaded`. View key **4** selects the global
`SceneRenderMode::MeshFlatShaded` override (replacing normal shading on that key).
Each triangle gets a geometric normal in the fragment shader from view-position
derivatives. Shared/welded vertices and smooth/imported normals do not soften its
edges. This uses the compact indexed buffers without expanded normal streams.

The default matte light is a camera-relative key above-left plus 20% ambient fill.
No scene light objects are needed. `Renderer::setDirectionalLight(direction,color)`
sets the camera-space ray direction and key colour; `setAmbientLight(color)` sets
the fill (scene rendering supplies the scene's ambient colour). Visible back faces
are lit from their visible side. Lighting and navigation only update uniforms and
matrices. `Lit` retains unlit colours; `NormalShaded` remains available explicitly
through the mesh/renderer APIs.

`MeshObject::getMaterial()` / `setMaterial()` own the surface independently of
`MeshData`. A material holds base colour/opacity, metallic, roughness, specular
and the colour convention; vertex/face colours remain geometry attributes. Two
objects can share one mesh buffer with different finishes. `duplicate()` and USD
scene snapshots retain the material. `SceneItem` carries it through import/export.

Flat shading evaluates diffuse lighting in linear RGB, adds a restrained
roughness-dependent Blinn highlight and broad ambient reflection, then encodes
the result to sRGB for display. It is compact studio shading, not a full PBR or
environment-map renderer. Imported `UsdPreviewSurface` base colour, opacity,
metallic, roughness and specular inputs are retained; source linear Rec.709/sRGB
primaries are assumed. Native snapshot attributes preserve those values. Portable
USD display colours combine material and geometry colours while exact attributes
keep their original factors for alice2. Procedural display colours are decoded
before lighting; unlit UI/graph colours are unaffected. Raw arrays and the
compatibility/vector-export path bake matching lighting at triangle vertices;
the GPU evaluates highlights per fragment with geometric triangle normals.

Mesh rendering uses GPU buffers by default. `MeshObject` uses a renderer-owned
GPU cache. The raw-array `Renderer::drawMesh()` API uses reusable streaming buffers,
uploading the supplied arrays each call because pointers carry no lifetime or
revision information. In-place edits therefore remain visible without explicit
invalidation. Raw-array lighting continues to use the compatibility pipeline.
Use persistent `MeshObject`/`MeshData` for large static geometry to avoid per-frame
uploads; the bricks sketch caches both its field and brick previews this way.
Changing brick-generation parameters replaces the preview; camera navigation
reuses its resident buffers. Line/point helpers remain separate drawing APIs.

For cached meshes, Shared vertex attributes
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
