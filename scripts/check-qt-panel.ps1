# scripts/check-qt-panel.ps1 - offscreen checks of the Qt port (apps/qt-interface/tests).
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-qt-panel.ps1
#
# Runs qmltestrunner with the MockBackend and CNC.Edges import paths, the same runtime the panel
# uses, then the backend contract test (lynx-backend-test.exe). Qt and OCCT directories come from
# CNC_QT_DIR/CNC_OCC_DIR. Exit code 0 - all tests passed.

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
# QtTest writes to a file here: when stdout is not a console handle it stays empty, and a silent
# green check would hide a broken suite. The log lives in build/ next to the panel build.
$qmlLog = Join-Path $root 'build\qt-interface\qmltest.txt'
& $runner @args -o ($qmlLog + ',txt')
if ($LASTEXITCODE -ne 0) { Fail "qmltestrunner вернул $LASTEXITCODE, лог: $qmlLog" }
$qmlTotals = Select-String -Path $qmlLog -Pattern '^Totals:' | Select-Object -Last 1
if (-not $qmlTotals) { Fail "qmltestrunner не оставил итог в $qmlLog" }
Write-Host ('check-qt-panel: ' + $qmlTotals.Line.Trim())

# The data contract and the simulator are plain C++: the test binary needs the Qt Core/Test DLLs,
# which are already on PATH above. It is built by scripts/build-qt-interface.ps1.
$backendTest = Join-Path $root 'build\qt-interface\lynx-backend-test.exe'
if (-not (Test-Path -LiteralPath $backendTest)) {
    Fail "нет $backendTest - соберите панель: scripts/build-qt-interface.ps1"
}
# QtTest writes to a file here: when stdout is not a console handle it stays empty, and a silent
# green check would hide a broken suite. The log lives in build/ next to the binary.
$backendLog = Join-Path $root 'build\qt-interface\lynx-backend-test.txt'
& $backendTest -o ($backendLog + ',txt')
if ($LASTEXITCODE -ne 0) { Fail "lynx-backend-test вернул $LASTEXITCODE, лог: $backendLog" }
$totals = Select-String -Path $backendLog -Pattern '^Totals:' | Select-Object -Last 1
if (-not $totals) { Fail "lynx-backend-test не оставил итог в $backendLog" }
Write-Host ('check-qt-panel: ' + $totals.Line.Trim())

Write-Host 'check-qt-panel: OK' -ForegroundColor Green
