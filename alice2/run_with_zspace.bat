@echo off
setlocal

echo ========================================
echo Alice2 3D Viewer - zSpace Run Script
echo ========================================
echo.

set "BUILD_DIR=build_zspace"
set "BIN_DIR=%BUILD_DIR%\bin\Release"
set "EXE_PATH=%BIN_DIR%\alice2.exe"
set "DLL_GLEW=%BIN_DIR%\glew32.dll"
set "DLL_GLFW=%BIN_DIR%\glfw3.dll"
set "DLL_ZSPACE_CORE=%BIN_DIR%\zSpace_Core.dll"
set "DLL_ZSPACE_INTERFACE=%BIN_DIR%\zSpace_Interface.dll"
set "DLL_ZSPACE_IO=%BIN_DIR%\zSpace_IO.dll"

pushd "%~dp0"
if errorlevel 1 goto :fail

if not exist "%EXE_PATH%" (
    echo ERROR: alice2.exe not found!
    echo Please build the zSpace configuration first using build_with_zspace.bat.
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

echo.
echo Launching Alice2 3D Viewer (zSpace)...
cd /d "%BIN_DIR%"
start alice2.exe
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
