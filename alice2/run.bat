@echo off
setlocal
cd /d "%~dp0"
set "BUILD_DIR=build"
if /I "%~1"=="clang" set "BUILD_DIR=build\clang-release"
if /I "%~1"=="cuda" set "BUILD_DIR=build_cuda"
if /I "%~1"=="test" set "BUILD_DIR=build_tests"
set "TARGET=%~2"
if "%TARGET%"=="" set "TARGET=alice2"
set "EXE=%BUILD_DIR%\bin\Release\%TARGET%.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\bin\%TARGET%.exe"
if not exist "%EXE%" (
    echo Executable not found. Build target %TARGET% first.
    exit /b 1
)
"%EXE%"
