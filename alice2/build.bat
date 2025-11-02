@echo off
setlocal

REM Usage:
REM   build.bat        -> normal build in "build"
REM   build.bat cuda   -> CUDA build in "build_cuda"
REM   build.bat test   -> test build in "build_tests"

set "CONFIG=Release"
set "BUILD_DIR=build"
set "EXTRA_FLAGS="

if /I "%~1"=="cuda" (
    set BUILD_DIR=build_cuda
    set EXTRA_FLAGS=-DALICE2_ENABLE_CUDA=ON
)
if /I "%~1"=="test" (
    set BUILD_DIR=build_tests
    set EXTRA_FLAGS=-DALICE2_BUILD_MODE=test
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
