param(
  [string]$BuildDir = "$PSScriptRoot/../build_zspace_v3",
  [string]$CoreDir = "$PSScriptRoot/../../../zspace_core",
  [string]$ToolsetsDir = "$PSScriptRoot/../../../zspace_toolsets",
  [string]$TetgenIncludeDir = "$PSScriptRoot/../../../zspace_toolsets/wasm/cache/tetgen",
  [string]$TetgenLibrary = "",
  [string]$OpenUSDRoot = '',
  [string]$OpenUSDPackage = '',
  [ValidateRange(1,32)][int]$Jobs = 6,
  [switch]$WithoutUSD,
  [switch]$Test
)
$ErrorActionPreference = 'Stop'
try {
  $BuildDir = [IO.Path]::GetFullPath($BuildDir)
  $CoreDir = [IO.Path]::GetFullPath($CoreDir)
  $ToolsetsDir = [IO.Path]::GetFullPath($ToolsetsDir)
  $TetgenIncludeDir = [IO.Path]::GetFullPath($TetgenIncludeDir)
  if ($TetgenLibrary) { $TetgenLibrary = [IO.Path]::GetFullPath($TetgenLibrary) }
  if (!$WithoutUSD) {
    $OpenUSDRoot = & "$PSScriptRoot/resolve-openusd-sdk.ps1" -OpenUSDRoot $OpenUSDRoot -OpenUSDPackage $OpenUSDPackage
    Write-Host "Using native OpenUSD SDK: $OpenUSDRoot"
  }
  $cmake = Get-Command cmake -ErrorAction SilentlyContinue
  if ($cmake) { $cmake = $cmake.Source }
  else {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (!(Test-Path $vswhere)) { throw 'Install CMake or Visual Studio C++ CMake tools.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
  }
  foreach ($path in @("$CoreDir/CMakeLists.txt", "$ToolsetsDir/include/zspace/slicing.h", "$TetgenIncludeDir/tetgen.h")) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing dependency: $path. Supply the corresponding path parameter." }
  }
  if (!$TetgenLibrary) {
    if (!(Test-Path "$TetgenIncludeDir/CMakeLists.txt")) { throw 'Supply -TetgenLibrary or a TetGen source checkout via -TetgenIncludeDir.' }
    & $cmake -S $TetgenIncludeDir -B "$BuildDir/tetgen-native" -G 'Visual Studio 17 2022' -A x64 -DBUILD_EXECUTABLE=OFF -DBUILD_LIBRARY=ON
    if ($LASTEXITCODE) { throw 'TetGen configuration failed.' }
    & $cmake --build "$BuildDir/tetgen-native" --config Release --parallel $Jobs -- /nr:false /clp:ErrorsOnly
    if ($LASTEXITCODE) { throw 'TetGen build failed.' }
    $TetgenLibrary = "$BuildDir/tetgen-native/Release/tetgen.lib"
  }
  if (!(Test-Path -LiteralPath $TetgenLibrary)) { throw "Missing native TetGen library: $TetgenLibrary" }
  if ($WithoutUSD -and $Test) { throw '-Test requires native USD; omit -WithoutUSD.' }
  $usd = if ($WithoutUSD) { 'OFF' } else { 'ON' }
  $tests = if ($Test) { 'ON' } else { 'OFF' }
  & $cmake -S "$PSScriptRoot/.." -B $BuildDir -G 'Visual Studio 17 2022' -A x64 `
    -DCMAKE_SUPPRESS_REGENERATION=ON -DALICE2_WITH_ZSPACE_CORE=ON `
    -DALICE2_USE_ZSPACE_SOURCE=ON "-DZSPACE_CORE_DIR=$CoreDir" "-DZSPACE_TOOLSETS_DIR=$ToolsetsDir" `
    -DZSPACE_TOOLSETS_WITH_TETGEN=ON -DZSPACE_TOOLSETS_SLICING_OPENMP=ON `
    "-DZSPACE_TETGEN_INCLUDE_DIR=$TetgenIncludeDir" "-DZSPACE_TETGEN_LIBRARY=$TetgenLibrary" `
    "-DALICE2_WITH_TS3DP_USD=$usd" "-DZSPACE_OPENUSD_ROOT=$OpenUSDRoot" `
    "-DALICE2_BUILD_SLICING_TESTS=$tests" "-DZSPACE_TOOLSETS_BUILD_TESTS=$tests"
  if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
  & $cmake --build $BuildDir --config Release --parallel $Jobs -- /nr:false /clp:ErrorsOnly
  if ($LASTEXITCODE) { throw 'Desktop build failed.' }
  if ($Test) {
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $ctest --test-dir $BuildDir -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Native integration tests failed.' }
  }
  Write-Host "Built $BuildDir/bin/Release/alice2.exe"
} catch { [Console]::Error.WriteLine($_); exit 1 }
exit 0
