# scripts/clean.ps1 - remove generated and temporary files (docs/FILE-STRUCTURE.md).
#
# Usage:
#   powershell ... -File scripts/clean.ps1                 # report only, nothing removed
#   powershell ... -File scripts/clean.ps1 -Artifacts      # loose build root files, tmp, logs, old cpp
#   powershell ... -File scripts/clean.ps1 -Next           # apps/react-interface/.next
#   powershell ... -File scripts/clean.ps1 -Build          # build/ entirely (rebuild after)
#   powershell ... -File scripts/clean.ps1 -All            # artifacts + next + build
#   add -DryRun to any of the above to print the targets only.
#
# The script only touches build/ and apps/react-interface/.next. A path outside those is refused.
# Source tree, docs, img/, data/, scripts/ and tools/ are never removed.

[CmdletBinding()]
param(
    [Alias('Artifacts')][switch]$CleanArtifacts,
    [Alias('Build')][switch]$CleanBuild,
    [Alias('Next')][switch]$CleanNext,
    [Alias('All')][switch]$CleanAll,
    [Alias('DryRun')][switch]$CleanDryRun
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$build = Join-Path $root 'build'
$next = Join-Path $root 'apps\react-interface\.next'

function Show-Size([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return 'missing' }
    $m = Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum
    return ('{0} files, {1:N1} MB' -f $m.Count, ($m.Sum / 1MB))
}

function Remove-Safe([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    $allowed = $full.StartsWith($build, [System.StringComparison]::OrdinalIgnoreCase) -or
               $full.StartsWith($next, [System.StringComparison]::OrdinalIgnoreCase)
    if (-not $allowed) { throw "clean: refusing to remove outside build/ or .next: $full" }
    if (-not (Test-Path -LiteralPath $full)) { return }
    if ($CleanDryRun) {
        Write-Host ('would remove ' + $full)
        return
    }
    Remove-Item -LiteralPath $full -Recurse -Force
    Write-Host ('removed ' + $full)
}

if (-not ($CleanArtifacts -or $CleanBuild -or $CleanNext -or $CleanAll)) {
    Write-Host 'clean: report only (add -Artifacts, -Next, -Build or -All to remove)' -ForegroundColor Cyan
    Write-Host ('  build/                  ' + (Show-Size $build))
    if (Test-Path -LiteralPath $build) {
        Get-ChildItem -LiteralPath $build -Directory | ForEach-Object {
            Write-Host ('    ' + $_.Name.PadRight(22) + (Show-Size $_.FullName))
        }
        $loose = @(Get-ChildItem -LiteralPath $build -File)
        Write-Host ('  loose files in build/   ' + $loose.Count)
        $loose | Select-Object -First 10 | ForEach-Object { Write-Host ('    ' + $_.Name) }
    }
    Write-Host ('  apps/react-interface/.next ' + (Show-Size $next))
    exit 0
}

if ($CleanAll) { $CleanArtifacts = $true; $CleanNext = $true; $CleanBuild = $true }

if ($CleanArtifacts) {
    # Every file directly in build/ is a one-off; the subdirectories are the builds.
    if (Test-Path -LiteralPath $build) {
        Get-ChildItem -LiteralPath $build -File | ForEach-Object { Remove-Safe $_.FullName }
    }
    foreach ($name in @('artifacts', 'tmp', 'logs', 'cpp')) {
        Remove-Safe (Join-Path $build $name)
    }
}
if ($CleanNext) { Remove-Safe $next }
if ($CleanBuild) { Remove-Safe $build }

Write-Host 'clean: done' -ForegroundColor Green
