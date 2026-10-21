@echo off
setlocal

REM MSBuild can fail if the parent environment contains both PATH and Path.
REM set "Path=%Path%"
REM set "PATH="

set "CONFIG=Release"
set "BUILD_DIR=build_zspace"
set "ZSPACE_SDK_DIR=%~1"
set "CMAKE_EXE=cmake"
set "BUILD_TOOL_ARGS=/m /clp:ErrorsOnly"

if "%ZSPACE_SDK_DIR%"=="" (
    set "ZSPACE_SDK_DIR=%~dp0sdk\zspace"
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



echo [alice2] Building with zSpace binary SDK
echo [alice2] zSpace SDK: "%ZSPACE_SDK_DIR%"
echo [alice2] cmake: "%CMAKE_EXE%"
echo.

pushd "%~dp0"
if errorlevel 1 goto :fail

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM A cache created without -A stores an empty generator platform. CMake then
REM refuses -A x64 even when that build is already the x64 host default.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /R /C:"^CMAKE_GENERATOR_PLATFORM:INTERNAL=x64$" "%BUILD_DIR%\CMakeCache.txt" >nul
    if errorlevel 1 (
        echo [alice2] Resetting CMake cache; generator platform is not x64.
        if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
        del /f /q "%BUILD_DIR%\CMakeCache.txt"
    )
)

"%CMAKE_EXE%" -A x64 -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG% -DALICE2_WITH_ZSPACE_CORE=ON -DALICE2_USE_ZSPACE_SOURCE=OFF -DZSPACE_SDK_DIR="%ZSPACE_SDK_DIR%"
if errorlevel 1 goto :fail_pop

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %CONFIG% -- %BUILD_TOOL_ARGS%
if errorlevel 1 goto :fail_pop

echo.
echo [alice2] zspace build finished successfully.
popd
if not defined ALICE2_NO_PAUSE pause
exit /b 0

:fail_pop
popd

:fail
echo.
echo [alice2] zspace build failed.
if not defined ALICE2_NO_PAUSE pause
exit /b 1
