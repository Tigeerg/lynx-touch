# tools/check.ps1 - the guard of the project style and structure (rules - CLAUDE.md).
#
# Run: pnpm guard
# The text rules are checked here and now. Checks that need pnpm/node run only if the tool is
# really found, otherwise they print skip.
#
# The marks below are not a growth target. A mark is lowered in the same commit that crossed it.

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

$marks = @{
  'apps/react-interface/lib/cnc/machine.ts' = 141  # the reducer composes, it does not absorb: a subsystem -> its own reducer
}

$fail = New-Object System.Collections.Generic.List[string]
$skip = New-Object System.Collections.Generic.List[string]

function Ok([string]$text) { Write-Host "ok    $text" -ForegroundColor Green }
function Bad([string]$text) { Write-Host "FAIL  $text" -ForegroundColor Red; $script:fail.Add($text) }
function Skipped([string]$text) { Write-Host "skip  $text" -ForegroundColor Yellow; $script:skip.Add($text) }

function Run-Tool([string]$label, [scriptblock]$cmd) {
  $code = 1
  try {
    # A native command writes diagnostics to stderr; with $ErrorActionPreference = 'Stop' that
    # becomes a terminating error, and the step is reported failed although it exited 0. The
    # exit code is the only verdict here.
    $ErrorActionPreference = 'Continue'
    & $cmd
    $code = $LASTEXITCODE
  } catch { $code = 1 }
  if ($code -ne 0) { Bad $label } else { Ok $label }
}

function Get-ToolVersion([string]$exe, [string[]]$arguments) {
  try { return (& $exe @arguments 2>$null | Out-String).Trim() } catch { return '' }
}

Write-Host 'CNC panel — guard' -ForegroundColor Cyan

$sources = @(
  Get-ChildItem -LiteralPath 'apps/react-interface/app', 'apps/react-interface/components', 'apps/react-interface/lib',
    'apps/qt-interface', 'modules/gcode-viewer' -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in '.ts', '.tsx', '.js', '.mjs', '.qml', '.vert', '.frag' }
)

# 1. In a COMMENT the "leads to" arrow is ASCII '->' only: '->' reads in the dispatcher's console (cp866) and in the logs.
#    Key glyphs (left/right/up/down) are a picture of the keys, not text: they are not touched.
$arrowHits = New-Object System.Collections.Generic.List[string]
foreach ($file in $sources) {
  $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $t = $lines[$i].TrimStart()
    if (($t.StartsWith('//') -or $t.StartsWith('/*') -or $t.StartsWith('*')) -and
        $lines[$i] -match '[\u2192\u21d2]') {
      $arrowHits.Add("$($file.FullName):$($i + 1)")
    }
  }
}
if ($arrowHits.Count -gt 0) {
  foreach ($h in $arrowHits) { Bad "стрелка Unicode в комментарии: $h" }
} else {
  Ok 'в комментариях стрелок Unicode нет'
}

# 2. The bundled fonts (Inter, JetBrains Mono) do not draw the pause glyph U+23F8: in a frame an
#    empty square stands in its place (measurement and analysis - GcodeViewer3D.qml). In QML
#    strings it is not set; a mention of the glyph in a comment that names the forbidden
#    character is not a hit.
$pauseHits = New-Object System.Collections.Generic.List[string]
foreach ($file in $sources) {
  if ($file.Extension -ne '.qml') { continue }
  $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $t = $line.TrimStart()
    if ($t.StartsWith('//') -or $t.StartsWith('/*') -or $t.StartsWith('*')) { continue }
    if ($line -match '\\u23F8|\u23F8') { $pauseHits.Add("$($file.FullName):$($i + 1)") }
  }
}
if ($pauseHits.Count -gt 0) {
  foreach ($h in $pauseHits) { Bad "глиф паузы U+23F8 в строке QML: $h" }
} else {
  Ok 'глифа паузы U+23F8 в строках QML нет'
}

