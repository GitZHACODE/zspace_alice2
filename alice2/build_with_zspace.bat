@echo off
setlocal

set "CONFIG=Release"
set "BUILD_DIR=build_zspace"
set "ZSPACE_CORE_DIR=%~1"
set "CMAKE_EXE=cmake"
set "BUILD_TOOL_ARGS=/m /clp:ErrorsOnly"

if "%ZSPACE_CORE_DIR%"=="" (
    set "ZSPACE_CORE_DIR=%~dp0..\..\zspace_core"
)

where cmake >nul 2>nul
if errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else (
        echo.
        echo [alice2] CMake was not found on PATH or in Visual Studio 2022.
        echo [alice2] Install CMake or Visual Studio Build Tools with C++ CMake tools.
        goto :fail
    )
)

echo.
echo [alice2] Building with zspace_core
echo [alice2] zspace_core: "%ZSPACE_CORE_DIR%"
echo [alice2] cmake: "%CMAKE_EXE%"
echo.

pushd "%~dp0"
if errorlevel 1 goto :fail

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG% -DALICE2_WITH_ZSPACE_CORE=ON -DZSPACE_CORE_DIR="%ZSPACE_CORE_DIR%"
if errorlevel 1 goto :fail_pop

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %CONFIG% -- %BUILD_TOOL_ARGS%
if errorlevel 1 goto :fail_pop

echo.
echo [alice2] zspace build finished successfully.
popd
pause
exit /b 0

:fail_pop
popd

:fail
echo.
echo [alice2] zspace build failed.
pause
exit /b 1
