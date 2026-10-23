@echo off
setlocal
cd /d "%~dp0"
set "CONFIG=Release"
set "BUILD_DIR=build"
set "EXTRA_FLAGS="
set "TARGET=%~2"
if "%TARGET%"=="" set "TARGET=alice2"
if /I "%~1"=="clang" (
    cmake --preset clang-release
    if errorlevel 1 exit /b 1
    cmake --build --preset clang-release --target "%TARGET%"
    exit /b
)
if /I "%~1"=="cuda" (
    set "BUILD_DIR=build_cuda"
    set "EXTRA_FLAGS=-DALICE2_ENABLE_CUDA=ON"
)
if /I "%~1"=="test" (
    set "BUILD_DIR=build_tests"
    set "EXTRA_FLAGS=-DALICE2_BUILD_MODE=test"
)
cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG% %EXTRA_FLAGS%
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --config %CONFIG% --target "%TARGET%" --parallel
