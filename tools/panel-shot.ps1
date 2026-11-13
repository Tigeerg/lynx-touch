# tools/panel-shot.ps1 - take one offscreen screenshot of the Qt panel.
#
# The panel is loaded directly (not through panel-host), with the simulator fixture and the
# requested theme/screen/workplace. The shot is the reference for the geometry and pixel checks;
# it is not an acceptance artefact by itself.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/panel-shot.ps1 `
#       -Out build/artifacts/screenshots/auto-idle.png -Fixture auto-idle -Screen prog -Workplace RUN -Theme 0
#
# Exit code: 0 - image saved; 1 - tool, fixture, executable or image is missing.

[CmdletBinding()]
param(
    [string]$Out = 'build/artifacts/screenshots/panel-shot.png',
    [string]$Fixture = 'auto-idle',
    [ValidateSet('pos', 'prog', 'offset', 'tool', 'alarm', 'service')]
    [string]$Screen = 'prog',
    [ValidateSet('SETUP', 'TEST', 'RUN')]
    [string]$Workplace = 'RUN',
    [ValidateRange(0, 5)]
    [int]$Theme = 0,
    [int]$DelayMs = 1200,
    [switch]$Touch,
    [string]$Backend = 'sim'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$text) {
    Write-Host "panel-shot: FAIL $text" -ForegroundColor Red
    exit 1
}
function First-Existing([string[]]$paths) {
    foreach ($p in $paths) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return ''
}

$exe = Join-Path $root 'build\qt-interface\lynx-panel.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    Fail "нет $exe - соберите панель: scripts/build-qt-interface.ps1"
}
$qtDir = First-Existing @($env:CNC_QT_DIR, 'C:\Qt\6.12.0\msvc2022_64',
    'C:\Qt\6.10.0\msvc2022_64', 'C:\Qt\6.9.0\msvc2022_64')
if (-not $qtDir) { Fail 'Qt6 не найден: задайте CNC_QT_DIR' }
$occDir = First-Existing @($env:CNC_OCC_DIR, 'D:\cnc-toolchain\occt-env\Library',
    $(if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }))
if (-not $occDir) { Fail 'OCCT не найден: задайте CNC_OCC_DIR' }

$fixturePath = Join-Path $root "apps\qt-interface\backend\fixtures\$Fixture.json"
if (-not (Test-Path -LiteralPath $fixturePath)) {
    Fail "нет фикстуры $fixturePath"
}
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
$outDir = Split-Path -Parent $Out
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
Remove-Item -LiteralPath $Out -ErrorAction SilentlyContinue

$pathParts = @(
    (Join-Path $qtDir 'bin')
    (Join-Path $occDir 'bin')
    (Join-Path $root 'build\qmlbackend\MockBackend')
    (Join-Path $root 'build\edges\CNC\Edges')
)
$env:PATH = ($pathParts -join ';') + ';' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'

$arguments = @(
    ('--backend=' + $Backend)
    ('--fixture=' + $fixturePath)
    ('--screen=' + $Screen)
    ('--workplace=' + $Workplace)
    ('--theme=' + $Theme)
    ('--shot=' + $Out)
    ('--shot-delay=' + $DelayMs)
)
if ($Touch) { $arguments += '--touch' }

# cmd /c waits for a GUI-subsystem process; Start-Process -Wait has returned before the frame in
# this setup, which made the screenshot race the splash and the first paint.
$quotedArguments = @()
foreach ($argument in $arguments) {
    $quotedArguments += '"' + $argument + '"'
}
$argLine = $quotedArguments -join ' '
$command = '"{0}" {1}' -f $exe, $argLine
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { Fail "lynx-panel вернул $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $Out)) { Fail "снимок не появился: $Out" }
Write-Host ("panel-shot: OK " + $Out) -ForegroundColor Green
