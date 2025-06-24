# ZSPACE
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/gitzhcode/zspace_core/LICENSE.MIT) [![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://github.com/gitzhcode/zspace_core/doxyoutput/) [![GitHub Releases](https://img.shields.io/github/release/gitzhcode/zspace_core.svg)](https://github.com/gitzhcode/zspace_core/releases) [![GitHub Issues](https://img.shields.io/github/issues/gitzhcode/zspace_core.svg)](http://github.com/gitzhcode/zspace_core/issues)

**ZSPACE** is a C++  library collection of geometry data-structures and algorithms framework. It is implemented as a header-only C++ library, whose dependencies, are header-only or static libraries. Hence **ZSPACE** can be easily embedded in C++ projects. 

Optionally the library may also be pre-compiled into a statically  or dynamically linked library, for faster compile times.

- [Installation](#Installation)
- [Launching](#Launching)
- [Citing](#Citing)
- [License](#license)
- [Third party dependcencies](#used-third-party-dependencies)

# Installation

1. Download and follow the instructions to install Visual Studio 2019 from [Microsoft](https://learn.microsoft.com/en-us/visualstudio/releases/2019/release-notes).
2. Open Visual Studio 2019 and click on "Clone a repository"
   ![VS_1](https://github.com/GitZHCODE/zspace_alice/blob/master/assets/1_VS.png)
3. Go to [GitZHCODE/zspace_alice](https://github.com/GitZHCODE/zspace_alice). 
   - Click on "Code" button and copy HTTPS directory from the web page.
     ![Git_3](https://github.com/GitZHCODE/zspace_alice/blob/master/assets/2_Git.png) 
   - Paste the directory to the "Repository location" in Visual Studio.
   - Click on "Clone".
     ![VS_4](https://github.com/GitZHCODE/zspace_alice/blob/master/assets/3_VS.png)

# Launching
1. Browse to "Users\name\source\repos\GitZHCODE\zspace_alice\ALICE_PLATFORM", double click on "ALICE.sln" to open the solution.
![Folder_5](https://github.com/GitZHCODE/zspace_alice/blob/master/assets/4_Folder.png)
2. Change the "Solution Configurations" to "Release_zSpaceDLL". Click on "Local Windows Debugger" (play button) to launch Alice.
![Alice_6](https://github.com/GitZHCODE/zspace_alice/blob/master/assets/5_VS.png)
3. If it successfully loaded, you should be able to see Alice viewer window and Console window pop up on your screen.
![Alice_7](https://github.com/GitZHCODE/zspace_alice/blob/master/assets/6_Alice.png)

# Citing
If you use the library of ZSPACE in a project, please refer to the GitHub repository.

@misc{zspace-framework,
      title  = {{zspace}: A simple C++ header-only collection of geometry data-structures, algorithms and city data visualization                       framework.},
      author = {Vishu Bhooshan and Shajay Bhooshan and others},
      note   = {https://github.com/venumb/ZSPACE},
      year   = {2018},
    }

# License
The library is licensed under the [MIT License](https://opensource.org/licenses/MIT).


# Third party dependencies
The library has some dependencies on third-party tools and services, which have different licensing as listed below.
Thanks a lot!

- [**OPENGL**](https://www.opengl.org/about/) for display methods. End users, independent software vendors, and others writing code based on the OpenGL API are free from licensing requirements.

- [**Eigen**](https://github.com/eigenteam/eigen-git-mirror) for matricies and related methods. It is an open source project licensed under
[MPL2](https://www.mozilla.org/MPL/2.0/).

- [**Spectra**](https://github.com/yixuan/spectra) for large scale eigen value problems. It is an open source project licensed under
[MPL2](https://www.mozilla.org/MPL/2.0/).

- [**Armadillo**](http://arma.sourceforge.net/) for matricies and related methods. It is an open source project licensed under
[Apache License 2.0](https://opensource.org/licenses/Apache-2.0).

- [**Alglib**](http://http://www.alglib.net/) free edition for linear programming optimisation methods.

- [**JSON for Modern C++**](https://github.com/nlohmann/json) to create a JSON file. It is an open source project licensed under
[MIT License](https://opensource.org/licenses/MIT).

- [**SQLITE**](https://www.sqlite.org/index.html) for SQL database engine. It is an open source project dedicated to the [public domain](https://en.wikipedia.org/wiki/Public_domain).

- [**LodePNG**](https://lodev.org/lodepng) for creating PNG images. It is a project licensed under 
[ZLIB License](https://zlib.net/zlib_license.html).

- [**TooJPEG**](https://create.stephan-brumme.com/toojpeg/) for creating JPEG images. It is a project licensed under 
[ZLIB License](https://zlib.net/zlib_license.html).

