@echo off
setlocal

set "ALICE2_CLEAN_PATH=%PATH%"
set "PATH="
set "Path="
set "PATH=%ALICE2_CLEAN_PATH%"
set "ALICE2_CLEAN_PATH="

set "CONFIG=Release"
set "BUILD_DIR=build_zspace_v3"
set "ZSPACE_CORE_DIR=%~1"
set "ZSPACE_TOOLSETS_DIR=%~2"
set "CMAKE_EXE=cmake"
set "BUILD_TOOL_ARGS=/m /nr:false /clp:ErrorsOnly"
set "USE_NINJA=0"
set "VSDEVCMD="
set "NINJA_EXE="

if "%ZSPACE_CORE_DIR%"=="" (
    set "ZSPACE_CORE_DIR=%~dp0..\..\zspace_core"
)

if "%ZSPACE_TOOLSETS_DIR%"=="" (
    set "ZSPACE_TOOLSETS_DIR=%~dp0..\..\zspace_toolsets"
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
        goto :fail
    )
)

where ninja >nul 2>nul
if not errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
        set "USE_NINJA=1"
        set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
        set "USE_NINJA=1"
        set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
        set "USE_NINJA=1"
        set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    )
)

REM Keep Alice zSpace builds in one stable folder. Visual Studio is the
REM stable default here; /nr:false avoids leaving MSBuild worker nodes behind.
set "USE_NINJA=0"

echo [alice2] Building with zspace_core and zspace_toolsets source
echo [alice2] zspace_core: "%ZSPACE_CORE_DIR%"
echo [alice2] zspace_toolsets: "%ZSPACE_TOOLSETS_DIR%"
echo [alice2] cmake: "%CMAKE_EXE%"
if "%USE_NINJA%"=="1" echo [alice2] generator: Ninja via "%VSDEVCMD%"
if "%USE_NINJA%"=="1" echo [alice2] ninja: "%NINJA_EXE%"
if "%USE_NINJA%"=="0" echo [alice2] generator: Visual Studio 17 2022
echo.

pushd "%~dp0"
if not "%ERRORLEVEL%"=="0" goto :fail

if exist "%BUILD_DIR%\CMakeFiles" if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [alice2] Cleaning incomplete %BUILD_DIR% configure folder.
    rmdir /s /q "%BUILD_DIR%"
)

if "%USE_NINJA%"=="1" (
    if exist "%BUILD_DIR%\CMakeFiles" if not exist "%BUILD_DIR%\CMakeCache.txt" (
        echo [alice2] Cleaning incomplete %BUILD_DIR% configure folder.
        rmdir /s /q "%BUILD_DIR%"
    )
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        findstr /C:"CMAKE_GENERATOR:INTERNAL=Ninja" "%BUILD_DIR%\CMakeCache.txt" >nul
        if errorlevel 1 (
            echo [alice2] Cleaning %BUILD_DIR% because it was configured with a different generator.
            rmdir /s /q "%BUILD_DIR%"
        )
    )
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

    call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
    if errorlevel 1 goto :fail_pop

    "%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_SUPPRESS_REGENERATION=ON -DALICE2_WITH_ZSPACE_CORE=ON -DALICE2_USE_ZSPACE_SOURCE=ON -DZSPACE_CORE_DIR="%ZSPACE_CORE_DIR%" -DALICE2_WITH_ZSPACE_TOOLSETS=ON -DZSPACE_TOOLSETS_DIR="%ZSPACE_TOOLSETS_DIR%"
    if errorlevel 1 goto :fail_pop

    "%CMAKE_EXE%" --build "%BUILD_DIR%"
    if errorlevel 1 goto :fail_pop
) else (
    if not "%VSDEVCMD%"=="" (
        call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
        if errorlevel 1 goto :fail_pop
    )
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        findstr /C:"CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022" "%BUILD_DIR%\CMakeCache.txt" >nul
        if errorlevel 1 (
            echo [alice2] Cleaning %BUILD_DIR% because it was configured with a different generator.
            rmdir /s /q "%BUILD_DIR%"
        )
    )
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        findstr /C:"ALICE2_USE_ZSPACE_SOURCE:BOOL=OFF" "%BUILD_DIR%\CMakeCache.txt" >nul
        if not errorlevel 1 (
            echo [alice2] Cleaning %BUILD_DIR% because it was previously configured against binary SDK instead of zspace_core source.
            rmdir /s /q "%BUILD_DIR%"
        )
    )
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
    if exist "%BUILD_DIR%\bin\Release\alice2.old.exe" del /f /q "%BUILD_DIR%\bin\Release\alice2.old.exe" >nul 2>nul
    if exist "%BUILD_DIR%\bin\Release\zSpace_Toolsets.old.dll" del /f /q "%BUILD_DIR%\bin\Release\zSpace_Toolsets.old.dll" >nul 2>nul
    if exist "%BUILD_DIR%\bin\Release\alice2.exe" move /y "%BUILD_DIR%\bin\Release\alice2.exe" "%BUILD_DIR%\bin\Release\alice2.old.exe" >nul 2>nul
    if exist "%BUILD_DIR%\bin\Release\zSpace_Toolsets.dll" move /y "%BUILD_DIR%\bin\Release\zSpace_Toolsets.dll" "%BUILD_DIR%\bin\Release\zSpace_Toolsets.old.dll" >nul 2>nul
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run_sanitized_cmake_build.ps1" -CMakeExe "%CMAKE_EXE%" -BuildDir "%BUILD_DIR%" -Config "%CONFIG%" -ZspaceCoreDir "%ZSPACE_CORE_DIR%" -ZspaceToolsetsDir "%ZSPACE_TOOLSETS_DIR%" -BuildToolArgs "%BUILD_TOOL_ARGS%"
    if errorlevel 1 goto :fail_pop
)

echo.
echo [alice2] zspace toolsets build finished successfully.
popd
if not defined ALICE2_NO_PAUSE pause
exit /b 0

:fail_pop
popd

:fail
echo.
echo [alice2] zspace toolsets build failed.
if not defined ALICE2_NO_PAUSE pause
exit /b 1
