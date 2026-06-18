[CmdletBinding()]
param(
    [string]$OutputDir,
    [string]$ZspaceSdkDir,
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir ".."))
$RepoDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir ".."))

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectDir "dist"
}
if ([string]::IsNullOrWhiteSpace($ZspaceSdkDir)) {
    $ZspaceSdkDir = Join-Path $ProjectDir "sdk\zspace"
}

$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$ZspaceSdkDir = [System.IO.Path]::GetFullPath($ZspaceSdkDir)
$BuildDir = Join-Path $ProjectDir "build_zspace"
$BuildBinDir = Join-Path $BuildDir "bin\$Configuration"
$RuntimeDir = Join-Path $OutputDir "alice2-runtime"
$DeveloperDir = Join-Path $OutputDir "alice2-developer"

if (-not (Test-Path -LiteralPath (Join-Path $ZspaceSdkDir "lib\cmake\zspace\zspaceConfig.cmake"))) {
    throw "zSpace SDK not found at '$ZspaceSdkDir'. Run scripts\package_zspace_sdk.ps1 first."
}

if (-not $SkipBuild) {
    $env:ALICE2_NO_PAUSE = "1"
    & (Join-Path $ProjectDir "build_with_zspace.bat") $ZspaceSdkDir
    Remove-Item Env:ALICE2_NO_PAUSE -ErrorAction SilentlyContinue
    if ($LASTEXITCODE -ne 0) { throw "alice2 zSpace build failed." }
}

$Executable = Join-Path $BuildBinDir "alice2.exe"
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "alice2 executable not found at '$Executable'."
}

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $RuntimeDir, $DeveloperDir -Force | Out-Null

function Copy-RequiredFile {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Source)) { throw "Required release file missing: '$Source'." }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

# Minimal runtime: executable, adjacent DLLs, data, and a root-aware launcher.
Copy-RequiredFile $Executable (Join-Path $RuntimeDir "alice2.exe")
foreach ($Name in @("zSpace_Core.dll", "zSpace_Interface.dll", "zSpace_IO.dll", "glew32.dll")) {
    Copy-RequiredFile (Join-Path $BuildBinDir $Name) (Join-Path $RuntimeDir $Name)
}
$GlfwDll = Join-Path $BuildBinDir "glfw3.dll"
if (Test-Path -LiteralPath $GlfwDll) {
    Copy-Item -LiteralPath $GlfwDll -Destination $RuntimeDir -Force
}
Copy-Item -LiteralPath (Join-Path $ProjectDir "run_with_zspace.bat") -Destination $RuntimeDir -Force
if (Test-Path -LiteralPath (Join-Path $BuildBinDir "data")) {
    Copy-Item -LiteralPath (Join-Path $BuildBinDir "data") -Destination $RuntimeDir -Recurse -Force
}

# Developer kit: complete alice2 build inputs plus the relocatable binary zSpace SDK.
foreach ($Name in @("CMakeLists.txt", "build_with_zspace.bat", "run_with_zspace.bat")) {
    Copy-RequiredFile (Join-Path $ProjectDir $Name) (Join-Path $DeveloperDir $Name)
}
foreach ($Directory in @("include", "src", "userSrc", "data", "scripts", "depends")) {
    $Source = Join-Path $ProjectDir $Directory
    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $DeveloperDir -Recurse -Force
    }
}
if (Test-Path -LiteralPath (Join-Path $RepoDir "agents")) {
    Copy-Item -LiteralPath (Join-Path $RepoDir "agents") -Destination $DeveloperDir -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $DeveloperDir "sdk") -Force | Out-Null
Copy-Item -LiteralPath $ZspaceSdkDir -Destination (Join-Path $DeveloperDir "sdk") -Recurse -Force

# Include a ready-to-run root binary while retaining the buildable developer tree.
Get-ChildItem -LiteralPath $RuntimeDir -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $DeveloperDir -Recurse -Force
}

$Forbidden = Get-ChildItem -LiteralPath (Join-Path $DeveloperDir "sdk\zspace") -Recurse -File | Where-Object {
    $_.Extension -eq ".pdb" -or
    ($_.Extension -in @(".cpp", ".cxx", ".cc") -and $_.FullName -match "[\\/]include[\\/]zspace[\\/]") -or
    $_.FullName -match "[\\/]include[\\/]src[\\/]"
}
if ($Forbidden) {
    throw "Developer package contains forbidden zSpace implementation/debug files."
}

Write-Host "[alice2] Runtime package:   $RuntimeDir"
Write-Host "[alice2] Developer package: $DeveloperDir"
