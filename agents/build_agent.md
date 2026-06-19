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
alice2/build_zspace/bin/Release/alice2.exe
alice2/build_zspace/bin/Release/zSpace_Core.dll
alice2/build_zspace/bin/Release/zSpace_Interface.dll
alice2/build_zspace/bin/Release/zSpace_IO.dll
```

After success, tell the user:

```bat
alice2\run_with_zspace.bat
```

Do not launch the viewer automatically unless the user asks.
