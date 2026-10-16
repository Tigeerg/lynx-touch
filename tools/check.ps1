# tools/check.ps1 — сторож стиля и структуры проекта (правила — CLAUDE.md).
#
# Запуск: pnpm guard
# Текстовые правила проверяются здесь и сейчас. Проверки, которым нужны pnpm/node/python,
# выполняются, если инструмент действительно найден, иначе печатают skip.
#
# Метки ниже — не цель роста. Метку понижают в том же коммите, в котором её перешагнули.

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

$marks = @{
  'apps/react-interface/lib/cnc/machine.ts' = 1254  # редуктор сводит, а не впитывает: подсистема -> свой редьюсер
}

$fail = New-Object System.Collections.Generic.List[string]
$skip = New-Object System.Collections.Generic.List[string]

function Ok([string]$text) { Write-Host "ok    $text" -ForegroundColor Green }
function Bad([string]$text) { Write-Host "FAIL  $text" -ForegroundColor Red; $script:fail.Add($text) }
function Warn([string]$text) { Write-Host "warn  $text" -ForegroundColor Yellow }
function Skipped([string]$text) { Write-Host "skip  $text" -ForegroundColor Yellow; $script:skip.Add($text) }

function Run-Tool([string]$label, [scriptblock]$cmd) {
  $code = 1
  try { & $cmd; $code = $LASTEXITCODE } catch { $code = 1 }
  if ($code -ne 0) { Bad $label } else { Ok $label }
}

function Get-ToolVersion([string]$exe, [string[]]$arguments) {
  try { return (& $exe @arguments 2>$null | Out-String).Trim() } catch { return '' }
}

Write-Host 'CNC panel — guard' -ForegroundColor Cyan

$sources = @(
  Get-ChildItem -LiteralPath 'apps/react-interface/app', 'apps/react-interface/components', 'apps/react-interface/lib',
    'apps/qt-interface', 'modules/gcode-viewer' -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in '.ts', '.tsx', '.js', '.mjs', '.py', '.qml', '.vert', '.frag' }
)

# 1. В КОММЕНТАРИИ стрелка «ведёт к» — только ASCII '->': '->' читается в консоли распорядителя (cp866) и в логах.
#    Глифы клавиш (влево/вправо/вверх/вниз) — изображение клавиш, а не текст: их не трогают.
$arrowHits = New-Object System.Collections.Generic.List[string]
foreach ($file in $sources) {
  $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $comment = $null
    if ($file.Extension -eq '.py') {
      $k = $line.IndexOf('#')
      if ($k -ge 0) { $comment = $line.Substring($k) }
    } else {
      $t = $line.TrimStart()
      if ($t.StartsWith('//') -or $t.StartsWith('/*') -or $t.StartsWith('*')) { $comment = $line }
    }
    if ($comment -and $comment -match '[\u2192\u21d2]') {
      $arrowHits.Add("$($file.FullName):$($i + 1)")
    }
  }
}
if ($arrowHits.Count -gt 0) {
  foreach ($h in $arrowHits) { Bad "стрелка Unicode в комментарии: $h" }
} else {
  Ok 'в комментариях стрелок Unicode нет'
}

# 2. Иконки только lucide (`lucide-react` в React, fonts/lucide.ttf в QML).
$iconPattern = 'react-icons|@heroicons|feather-icons|phosphor-icons|@tabler/icons|bootstrap-icons|@radix-ui/react-icons'
$iconHits = $sources | Select-String -Pattern $iconPattern -ErrorAction SilentlyContinue
if ($iconHits) {
  foreach ($h in $iconHits) { Bad "чужой набор иконок в $($h.Path):$($h.LineNumber)" }
} else {
  Ok 'набор иконок — lucide'
}

# 3. Метки размера: держатся равенством, растут и падают с правкой метки.
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

