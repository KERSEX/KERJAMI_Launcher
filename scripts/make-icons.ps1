<#
.SYNOPSIS
    Generates the launcher's logo and icon assets from resources/images/logo-source.png.

.DESCRIPTION
    Writes next to the source image:
      logo.png        full logo (emblem and lettering) with transparent background
      emblem.png      only the emblem above the lettering, transparent background
      app-<n>.png     square application icons (16 to 256 px)
      app.ico         the same sizes as one icon file for the executable

    "Transparent background" means: the source is a logo on pure black, so the
    brightness of each pixel becomes its opacity. That keeps glow effects intact
    on any dark surface of the user interface.

    The generated files are committed. Run this script only when the source
    image changes. Needs Windows PowerShell 5.1 (System.Drawing).
#>
[CmdletBinding()]
param(
    [string]$SourceImage
)

$ErrorActionPreference = 'Stop'
if (-not $SourceImage) {
    # Not a parameter default: $PSScriptRoot is empty there in Windows PowerShell 5.1.
    $SourceImage = Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\images\logo-source.png'
}
Add-Type -AssemblyName System.Drawing

$code = @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class LogoTools
{
    static byte[] ReadBgra(Bitmap bmp, out int stride)
    {
        BitmapData d = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try
        {
            stride = d.Stride;
            byte[] bytes = new byte[d.Stride * bmp.Height];
            Marshal.Copy(d.Scan0, bytes, 0, bytes.Length);
            return bytes;
        }
        finally { bmp.UnlockBits(d); }
    }

    static int Brightest(byte[] px, int i)
    {
        return Math.Max(px[i], Math.Max(px[i + 1], px[i + 2]));
    }

    // Row where the first block of content ends (exclusive).
    public static int FirstBandEnd(Bitmap bmp, int threshold)
    {
        int stride;
        byte[] px = ReadBgra(bmp, out stride);
        bool inBand = false;
        for (int y = 0; y < bmp.Height; y++)
        {
            bool has = false;
            for (int x = 0; x < bmp.Width && !has; x++)
                has = Brightest(px, y * stride + x * 4) > threshold;
            if (has) inBand = true;
            else if (inBand) return y;
        }
        return bmp.Height;
    }

    // Smallest rectangle that contains all pixels brighter than the threshold in rows [top, bottom).
    public static Rectangle ContentBounds(Bitmap bmp, int threshold, int top, int bottom)
    {
        int stride;
        byte[] px = ReadBgra(bmp, out stride);
        int minX = int.MaxValue, minY = int.MaxValue, maxX = -1, maxY = -1;
        for (int y = top; y < bottom; y++)
            for (int x = 0; x < bmp.Width; x++)
                if (Brightest(px, y * stride + x * 4) > threshold)
                {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
        if (maxX < 0) throw new InvalidOperationException("No content found.");
        return new Rectangle(minX, minY, maxX - minX + 1, maxY - minY + 1);
    }

    public static Bitmap Crop(Bitmap src, Rectangle r)
    {
        Bitmap o = new Bitmap(r.Width, r.Height, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(o))
            g.DrawImage(src, new Rectangle(0, 0, r.Width, r.Height), r, GraphicsUnit.Pixel);
        return o;
    }

    static Bitmap ResizeOnce(Bitmap src, int w, int h)
    {
        Bitmap o = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(o))
        using (ImageAttributes ia = new ImageAttributes())
        {
            ia.SetWrapMode(WrapMode.TileFlipXY);
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.SmoothingMode = SmoothingMode.HighQuality;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.CompositingQuality = CompositingQuality.HighQuality;
            g.DrawImage(src, new Rectangle(0, 0, w, h), 0, 0, src.Width, src.Height, GraphicsUnit.Pixel, ia);
        }
        return o;
    }

    // Halves step by step first: one big jump would alias fine details.
    public static Bitmap Resize(Bitmap src, int w, int h)
    {
        Bitmap cur = src;
        bool own = false;
        while (cur.Width > 2 * w || cur.Height > 2 * h)
        {
            Bitmap next = ResizeOnce(cur, Math.Max(w, cur.Width / 2), Math.Max(h, cur.Height / 2));
            if (own) cur.Dispose();
            cur = next;
            own = true;
        }
        Bitmap result = ResizeOnce(cur, w, h);
        if (own) cur.Dispose();
        return result;
    }

    // Brightness becomes opacity; colour is normalised so the brightest channel is 255.
    public static Bitmap BlackToAlpha(Bitmap opaque)
    {
        const int floor = 8; // removes compression noise near black
        int w = opaque.Width, h = opaque.Height, stride;
        byte[] px = ReadBgra(opaque, out stride);
        byte[] result = new byte[px.Length];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int i = y * stride + x * 4;
                int m = Brightest(px, i);
                if (m <= floor) continue; // stays fully transparent
                int alpha = Math.Min(255, (m - floor) * 255 / (255 - floor));
                result[i] = (byte)Math.Min(255, px[i] * 255 / m);
                result[i + 1] = (byte)Math.Min(255, px[i + 1] * 255 / m);
                result[i + 2] = (byte)Math.Min(255, px[i + 2] * 255 / m);
                result[i + 3] = (byte)alpha;
            }
        Bitmap o = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        BitmapData d = o.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        try
        {
            for (int y = 0; y < h; y++)
                Marshal.Copy(result, y * stride, IntPtr.Add(d.Scan0, y * d.Stride), w * 4);
        }
        finally { o.UnlockBits(d); }
        return o;
    }

    static GraphicsPath RoundedRect(float size, float radius)
    {
        float d = radius * 2;
        GraphicsPath p = new GraphicsPath();
        p.AddArc(0, 0, d, d, 180, 90);
        p.AddArc(size - d, 0, d, d, 270, 90);
        p.AddArc(size - d, size - d, d, d, 0, 90);
        p.AddArc(0, size - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    // Black rounded tile with the emblem in the middle. Black matches the
    // source background, so the emblem blends in without a visible edge.
    public static Bitmap Tile(Bitmap emblemOpaque, int size)
    {
        Bitmap tile = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(tile))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (GraphicsPath path = RoundedRect(size, size * 0.22f))
            using (SolidBrush brush = new SolidBrush(Color.Black))
                g.FillPath(brush, path);

            int w = (int)Math.Round(size * 0.90);
            int h = (int)Math.Round(w * (double)emblemOpaque.Height / emblemOpaque.Width);
            using (Bitmap scaled = Resize(emblemOpaque, w, h))
                g.DrawImage(scaled, (size - w) / 2, (size - h) / 2, w, h);
        }
        return tile;
    }

    static byte[] PngBytes(Bitmap bmp)
    {
        using (MemoryStream ms = new MemoryStream())
        {
            bmp.Save(ms, ImageFormat.Png);
            return ms.ToArray();
        }
    }

    // Classic icon image: 32-bit BGRA rows bottom-up plus an (unused) AND mask.
    static byte[] DibBytes(Bitmap bmp)
    {
        int w = bmp.Width, h = bmp.Height, stride;
        byte[] px = ReadBgra(bmp, out stride);
        int maskStride = ((w + 31) / 32) * 4;
        using (MemoryStream ms = new MemoryStream())
        using (BinaryWriter bw = new BinaryWriter(ms))
        {
            bw.Write(40);
            bw.Write(w);
            bw.Write(h * 2);
            bw.Write((short)1);
            bw.Write((short)32);
            bw.Write(0);
            bw.Write(w * h * 4 + maskStride * h);
            bw.Write(0); bw.Write(0); bw.Write(0); bw.Write(0);
            for (int y = h - 1; y >= 0; y--)
                bw.Write(px, y * stride, w * 4);
            bw.Write(new byte[maskStride * h]);
            return ms.ToArray();
        }
    }

    // Large sizes are PNG-compressed, small ones classic bitmaps: every
    // resource compiler and Windows version reads that combination.
    public static void WriteIco(string path, Bitmap[] images)
    {
        byte[][] blobs = new byte[images.Length][];
        for (int i = 0; i < images.Length; i++)
            blobs[i] = images[i].Width >= 256 ? PngBytes(images[i]) : DibBytes(images[i]);

        using (FileStream fs = File.Create(path))
        using (BinaryWriter bw = new BinaryWriter(fs))
        {
            bw.Write((short)0);
            bw.Write((short)1);
            bw.Write((short)images.Length);
            int offset = 6 + 16 * images.Length;
            for (int i = 0; i < images.Length; i++)
            {
                bw.Write((byte)(images[i].Width >= 256 ? 0 : images[i].Width));
                bw.Write((byte)(images[i].Height >= 256 ? 0 : images[i].Height));
                bw.Write((byte)0);
                bw.Write((byte)0);
                bw.Write((short)1);
                bw.Write((short)32);
                bw.Write(blobs[i].Length);
                bw.Write(offset);
                offset += blobs[i].Length;
            }
            foreach (byte[] blob in blobs) bw.Write(blob);
        }
    }
}
'@
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition $code -Language CSharp

