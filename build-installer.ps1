# Builds the app and the NSIS installer into dist\.
# Usage: .\build-installer.ps1 [-Version 1.0.0]
param(
    [string]$Version = '1.0.0'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $root 'build.ps1') -Version $Version

$makensis = $null
foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'NSIS\makensis.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\NSIS\makensis.exe'))) {
    if (Test-Path $candidate) { $makensis = $candidate; break }
}
if (-not $makensis) {
    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) { $makensis = $cmd.Source }
}
if (-not $makensis) { throw "makensis.exe not found. Install NSIS (winget install NSIS.NSIS)." }

New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null

Write-Host ""
Write-Host "Building installer $Version ..."
$num = ($Version -split '-')[0]
if ($num -notmatch '^\d+\.\d+\.\d+$') { $num = '0.0.0' }
& $makensis /V2 "/DVERSION=$Version" "/DVERSION_NUM=$num" (Join-Path $root 'installer.nsi')
if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE" }

$setup = Join-Path $root "dist\DeepPeak-$Version-setup.exe"
if (-not (Test-Path $setup)) { throw "installer not produced: $setup" }

$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
$size = (Get-Item $setup).Length
Write-Host ""
Write-Host "Installer: $setup"
Write-Host ("Size     : {0:N0} bytes" -f $size)
Write-Host "SHA256   : $hash"
Set-Content -Path (Join-Path $root "dist\SHA256.txt") -Value "$hash  DeepPeak-$Version-setup.exe" -Encoding ASCII
