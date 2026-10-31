# scripts/write-panel-data.ps1 - regenerate apps/qt-interface/panel-data.js from the React tables.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/write-panel-data.ps1
#
# The generator itself is lib/cnc/panel-data.ts; this script only sets PANEL_DATA_WRITE=1 and runs
# the test file that owns it with the repository vitest. A normal `pnpm test` compares the committed
# module instead of writing it.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$app = Join-Path $root 'apps\react-interface'
$vitest = Join-Path $app 'node_modules\.bin\vitest.cmd'
if (-not (Test-Path -LiteralPath $vitest)) {
    Write-Host 'write-panel-data: FAIL vitest not found - run the app install first' -ForegroundColor Red
    exit 1
}

$env:PANEL_DATA_WRITE = '1'
Push-Location $app
try {
    & $vitest run lib/cnc/panel-data.test.ts
    $code = $LASTEXITCODE
} finally {
    Pop-Location
    Remove-Item Env:PANEL_DATA_WRITE -ErrorAction SilentlyContinue
}
if ($code -ne 0) {
    Write-Host 'write-panel-data: FAIL' -ForegroundColor Red
    exit $code
}
Write-Host 'write-panel-data: OK apps/qt-interface/panel-data.js' -ForegroundColor Green
