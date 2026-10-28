param([ValidateSet('topology','harmonic')][string]$Mode='topology',[int]$Layer=32,[ValidateSet('facetag','printmesh')][string]$View='facetag',[ValidatePattern('^[a-z0-9_-]+$')][string]$Suffix='verified')
$ErrorActionPreference='Stop'
$runtime=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build_surface_height/bin/Release'))
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$images=Join-Path $repo 'agents/docs/images'
$camera=Join-Path $runtime 'src/cameras/camera_saves.json'
$before=(Get-FileHash -LiteralPath $camera -Algorithm SHA256).Hash
$inputPath='\\zaha-hadid.com\Data\Projects\4163_Carbcomn\4163_Codex\shared-assets\slicer_inputs\example_facetags_3-6.usda'
$inputHash=(Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash
$name=if($View -eq 'printmesh'){"print_mesh_main_${Mode}_f1_${Suffix}"}else{"facetag_main_${Mode}_layer${Layer}_f1_${Suffix}"}
$oldSize=$env:ALICE2_SCREENSHOT_SIZE
try {
 $env:ALICE2_SCREENSHOT_SIZE='3840x2160'
 $env:ALICE2_VALIDATE_SLICER_INPUT=$inputPath
 $env:ALICE2_VALIDATE_SLICER_OUTPUT=''
 $env:ALICE2_VALIDATE_SLICER_MODE=$Mode
 $env:ALICE2_VALIDATE_DISPLAY_MODE=if($View -eq 'printmesh'){if($Mode -eq 'topology'){'5'}else{'6'}}else{if($Mode -eq 'topology'){'8'}else{'9'}}
 $env:ALICE2_VALIDATE_LAYER=[string]$Layer
 $env:ALICE2_VALIDATE_RELAX_ITERATIONS='8'
 $env:ALICE2_VALIDATE_ALLOW_PARTIAL='0'
 $env:OMP_NUM_THREADS='4'
 New-Item -ItemType Directory -Path (Join-Path $repo 'alice2/build_facetags_review') -Force | Out-Null
 $outLog=Join-Path $repo ('alice2/build_facetags_review/'+$name+'.log');$errLog=Join-Path $repo ('alice2/build_facetags_review/'+$name+'.err')
 $start=Get-Date
 $owned=Start-Process -FilePath (Join-Path $runtime 'alice2.exe') -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
 $deadline=(Get-Date).AddSeconds(300)
 while(!$owned.HasExited -and (Get-Date) -lt $deadline) {
   Start-Sleep -Milliseconds 500;$owned.Refresh()
   if(Test-Path -LiteralPath $errLog){$errors=Get-Content -LiteralPath $errLog -Raw -ErrorAction SilentlyContinue;if($errors -match '\[SlicerViewValidation\] FAILED:|SketchManager Error: Error in sketch setup:') {if(!$owned.HasExited){Stop-Process -Id $owned.Id};throw $errors}}
 }
 if(!$owned.HasExited){Stop-Process -Id $owned.Id;throw 'Owned main-workflow capture timed out'}
 $log=(Get-Content -LiteralPath $outLog -Raw)+(Get-Content -LiteralPath $errLog -Raw)
 if($log -match 'PARTIAL, noncompliant|\[SlicerViewValidation\] FAILED:|\[Print mesh\]' -or $log -notmatch '\[SCREENSHOT\] Native render 3840x2160, UI scale=1'){throw ('Capture failed: '+$log)}
 if($View -eq 'facetag' -and $log -notmatch '\[FaceTagField\].*labels=0 3 4 5 6'){throw 'FaceTag ownership field did not render all four side IDs'}
 $png=Get-ChildItem -LiteralPath (Join-Path $runtime 'src/screenshots') -Filter '*_F1.png' | Where-Object LastWriteTime -ge $start | Sort-Object LastWriteTime -Descending | Select-Object -First 1
 if(!$png){throw 'No fresh F1 capture'}
 New-Item -ItemType Directory -Path $images -Force | Out-Null
 $destination=Join-Path $images ($name+'.png')
 if(Test-Path -LiteralPath $destination){throw 'Preserve the existing documentation image'}
 Copy-Item -LiteralPath $png.FullName -Destination $destination
 if($before -ne (Get-FileHash -LiteralPath $camera -Algorithm SHA256).Hash){throw 'Saved F1 camera file changed'}
 $identities=@();foreach($dll in @('alice2.exe','zSpace_Core.dll','zSpace_Interface.dll','zSpace_IO.dll','zSpace_Toolsets.dll')){$path=Join-Path $runtime $dll;$identities+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}}
 $manifest=@{width=3840;height=2160;uiScale=1;slot='F1';mode=$Mode;layer=$Layer;input=$inputPath;inputSHA256=$inputHash;displayMode=[int]$env:ALICE2_VALIDATE_DISPLAY_MODE;relaxIterations=8;heightOptions='Authored/default settings preserved';exportEnabled=$false;cameraPath=$camera;cameraSHA256=$before;cameraUnchanged=$true;runtime=$identities;sourceImage=$png.FullName;finalImage=$destination;sourceImageSHA256=(Get-FileHash -LiteralPath $png.FullName -Algorithm SHA256).Hash;finalImageSHA256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash;stdout=$outLog;stderr=$errLog;capturedUTC=(Get-Date).ToUniversalTime().ToString('o')}
 $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $images ($name+'.capture.json')) -Encoding utf8
 Write-Output $destination
}finally{$env:ALICE2_SCREENSHOT_SIZE=$oldSize}
