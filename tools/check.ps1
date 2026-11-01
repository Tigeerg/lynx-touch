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
  'apps/react-interface/lib/cnc/machine.ts' = 147  # the reducer composes, it does not absorb: a subsystem -> its own reducer
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

# 4b. The product version is a single number in five places: VERSION, projects.json, the first
#     entry of CHANGELOG.md, the README.md badge, the VERSIONING.md header. A divergence is red
#     with the paths and the numbers. Pure PowerShell, no tools needed.
function Get-FirstGroup([string]$path, [string]$pattern) {
  $hit = Select-String -LiteralPath $path -Pattern $pattern -Encoding UTF8 | Select-Object -First 1
  if ($hit) { return $hit.Matches.Groups[1].Value }
  return $null
}
$versionSources = [ordered]@{
  'VERSION'                          = (Get-Content -LiteralPath 'VERSION' -TotalCount 1).Trim()
  'projects.json (product.version)'  = (Get-Content -LiteralPath 'projects.json' -Encoding UTF8 -Raw | ConvertFrom-Json).version
  'CHANGELOG.md (первая запись ## )' = Get-FirstGroup 'CHANGELOG.md' '^## (\d+\.\d+\.\d+)'
  'README.md (бейдж)'                = Get-FirstGroup 'README.md' 'badge/версия-(\d+\.\d+\.\d+)-'
  'VERSIONING.md (шапка)'            = Get-FirstGroup 'VERSIONING.md' 'Текущая версия — \*\*(\d+\.\d+\.\d+)\*\*'
}
$versionOk = $true
foreach ($entry in $versionSources.GetEnumerator()) {
  if ([string]::IsNullOrEmpty($entry.Value)) {
    Bad "версия продукта: в $($entry.Key) номер не найден"
    $versionOk = $false
  } elseif ($entry.Value -ne $versionSources['VERSION']) {
    Bad "версия продукта: $($entry.Key) = '$($entry.Value)', а VERSION = '$($versionSources['VERSION'])'"
    $versionOk = $false
  }
}
if ($versionOk) { Ok "версии продукта согласованы: $($versionSources['VERSION'])" }

# 4c. Each project version in projects.json must match the project manifest where one exists
#     (VERSIONING.md section 3): react-interface -> package.json, gcode-viewer -> VERSION plus
#     viewerVersion in the QML, quickprog -> core/version.h. qt-interface and cad-testdata have
#     no manifest of their own by design. A divergence is red with the paths and the numbers;
#     without this step a project change can ride a foreign commit with no version bump at all.
$projectsJson = Get-Content -LiteralPath 'projects.json' -Encoding UTF8 -Raw | ConvertFrom-Json
function Project-Version([string]$id) {
  $p = $projectsJson.projects | Where-Object { $_.id -eq $id }
  if ($p) { return $p.version }
  return $null
}
$projectSources = [ordered]@{
  'react-interface: package.json'   = Get-FirstGroup 'apps/react-interface/package.json' '"version":\s*"(\d+\.\d+\.\d+)"'
  'gcode-viewer: VERSION'           = (Get-Content -LiteralPath 'modules/gcode-viewer/VERSION' -TotalCount 1).Trim()
  'gcode-viewer: GcodeViewer3D.qml' = Get-FirstGroup 'modules/gcode-viewer/GcodeViewer3D.qml' 'viewerVersion:\s*"(\d+\.\d+\.\d+)"'
  'quickprog: core/version.h'       = Get-FirstGroup 'apps/quickprog/core/version.h' 'kGeneratorVersion\s*=\s*"(\d+\.\d+\.\d+)"'
}
$projectOk = $true
foreach ($entry in $projectSources.GetEnumerator()) {
  $id = ($entry.Key -split ':')[0]
  $expected = Project-Version $id
  if ([string]::IsNullOrEmpty($entry.Value)) {
    Bad "версия ${id}: в '$($entry.Key)' номер не найден"
    $projectOk = $false
  } elseif ([string]::IsNullOrEmpty($expected)) {
    Bad "версия ${id}: проект не найден в projects.json"
    $projectOk = $false
  } elseif ($entry.Value -ne $expected) {
    Bad "версия ${id}: $($entry.Key) = '$($entry.Value)', а projects.json = '$expected'"
    $projectOk = $false
  }
}
if ($projectOk) { Ok "версии проектов согласованы с манифестами" }

