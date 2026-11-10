# USD scene I/O

USD scene I/O is a standard viewer dependency. `alice2_core` and core-only builds
remain independent of USD and graphics.

## SDK setup

Windows builds automatically prepare the existing, checksum-pinned `USD_Lite.zip`
under `alice2/depends/USD_Lite`. It is a Release Windows SDK; use a compatible MSVC
ABI/compiler/runtime. Build a matching SDK for other platforms or configurations.

To reproduce a smaller SDK from OpenUSD 25.11, from `alice2/`:

```sh
python scripts/dependencies.py usd
```

The build tool requires Python 3.9+, CMake, Ninja, a C++ compiler and TBB development
files. On Windows, run in the compiler's developer shell and supply a TBB install:

```powershell
python scripts/dependencies.py usd --tbb-root C:/deps/tbb
```

`--prefix`, `--source-dir`, `--build-dir`, `--generator`, `--config` and `--jobs`
support alternate SDK locations and offline/prepared source checkouts. The default
uses two parallel jobs to limit compiler memory consumption. A Debug SDK should use
a separate prefix and build directory. Select an SDK with:

```sh
cmake -S . -B build -DALICE2_USD_ROOT=/path/to/USD_Lite
```

If TBB is outside the system search paths, also supply
`-DCMAKE_PREFIX_PATH=/path/to/tbb` when configuring alice2.

Use the same compiler and C++ standard library for the SDK and its consumers when
possible. For alice2's Linux Clang presets, build the SDK with
`CC=clang CXX=clang++ python scripts/dependencies.py usd` in a fresh SDK build
directory. USD 25.11 has compiler-dependent container layouts; the scene bridge
keeps its transform cache inside the SDK to avoid exposing that layout to alice2.

The OpenUSD source revision is pinned and checksummed in
`scripts/dependencies.py`. The SDK recipe turns off Python, imaging/Hydra, GPU APIs,
viewers, Exec, validation, CLI tools, examples, tutorials, tests and optional plugins.
One additional CMake allowlist keeps only `ar`, `kind`, `sdf`, `sdr`, `pcp`, `usd`,
`usdGeom`, and `usdShade` from USD's schema/library directory. Their base-library
dependencies remain. The allowlist is applied to the prepared source checkout;
there is no long-lived OpenUSD fork. Use a fresh prefix when measuring size so old
installations don't leave unused resources behind.

The resulting monolithic runtime uses TBB and normal platform libraries. SDK headers
are development assets and are not staged with apps. Windows DLLs and USD plugin
resources are staged by the existing runtime helper. Linux/macOS builds use SDK
library paths through CMake; relocated apps need their SDK runtime available.
Release installs strip symbols where the toolchain supports it.
`ALICE2_USD_PLUGIN_DIR` can override resource discovery for a custom installation.

## Scene import/export

Viewer sketches already include the API through `<alice2/viewer.h>`:

```cpp
alice2::loadScene(scene(), "data/scene.usda");        // append
alice2::loadScene(scene(), "data/scene.usdc", true);  // replace after successful read
alice2::saveScene(scene(), "data/export.usdc");
```

Mesh polygon topology, graph topology, names, world transforms, visibility and basic
colors/opacity are stored. Bound material diffuse color and opacity are imported
without evaluating texture/shader networks. USD linear curves import as graphs; points import as
graphs without edges. Exported graph curves also carry custom vertex/edge arrays
to preserve shared/isolated vertices and duplicate edges exactly. Other USD readers
can display the curve edges; alice2 understands the exact graph arrays.

Import resolves nested transforms and references through USD, converts to metres
and Z up, rebuilds mesh boundary edges/normals, and releases the stage. Hierarchy is flattened
into object world transforms. Shear/reflection that alice2's TRS cannot represent
is baked into geometry. No USD types enter public alice2 headers. Headless geometry
tools can link `alice2_scene_io` and use `readSceneFile` / `writeSceneFile`, whose
temporary records contain existing `MeshData` / `GraphData` and matrices.

This is a static geometry interchange API. It does not preserve arbitrary USD
metadata, animation, live layer editing, or texture/shader networks. Cubic curves are rejected. Unsupported non-geometry prims are ignored on
import; exporting scene objects other than meshes/graphs reports an error instead
of silently dropping them. Authored per-face and per-vertex alice2 colors round-trip;
external display colors support constant, uniform, vertex and varying interpolation.

## Checks

```sh
cmake --build build --config Release --target alice2_scene_io_checks_runtime
ctest --test-dir build -C Release -R '^scene_io$' --output-on-failure
cmake --build build --config Release --target alice2_scene_objects_checks_runtime
ctest --test-dir build -C Release -R '^scene_objects$' --output-on-failure
```

OpenUSD and TBB retain their upstream licenses.
