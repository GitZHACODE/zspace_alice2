# Current Agent Plan

Status: completed

## User Intent

Create a relocatable Windows alice2 release that runs and can be rebuilt with zSpace integration on another machine without distributing the private `zspace_core` source tree.

## Participating Agents

- `zspace_agent`: verify the public binary SDK layout and imported CMake targets.
- `alice2_agent`: align build, run, data, and release-directory behavior.
- `code_agent`: update the packaging scripts with the smallest practical surface.
- `build_agent`: build from the packaged SDK and validate release contents.
- `document_agent`: not triggered.

## Proposed Changes

- Replace the legacy zSpace SDK copier with the native zSpace CMake build/install flow.
- Package public headers, import libraries, runtime DLLs, generated CMake package files, and no zSpace implementation sources.
- Add an alice2 release packager that produces runtime and developer distributions.
- Make `run_with_zspace.bat` support both a packaged root executable and the local build output.
- Complete alice2 install rules for data and runtime DLL deployment.
- Update SDK/release usage notes and add package validation checks.

## Build Command

```bat
alice2\build_with_zspace.bat
```

## Acceptance Checks

- zSpace SDK is consumed through `find_package(zspace CONFIG)` with source mode disabled.
- SDK contains `include`, `lib`, `bin`, and `lib/cmake/zspace`.
- SDK contains no `zspace_core/src`, implementation `.cpp`, or PDB files.
- alice2 output contains the executable, required zSpace DLLs, and runtime data.
- run script works from both the repository and packaged release root.
- clean Release build succeeds against the binary SDK.

## Implementation Status

- [x] Inspect current binary SDK and CMake export support.
- [x] Replace legacy SDK packaging.
- [x] Add alice2 release packaging and validation.
- [x] Complete runtime install/run behavior.
- [x] Configure alice2 exclusively against the installed binary SDK.
- [x] Validate SDK privacy and required artifacts.
- [x] Complete final MSBuild and generated release-package validation.

