@echo off
setlocal

REM Usage:
REM   build.bat        -> normal build in "build_zspace_v3"
REM   build.bat cuda   -> CUDA build in "build_zspace_v3"
REM   build.bat test   -> test build in "build_zspace_v3"

set "CONFIG=Release"
set "BUILD_DIR=build_zspace_v3"
set "EXTRA_FLAGS="

if /I "%~1"=="cuda" (
    set EXTRA_FLAGS=-DALICE2_ENABLE_CUDA=ON
)
if /I "%~1"=="test" (
    set EXTRA_FLAGS=-DALICE2_BUILD_MODE=test
)
if "%~1"=="" (
    if exist "%~dp0..\..\zspace_core\CMakeLists.txt" (
        if exist "%~dp0..\..\zspace_toolsets\CMakeLists.txt" (
            call "%~dp0build_with_zspace_toolsets.bat"
            if errorlevel 1 exit /b 1
            exit /b 0
        )
    )
)

echo.
echo [alice2] Configuring into "%BUILD_DIR%" (%CONFIG%) %EXTRA_FLAGS%
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM Configure (generator-agnostic)
cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG% %EXTRA_FLAGS%
if errorlevel 1 goto :fail

echo.
echo [alice2] Building...
cmake --build "%BUILD_DIR%" --config %CONFIG% -- /m
if errorlevel 1 goto :fail

echo.
echo [alice2] Build finished successfully.
goto :eof

:fail
echo.
echo [alice2] Build failed.
exit /b 1
