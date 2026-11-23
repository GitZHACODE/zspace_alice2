@echo off
setlocal

echo ========================================
echo Alice2 3D Viewer - zSpace Run Script
echo ========================================
echo.

set "BUILD_DIR=build_zspace_v3"
if not "%~1"=="" (
    if exist "%~f1" (
        set "SCALE_TRANS_CURVES=%~f1"
        set "MESH_PATTERN_USD=%~f1"
    ) else (
        set "SCALE_TRANS_CURVES=%~1"
        set "MESH_PATTERN_USD=%~1"
    )
)
if exist "%~dp0%BUILD_DIR%\bin\alice2.exe" (
    set "BIN_DIR=%BUILD_DIR%\bin"
) else (
    set "BIN_DIR=%BUILD_DIR%\bin\Release"
)
if exist "%~dp0alice2.exe" set "BIN_DIR=."
set "EXE_PATH=%BIN_DIR%\alice2.exe"
set "DLL_GLEW=%BIN_DIR%\glew32.dll"
set "DLL_GLFW=%BIN_DIR%\glfw3.dll"
set "DLL_ZSPACE_CORE=%BIN_DIR%\zSpace_Core.dll"
set "DLL_ZSPACE_INTERFACE=%BIN_DIR%\zSpace_Interface.dll"
set "DLL_ZSPACE_IO=%BIN_DIR%\zSpace_IO.dll"
set "DLL_ZSPACE_TOOLSETS=%BIN_DIR%\zSpace_Toolsets.dll"

pushd "%~dp0"
if errorlevel 1 goto :fail

if not exist "%EXE_PATH%" (
    echo ERROR: alice2.exe not found!
    echo Expected alice2.exe beside this script, in build_zspace_v3\bin, or in build_zspace_v3\bin\Release.
    echo Build the zSpace toolsets configuration first using build_with_zspace_toolsets.bat.
    goto :fail_pop
)

echo Checking for required DLLs for zSpace build...
if not exist "%DLL_GLEW%" (
    echo WARNING: glew32.dll not found in %BIN_DIR%. The program may not run correctly.
)
if not exist "%DLL_GLFW%" (
    echo WARNING: glfw3.dll not found in %BIN_DIR%. The program may not run correctly.
)
if not exist "%DLL_ZSPACE_CORE%" (
    echo WARNING: zSpace_Core.dll not found in %BIN_DIR%. The program may not run correctly.
)
if not exist "%DLL_ZSPACE_INTERFACE%" (
    echo WARNING: zSpace_Interface.dll not found in %BIN_DIR%. The program may not run correctly.
)
if not exist "%DLL_ZSPACE_IO%" (
    echo WARNING: zSpace_IO.dll not found in %BIN_DIR%. The program may not run correctly.
)
if not exist "%DLL_ZSPACE_TOOLSETS%" (
    echo WARNING: zSpace_Toolsets.dll not found in %BIN_DIR%. The program may not run correctly.
)

echo.
echo Launching Alice2 3D Viewer (zSpace) from %BIN_DIR%...
cd /d "%BIN_DIR%"
start "" alice2.exe
cd /d "%~dp0"
echo Alice2 launched successfully!
echo.

popd
endlocal
exit /b 0

:fail_pop
popd

:fail
echo.
echo [alice2] zSpace run failed.
pause
exit /b 1
