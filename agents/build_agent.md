# Build Agent

The `build_agent` owns build verification and error triage.

## Build Command

Run from the repository root or an equivalent working directory:

```bat
alice2\build_with_zspace.bat
```

From inside the `alice2` folder:

```bat
build_with_zspace.bat
```

The first build downloads the pinned native OpenUSD SDK from a public GitHub
Release and verifies its SHA-256. It caches the SDK locally, so subsequent
builds do not need the download. If the download is unavailable, use
`build_with_zspace.bat -OpenUSDRoot "<native SDK path>"` or
`-OpenUSDPackage "<matching ZIP path>"`. Keep downloaded SDK files out of git.

## Responsibilities

- Run the zSpace-enabled alice2 build after code changes.
- Ignore warning noise unless warnings indicate a real new problem.
- Extract the first useful compiler or linker error.
- Identify which agent should patch the issue.
- Repeat the build after each fix.
- Stop only when the build succeeds or an external dependency is missing.

## Success Output

A clean build should produce:

```text
alice2/build_zspace_v3/bin/Release/alice2.exe
alice2/build_zspace_v3/bin/Release/zSpace_Core.dll
alice2/build_zspace_v3/bin/Release/zSpace_Interface.dll
alice2/build_zspace_v3/bin/Release/zSpace_IO.dll
alice2/build_zspace_v3/bin/Release/zSpace_Toolsets.dll
```

After success, tell the user:

```bat
alice2\run_with_zspace.bat
```

Do not launch the viewer automatically unless the user asks.

For TASK-008 validation, Lin requests screenshots from the actual Alice2 view,
using the camera saved in F1. Preserve `src/cameras/camera_saves.json` in the
selected runtime working directory. The slicer sketch's opt-in
`ALICE2_VALIDATE_SLICER_INPUT` hook runs the real import/pipeline and saved-camera
framebuffer capture; normal interactive launches are unchanged. Do not substitute
a browser report or generated geometry picture for this application evidence.

For an explicit PNG resolution from saved cameras, set `ALICE2_SCREENSHOT_SIZE`
to e.g. `3840x2160`. Alice renders to a multisampled offscreen target at that
resolution with 1x UI pixel sizes, independently of monitor/window dimensions.
Without this option, saved-camera screenshots retain the window resolution.
