# ZSPACE
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/gitzhcode/zspace_core/LICENSE.MIT) [![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://github.com/gitzhcode/zspace_core/doxyoutput/) [![GitHub Releases](https://img.shields.io/github/release/gitzhcode/zspace_core.svg)](https://github.com/gitzhcode/zspace_core/releases) [![GitHub Issues](https://img.shields.io/github/issues/gitzhcode/zspace_core.svg)](http://github.com/gitzhcode/zspace_core/issues)

**ZSPACE** is as a C++  library collection of geometry data-structures and algorithms framework. It is implemented as a header-only C++ library, whose dependencies, are header-only or static libraries. Hence **ZSPACE** can be easily embedded in C++ projects. 

Optionally the library may also be pre-compiled into a statically  or dynamically linked library, for faster compile times.

- [ZSPACE](#zspace)
- [Installation](#installation)
- [Launching](#launching)
- [Citing](#citing)
- [License](#license)
- [Third party dependencies](#third-party-dependencies)

# Installation

1. Install [Git](https://git-scm.com/downloads)
2. Install Git LFS by running: `git lfs install`
3. Install [CMake](https://cmake.org/download/) 3.21 or newer
4. For Linux/macOS, install `clang++` and Ninja (for example, on Ubuntu/Debian: `sudo apt install clang ninja-build`; on Arch Linux: `sudo pacman -S clang ninja`).
5. For the recommended Windows build without Visual Studio:
   - Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 CLANG64** shell.
   - In that shell, install the required packages:
   ```sh
   pacman -S --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-glfw mingw-w64-clang-x86_64-glew mingw-w64-clang-x86_64-eigen3 mingw-w64-clang-x86_64-nlohmann-json mingw-w64-clang-x86_64-stb
   ```
6. Clone this repository:
   ```sh
   git clone <repo-url>
   ```

# Launching

1. Linux/macOS recommended:
   ```sh
   cd alice2
   ./build.sh
   ./run.sh
   ```
   Useful modes:
   ```sh
   ./build.sh debug
   ./run.sh debug
   ./build.sh test
   ./run.sh test
   ```
2. Windows PowerShell:
    ```sh
    cd alice2
    ./build.bat clang
    ./run.bat clang
    ```
3. Alternatively, build using CMake directly:
   ```sh
   cd alice2
   cmake --preset clang-release
   cmake --build --preset clang-release
   ```
4. Legacy Windows build:
    ```sh
    cd alice2
    ./build.bat
    ./run.bat
    ```
    OR with cuda

    ```sh
    cd alice2
    ./build.bat cuda
    ./run.bat cuda
    ```

clangd uses `alice2/build/clang-debug/compile_commands.json`; configure once with `./build.sh debug` for editor indexing.

# Citing
If you use Alice2 in a project, please refer to the GitHub repository.

@misc{zspace-framework,
      title  = {{alice2}: A simple 3D viewer and C++ header-only collection of geometry data-structures, algorithms.},
      author = {Taizhong Chen},
      note   = {https://github.com/GitZHCODE/zspace_alice2},
      year   = {2025},
    }

# License
The library is licensed under the [MIT License](https://opensource.org/licenses/MIT).


# Third party dependencies
The library has some dependencies on third-party tools and services, which have different licensing as listed below.
Thanks a lot!

- [**OPENGL**](https://www.opengl.org/about/) for display methods. End users, independent software vendors, and others writing code based on the OpenGL API are free from licensing requirements.
- [**STB**](https://github.com/nothings/stb) for text rendering. These single-file libraries are released into the public domain and can be used freely for any purpose.
- [**GLEW**](http://glew.sourceforge.net/) for managing OpenGL extensions. GLEW is open-source and distributed under the Modified BSD license, allowing free use in both open and closed source projects.
- [**GLFW**](https://www.glfw.org/) for window and input management. GLFW is licensed under the zlib/libpng license, permitting free use in commercial and non-commercial applications.
- [**GL2PS**](https://geuz.org/gl2ps/) for SVG export from OpenGL views.
- [**nlohmann/json**](https://github.com/nlohmann/json) for modern C++ JSON parsing and serialization. It is licensed under the MIT License, allowing unrestricted use, modification, and distribution.
- [**CUDA Toolkit**](https://developer.nvidia.com/cuda-toolkit) for GPU-accelerated computation and parallel processing support. CUDA is developed and distributed by NVIDIA and requires compatible NVIDIA hardware and drivers.
