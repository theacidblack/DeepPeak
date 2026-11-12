# Dev helper: starts DeepPeak, opens the panel, screenshots it to a PNG.
# Usage: .\tools\capture.ps1 -Theme dark|light|system -Out docs\panel-dark.png
param(
    [string]$Theme = 'system',
    [string]$Out   = 'panel.png'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe  = Join-Path $root 'build\DeepPeak.exe'
if (-not (Test-Path $exe)) { throw "Build first: .\build.ps1" }

Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class DeepPeakShot {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public InputUnion u; }
  [StructLayout(LayoutKind.Explicit)] public struct InputUnion { [FieldOffset(0)] public KEYBDINPUT kb; [FieldOffset(0)] public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string name);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int cb);
  public static void Key(ushort vk, bool up) {
    var i = new INPUT[1]; i[0].type = 1; i[0].u.kb.wVk = vk; i[0].u.kb.dwFlags = up ? 2u : 0u;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Chord() {
    Key(0x11,false); System.Threading.Thread.Sleep(60);
    Key(0x44,false); System.Threading.Thread.Sleep(60);
    Key(0x44,true);  System.Threading.Thread.Sleep(60);
    Key(0x50,false); System.Threading.Thread.Sleep(60);
    Key(0x50,true);  System.Threading.Thread.Sleep(90);
    Key(0x11,true);  System.Threading.Thread.Sleep(450);
  }
  public static string Capture(string path) {
    IntPtr h = FindWindow("DeepPeakPanel", null);
    if (h == IntPtr.Zero) return "no window";
    RECT r; GetWindowRect(h, out r);
    int w = r.Right - r.Left, hh = r.Bottom - r.Top;
    var bmp = new System.Drawing.Bitmap(w, hh);
    var g = System.Drawing.Graphics.FromImage(bmp);
    g.CopyFromScreen(r.Left, r.Top, 0, 0, bmp.Size);
    bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    g.Dispose(); bmp.Dispose();
    return r.Left + "," + r.Top + " " + w + "x" + hh;
  }
}
'@ -ReferencedAssemblies System.Drawing

Get-Process DeepPeak -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

if ($Theme -ne 'system') { $env:DEEPPEAK_THEME = $Theme } else { Remove-Item Env:DEEPPEAK_THEME -ErrorAction SilentlyContinue }
Start-Process -FilePath $exe
Start-Sleep -Milliseconds 1200

[DeepPeakShot]::Chord()
$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$info = [DeepPeakShot]::Capture($outPath)
Write-Host "captured $info -> $outPath"
