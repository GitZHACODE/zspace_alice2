# Research apps

`userSrc/` contains research folders. Each folder declares its sketch apps and
custom classes in `CMakeLists.txt`. The viewer and geometry libraries are shared
between builds; selecting another folder does not compile unrelated sketches.

## Build and run a folder

From `alice2/` on Windows:

```powershell
.\build.bat --base
.\run.bat --base
.\build.bat --bestfit
.\run.bat --bestfit --app=sketch_bestfit_tna_v2
.\build.bat --all
.\build.bat --last
.\run.bat --last
```

`--base` builds all apps declared in `userSrc/base/`; `--bestfit` builds all
configured apps declared in `userSrc/bestfit/`. The default is `--base`.
`--debug`, `--clang`, and `--cuda` select other configurations/toolchains.
`--tests` builds the interactive test sketches.

The former geometry collection is split into these groups:

- `--bestfit`: only sketch_bestfit_tna_v2 is enabled; the other best-fit sketches remain available as commented entries.
- `--gauss_thinning`: gauss_thinning.
- `--miq`: MIQ curvature/stress remeshing and stress-aligned streamlines (requires MIQ enabled).
- `--planarise`: planarise and circular_conical.
- `--tna`: sketch_tna.

Each group has its own output folder, data, and user state. Existing files in the
old `build/Release/geometry/` are left intact; new builds use the groups above.

On Windows, `--all` builds all configured research apps, interactive tests, and
numerical checks (when enabled). Optional modules still follow the CMake options.
`build.bat --last` repeats the last successful build selection, including its
configuration and toolchain; if the last build was `--all`, it builds all again.
`run.bat --last` uses the most recently built or selected individual folder and
remembers its selected executable. Building `--all` preserves that individual
selection, or defaults it to base when there is no history. A multi-app folder
needs an explicit `--app=...` the first time. History is local to this checkout
in `build/.build-history.json`; failed builds do not replace it.

You can override a remembered configuration with `--debug` / `--release` or
`--clang` / `--cuda` / `--msvc`. Pass application arguments after `--`, for example
`run.bat --last -- --list-sketches`.

The equivalent shell commands are `./build.sh --base` and `./run.sh --base`.
Shell builds use Clang/Ninja by default. Windows `--clang` and shell builds use
`build/clang-release/` or `build/clang-debug/` as their shared CMake directory.

A normal Windows build produces:

```text
build/
  Release/
    base/
      sketch_base.exe
      data/
      .user/
      ...third-party DLLs (if needed)
    bestfit/
      sketch_bestfit_tna_v2.exe
      data/
      .user/
```

Windows system libraries and Microsoft C/C++ runtime DLLs are not bundled.
Windows machines need the matching Visual C++ Redistributable installed.
Third-party DLLs, such as GLFW or MIQ's OpenBLAS dependencies, remain app-local.

Each group folder can be copied and run independently on a compatible operating
system. Apps use their own executable directory as the working directory, even
when launched from another directory or through PATH. Apps within the same group
share that group's data and user state. Debug output goes under `Debug/`.

Direct CMake usage remains available:

```powershell
cmake -S . -B build
cmake --build build --config Release --target user_base
cmake --build build --config Release --target sketch_minimal
```

`user_<folder>` builds and stages a whole folder. Individual app targets remain
available. The previous duplicate `alice2` base executable has been removed.
The editor's default Build/Run tasks use these same scripts. F5 has separate
Windows and Linux configurations pointing to their regular Debug output folders.

## Data and settings

Put inputs directly in the app group's runtime directory, for example
`build/Release/bestfit/data/`. Apps read and write `data/...` relative to their
own executable directory; settings stay in the sibling `.user/` directory.
Debug builds use `build/Debug/<group>/data/`; other toolchains use the equivalent
folder under their configured build directory.

There are no data seeds or data directories under `userSrc/`. Builds only create
empty runtime directories if needed and stage third-party DLLs. Rebuilding never
replaces, restores, or deletes runtime data or settings. A fresh build needs its
inputs placed in its own data directory before running the relevant experiment.
The existing data has been preserved in the regular Release group folders.

Do not use source-tree absolute paths or `__FILE__` fallbacks for runtime inputs.
Some experiments require inputs that were never supplied; place those files in
their runtime data directory. WaveLatent inputs and exports also use `data/`.

## Start an experiment

Run `./scripts/setup_user_sketch.ps1 my_study` to copy the minimal example and
register its folder, then run `./build.bat --my_study`.
For a manual copy, rename the copied sketch file to `sketch_my_study.cpp`, add
`add_subdirectory(my_study)` to `userSrc/CMakeLists.txt`, and use this CMake list:

```cmake
set(my_study_sketches
    sketch_my_study
    # sketch_another_experiment
)

foreach(sketch IN LISTS my_study_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp Orbit.cpp
        LIBRARIES alice2_tna)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_my_study_examples SOURCES
#     sketch_my_study.cpp Orbit.cpp
#     LIBRARIES alice2_tna
# )
```

Every research folder uses this pattern. Add, remove, or comment out names in
its `<folder>_sketches` list, then build that folder. An empty list is valid.
Names omit `.cpp`; the corresponding source file must exist. Keep folder library
links in the loop, and add custom implementation files alongside `${sketch}.cpp`
(`Orbit.cpp` is used by the minimal example). `LIBRARIES` is optional.

Collections are commented examples by default. Uncomment one only after listing
its source files; a collection with no sources is not a valid app. Run scripts
use the configured sketch list, so disabled sketches are not selected even if
an executable from an earlier build still exists in the output directory.

Use `<alice2/core.h>` for computation classes and `<alice2/viewer.h>` for sketches.
`ISketch::draw(Renderer&, Camera&)` keeps direct renderer access. Register sketches
with `ALICE2_REGISTER_SKETCH_AUTO(MySketch)`; there are no `__MAIN__` toggles.
`--list-sketches` lists an app's linked sketches without opening a window.

## Libraries and checks

`alice2_core` contains math, transforms, mesh/graph data and tensor data without
OpenGL dependencies. `alice2_viewer` contains the scene, window, rendering, input,
UI, and existing half-edge display wrappers. Algorithms are opt-in targets:
`alice2_fields`, `alice2_projection`, `alice2_analysis`, `alice2_tna`, `alice2_fdm`,
`alice2_stacking`, `alice2_stereotomy`, `alice2_remeshing`, `alice2_wave_latent`.
Some legacy algorithms still accept viewer objects; their APIs are unchanged.

MIQ and Directional are off by default. Enable MIQ with
`-DALICE2_ENABLE_MIQ_REMESH=ON` to include the apps in the miq group.
`field/sketch_voronoi.cpp` remains a reference using retired scalar-field APIs.

```powershell
cmake --build build --config Release --target alice2_core_checks_runtime alice2_checks_runtime
ctest --test-dir build -C Release --output-on-failure
.\build\Release\checks\alice2_checks.exe --check-projected-mixed-fdm
```

The checks also have their own runnable folder. For a geometry-only build, use
`-DALICE2_BUILD_VIEWER=OFF`; no graphics dependencies are configured.
