# Writes res/icon.ico with uncompressed 32-bit (DIB) images. Windows XP cannot read
# the PNG-compressed images modern .ico files use.
#
#   .\tools\make-xp-icon.ps1 -Png path\to\icon.png
param(
    [Parameter(Mandatory)] [string]$Png,
    [string]$Out = "$PSScriptRoot\..\res\icon.ico",
    [int[]]$Sizes = @(16, 24, 32, 48, 64)
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [System.Drawing.Image]::FromFile((Resolve-Path $Png))
$images = foreach ($s in $Sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.DrawImage($src, 0, 0, $s, $s); $g.Dispose()

    $ms = [IO.MemoryStream]::new()
    $bw = [IO.BinaryWriter]::new($ms)
    $maskStride = [int][Math]::Ceiling($s / 32) * 4
    # BITMAPINFOHEADER; the height counts the colour image and the AND mask.
    $bw.Write([int]40); $bw.Write([int]$s); $bw.Write([int]($s * 2)); $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]0); $bw.Write([int]($s * $s * 4 + $maskStride * $s)); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) {
            $c = $bmp.GetPixel($x, $y); $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
        }
    }
    # AND mask: transparent where alpha is low, for displays without alpha blending.
    for ($y = $s - 1; $y -ge 0; $y--) {
        $row = New-Object byte[] $maskStride
        for ($x = 0; $x -lt $s; $x++) { if ($bmp.GetPixel($x, $y).A -lt 128) { $row[[int][Math]::Floor($x / 8)] = $row[[int][Math]::Floor($x / 8)] -bor (0x80 -shr ($x % 8)) } }
        $bw.Write($row)
    }
    $bmp.Dispose()
    , @($s, $ms.ToArray())
}
$src.Dispose()

$buf = [IO.MemoryStream]::new()
$w = [IO.BinaryWriter]::new($buf)
$w.Write([int16]0); $w.Write([int16]1); $w.Write([int16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $w.Write([byte]$img[0]); $w.Write([byte]$img[0]); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([int16]1); $w.Write([int16]32); $w.Write([int]$img[1].Length); $w.Write([int]$offset)
    $offset += $img[1].Length
}
foreach ($img in $images) { $w.Write($img[1]) }
[IO.File]::WriteAllBytes((Join-Path (Resolve-Path (Split-Path $Out)) (Split-Path -Leaf $Out)), $buf.ToArray())
Write-Host "Wrote $Out ($($images.Count) images)"
