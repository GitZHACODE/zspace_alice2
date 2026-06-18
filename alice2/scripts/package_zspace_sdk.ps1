# This script copies built DLLs, import libraries, and header files of zspace_core
# into alice2/depends/zspace directory and compresses it to depends/zspace.zip

$ErrorActionPreference = "Stop"

# Get script parent folder (alice2/scripts/)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = Resolve-Path (Join-Path $ScriptDir "..")
$DependsDir = Join-Path $ProjectDir "depends"
$SdkDir = Join-Path $DependsDir "zspace"
$ZspaceCoreDir = Resolve-Path (Join-Path $ProjectDir "..\..\zspace_core")
$BuildDir = Join-Path $ProjectDir "build_zspace"

Write-Host "Creating SDK directories..."
$SdkIncludeDir = Join-Path $SdkDir "include\zspace"
$SdkLibDir = Join-Path $SdkDir "lib"
$SdkBinDir = Join-Path $SdkDir "bin"

New-Item -ItemType Directory -Path $SdkIncludeDir -Force | Out-Null
New-Item -ItemType Directory -Path $SdkLibDir -Force | Out-Null
New-Item -ItemType Directory -Path $SdkBinDir -Force | Out-Null

# 1. Copy headers
Write-Host "Copying headers from $ZspaceCoreDir\include\zspace..."
Copy-Item -Path "$ZspaceCoreDir\include\zspace\*" -Destination $SdkIncludeDir -Recurse -Force

# Copy zspace third_party depends headers to include/depends
Write-Host "Copying third_party depends headers from $ZspaceCoreDir\third_party\depends..."
$SdkDependsDir = Join-Path $SdkDir "include\depends"
New-Item -ItemType Directory -Path $SdkDependsDir -Force | Out-Null
Copy-Item -Path "$ZspaceCoreDir\third_party\depends\*" -Destination $SdkDependsDir -Recurse -Force

# Copy zspace src files to include/src (required by template definitions)
Write-Host "Copying src files from $ZspaceCoreDir\src..."
$SdkSrcDir = Join-Path $SdkDir "include\src"
New-Item -ItemType Directory -Path $SdkSrcDir -Force | Out-Null
Copy-Item -Path "$ZspaceCoreDir\src\*" -Destination $SdkSrcDir -Recurse -Force

# 2. Copy libs
Write-Host "Copying lib files from $BuildDir\lib\Release..."
Copy-Item -Path "$BuildDir\lib\Release\zSpace_*.lib" -Destination $SdkLibDir -Force

# 3. Copy dlls
Write-Host "Copying dll files from $BuildDir\bin\Release..."
Copy-Item -Path "$BuildDir\bin\Release\zSpace_*.dll" -Destination $SdkBinDir -Force

# 4. Zip SDK
$ZipPath = Join-Path $DependsDir "zspace.zip"
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
}
Write-Host "Compressing SDK to $ZipPath..."
Compress-Archive -Path $SdkDir -DestinationPath $ZipPath -Force

Write-Host "zSpace SDK successfully packaged at: $SdkDir"
Write-Host "zSpace SDK zip successfully created at: $ZipPath"