$outDir = Split-Path -Parent $SourceImage
$threshold = 40 # brightness above which a pixel counts as content
$margin = 24    # keeps the glow around the shapes

function Get-PaddedRect([Drawing.Rectangle]$Rect, [Drawing.Size]$ImageSize) {
    $padded = [Drawing.Rectangle]::Inflate($Rect, $margin, $margin)
    return [Drawing.Rectangle]::Intersect($padded, [Drawing.Rectangle]::new(0, 0, $ImageSize.Width, $ImageSize.Height))
}

function Save-Png([Drawing.Bitmap]$Bitmap, [string]$Name) {
    $path = Join-Path $outDir $Name
    $Bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    "{0,-14} {1,4} x {2,-4} {3,7:N1} KB" -f $Name, $Bitmap.Width, $Bitmap.Height, ((Get-Item -LiteralPath $path).Length / 1KB)
}

$source = [Drawing.Bitmap]::new($SourceImage)
try {
    $emblemEnd = [LogoTools]::FirstBandEnd($source, $threshold)
    $logoRect = Get-PaddedRect ([LogoTools]::ContentBounds($source, $threshold, 0, $source.Height)) $source.Size
    $emblemRect = Get-PaddedRect ([LogoTools]::ContentBounds($source, $threshold, 0, $emblemEnd)) $source.Size
    "Emblem: $emblemRect, Logo: $logoRect"

    $logoOpaque = [LogoTools]::Crop($source, $logoRect)
    $emblemOpaque = [LogoTools]::Crop($source, $emblemRect)

    # Transparent versions for dark surfaces in the user interface.
    $logoWidth = 640
    $scaled = [LogoTools]::Resize($logoOpaque, $logoWidth, [int][Math]::Round($logoWidth * $logoOpaque.Height / $logoOpaque.Width))
    $logo = [LogoTools]::BlackToAlpha($scaled)
    Save-Png $logo 'logo.png'

    $emblemWidth = 280
    $scaled = [LogoTools]::Resize($emblemOpaque, $emblemWidth, [int][Math]::Round($emblemWidth * $emblemOpaque.Height / $emblemOpaque.Width))
    $emblem = [LogoTools]::BlackToAlpha($scaled)
    Save-Png $emblem 'emblem.png'

    # Application icons.
    $sizes = 16, 24, 32, 48, 64, 128, 256
    $tiles = @()
    foreach ($size in $sizes) {
        $tile = [LogoTools]::Tile($emblemOpaque, $size)
        Save-Png $tile "app-$size.png"
        $tiles += $tile
    }
    $icoPath = Join-Path $outDir 'app.ico'
    [LogoTools]::WriteIco($icoPath, [Drawing.Bitmap[]]$tiles)
    "{0,-14} {1} sizes {2,7:N1} KB" -f 'app.ico', $tiles.Count, ((Get-Item -LiteralPath $icoPath).Length / 1KB)
} finally {
    $source.Dispose()
}
