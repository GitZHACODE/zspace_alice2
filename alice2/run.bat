@echo off

echo ========================================
echo Alice2 3D Viewer - Run Script
echo ========================================
echo.

:: Check if we're in the right directory
if not exist "build\bin\Release\alice2.exe" (
    echo ERROR: alice2.exe not found!
    echo Please build the project first using build.bat.
    pause
    exit /b 1
)

:: Check for required DLLs
echo Checking for required DLLs...
if not exist "build\bin\Release\glew32.dll" (
    echo WARNING: glew32.dll not found in build\bin\Release. The program may not run correctly.
)
if not exist "build\bin\Release\glfw3.dll" (
    echo WARNING: glfw3.dll not found in build\bin\Release. The program may not run correctly.
)

echo.
echo Launching Alice2 3D Viewer...
cd /d build\bin\Release
start alice2.exe
cd /d ..\..\..
echo Alice2 launched successfully!
echo.
