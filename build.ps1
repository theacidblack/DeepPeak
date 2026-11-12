# Builds DeepPeak.exe into .\build using the Visual Studio Build Tools.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install Visual Studio Build Tools with C++ workload." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No MSVC C++ toolset found. Install the 'Desktop development with C++' workload." }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

Push-Location $build
try {
    $cmd = "`"$vcvars`" >nul && rc /nologo /fo `"$build\app.res`" `"$root\app.rc`" && cl /nologo /std:c++17 /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE `"$root\src\main.cpp`" `"$build\app.res`" /Fe:DeepPeak.exe /link /SUBSYSTEM:WINDOWS"
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
