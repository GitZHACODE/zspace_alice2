@echo off
setlocal

echo ========================================
echo Alice2 3D Viewer - Run Script
echo ========================================
echo.

:: Default build folder
set "BUILD_DIR=build"
set "MODE_HINT=standard"

:: Optional CUDA mode
if /I "%~1"=="cuda" (
    set "BUILD_DIR=build_cuda"
    set "MODE_HINT=cuda"
)

:: Optional Clang/Ninja mode
if /I "%~1"=="clang" (
    set "BUILD_DIR=build\clang-release"
    set "MODE_HINT=clang"
)

:: Optional test mode
if /I "%~1"=="test" (
    set "BUILD_DIR=build_tests"
    set "MODE_HINT=test"
)

set "EXE_PATH=%BUILD_DIR%\bin\Release\alice2.exe"
set "DLL_GLEW=%BUILD_DIR%\bin\Release\glew32.dll"
set "DLL_GLFW=%BUILD_DIR%\bin\Release\glfw3.dll"

if /I "%MODE_HINT%"=="clang" (
    set "EXE_PATH=%BUILD_DIR%\bin\alice2.exe"
    set "DLL_GLEW=%BUILD_DIR%\bin\glew32.dll"
    set "DLL_GLFW=%BUILD_DIR%\bin\glfw3.dll"
)

:: Check if executable exists
if not exist "%EXE_PATH%" (
    echo ERROR: alice2.exe not found!
    echo Please build the project first using build.bat, build.bat clang, build.bat cuda, or build.bat test.
    pause
    exit /b 1
)

:: Check for required DLLs
echo Checking for required DLLs for %MODE_HINT% build...
if not exist "%DLL_GLEW%" (
    echo WARNING: glew32.dll not found at %DLL_GLEW%. The program may not run correctly.
)
if not exist "%DLL_GLFW%" (
    echo WARNING: glfw3.dll not found at %DLL_GLFW%. The program may not run correctly.
)

echo.
echo Launching Alice2 3D Viewer (%MODE_HINT%)...
if /I "%MODE_HINT%"=="clang" (
    cd /d "%BUILD_DIR%\bin"
) else (
    cd /d "%BUILD_DIR%\bin\Release"
)
start alice2.exe
cd /d "%~dp0"
echo Alice2 launched successfully!
echo.

endlocal
