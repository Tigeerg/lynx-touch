# tools/check.ps1 — сторож стиля и структуры проекта (правила — CLAUDE.md).
#
# Запуск: pnpm guard
# Текстовые правила проверяются здесь и сейчас. Проверки, которым нужны pnpm/node,
# выполняются, если инструмент действительно найден, иначе печатают skip.
#
# Метки ниже — не цель роста. Метку понижают в том же коммите, в котором её перешагнули.

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

$marks = @{
  'apps/react-interface/lib/cnc/machine.ts' = 626  # редуктор сводит, а не впитывает: подсистема -> свой редьюсер
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

# 1. В КОММЕНТАРИИ стрелка «ведёт к» — только ASCII '->': '->' читается в консоли распорядителя (cp866) и в логах.
#    Глифы клавиш (влево/вправо/вверх/вниз) — изображение клавиш, а не текст: их не трогают.
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

# 2. Глиф паузы U+23F8 приложенные шрифты (Inter, JetBrains Mono) не рисуют: в кадре на его месте
#    пустой квадрат (замер и разбор - GcodeViewer3D.qml). В строках QML его не ставят; упоминание
#    глифа в комментарии, которое называет запрещённый символ, находкой не считается.
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

# 3. Иконки только lucide (`lucide-react` в React, fonts/lucide.ttf в QML).
$iconPattern = 'react-icons|@heroicons|feather-icons|phosphor-icons|@tabler/icons|bootstrap-icons|@radix-ui/react-icons'
$iconHits = $sources | Select-String -Pattern $iconPattern -ErrorAction SilentlyContinue
if ($iconHits) {
  foreach ($h in $iconHits) { Bad "чужой набор иконок в $($h.Path):$($h.LineNumber)" }
} else {
  Ok 'набор иконок — lucide'
}

# 4. Метки размера: держатся равенством, растут и падают с правкой метки.
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

# 5. Инструментальные проверки — если инструмент настоящий, а не заглушка.
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

# 6. Сборка C++ (ядро gvcore, геометрии gvqtgeom, слой STEP gvstep, плагин
#    CNC.Edges, контроллер MockBackend, точка входа gcodeviewer) и прогон
#    пробников напрямую, ключом --manifest: сверка рёбер STEP, супа, обводки STL,
#    топологии силуэта, аналитических поверхностей и семейств касательных с
#    эталоном oracle_manifest.json без интерпретатора. Нужен полный C++-тулчейн:
#    Qt6, OCCT, cmake, ninja, vcvars64. Тулчейна нет - шаг пропускается.
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
  # Пробники линкуются с DLL OCCT и Qt: их каталоги идут в PATH до прогонов.
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
  Run-Tool 'stock_sim_probe (аналитика поля съёма)' { & 'build/edges/stock_sim_probe.exe' }
  Run-Tool 'gcodeviewer --smoke' { & 'build/viewer/gcodeviewer.exe' --smoke }
  Run-Tool 'stock_bind_probe (привязка поля к траектории)' { & 'build/viewer/stock_bind_probe.exe' }
} else {
  Skipped 'сборка C++ и прогоны пробников: нет тулчейна (Qt6, OCCT, cmake, ninja, vcvars64)'
}

# 7. C++-точка входа вьювера (build/viewer/gcodeviewer.exe): offscreen-прогон на
#    реальном УП. Нужны собранный exe и Qt в PATH (exe линкуется с Qt6Core.dll);
#    каталог плагина и DLL OCCT точка входа добавляет себе сама. Нет exe или Qt -
#    шаг пропускается.
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

# 8. Пробник горячих клавиш вьювера (build/viewer/keyprobe.exe): подаёт клавиши с
#    признаками настоящего события (скан-код, VK и символ по раскладке) на русской и
#    английской раскладках и сверяет, что каждая горячая клавиша переключила своё
#    свойство. Дефект «горячие клавиши требовательны к языку» ловится здесь: до фикса
#    на русской раскладке не срабатывает ни одна буква. Платформа offscreen, живой
#    сессии не нужно. Нет пробника или Qt - шаг пропускается.
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