# 3. Icons are lucide only (`lucide-react` in React, fonts/lucide.ttf in QML).
$iconPattern = 'react-icons|@heroicons|feather-icons|phosphor-icons|@tabler/icons|bootstrap-icons|@radix-ui/react-icons'
$iconHits = $sources | Select-String -Pattern $iconPattern -ErrorAction SilentlyContinue
if ($iconHits) {
  foreach ($h in $iconHits) { Bad "чужой набор иконок в $($h.Path):$($h.LineNumber)" }
} else {
  Ok 'набор иконок — lucide'
}

# 4. Size marks: held by equality, they grow and fall with the mark edit.
foreach ($rel in $marks.Keys) {
  $count = (Get-Content -LiteralPath $rel).Count
  $mark = $marks[$rel]
  if ($count -gt $mark) {
    Bad "${rel}: $count строк больше метки $mark — вынеси подсистему в свой редьюсер, метку не поднимай"
  } elseif ($count -lt $mark) {
    Bad "${rel}: $count строк меньше метки $mark — понизь метку в tools/check.ps1 тем же коммитом"
  } else {
    Ok "${rel}: $count строк"
  }
}

# 5. Tool checks - if the tool is real, not a stub.
$pnpmReal = $false
if (Get-Command pnpm -ErrorAction SilentlyContinue) {
  if ((Get-ToolVersion 'pnpm' @('-v')) -match '^\d+\.') { $pnpmReal = $true }
}
if ($pnpmReal) {
  foreach ($script in 'typecheck', 'test', 'format:check') {
    Run-Tool "pnpm run $script" { pnpm -C 'apps/react-interface' run $script }
  }
} else {
  Skipped 'pnpm: typecheck, test, format:check'
}

# 6. C++ build (the gvcore core, the gvqtgeom geometries, the gvstep STEP layer, the CNC.Edges
#    plugin, the MockBackend controller, the gcodeviewer entry point) and running the probes
#    directly, with the --manifest key: comparing the STEP edges, the soup, the STL outline, the
#    silhouette topology, the analytic surfaces and the tangent families with the
#    oracle_manifest.json reference without an interpreter. Needs the full C++ toolchain:
#    Qt6, OCCT, cmake, ninja, vcvars64. Without the toolchain the step is skipped.
$qtDir = if ($env:CNC_QT_DIR) { $env:CNC_QT_DIR } else { 'C:\Qt\6.12.0\msvc2022_64' }
$occDir = if ($env:CNC_OCC_DIR) { $env:CNC_OCC_DIR }
          elseif ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }
          else { 'C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library' }
