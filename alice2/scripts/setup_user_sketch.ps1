param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')]
    [string]$Name
)
$ErrorActionPreference = 'Stop'
$researchDir = Join-Path $PSScriptRoot '../usrSrc'
$destination = Join-Path $researchDir $Name
if (Test-Path -LiteralPath $destination) { throw "Already exists: $destination" }
Copy-Item -LiteralPath (Join-Path $researchDir 'minimal') -Destination $destination -Recurse
$cmake = "alice2_add_sketch(sketch_$Name SOURCES sketch.cpp Orbit.cpp)"
Set-Content -LiteralPath (Join-Path $destination 'CMakeLists.txt') -Value $cmake
Add-Content -LiteralPath (Join-Path $researchDir 'CMakeLists.txt') -Value "add_subdirectory($Name)"
Write-Host "Created usrSrc/$Name. Reconfigure CMake, then build target sketch_$Name."