# 4. preview.qml — сгенерированный файл: держи его не старее main.qml.
$mainQml = Get-Item -LiteralPath 'apps/qt-interface/main.qml'
$previewQml = Get-Item -LiteralPath 'apps/qt-interface/preview.qml'
if ($mainQml.LastWriteTime -gt $previewQml.LastWriteTime.AddSeconds(1)) {
  Warn 'apps/qt-interface/main.qml новее preview.qml — перегенерируй: python apps/qt-interface/gen_preview_qml.py'
} else {
  Ok 'preview.qml не отстаёт от main.qml'
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

$pythonReal = $false
if (Get-Command python -ErrorAction SilentlyContinue) {
  if ((Get-ToolVersion 'python' @('-c', "import sys; print('%d.%d' % sys.version_info[:2])")) -match '^3\.\d+$') {
    $pythonReal = $true
  }
}
if ($pythonReal) {
  Run-Tool 'py_compile mock_backend.py' { python -m py_compile modules/gcode-viewer/mock_backend.py }
  $env:QT_QPA_PLATFORM = 'offscreen'
  Run-Tool 'smoke_test_qml.py' { python modules/gcode-viewer/smoke_test_qml.py }
  # Кода возврата мало: фоновая трассировка или записанное предупреждение внутри
  # прогона (например, отказ скана коллизий) код выхода не меняют - прогон всё
  # равно печатает "loaded OK". Такие отказы обязаны быть видны.
  Run-Tool 'preview_3d.py offscreen' {
    $log = python modules/gcode-viewer/preview_3d.py 'data/cad-testdata/Test_model.ngc' 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0 -or ($log | Select-String -Pattern 'Traceback|^(WARNING|ERROR) ' -Quiet)) { cmd /c exit 1 }
    else { cmd /c exit 0 }
  }
  Run-Tool 'test_tangent_policy.py' { python modules/gcode-viewer/test_tangent_policy.py }
  Run-Tool 'test_normals_golden.py' { python modules/gcode-viewer/test_normals_golden.py }
  Run-Tool 'test_normals_orientation.py' { python modules/gcode-viewer/test_normals_orientation.py }
  Run-Tool 'test_outline_segments.py' { python modules/gcode-viewer/test_outline_segments.py }
  Run-Tool 'test_module_names.py' { python modules/gcode-viewer/test_module_names.py }
  Run-Tool 'test_gcode_parser.py' { python modules/gcode-viewer/test_gcode_parser.py }
  Run-Tool 'test_drill_cycles.py' { python modules/gcode-viewer/test_drill_cycles.py }
  Run-Tool 'test_tool_profile.py' { python modules/gcode-viewer/test_tool_profile.py }
  Run-Tool 'test_collision.py' { python modules/gcode-viewer/test_collision.py }
  Run-Tool 'oracle_dump.py' { python modules/gcode-viewer/oracle_dump.py }
} else {
  Skipped 'python: py_compile, offscreen-прогоны, test_tangent_policy.py, test_normals_golden.py'
}

# 6. Сборка QML-плагина CNC.Edges (StepEdges) и сверка рёбер STEP с питоновским
#    эталоном. Нужен полный C++-тулчейн: Qt6, OCCT, cmake, ninja, vcvars64.
#    Тулчейна нет — шаг пропускается, как остальные инструментальные проверки.
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

if ($cppReady -and $pythonReal) {
  Run-Tool 'build-viewer.ps1 (плагин CNC.Edges, ядро gvcore, геометрии, STEP)' {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
  }
  if (Test-Path -LiteralPath 'build/edges/CNC/Edges/qmldir') {
    Ok 'модуль CNC.Edges собран (build/edges/CNC/Edges)'
  } else {
    Bad 'модуль CNC.Edges не собран: нет build/edges/CNC/Edges/qmldir'
  }
  if (Test-Path -LiteralPath 'build/edges/stepledges_probe.exe') {
    Run-Tool 'test_stepledges_plugin.py' { python modules/gcode-viewer/test_stepledges_plugin.py }
  } else {
    Bad 'нет пробника build/edges/stepledges_probe.exe'
  }
  if (Test-Path -LiteralPath 'build/edges/gcode_probe.exe') {
    Run-Tool 'test_gcode_core.py' { python modules/gcode-viewer/test_gcode_core.py }
  } else {
    Bad 'нет пробника ядра build/edges/gcode_probe.exe'
  }
  if (Test-Path -LiteralPath 'build/edges/geometry_probe.exe') {
    Run-Tool 'test_geometry_core.py' { python modules/gcode-viewer/test_geometry_core.py }
  } else {
    Bad 'нет пробника геометрий build/edges/geometry_probe.exe'
  }
  if (Test-Path -LiteralPath 'build/edges/step_probe.exe') {
    Run-Tool 'test_step_core.py' { python modules/gcode-viewer/test_step_core.py }
  } else {
    Bad 'нет пробника STEP и нормалей build/edges/step_probe.exe'
  }
} else {
  Skipped 'сборка плагина CNC.Edges, ядра gvcore и сверка с эталоном: нет Qt6/OCCT/cmake/ninja/vcvars64'
}

Write-Host ''
if ($fail.Count -gt 0) {
  Write-Host "GUARD: FAIL ($($fail.Count))" -ForegroundColor Red
  exit 1
}
$tail = if ($skip.Count -gt 0) { " (пропущено: $($skip.Count))" } else { '' }
Write-Host "GUARD: OK$tail" -ForegroundColor Green
exit 0
