$Run = $false
$ErrorActionPreference = 'Stop'
$group = ''
$config = ''
$mode = ''
$app = ''
$appExplicit = $false
$last = $false
$appArgs = @()
for ($i = 0; $i -lt $args.Count; $i++) {
    $option = $args[$i]
    if ($option -eq '--') { $appArgs = @($args | Select-Object -Skip ($i + 1)); break }
    switch -Regex ($option) {
        '^(-Run|--run)$' { $Run = $true; continue }
        '^--help$' {
            Write-Host 'build.bat [--<folder>|--all|--last] [--debug|--release] [--clang|--cuda|--msvc]'
            Write-Host 'run.bat [--<folder>|--last] [--app=executable_name] [configuration options] [-- app arguments]'
            exit 0
        }
        '^--last$' { $last = $true; continue }
        '^--debug$' { $config = 'Debug'; continue }
        '^--release$' { $config = 'Release'; continue }
        '^--clang$' { $mode = 'clang'; continue }
        '^--cuda$' { $mode = 'cuda'; continue }
        '^--msvc$' { $mode = 'msvc'; continue }
        '^--app=([A-Za-z][A-Za-z0-9_]*)$' { $app = $Matches[1]; $appExplicit = $true; continue }
        '^--([A-Za-z][A-Za-z0-9_]*)$' {
            if ($group) { throw 'Choose one folder, --all, or --last.' }
            $group = $Matches[1]
            continue
        }
        default { throw "Unknown argument: $option. Use --help." }
    }
}
if ($last -and $group) { throw 'Use --last without another folder or --all.' }
if (-not $Run -and $appArgs.Count) { throw 'Application arguments are only supported by run.bat.' }
$project = Split-Path $PSScriptRoot -Parent
$historyFile = Join-Path $project 'build/.build-history.json'
$history = @{ lastBuild = $null; lastGroup = $null }
if (Test-Path -LiteralPath $historyFile) {
    $saved = Get-Content -LiteralPath $historyFile -Raw | ConvertFrom-Json
    $history.lastBuild = $saved.lastBuild
    $history.lastGroup = $saved.lastGroup
}
if ($last) {
    $previous = $history.lastBuild
    if ($Run) { $previous = $history.lastGroup }
    if (-not $previous) { throw 'No previous selection. Run build.bat --base (or another folder) first.' }
    $group = $previous.group
    if (-not $config) { $config = $previous.config }
    if (-not $mode) { $mode = $previous.mode }
    if (-not $app) { $app = $previous.app }
}
if (-not $group) { $group = 'base' }
if (-not $config) { $config = 'Release' }
if (-not $mode) { $mode = 'msvc' }
if ($group -notmatch '^[A-Za-z][A-Za-z0-9_]*$' -or $config -notin @('Release', 'Debug') -or $mode -notin @('msvc', 'clang', 'cuda')) {
    throw 'Invalid build selection in command or history.'
}
if ($app -and $app -notmatch '^[A-Za-z][A-Za-z0-9_]*$') { throw 'Invalid executable name.' }
if ($group -eq 'all') {
    if ($Run -or $app) { throw '--all is a build selection. Run one folder or use run.bat --last.' }
} else {
    $source = Join-Path $project "userSrc/$group/CMakeLists.txt"
    if ($group -eq 'tests') { $source = Join-Path $project 'tests/CMakeLists.txt' }
    if (-not (Test-Path -LiteralPath $source)) { throw "No build group: $group (expected userSrc/$group/CMakeLists.txt)" }
}
function Save-History {
    New-Item -ItemType Directory -Force (Split-Path $historyFile -Parent) | Out-Null
    $json = $history | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText($historyFile, $json, (New-Object System.Text.UTF8Encoding($false)))
}
$selection = [pscustomobject]@{ group = $group; config = $config; mode = $mode; app = $app }
# Keep a selected app when rebuilding the same folder and configuration.
if (-not $app -and $history.lastGroup -and $history.lastGroup.group -eq $group -and
    $history.lastGroup.config -eq $config -and $history.lastGroup.mode -eq $mode) {
    $app = $history.lastGroup.app
    $selection.app = $app
}
Push-Location $project
try {
    $buildDir = 'build'
    $preset = ''
    if ($mode -eq 'clang') {
        $preset = 'clang-' + $config.ToLowerInvariant()
        $buildDir = 'build/' + $preset
    } elseif ($mode -eq 'cuda') { $buildDir = 'build_cuda' }
    $runtime = Join-Path $project "$buildDir/$config/$group"
    if (-not $Run) {
        if ($preset) { & cmake --preset $preset }
        else {
            $flags = @('-S', '.', '-B', $buildDir, "-DCMAKE_BUILD_TYPE=$config")
            if ($mode -eq 'cuda') { $flags += '-DALICE2_ENABLE_CUDA=ON' }
            & cmake @flags
        }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & cmake --build $buildDir --config $config --target "user_$group" --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $history.lastBuild = $selection
        if ($group -ne 'all') { $history.lastGroup = $selection }
        elseif (-not $history.lastGroup) {
            $history.lastGroup = [pscustomobject]@{ group = 'base'; config = $config; mode = $mode; app = '' }
        }
        Save-History
        if ($group -eq 'all') { Write-Host "Runnable folders: $(Join-Path $project "$buildDir/$config")" }
        else { Write-Host "Runnable folder: $runtime" }
    } else {
        $manifest = Join-Path $project "$buildDir/groups/$group.txt"
        if (-not (Test-Path -LiteralPath $manifest)) { throw "Build the folder first: build.bat --$group" }
        $enabledApps = @(Get-Content -LiteralPath $manifest | Where-Object { $_ })
        if ($app -and $app -notin $enabledApps) {
            if ($appExplicit) { throw "App '$app' is not enabled in userSrc/$group/CMakeLists.txt." }
            $app = ''
            $selection.app = ''
        }
        if (-not $enabledApps.Count) { throw "No sketches enabled in userSrc/$group/CMakeLists.txt." }
        if ($app) { $exe = Join-Path $runtime "$app.exe" }
        else {
            $executables = @($enabledApps | ForEach-Object { Get-Item -LiteralPath (Join-Path $runtime "$_.exe") })
            if ($executables.Count -ne 1) {
                $executables | ForEach-Object { Write-Host $_.BaseName }
                throw 'Select an app with --app=executable_name; run.bat --last will remember it.'
            }
            $exe = $executables[0].FullName
            $selection.app = $executables[0].BaseName
        }
        if (-not (Test-Path -LiteralPath $exe)) { throw "Build the app first: $exe" }
        $history.lastGroup = $selection
        Save-History
        & $exe @appArgs
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