# 5. Tool checks - if the tool is real, not a stub.
$pnpmExe = $null
$pnpmPrefix = @()
$pnpmCmd = Get-Command pnpm.cmd -ErrorAction SilentlyContinue
if ($pnpmCmd) {
  $pnpmExe = $pnpmCmd.Source
} else {
  $corepackCmd = Get-Command corepack.cmd -ErrorAction SilentlyContinue
  if ($corepackCmd) {
    $pnpmExe = $corepackCmd.Source
    $pnpmPrefix = @('pnpm')
  }
}
$pnpmReal = $false
if ($pnpmExe) {
  if ((Get-ToolVersion $pnpmExe ($pnpmPrefix + @('-v'))) -match '^\d+\.') { $pnpmReal = $true }
}
if ($pnpmReal) {
  foreach ($script in 'typecheck', 'test', 'format:check') {
    Run-Tool "pnpm run $script" { & $pnpmExe @pnpmPrefix -C 'apps/react-interface' run $script }
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
$cmakeExe = if ($env:CNC_CMAKE) { $env:CNC_CMAKE }
            elseif (Get-Command cmake -ErrorAction SilentlyContinue) { (Get-Command cmake).Source }
            else { 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
$ninjaExe = if ($env:CNC_NINJA) { $env:CNC_NINJA }
            elseif (Get-Command ninja -ErrorAction SilentlyContinue) { (Get-Command ninja).Source }
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

# 6b. The Qt panel itself: the standalone debug executable and its offscreen checks. They need the
# built MockBackend module (the viewer build above), not a second toolchain check.
if ($cppReady -and (Test-Path -LiteralPath 'build/qmlbackend/MockBackend/gvqmlbackend.dll')) {
  Run-Tool 'build-qt-interface.ps1 (панель Qt)' {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-qt-interface.ps1
  }
  if (Test-Path -LiteralPath 'build/qt-interface/lynx-panel.exe') {
    Ok 'панель Qt собрана (build/qt-interface/lynx-panel.exe)'
  } else {
    Bad 'панель Qt не собрана: нет build/qt-interface/lynx-panel.exe'
  }
  Run-Tool 'Qt-панель offscreen (qmltestrunner)' {
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-qt-panel.ps1
  }
} else {
  Skipped 'панель Qt: нет build/qmlbackend/MockBackend - соберите вьювер'
}

# 7. The C++ viewer entry point (build/viewer/gcodeviewer.exe): an offscreen run on the real NC
#    program. Needs the built exe and Qt in PATH (the exe links against Qt6Core.dll); the entry
#    point adds the plugin directory and the OCCT DLLs itself. Without the exe or Qt the step is
#    skipped.
$viewerExe = 'build/viewer/gcodeviewer.exe'
if ($cppReady -and (Test-Path -LiteralPath $viewerExe) -and (Test-Path -LiteralPath (Join-Path $qtDir 'bin'))) {
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

# 7b. Frame capture on the live platform: the guard checks that the exe writes a 1024x600 PNG.
#     Offscreen Quick3D does not draw the scene, so this is the same headless path as checks.txt.
if ($cppReady -and (Test-Path -LiteralPath $viewerExe)) {
  Run-Tool 'gcodeviewer --frame' {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATH = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    $frameDir = Join-Path $PWD 'build\artifacts\screenshots'
    New-Item -ItemType Directory -Force -Path $frameDir | Out-Null
    $frame = Join-Path $frameDir 'frame_check.png'
    & $viewerExe --frame $frame 'data/cad-testdata/Test_model.ngc' 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $frame)) { cmd /c exit 1 } else { cmd /c exit 0 }
  }
} else {
  Skipped 'gcodeviewer --frame: нет собранной точки входа'
}

# 8. The viewer hotkey probe (build/viewer/keyprobe.exe): feeds keys with the signs of a real
#    event (scan code, VK and the symbol by layout) in the Russian and English layouts and
#    checks that each hotkey switched its own property. The "hotkeys require the language"
#    defect is caught here: before the fix not a single letter works in the Russian layout. The
#    platform is offscreen, no live session is needed. Without the probe or Qt the step is skipped.
$keyProbeExe = 'build/viewer/keyprobe.exe'
if ($cppReady -and (Test-Path -LiteralPath $keyProbeExe) -and (Test-Path -LiteralPath (Join-Path $qtDir 'bin'))) {
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

# 9. The quick programming demo (apps/quickprog, PLAN-QUICKPROG): the self-check
#    build/quickprog/quickprog_check.exe built by scripts/build-quickprog.ps1. It needs
#    only the C++ compiler and the standard library, so it does not wait for the Qt/OCCT
#    toolchain. The check compares the generator version with projects.json (the
#    case prints skip when the manifest is not above the run directory) and runs the
#    rest of the cases; a non-zero code is red. No built check - the step is skipped.
$quickprogCheck = @('build/quickprog/quickprog_check.exe', 'build/quickprog/quickprog_check') |
  Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($quickprogCheck) {
  Run-Tool 'quickprog_check (сборка и проверки)' { & $quickprogCheck }
} else {
  Skipped 'quickprog_check: нет собранной проверки (scripts/build-quickprog.ps1)'
}

# 10. File structure (docs/FILE-STRUCTURE.md): generated files never enter git, no binaries in
#     the tree, the build root has only project directories and screenshots do not lie around.
$trackedGenerated = @(& git ls-files -- build .next 2>$null)
if ($trackedGenerated.Count -gt 0) {
  foreach ($path in $trackedGenerated) { Bad "в git попал сгенерированный файл: $path" }
} else {
  Ok 'сгенерированные файлы (build/, .next) не отслеживаются'
}
$trackedBinary = @(& git ls-files -- '*.exe' '*.dll' '*.pdb' '*.lib' '*.obj' 2>$null)
if ($trackedBinary.Count -gt 0) {
  foreach ($path in $trackedBinary) { Bad "в git попал бинарник: $path" }
} else {
  Ok 'бинарники (exe/dll/pdb/lib/obj) не отслеживаются'
}
$looseBuild = @(Get-ChildItem -LiteralPath 'build' -File -ErrorAction SilentlyContinue)
if ($looseBuild.Count -gt 0) {
  foreach ($file in $looseBuild) { Bad "файл в корне build/: $($file.Name) - перенесите в build/artifacts или удалите" }
} else {
  Ok 'в корне build/ только каталоги'
}
$rootPng = @(Get-ChildItem -LiteralPath '.' -File -Filter '*.png' -ErrorAction SilentlyContinue)
if ($rootPng.Count -gt 0) {
  foreach ($file in $rootPng) { Bad "скриншот в корне репозитория: $($file.Name) - ему место в img/ или build/artifacts/" }
} else {
  Ok 'в корне репозитория нет *.png'
}

Write-Host ''
if ($fail.Count -gt 0) {
  Write-Host "GUARD: FAIL ($($fail.Count))" -ForegroundColor Red
  exit 1
}
$tail = if ($skip.Count -gt 0) { " (пропущено: $($skip.Count))" } else { '' }
Write-Host "GUARD: OK$tail" -ForegroundColor Green
exit 0
