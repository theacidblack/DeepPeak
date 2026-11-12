# Live behaviour test: the app must actually tick.
#
# Starts the app, then samples the tray tooltip once a second through UI
# Automation. A build without a running timer shows a frozen countdown, which is
# exactly the bug this catches: every clip and colour is driven by that tick.
#
# Usage: .\tools\test-live.ps1 [-Exe .\build\DeepPeak.exe] [-Seconds 130]

param(
    [string]$Exe = '',
    [int]$Seconds = 130
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe) { $Exe = Join-Path $root 'build\DeepPeak.exe' }
if (-not (Test-Path $Exe)) { throw "not found: $Exe (run .\build.ps1 first)" }

# Close any running copy cleanly so the test starts from one known instance.
Get-Process DeepPeak -ErrorAction SilentlyContinue | ForEach-Object {
    try { & $Exe --quit | Out-Null } catch { }
}
Start-Sleep -Milliseconds 900
if (Get-Process DeepPeak -ErrorAction SilentlyContinue) {
    throw 'a DeepPeak process is still running; close it before testing'
}

function Read-TrayTip {
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)
    $hits = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants, $cond) |
        Where-Object { $_.Current.Name -like 'DeepPeak - *' }
    if (-not $hits) { return $null }
    # Ghost icons from killed processes can merge into the same element; the
    # live instance is the last one in the string.
    $name = ($hits | Select-Object -First 1).Current.Name
    $all = [regex]::Matches($name, 'DeepPeak - (Peak|Off-peak) 1?2?x - \w+ for (?:(\d+)h )?(\d+)m')
    if ($all.Count -eq 0) { return $null }
    $m = $all[$all.Count - 1]
    $minutes = 0
    if ($m.Groups[2].Success) { $minutes += [int]$m.Groups[2].Value * 60 }
    $minutes += [int]$m.Groups[3].Value
    return [pscustomobject]@{ Text = $m.Value; Tier = $m.Groups[1].Value; Minutes = $minutes }
}

$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 3
if ($proc.HasExited) { throw "app exited immediately (code $($proc.ExitCode))" }

$samples = @()
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    $tip = Read-TrayTip
    if ($tip) { $samples += $tip }
    Start-Sleep -Seconds 1
}

$failures = @()
if ($samples.Count -lt 10) {
    $failures += "tray tooltip not readable ($($samples.Count) samples)"
} else {
    $first = $samples[0]
    $last = $samples[-1]
    $changes = 0
    for ($i = 1; $i -lt $samples.Count; $i++) {
        if ($samples[$i].Text -ne $samples[$i - 1].Text) { $changes++ }
    }
    if ($first.Tier -ne $last.Tier) {
        # A real price switch inside the window is fine, just report it.
    } elseif ($last.Minutes -gt $first.Minutes) {
        $failures += "countdown went up: $($first.Text) -> $($last.Text)"
    } elseif ($last.Minutes -eq $first.Minutes) {
        $failures += "countdown frozen at $($first.Minutes)m for $Seconds s: the timer is not running"
    }
}

& $Exe --quit | Out-Null
Start-Sleep -Milliseconds 900
if (Get-Process DeepPeak -ErrorAction SilentlyContinue) {
    $failures += 'app did not exit on --quit'
}

Write-Host ''
if ($samples.Count -gt 0) {
    Write-Host ("first sample : " + $samples[0].Text)
    Write-Host ("last sample  : " + $samples[-1].Text)
    Write-Host ("samples      : " + $samples.Count + "  text changes: " + $changes)
}
if ($failures.Count -eq 0) {
    Write-Host 'result       : PASS'
    exit 0
}
$failures | ForEach-Object { Write-Host ("FAIL: " + $_) }
Write-Host 'result       : FAIL'
exit 1
