# Generates app.ico (green/red split circle) used for the exe icon.
# Small frames are stored as 32bpp DIBs and 256px as PNG, which is what most
# Windows tools and installers expect.
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$root  = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$out   = Join-Path $root 'app.ico'
$sizes = 16, 24, 32, 48, 64, 128, 256

function New-Bitmap([int]$size) {
    $bmp = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad  = [math]::Max(1, [int]($size * 0.06))
    $rect = [System.Drawing.Rectangle]::new($pad, $pad, $size - 2*$pad, $size - 2*$pad)

    # Left half green, right half red: the two billing states.
    $green = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0x6C, 0xCB, 0x5F))
    $red   = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 0xC4, 0x2B, 0x1C))
    $g.FillPie($green, $rect, 90, 180)
    $g.FillPie($red, $rect, 270, 180)

    if ($size -ge 32) {
        $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(40, 255, 255, 255), [math]::Max(1, $size * 0.02))
        $g.DrawEllipse($pen, $rect)
        $pen.Dispose()
    }
    $green.Dispose(); $red.Dispose(); $g.Dispose()
    return $bmp
}

function Get-PngBytes([int]$size) {
    $bmp = New-Bitmap $size
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return ,$ms.ToArray()
}

function Get-DibBytes([int]$size) {
    $bmp = New-Bitmap $size
    $rect = [System.Drawing.Rectangle]::new(0, 0, $size, $size)
    $locked = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $locked.Stride
    $pixels = [byte[]]::new($stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
    $bmp.UnlockBits($locked)
    $bmp.Dispose()

    $maskRow = [int]([math]::Floor(($size + 31) / 32)) * 4
    $maskBytes = [byte[]]::new($maskRow * $size)   # all zero = fully opaque

    $ms = [System.IO.MemoryStream]::new()
    $bw = [System.IO.BinaryWriter]::new($ms)
    # BITMAPINFOHEADER with doubled height (XOR + AND masks)
    $bw.Write([UInt32]40)
    $bw.Write([Int32]$size)
    $bw.Write([Int32]($size * 2))
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]($size * $size * 4 + $maskBytes.Length))
    $bw.Write([Int32]0); $bw.Write([Int32]0)
    $bw.Write([UInt32]0); $bw.Write([UInt32]0)
    # BGRA rows, bottom-up
    for ($y = $size - 1; $y -ge 0; $y--) {
        $bw.Write($pixels, $y * $stride, $size * 4)
    }
    $bw.Write($maskBytes)
    $bw.Flush()
    return ,$ms.ToArray()
}

$frames = @{}
foreach ($s in $sizes) {
    $frames[$s] = if ($s -ge 256) { Get-PngBytes $s } else { Get-DibBytes $s }
}

$fs = [System.IO.File]::Create($out)
$bw = [System.IO.BinaryWriter]::new($fs)
$bw.Write([UInt16]0)            # reserved
$bw.Write([UInt16]1)            # type: icon
$bw.Write([UInt16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    $data = $frames[$s]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)          # palette
    $bw.Write([Byte]0)          # reserved
    $bw.Write([UInt16]1)        # planes
    $bw.Write([UInt16]32)       # bpp
    $bw.Write([UInt32]$data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($s in $sizes) { $bw.Write($frames[$s]) }
$bw.Flush(); $bw.Close(); $fs.Close()

Write-Host "wrote $out ($([math]::Round((Get-Item $out).Length/1KB,1)) KB, $($sizes.Count) frames)"
