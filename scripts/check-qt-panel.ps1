# scripts/check-qt-panel.ps1 - offscreen checks of the Qt port (apps/qt-interface/tests).
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-qt-panel.ps1
#
# Runs qmltestrunner with the MockBackend and CNC.Edges import paths, the same runtime the panel
# uses. Qt and OCCT directories come from CNC_QT_DIR/CNC_OCC_DIR. Exit code 0 - all tests passed.

[CmdletBinding()]
param(
    [string]$QtDir = $env:CNC_QT_DIR,
    [string]$OccDir = $env:CNC_OCC_DIR
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$text) {
    Write-Host "check-qt-panel: FAIL $text" -ForegroundColor Red
    exit 1
}
function First-Existing([string[]]$paths) {
    foreach ($p in $paths) { if ($p -and (Test-Path -LiteralPath $p)) { return $p } }
    return ''
}

if (-not $QtDir) {
    $QtDir = First-Existing @('C:\Qt\6.12.0\msvc2022_64', 'C:\Qt\6.10.0\msvc2022_64', 'C:\Qt\6.9.0\msvc2022_64')
}
if (-not $QtDir) { Fail 'Qt6 не найден: задайте CNC_QT_DIR' }
$runner = Join-Path $QtDir 'bin\qmltestrunner.exe'
if (-not (Test-Path -LiteralPath $runner)) { Fail "нет $runner" }

if (-not $OccDir) {
    $OccDir = First-Existing @(
        $(if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }),
        'D:\cnc-toolchain\occt-env\Library',
        'C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library')
}

$env:PATH = (Join-Path $root 'build\qmlbackend\MockBackend') + ';' + (Join-Path $root 'build\edges\CNC\Edges') + ';' + (Join-Path $QtDir 'bin') + ';' + $(if ($OccDir) { (Join-Path $OccDir 'bin') + ';' } else { '' }) + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'

$args = @(
    '-input', (Join-Path $root 'apps\qt-interface\tests'),
    '-import', (Join-Path $root 'build\qmlbackend'),
    '-import', (Join-Path $root 'build\edges')
)
& $runner @args
if ($LASTEXITCODE -ne 0) { Fail "qmltestrunner вернул $LASTEXITCODE" }
Write-Host 'check-qt-panel: OK' -ForegroundColor Green
