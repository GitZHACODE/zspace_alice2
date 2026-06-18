[CmdletBinding()]
param(
    [string]$ZspaceCoreDir,
    [string]$OutputDir,
    [string]$BuildDir,
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir ".."))

if ([string]::IsNullOrWhiteSpace($ZspaceCoreDir)) {
    $ZspaceCoreDir = Join-Path $ProjectDir "..\..\zspace_core"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectDir "sdk\zspace"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ZspaceCoreDir "build\alice2-sdk"
}

$ZspaceCoreDir = [System.IO.Path]::GetFullPath($ZspaceCoreDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if (-not (Test-Path -LiteralPath (Join-Path $ZspaceCoreDir "CMakeLists.txt"))) {
    throw "zspace_core was not found at '$ZspaceCoreDir'."
}
$CmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$CmakeExe = if ($CmakeCommand) { $CmakeCommand.Source } else { $null }
if (-not $CmakeExe) {
    $CmakeCandidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    $CmakeExe = $CmakeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $CmakeExe) { throw "CMake was not found on PATH or in Visual Studio 2022." }

Write-Host "[zSpace SDK] Source:  $ZspaceCoreDir"
Write-Host "[zSpace SDK] Build:   $BuildDir"
Write-Host "[zSpace SDK] Install: $OutputDir"

if (-not $SkipBuild) {
    & $CmakeExe -S $ZspaceCoreDir -B $BuildDir `
        -DZSPACE_BUILD_SHARED=ON `
        -DZSPACE_BUILD_INTERFACE=ON `
        -DZSPACE_BUILD_IO=ON `
        -DZSPACE_BUILD_DISPLAY=OFF `
        -DZSPACE_BUILD_INTEROP=OFF `
        -DZSPACE_BUILD_TESTS=OFF `
        -DZSPACE_WITH_OPENGL=OFF
    if ($LASTEXITCODE -ne 0) { throw "zSpace CMake configuration failed." }

    & $CmakeExe --build $BuildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "zSpace build failed." }
}

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}

& $CmakeExe --install $BuildDir --config $Configuration --prefix $OutputDir
if ($LASTEXITCODE -ne 0) { throw "zSpace SDK installation failed." }

$RequiredFiles = @(
    "include\zspace\interface.h",
    "lib\zSpace_Core.lib",
    "lib\zSpace_Interface.lib",
    "lib\zSpace_IO.lib",
    "bin\zSpace_Core.dll",
    "bin\zSpace_Interface.dll",
    "bin\zSpace_IO.dll",
    "lib\cmake\zspace\zspaceConfig.cmake",
    "lib\cmake\zspace\zspaceTargets.cmake"
)

foreach ($RelativePath in $RequiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $OutputDir $RelativePath))) {
        throw "Incomplete zSpace SDK: missing '$RelativePath'."
    }
}

$PrivateFiles = Get-ChildItem -LiteralPath $OutputDir -Recurse -File | Where-Object {
    $_.Extension -eq ".pdb" -or
    ($_.Extension -in @(".cpp", ".cxx", ".cc") -and $_.FullName -match "[\\/]include[\\/]zspace[\\/]") -or
    $_.FullName -match "[\\/]include[\\/]src[\\/]"
}
if ($PrivateFiles) {
    $List = ($PrivateFiles.FullName -join [Environment]::NewLine)
    throw "The SDK contains private implementation/debug files:$([Environment]::NewLine)$List"
}

Write-Host "[zSpace SDK] Complete. No zSpace implementation sources or PDB files were packaged."
