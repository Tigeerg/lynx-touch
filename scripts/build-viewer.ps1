# scripts/build-viewer.ps1 — сборка QML-плагина CNC.Edges (StepEdges) из modules/gcode-viewer/cpp.
#
# Запуск:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunCoreProbe
#
# Генератор — Ninja, конфигурация Release, компилятор MSVC. Пути SDK берутся из
# окружения с умолчаниями: CNC_QT_DIR (Qt6 с Quick3D), CNC_OCC_DIR (OCCT + Eigen),
# CNC_VCVARS (vcvars64.bat), CNC_CMAKE, CNC_NINJA. Скрипт не привязан к одной
# машине: всё, что не найдено, названо в ошибке, а не угадано.
#
# Окружение MSVC снимается одним вызовом vcvars64.bat (cmd /c "call ... && set"),
# дальше cmake и cl запускаются из PowerShell напрямую: так путь репозитория с
# кириллицей доезжает до инструментов без перекодировки (cmd.exe читает .cmd в
# кодировке консоли и портит не-ASCII пути).
#
# Собранный модуль лежит в <build>/CNC/Edges/ (qmldir, cncedgesplugin.dll,
# cncedgespluginplugin.dll); каталог <build> — это import path для QML и для
# engine.addImportPath(...). Пробник — в <build>/stepledges_probe.exe.
# Той же сборкой собирается чистое ядро gvcore (без Qt и OCCT) и его пробник
# <build>/gcode_probe.exe; сверку ядра с эталоном ведёт
# modules/gcode-viewer/test_gcode_core.py.
#
# Код возврата: 0 — собрано, плагин на месте; 1 — инструмент не найден или ошибка сборки.

[CmdletBinding()]
param(
    # Каталог Qt6 (внутри ожидается lib/cmake/Qt6/Qt6Config.cmake).
    [string]$QtDir = $env:CNC_QT_DIR,
    # Каталог OCCT/Eigen (внутри ожидается lib/cmake/opencascade/OpenCASCADEConfig.cmake).
    [string]$OccDir = $env:CNC_OCC_DIR,
    # Каталог сборки относительно корня репозитория.
    [string]$BuildDir = 'build/edges',
    # Дополнительно прогнать пробник сегментов на data/cad-testdata/Test_model.STEP.
    [switch]$RunProbe,
    # Дополнительно прогнать пробник ядра на data/cad-testdata/Test_model.ngc.
    [switch]$RunCoreProbe,
    # Только конфигурирование, без сборки.
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'modules\gcode-viewer\cpp'
$probeStep = Join-Path $root 'data\cad-testdata\Test_model.STEP'
$probeNgc = Join-Path $root 'data\cad-testdata\Test_model.ngc'
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}

function Fail([string]$text) {
    Write-Host "build-viewer: FAIL $text" -ForegroundColor Red
    exit 1
}

# Первый существующий путь из списка; пустая строка, если ничего не нашлось.
function First-Existing([string[]]$paths) {
    foreach ($p in $paths) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return ''
}

function Find-Tool([string]$override, [string[]]$paths, [string]$name) {
    $hit = First-Existing (@($override) + $paths)
    if ($hit) { return $hit }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return ''
}

if (-not $QtDir) {
    $QtDir = First-Existing @('C:\Qt\6.12.0\msvc2022_64', 'C:\Qt\6.10.0\msvc2022_64', 'C:\Qt\6.9.0\msvc2022_64')
}
if (-not $QtDir) { Fail 'Qt6 не найден: задайте CNC_QT_DIR=<каталог Qt6>/msvc2022_64' }
if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    Fail "не найдено $QtDir/lib/cmake/Qt6/Qt6Config.cmake — проверьте CNC_QT_DIR"
}

if (-not $OccDir) {
    $OccDir = First-Existing @(
        $(if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }),
        'C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library')
}
if (-not $OccDir) { Fail 'OCCT не найден: задайте CNC_OCC_DIR=<каталог OCCT>/Library' }
if (-not (Test-Path -LiteralPath (Join-Path $OccDir 'lib\cmake\opencascade\OpenCASCADEConfig.cmake'))) {
    Fail "не найдено $OccDir/lib/cmake/opencascade/OpenCASCADEConfig.cmake — проверьте CNC_OCC_DIR"
}

