# Alice2

Alice2 is a C++ research framework with a reusable 3D viewer and geometry core.
Custom classes and sketches live in separate research folders. Each enabled sketch
builds as an app that links the compiled framework and the algorithm libraries it
needs, keeping unrelated sketches out of the edit-build-run cycle.

- [Requirements](#requirements)
- [Build and run](#build-and-run)
- [Research folders](#research-folders)
- [Enable or disable sketches](#enable-or-disable-sketches)
- [Create an experiment](#create-an-experiment)
- [Runtime data and settings](#runtime-data-and-settings)
- [Checks](#checks)
- [Citing](#citing)
- [License and dependencies](#license-and-dependencies)

## Requirements

- Git and Git LFS (`git lfs install`).
- CMake 3.21 or newer and a C++20 compiler.
- Windows: Visual Studio 2022 or its C++ Build Tools with a Windows SDK for the
  default MSVC build. Clang/Ninja is available through `--clang`.
- Linux/macOS: Clang and Ninja, plus the required system development libraries.
- OpenGL, GLFW, Eigen, nlohmann/json, STB, and gl2ps; Windows also uses GLEW.
  MSVC can use dependencies prepared under `alice2/depends/`. Clang builds prefer
  system packages. See [dependency setup](alice2/cmake/Dependencies.cmake).

MIQ/CoMiSo, Directional, and CUDA are optional and disabled by default. Enable an
optional integration in CMake before building sketches that require it, for example:

```powershell
cd alice2
cmake -S . -B build -DALICE2_ENABLE_MIQ_REMESH=ON
```

## Build and run

From the repository root on Windows:

```powershell
cd alice2
.\build.bat --base
.\run.bat --base

.\build.bat --bestfit
.\run.bat --bestfit
```

`build.bat` with no arguments selects `base`. Each folder build compiles its enabled
apps and reuses the common libraries in the regular `alice2/build/` directory.
`bestfit` currently enables only `sketch_bestfit_tna_v2`.

```powershell
.\build.bat --all                 # Build all enabled apps and checks
.\build.bat --last                # Repeat the last successful build selection
.\run.bat --last                  # Run the last individual folder/app selection
.\build.bat --base --debug        # Debug configuration
.\build.bat --base --clang        # Clang/Ninja toolchain
.\build.bat --base --cuda         # CUDA configuration

.\run.bat --planarise --app=sketch_circular_conical
.\run.bat --last -- --list-sketches
```

For a folder with multiple enabled apps, select one with `--app=...`; subsequent
`run.bat --last` calls remember it. `build.bat --last` also remembers the configuration
and toolchain. After an `--all` build, `build.bat --last` repeats all, while
`run.bat --last` keeps the previous individual selection. Failed builds do not replace
history, which is stored locally in `build/.build-history.json`.

Use `--debug` / `--release` and `--clang` / `--cuda` / `--msvc` to override a remembered
configuration. Pass application arguments after `--`. Run either batch script with
`--help` for its command syntax.

Native Linux/macOS builds use the shell scripts:

```bash
cd alice2
./build.sh --base
./run.sh --base
./build.sh --bestfit --debug
./run.sh --bestfit --debug
```

These scripts use Clang/Ninja, with build directories under `build/clang-release/`
or `build/clang-debug/`. The `--all` and `--last` shortcuts above are Windows batch
commands. The current refactor has been build-tested on Windows; native Linux/macOS
builds have not yet been verified.

The editor's default Build/Run tasks use the same regular scripts. The debugger has
separate Windows and Linux configurations for the base app.

## Research folders

```text
alice2/
  src/                     # Compiled framework and algorithm implementations
  include/                 # Public headers
  userSrc/
    base/
    bestfit/
    field/
    gauss_thinning/
    miq/
    planarise/
    tna/
    stack/
    waveLatent/
    minimal/               # Custom class + direct renderer example
  tests/                   # Interactive test sketches
  checks/                  # Numerical checks
  build/
    Release/
      bestfit/
        sketch_bestfit_tna_v2.exe
        data/
        .user/
```

Each configured research folder has its own `CMakeLists.txt` and build flag, such as
`--tna`, `--field`, or `--stack`. `miq` includes curvature/stress remeshing and
stress-aligned streamlines when MIQ is enabled. `planarise` includes planarisation
and circular/conical constraints. Use `--tests` for the interactive test sketches.

The framework is split into `alice2_core`, `alice2_viewer`, and optional algorithm
libraries. New computation classes can include `<alice2/core.h>`; sketches can include
`<alice2/viewer.h>`. `ISketch::draw(Renderer&, Camera&)` retains direct renderer access.

## Enable or disable sketches

Edit the `<folder>_sketches` list in that folder's `CMakeLists.txt`. Names omit `.cpp`:

```cmake
set(bestfit_sketches
    sketch_bestfit_tna_v2
    # sketch_bestfit_tna
    # sketch_projected_mixed_fdm
    # sketch_tna_offset_bestfit
)

foreach(sketch IN LISTS bestfit_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp
        LIBRARIES alice2_tna alice2_json)
endforeach()
```

Add, remove, or comment out names, then run `build.bat --bestfit` again. Empty lists
are valid. Keep the folder's library links and add custom implementation files to
`SOURCES` when needed; the minimal example also compiles `Orbit.cpp`.

Optional collections are provided as commented examples in each research folder.
Uncomment a collection only after listing its source files. Its switcher sees only
those linked sketches. Run scripts use the configured app list and ignore disabled
sketches even if old executables remain in the output folder.

## Create an experiment

From `alice2/` on Windows:

```powershell
.\scripts\setup_user_sketch.bat my_study
.\build.bat --my_study
.\run.bat --my_study
```

The generator copies the minimal example, creates an editable sketch-list template,
and registers the folder. Keep custom classes beside the sketch; register the sketch
with `ALICE2_REGISTER_SKETCH_AUTO(MySketch)`.

For manual setup, add the folder with `add_subdirectory(my_study)` in
[`userSrc/CMakeLists.txt`](alice2/userSrc/CMakeLists.txt), and give its app target a
unique name. See the [research workflow](alice2/userSrc/README.md) for more detail.

## Runtime data and settings

Put inputs directly in the app group's runtime folder, for example
`alice2/build/Release/bestfit/data/`. Apps use their executable directory as the
working directory, so inputs and exports use `data/...` and settings use `.user/`.
Apps in the same group share those directories.

There is no `data/` directory under `userSrc/` and no source-data seeding. Builds
create missing runtime directories and stage third-party DLLs; they do not overwrite
inputs, exports, or settings. Fresh builds need their experiment inputs supplied
in the corresponding runtime `data/` folder.

MSVC Debug output uses `build/Debug/<folder>/`; other toolchains use the equivalent
configuration/folder layout under their own build directory. Runtime folders can be
moved on a compatible platform with the required system libraries installed.
Windows system and Microsoft C/C++ runtime DLLs are not bundled; the matching Visual
C++ Redistributable is a machine prerequisite. Required third-party DLLs, such as
GLFW or OpenBLAS dependencies, can be included beside the executable.

## Checks

From `alice2/`:

```powershell
.\build.bat --all
ctest --test-dir build -C Release --output-on-failure
```

Numerical checks are separate from sketch apps and appear under
`build/Release/checks/`. To build just those targets:

```powershell
cmake --build build --config Release --target alice2_core_checks_runtime alice2_checks_runtime
```

For geometry-only work, configure a separate build with
`-DALICE2_BUILD_VIEWER=OFF`, then build `alice2_core` or `alice2_core_checks_runtime`.
This skips graphics dependency setup.

## Citing

If you use Alice2 in a project, please refer to this repository:

```bibtex
@misc{zspace-framework,
  title = {{alice2}: A simple 3D viewer and C++ collection of geometry data-structures and algorithms},
  author = {Taizhong Chen},
  note = {https://github.com/GitZHCODE/zspace_alice2},
  year = {2025}
}
```

## License and dependencies

The framework is licensed under the [MIT License](https://opensource.org/licenses/MIT).
Third-party dependencies have their own licenses.

- [OpenGL](https://www.opengl.org/), [GLFW](https://www.glfw.org/), and
  [GLEW](https://glew.sourceforge.net/) provide graphics and windowing support.
- [STB](https://github.com/nothings/stb) provides text and image utilities;
  [gl2ps](https://geuz.org/gl2ps/) supports vector export.
- [Eigen](https://eigen.tuxfamily.org/) provides linear algebra;
  [nlohmann/json](https://github.com/nlohmann/json) provides JSON support.
- [libigl](https://github.com/libigl/libigl) / CoMiSo and
  [Directional](https://github.com/avaxman/Directional) support optional geometry integrations.
- [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) supports optional GPU computation.