$vcvarsCandidates = @($env:CNC_VCVARS,
  'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
  $vsRoot = (& $vswhere -latest -products * -property installationPath 2>$null | Out-String).Trim()
  if ($vsRoot) { $vcvarsCandidates += (Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat') }
}
$cmakeExe = if (Get-Command cmake -ErrorAction SilentlyContinue) { (Get-Command cmake).Source }
            else { 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
$ninjaExe = if (Get-Command ninja -ErrorAction SilentlyContinue) { (Get-Command ninja).Source }
            else { 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' }
$vcvars = $vcvarsCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
$cppReady = (Test-Path -LiteralPath (Join-Path $qtDir 'lib\cmake\Qt6\Qt6Config.cmake')) -and
            (Test-Path -LiteralPath (Join-Path $occDir 'lib\cmake\opencascade\OpenCASCADEConfig.cmake')) -and
            $vcvars -and (Test-Path -LiteralPath $cmakeExe) -and (Test-Path -LiteralPath $ninjaExe)

if ($cppReady) {
  Run-Tool 'build-viewer.ps1 (ядро gvcore, геометрии, STEP, плагин, контроллер, точка входа)' {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
  }
  if (Test-Path -LiteralPath 'build/edges/CNC/Edges/qmldir') {
    Ok 'модуль CNC.Edges собран (build/edges/CNC/Edges)'
  } else {
    Bad 'модуль CNC.Edges не собран: нет build/edges/CNC/Edges/qmldir'
  }
  # The probes link against the OCCT and Qt DLLs: their directories go to PATH before the runs.
  $env:QT_QPA_PLATFORM = 'offscreen'
  $env:PATH = "$(Join-Path $qtDir 'bin');$(Join-Path $occDir 'bin');$PWD\build\edges;$PWD\build\edges\CNC\Edges;$PWD\build\qmlbackend;" + $env:PATH
  $manifest = 'modules/gcode-viewer/oracle_manifest.json'
  Run-Tool 'stepledges_probe (рёбра STEP)' { & 'build/edges/stepledges_probe.exe' 'data/cad-testdata/Test_model.STEP' }
  Run-Tool 'gcode_probe --manifest' { & 'build/edges/gcode_probe.exe' --manifest $manifest }
  Run-Tool 'geometry_probe --manifest' { & 'build/edges/geometry_probe.exe' --manifest $manifest }
  Run-Tool 'step_probe --manifest' { & 'build/edges/step_probe.exe' --manifest $manifest }
  Run-Tool 'stl_probe --manifest' { & 'build/edges/stl_probe.exe' --manifest $manifest }
  Run-Tool 'silhouette_probe --manifest' { & 'build/edges/silhouette_probe.exe' --manifest $manifest }
  Run-Tool 'tangents_probe --manifest' { & 'build/edges/tangents_probe.exe' --manifest $manifest }
  Run-Tool 'version_probe (версии проектов)' { & 'build/edges/version_probe.exe' }
  Run-Tool 'version_probe (путь с кириллицей)' { & 'build/edges/version_probe.exe' "$PWD" }
  Run-Tool 'stock_sim_probe (аналитика поля съёма)' { & 'build/edges/stock_sim_probe.exe' }
  Run-Tool 'gcodeviewer --smoke' { & 'build/viewer/gcodeviewer.exe' --smoke }
  Run-Tool 'stock_bind_probe (привязка поля к траектории)' { & 'build/viewer/stock_bind_probe.exe' }
} else {
  Skipped 'сборка C++ и прогоны пробников: нет тулчейна (Qt6, OCCT, cmake, ninja, vcvars64)'
}

# 7. The C++ viewer entry point (build/viewer/gcodeviewer.exe): an offscreen run on the real NC
#    program. Needs the built exe and Qt in PATH (the exe links against Qt6Core.dll); the entry
#    point adds the plugin directory and the OCCT DLLs itself. Without the exe or Qt the step is
#    skipped.
$viewerExe = 'build/viewer/gcodeviewer.exe'
if ((Test-Path -LiteralPath $viewerExe) -and (Test-Path -LiteralPath (Join-Path $qtDir 'bin'))) {
  Run-Tool 'gcodeviewer.exe offscreen' {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    $log = & $viewerExe 'data/cad-testdata/Test_model.ngc' 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0 -or ($log | Select-String -Pattern 'FAIL|\[QML warning\]|Traceback' -Quiet)) { cmd /c exit 1 }
    else { cmd /c exit 0 }
  }
} else {
  Skipped 'gcodeviewer.exe offscreen: нет собранной точки входа или Qt'
}

# 8. The viewer hotkey probe (build/viewer/keyprobe.exe): feeds keys with the signs of a real
#    event (scan code, VK and the symbol by layout) in the Russian and English layouts and
#    checks that each hotkey switched its own property. The "hotkeys require the language"
#    defect is caught here: before the fix not a single letter works in the Russian layout. The
#    platform is offscreen, no live session is needed. Without the probe or Qt the step is skipped.
$keyProbeExe = 'build/viewer/keyprobe.exe'
if ((Test-Path -LiteralPath $keyProbeExe) -and (Test-Path -LiteralPath (Join-Path $qtDir 'bin'))) {
  Run-Tool 'keyprobe.exe (раскладки ru и en)' {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    $log = & $keyProbeExe 'data/cad-testdata/Test_model.ngc' 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0 -or ($log | Select-String -Pattern 'FAIL|\[QML warning\]|Traceback' -Quiet)) { cmd /c exit 1 }
    else { cmd /c exit 0 }
  }
} else {
  Skipped 'keyprobe.exe: нет собранного пробника или Qt'
}

Write-Host ''
if ($fail.Count -gt 0) {
  Write-Host "GUARD: FAIL ($($fail.Count))" -ForegroundColor Red
  exit 1
}
$tail = if ($skip.Count -gt 0) { " (пропущено: $($skip.Count))" } else { '' }
Write-Host "GUARD: OK$tail" -ForegroundColor Green
exit 0
