# tools/panel-diff.ps1 - compare two panel screenshots with a channel tolerance.
#
# Pixel parity between Chromium and Qt is not "every byte equal": their text rasterisers differ.
# The tool therefore compares a reference and an actual shot with an explicit channel tolerance,
# writes a red overlay of the differing pixels for the reviewer, and fails when the share of
# differing pixels is over the budget.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/panel-diff.ps1 `
#       -Reference img/react/auto.png -Actual build/artifacts/screenshots/auto.png `
#       -Diff build/artifacts/screenshots/auto.diff.png -Tolerance 8 -MaxDiff 0.005
#
# Exit code: 0 - within the budget; 1 - over the budget, sizes differ or a file is missing.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Reference,
    [Parameter(Mandatory = $true)][string]$Actual,
    [string]$Diff = 'build/artifacts/screenshots/panel.diff.png',
    [ValidateRange(0, 255)][int]$Tolerance = 8,
    [ValidateRange(0, 1)][double]$MaxDiff = 0.005
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Pixel work stays in C#: a PowerShell loop over 1024x600 costs tens of seconds, which would make
# the check too slow to run on every panel change.
if (-not ('PanelDiff' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class PanelDiff
{
    public static long Different;
    public static int Total;

    public static bool Compare(string reference, string actual, string diff, int tolerance, out string error)
    {
        error = string.Empty;
        using (var refBitmap = new Bitmap(reference))
        using (var actBitmap = new Bitmap(actual))
        {
            if (refBitmap.Width != actBitmap.Width || refBitmap.Height != actBitmap.Height)
            {
                error = string.Format("sizes differ: {0}x{1} and {2}x{3}",
                    refBitmap.Width, refBitmap.Height, actBitmap.Width, actBitmap.Height);
                return false;
            }

            var rect = new Rectangle(0, 0, actBitmap.Width, actBitmap.Height);
            var format = PixelFormat.Format32bppArgb;
            var refData = refBitmap.LockBits(rect, ImageLockMode.ReadOnly, format);
            var actData = actBitmap.LockBits(rect, ImageLockMode.ReadOnly, format);
            var stride = actData.Stride;
            var size = stride * actBitmap.Height;
            var refBytes = new byte[size];
            var actBytes = new byte[size];
            Marshal.Copy(refData.Scan0, refBytes, 0, size);
            Marshal.Copy(actData.Scan0, actBytes, 0, size);
            refBitmap.UnlockBits(refData);
            actBitmap.UnlockBits(actData);

            using (var diffBitmap = new Bitmap(actBitmap.Width, actBitmap.Height, format))
            {
                var diffData = diffBitmap.LockBits(rect, ImageLockMode.WriteOnly, format);
                var diffBytes = new byte[size];
                Array.Copy(actBytes, diffBytes, size);

                Different = 0;
                Total = actBitmap.Width * actBitmap.Height;
                for (var y = 0; y < actBitmap.Height; ++y)
                {
                    var row = y * stride;
                    for (var x = 0; x < actBitmap.Width; ++x)
                    {
                        var i = row + x * 4;
                        if (Math.Abs(refBytes[i] - actBytes[i]) > tolerance
                            || Math.Abs(refBytes[i + 1] - actBytes[i + 1]) > tolerance
                            || Math.Abs(refBytes[i + 2] - actBytes[i + 2]) > tolerance)
                        {
                            ++Different;
                            diffBytes[i] = 40;
                            diffBytes[i + 1] = 40;
                            diffBytes[i + 2] = 220;
                            diffBytes[i + 3] = 255;
                        }
                    }
                }

                Marshal.Copy(diffBytes, 0, diffData.Scan0, size);
                diffBitmap.UnlockBits(diffData);
                diffBitmap.Save(diff, ImageFormat.Png);
            }
        }
        return true;
    }
}
'@
}

foreach ($path in @($Reference, $Actual)) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "panel-diff: FAIL нет файла $path" -ForegroundColor Red
        exit 1
    }
}
if (-not [System.IO.Path]::IsPathRooted($Diff)) {
    $Diff = Join-Path (Split-Path -Parent $PSScriptRoot) $Diff
}
$diffDir = Split-Path -Parent $Diff
if ($diffDir) { New-Item -ItemType Directory -Force -Path $diffDir | Out-Null }

$errorText = ''
$compared = [PanelDiff]::Compare($Reference, $Actual, $Diff, $Tolerance, [ref]$errorText)
if (-not $compared) {
    Write-Host "panel-diff: FAIL $errorText" -ForegroundColor Red
    exit 1
}

$share = [PanelDiff]::Different / [PanelDiff]::Total
$summary = 'panel-diff: different {0} of {1} px ({2:P2}), tolerance {3}, budget {4:P2}' -f `
    [PanelDiff]::Different, [PanelDiff]::Total, $share, $Tolerance, $MaxDiff
if ($share -gt $MaxDiff) {
    Write-Host ($summary + ' -> FAIL') -ForegroundColor Red
    Write-Host ("panel-diff: diff " + $Diff)
    exit 1
}
Write-Host ($summary + ' -> OK') -ForegroundColor Green
Write-Host ("panel-diff: diff " + $Diff)
