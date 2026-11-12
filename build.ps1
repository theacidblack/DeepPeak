# Builds DeepPeak.exe into .\build using the Visual Studio Build Tools.
# Usage: .\build.ps1 [-Version 1.0.0]
param(
    [string]$Version = '1.0.0'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

# Numeric part only: 1.0.1-3-gabc -> 1.0.1, anything unusable -> 0.0.0
$num = ($Version -split '-')[0]
if ($num -notmatch '^\d+\.\d+\.\d+$') { $num = '0.0.0' }
$parts = $num.Split('.')
$fileVersion = '{0},{1},{2},0' -f $parts[0], $parts[1], $parts[2]

# app.rc stays the human-edited template; the version and the icon path are
# injected into a generated copy so a tagged build reports the right version.
$rcText = Get-Content -Raw (Join-Path $root 'app.rc')
$rcText = $rcText -replace 'FILEVERSION [\d,]+', "FILEVERSION $fileVersion"
$rcText = $rcText -replace 'PRODUCTVERSION [\d,]+', "PRODUCTVERSION $fileVersion"
$rcText = $rcText -replace 'VALUE "FileVersion", "[^"]*"', "VALUE `"FileVersion`", `"$num.0`""
$rcText = $rcText -replace 'VALUE "ProductVersion", "[^"]*"', "VALUE `"ProductVersion`", `"$num.0`""
$iconPath = (Join-Path $root 'app.ico').Replace('\', '/')
$rcText = $rcText -replace '1 ICON "app.ico"', "1 ICON `"$iconPath`""
$generatedRc = Join-Path $build 'app.rc'
Set-Content -Path $generatedRc -Value $rcText -Encoding utf8

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install Visual Studio Build Tools with C++ workload." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No MSVC C++ toolset found. Install the 'Desktop development with C++' workload." }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

Push-Location $build
try {
    $cmd = "`"$vcvars`" >nul && rc /nologo /fo `"$build\app.res`" `"$generatedRc`" && cl /nologo /std:c++17 /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE `"$root\src\main.cpp`" `"$build\app.res`" /Fe:DeepPeak.exe /link /SUBSYSTEM:WINDOWS"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "compile failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Copy-Item (Join-Path $root 'sounds') $build -Recurse -Force

$exe = Join-Path $build 'DeepPeak.exe'
Write-Host ""
Write-Host "Built: $exe"
Write-Host ("Size : {0:N0} bytes" -f (Get-Item $exe).Length)
Write-Host "Version: $num"
