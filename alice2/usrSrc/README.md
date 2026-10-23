# Research apps

`usrSrc/` is the place for custom classes and sketches. Each app links the compiled
viewer and only the algorithm libraries it needs. Sources are selected by CMake;
there are no `__MAIN__` switches.

From `alice2/`, configure once, then build the target you are working on:

```powershell
cmake -S . -B build -DALICE2_ENABLE_MIQ_REMESH=OFF
cmake --build build --config Release --target sketch_minimal
.\build\bin\Release\sketch_minimal.exe
```

With Clang/Ninja, use `cmake --preset clang-debug` and
`cmake --build --preset clang-debug --target sketch_minimal`.
The executable is `build/clang-debug/bin/sketch_minimal` (plus `.exe` on Windows).
`build.bat clang sketch_minimal` and `build.sh debug sketch_minimal` select targets too.
The default `alice2` target runs the base sketch; it does not build every experiment.

## Start an experiment

Run `./scripts/setup_user_sketch.ps1 my_study` to copy the minimal example and
register its folder, or copy `minimal/` to a folder such as `my_study/` manually. For a manual copy, rename its CMake target and add
`add_subdirectory(my_study)` to `usrSrc/CMakeLists.txt`:

```cmake
alice2_add_sketch(sketch_my_study
    SOURCES sketch.cpp MySolver.cpp
    LIBRARIES alice2_tna)
```

`LIBRARIES` is optional. List your custom implementation files in `SOURCES`.
They stay in your folder and do not become framework code. Reconfigure after
adding a folder or changing the source list. Ordinary source edits only need a build.

Include `<alice2/core.h>` in computation classes and `<alice2/viewer.h>` in sketches.
`alice2.h` remains a compatibility umbrella for existing sketches. Modules have
separate headers, for example `<tna/TnaSolver.h>`.
`ISketch::draw(Renderer&, Camera&)` retains direct access to the renderer;
`scene()`, `camera()`, `input()`, and `renderer()` are also available to sketches.
`minimal/` demonstrates a custom class and direct renderer calls.

Register a sketch with `ALICE2_REGISTER_SKETCH_AUTO(MySketch)`.
Sketch sources belong directly to executable targets, so registration is retained
by the linker. `--list-sketches` prints the sketches linked into an app without
opening a window.

List several sketch source files to create a collection. `sketch_field_examples`
is a working example; its switcher only sees that collection.

## Libraries

- `alice2_core`: math, transforms, mesh and graph data, tensor data. No OpenGL,
  windowing, or external dependencies.
- `alice2_viewer`: window, rendering, camera, UI, scene objects, sketch lifecycle,
  and the existing half-edge mesh/graph display wrappers.
- `alice2_fields`, `alice2_projection`, `alice2_analysis`, `alice2_tna`,
  `alice2_fdm`, `alice2_stacking`, `alice2_stereotomy`, `alice2_remeshing`,
  `alice2_wave_latent`: opt-in compiled modules.

Some legacy modules still accept viewer objects (`ComputeMesh` inherits
`MeshObject`). They link the viewer explicitly; this first refactor does not
change their numerical APIs. New computation classes can use `MeshData` and
`GraphData` directly. `alice2_fdm` and `alice2_stacking` already work without the
viewer at the target dependency level.

MIQ and Directional are off by default. Enable MIQ at configure time with
`-DALICE2_ENABLE_MIQ_REMESH=ON` to expose the MIQ sketch targets and build CoMiSo.
Libraries are reused within the same build directory and configuration. Changing
public headers or compiler options can still rebuild their consumers. Separate
apps still need to link after their code changes.

Apps start with `alice2/` as their working directory, preserving existing
`data/...` paths. Put experiment-specific files under `usrSrc/my_study/data/`
and use that path, or resolve them relative to `__FILE__`.

The old `field/sketch_voronoi.cpp` is retained as a reference and has no app
target: it uses the retired `ContourData`, `sample_nearest`, and `gradientAt` APIs.

## Checks

Numerical commands previously embedded in the viewer live in `alice2_checks`:

```powershell
cmake --build build --config Release --target alice2_core_checks alice2_checks
ctest --test-dir build -C Release --output-on-failure
.\build\bin\Release\alice2_checks.exe --check-projected-mixed-fdm
```

For geometry-only work, configure with `-DALICE2_BUILD_VIEWER=OFF`. This avoids
finding or downloading graphics and algorithm dependencies entirely; build
`alice2_core` or `alice2_core_checks` in that configuration.
