# scripts/link-runtime.ps1 - keep one Qt/OCCT runtime under build/runtime and link it into the
# executable folders, so Windows still finds the DLLs next to each exe without storing them twice.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/link-runtime.ps1
#   powershell ... -Targets build/viewer,build/edges,build/qt-interface
#
# The first target that has Qt6Core.dll becomes the source of the Qt runtime; the OCCT runtime comes
# from CNC_OCC_DIR (novtk is enough) or from build/qt-interface/occt. Files are linked with NTFS
# hardlinks, directories (qml, platforms, ...) with junctions; if the file system refuses, the helper
# falls back to a copy. Only paths under build/ are touched; source tree, docs and img/ are never.

[CmdletBinding()]
param(
    [string[]]$Targets = @('build/viewer', 'build/edges', 'build/qt-interface'),
    [string]$QtDir = $env:CNC_QT_DIR,
    [string]$OccDir = $env:CNC_OCC_DIR
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$runtime = Join-Path $build 'runtime'
$runtimeQt = Join-Path $runtime 'qt'
$runtimeOcct = Join-Path $runtime 'occt'

function Absolute([string]$path) {
    if ([System.IO.Path]::IsPathRooted($path)) { return [System.IO.Path]::GetFullPath($path) }
    return [System.IO.Path]::GetFullPath((Join-Path $root $path))
}
function Ensure-Dir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Force -Path $path | Out-Null }
}
function Remove-Safe([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    $item = Get-Item -LiteralPath $path -Force
    if ($item.LinkType -eq 'Junction' -or $item.LinkType -eq 'SymbolicLink') {
        # rmdir removes the link, never the target's files.
        & cmd.exe /c rmdir "$path" | Out-Null
        return
    }
    Remove-Item -LiteralPath $path -Recurse -Force
}
function Link-File([string]$source, [string]$dest) {
    if (-not (Test-Path -LiteralPath $source)) { return }
    if (Test-Path -LiteralPath $dest) { Remove-Safe $dest }
    try {
        New-Item -ItemType HardLink -Path $dest -Target $source -Force | Out-Null
    } catch {
        Copy-Item -LiteralPath $source -Destination $dest -Force
    }
}
function Link-Dir([string]$source, [string]$dest) {
    if (-not (Test-Path -LiteralPath $source)) { return }
    if (Test-Path -LiteralPath $dest) { Remove-Safe $dest }
    try {
        New-Item -ItemType Junction -Path $dest -Target $source | Out-Null
    } catch {
        Copy-Item -LiteralPath $source -Destination $dest -Recurse -Force
    }
}

$absoluteTargets = @($Targets | ForEach-Object { Absolute $_ })
foreach ($target in $absoluteTargets) {
    if (-not $target.StartsWith($build, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "link-runtime: refusing a target outside build/: $target"
    }
}

Ensure-Dir $runtime
$qtSource = $absoluteTargets | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Qt6Core.dll') } | Select-Object -First 1
if (-not $qtSource) { throw 'link-runtime: no target with Qt6Core.dll; build a Qt executable first' }

# --- Qt runtime -------------------------------------------------------------------------------
Ensure-Dir $runtimeQt
$qtFiles = @('Qt6*.dll', 'd3dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll', 'opengl32sw.dll', 'vc_redist.x64.exe')
$qtDirs = @('qml', 'platforms', 'imageformats', 'iconengines', 'generic', 'networkinformation', 'tls', 'qmltooling', '.qt')
if (-not (Test-Path -LiteralPath (Join-Path $runtimeQt 'Qt6Core.dll'))) {
    foreach ($pattern in $qtFiles) {
        Get-ChildItem -LiteralPath $qtSource -File -Filter $pattern -ErrorAction SilentlyContinue |
            Copy-Item -Destination $runtimeQt -Force
    }
    foreach ($dir in $qtDirs) {
        $from = Join-Path $qtSource $dir
        if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination (Join-Path $runtimeQt $dir) -Recurse -Force }
    }
    Write-Host "link-runtime: Qt runtime from $qtSource"
}

# --- OCCT runtime -----------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath (Join-Path $runtimeOcct 'TKernel.dll'))) {
    Ensure-Dir $runtimeOcct
    $occtSource = ''
    if ($OccDir -and (Test-Path -LiteralPath (Join-Path $OccDir 'bin'))) { $occtSource = Join-Path $OccDir 'bin' }
    elseif (Test-Path -LiteralPath (Join-Path $build 'qt-interface\occt')) { $occtSource = Join-Path $build 'qt-interface\occt' }
    if ($occtSource) {
        Copy-Item -Path (Join-Path $occtSource '*.dll') -Destination $runtimeOcct -Force
        Write-Host "link-runtime: OCCT runtime from $occtSource"
    } else {
        throw 'link-runtime: no OCCT source; set CNC_OCC_DIR'
    }
}

# --- Link the runtime into every target -------------------------------------------------------
foreach ($target in $absoluteTargets) {
    if (-not (Test-Path -LiteralPath $target)) { continue }

    foreach ($pattern in $qtFiles) {
        Get-ChildItem -LiteralPath $target -File -Filter $pattern -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-Safe $_.FullName }
    }
    foreach ($dir in $qtDirs) { Remove-Safe (Join-Path $target $dir) }
    foreach ($pattern in $qtFiles) {
        Get-ChildItem -LiteralPath $runtimeQt -File -Filter $pattern -ErrorAction SilentlyContinue |
            ForEach-Object { Link-File $_.FullName (Join-Path $target $_.Name) }
    }
    foreach ($dir in $qtDirs) { Link-Dir (Join-Path $runtimeQt $dir) (Join-Path $target $dir) }

    # qt-interface keeps OCCT in a subfolder (main.cpp adds it to PATH); the others have the DLLs
    # flat next to the exe, so they get hardlinks.
    $occtDir = Join-Path $target 'occt'
    if ([System.IO.Directory]::Exists($occtDir)) {
        Remove-Safe $occtDir
        Link-Dir $runtimeOcct $occtDir
    } else {
        Get-ChildItem -LiteralPath $runtimeOcct -File -Filter '*.dll' |
            ForEach-Object { Link-File $_.FullName (Join-Path $target $_.Name) }
    }
}

$size = (Get-ChildItem -LiteralPath $build -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
Write-Host ('link-runtime: OK, build/ = {0:N1} MB' -f ($size / 1MB)) -ForegroundColor Green
exit 0
