param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')]
    [string]$Name
)
$ErrorActionPreference = 'Stop'
$researchDir = Join-Path $PSScriptRoot '../userSrc'
$destination = Join-Path $researchDir $Name
if (Test-Path -LiteralPath $destination) { throw "Already exists: $destination" }
Copy-Item -LiteralPath (Join-Path $researchDir 'minimal') -Destination $destination -Recurse
Rename-Item -LiteralPath (Join-Path $destination 'sketch_minimal.cpp') -NewName "sketch_$Name.cpp"
$cmake = @'
# Add or remove sketch names here (without .cpp). An empty list is valid.
set(@NAME@_sketches
    sketch_@NAME@
)

foreach(sketch IN LISTS @NAME@_sketches)
    alice2_add_sketch(${sketch} SOURCES ${sketch}.cpp Orbit.cpp)
endforeach()

# Optional collection: uncomment and list its sketch source files to enable it.
# alice2_add_sketch(sketch_@NAME@_examples SOURCES
#     sketch_@NAME@.cpp Orbit.cpp
# )
'@
Set-Content -LiteralPath (Join-Path $destination 'CMakeLists.txt') -Value ($cmake.Replace('@NAME@', $Name))
Add-Content -LiteralPath (Join-Path $researchDir 'CMakeLists.txt') -Value "add_subdirectory($Name)"
Write-Host "Created userSrc/$Name. Build it with build.bat --$Name."