$vsRoot = ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = (& $vswhere -latest -products * -property installationPath 2>$null | Out-String).Trim()
}
$vcvars = Find-Tool $env:CNC_VCVARS @(
    $(if ($vsRoot) { Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat' }),
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
) 'vcvars64.bat'
if (-not $vcvars) { Fail 'vcvars64.bat не найден: задайте CNC_VCVARS=<полный путь>' }

$cmake = Find-Tool $env:CNC_CMAKE @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
) 'cmake.exe'
if (-not $cmake) { Fail 'cmake не найден: задайте CNC_CMAKE=<полный путь к cmake.exe>' }

$ninja = Find-Tool $env:CNC_NINJA @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
) 'ninja.exe'
if (-not $ninja) { Fail 'ninja не найден: задайте CNC_NINJA=<полный путь к ninja.exe>' }

$prefix = "$QtDir;$OccDir"

Write-Host "build-viewer: Qt  = $QtDir" -ForegroundColor Cyan
Write-Host "build-viewer: OCCT= $OccDir" -ForegroundColor Cyan
Write-Host "build-viewer: cmake=$cmake"
Write-Host "build-viewer: ninja=$ninja"
Write-Host "build-viewer: build=$BuildDir"

# vcvars64.bat — batch-файл: его окружение снимается один раз в переменные процесса,
# дальше cmake и cl запускаются прямо из PowerShell. Так путь репозитория с кириллицей
# доезжает до инструментов без перекодировки (cmd.exe читает .cmd в кодировке консоли).
$envText = & cmd.exe /c ('call "' + $vcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { Fail "vcvars64.bat не отработал: $vcvars" }
$applied = 0
foreach ($line in $envText) {
    $k = $line.IndexOf('=')
    if ($k -lt 1) { continue }
    [Environment]::SetEnvironmentVariable($line.Substring(0, $k), $line.Substring($k + 1))
    $applied++
}
if ($applied -lt 10) { Fail 'окружение MSVC не получено: проверьте CNC_VCVARS' }

$env:VSLANG = '1033'                       # сообщения компилятора латиницей: читаемы в любой консоли

& $cmake -S $srcDir -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_PREFIX_PATH="$prefix" `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE -ne 0) { Fail "конфигурирование не прошло (код $LASTEXITCODE)" }

if (-not $ConfigureOnly) {
    & $cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { Fail "сборка не прошла (код $LASTEXITCODE)" }
}

$moduleDir = Join-Path $BuildDir 'CNC\Edges'
$qmldir = Join-Path $moduleDir 'qmldir'
$plugin = Join-Path $moduleDir 'cncedgesplugin.dll'
if ($ConfigureOnly) {
    Write-Host "build-viewer: OK (только конфигурирование)" -ForegroundColor Green
    exit 0
}
if (-not (Test-Path -LiteralPath $qmldir)) { Fail "модуль не создан: нет $qmldir" }
if (-not (Test-Path -LiteralPath $plugin)) { Fail "модуль не создан: нет $plugin" }

Write-Host ''
Write-Host '--- CNC/Edges/qmldir ---'
Get-Content -LiteralPath $qmldir
Write-Host ("build-viewer: модуль  " + $moduleDir)
Write-Host ("build-viewer: плагин  " + $plugin)
Write-Host ("build-viewer: import path для QML: " + $BuildDir)

$probe = Join-Path $BuildDir 'stepledges_probe.exe'
if (Test-Path -LiteralPath $probe) {
    Write-Host ("build-viewer: пробник  " + $probe)
}

$coreProbe = Join-Path $BuildDir 'gcode_probe.exe'
if (Test-Path -LiteralPath $coreProbe) {
    Write-Host ("build-viewer: пробник ядра " + $coreProbe)
} else {
    Fail "ядро не собрано: нет $coreProbe"
}

if ($RunProbe) {
    if (-not (Test-Path -LiteralPath $probe)) { Fail "пробника нет: $probe" }
    if (-not (Test-Path -LiteralPath $probeStep)) { Fail "нет тестовой модели: $probeStep" }
    $env:PATH = "$moduleDir;$OccDir\bin;$QtDir\bin;$env:PATH"   # DLL OCCT, Qt и плагина
    & $probe $probeStep
    if ($LASTEXITCODE -ne 0) { Fail "пробник вернул $LASTEXITCODE" }
}

if ($RunCoreProbe) {
    if (-not (Test-Path -LiteralPath $probeNgc)) { Fail "нет тестовой УП: $probeNgc" }
    # Пробнику ядра DLL не нужны: gvcore статическая, Qt и OCCT в ней не участвуют.
    $dumpDir = Join-Path $env:TEMP 'gvcore_build_probe'
    New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
    & $coreProbe $probeNgc $dumpDir
    if ($LASTEXITCODE -ne 0) { Fail "пробник ядра вернул $LASTEXITCODE" }
}

Write-Host 'build-viewer: OK' -ForegroundColor Green
exit 0
