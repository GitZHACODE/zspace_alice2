# zSpace Binary SDK

Place the installed zSpace SDK in this folder as `zspace/` for normal user builds:

```text
alice2/sdk/zspace/
  include/
  lib/
  bin/
  lib/cmake/zspace/zspaceConfig.cmake
  version.txt
```

The SDK contains public headers, import libraries, runtime DLLs, and CMake package files. It does not contain zspace_core source files.

Create or refresh it from a private zspace_core checkout with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_zspace_sdk.ps1
```

The generated CMake package must remain under `lib/cmake/zspace`; this makes the SDK relocatable and allows alice2 to use `find_package(zspace CONFIG)`.

You can also keep the SDK elsewhere and pass its path to:

```bat
build_with_zspace.bat C:\path\to\zspace_sdk
```

Create clean runtime and developer distributions with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_alice2_release.ps1
```

The runtime package contains only files needed to launch alice2. The developer package additionally contains alice2 sources and dependencies needed to rebuild sketches, but never zspace_core implementation sources.
