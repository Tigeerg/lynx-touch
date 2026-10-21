# scripts/build-viewer.ps1 - build of the QML plugin CNC.Edges (StepEdges) from modules/gcode-viewer/cpp.
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunCoreProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunStepProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunStlProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunSilhouetteProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunTangentsProbe
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1 -RunViewer
#
# Generator - Ninja, Release configuration, MSVC compiler. SDK paths are taken from
# the environment with defaults: CNC_QT_DIR (Qt6 with Quick3D), CNC_OCC_DIR (OCCT),
# CNC_VCVARS (vcvars64.bat), CNC_CMAKE, CNC_NINJA. The script is not tied to a single
# machine: everything that is not found is named in the error, not guessed.
#
# The MSVC environment is captured with a single call to vcvars64.bat (cmd /c "call ... && set"),
# after that cmake and cl are launched from PowerShell directly: this way the repository path with
# Cyrillic reaches the tools without re-encoding (cmd.exe reads .cmd in the
# console encoding and corrupts non-ASCII paths).
#
# The built module is in <build>/CNC/Edges/ (qmldir, cncedgesplugin.dll,
# cncedgespluginplugin.dll); the <build> directory is the import path for QML. The probe is
# in <build>/stepledges_probe.exe.
# The same build also builds the pure core gvcore (without Qt and OCCT) and its probe
# <build>/gcode_probe.exe: it compares its digests against the reference
# modules/gcode-viewer/oracle_manifest.json with the --manifest key.
#
# The same build also builds the Qt Quick 3D geometries (target gvqtgeom) and their probe
# <build>/geometry_probe.exe; it builds its own input from the NC program and compares the digests
# of the buffers with the manifest, so the script has no separate run key - it only
# checks that the probe is built.
#
# The same build also builds the STEP layer and crease-aware normals (target gvstep, probe
# <build>/step_probe.exe, stage 4a of the port plan); the comparison with the manifest is done by the same
# --manifest key. This target does not need Qt: only OCCT.
#
# STL import and mesh outline (stage 4b of the port plan) live in the gvcore core, their
# probe is <build>/stl_probe.exe; the comparison with the manifest is done by the same --manifest key.
# This target needs neither Qt nor OCCT.
#
# The silhouette topology (stage 4c of the port plan) lives in the gvcore core, its probe is
# <build>/silhouette_probe.exe: it takes the set of vertices from STEP through the gvstep target,
# so it needs the OCCT DLLs (Qt - no). The comparison with the manifest is done by the --manifest key.
#
# The analytic surfaces of revolution of STEP (stage 4d of the port plan) live in
# gvstep (occt/step_surfaces), the tangent families - in gvcore
# (core/tangents), their common probe is <build>/tangents_probe.exe: it also takes
# the set and the surfaces from STEP through gvstep, builds the packed STEP edges itself from
# the same shape by the same path as the runtime (occt/step_edges + core/packing), and
# prints alongside the digest of its own edge buffer, so it needs the
# OCCT DLLs (Qt - no). Its --manifest mode compares the 22 tangents.* records from its
# edge buffer; the buffer does not match the manifest digest byte for byte (the set of
# segments is the same, the order and the low bits are not), so the caveat and the numbers are in
# cpp/tangents_probe.cpp and cpp/CMakeLists.txt.
#
# The same build also builds the C++ viewer controller (stage 5.4a of the port plan):
# the QML module MockBackend 1.0 (target gvqmlbackend) in <repo>/build/qmlbackend/
# MockBackend - the GcodeToolpath type and the six geometries of stage 3. This is the only
# viewer backend.
#
# The same build also builds the C++ viewer entry point:
# <repo>/build/viewer/gcodeviewer.exe, the source is cpp/viewer/main.cpp. It loads
# GcodeViewer3D.qml into QQuickView and works only with the MockBackend plugin:
# there is no Python backend either in the runtime or in the repository. Launch - with Qt in PATH
# (the exe links against Qt6Core.dll); the plugin directory and the OCCT DLLs the entry point adds
# to itself:
#   build\viewer\gcodeviewer.exe data\cad-testdata\Test_model.ngc
#   build\viewer\gcodeviewer.exe --smoke
#   build\viewer\gcodeviewer.exe --frame frame.png data\cad-testdata\Test_model.ngc
#
# Exit code: 0 - built, the plugin is in place; 1 - tool not found or build error.

[CmdletBinding()]
param(
    # Qt6 directory (lib/cmake/Qt6/Qt6Config.cmake is expected inside).
    [string]$QtDir = $env:CNC_QT_DIR,
    # OCCT directory (lib/cmake/opencascade/OpenCASCADEConfig.cmake is expected inside).
    [string]$OccDir = $env:CNC_OCC_DIR,
    # Build directory relative to the repository root.
    [string]$BuildDir = 'build/edges',
    # Additionally run the segments probe on data/cad-testdata/Test_model.STEP.
    [switch]$RunProbe,
    # Additionally run the core probe on data/cad-testdata/Test_model.ngc.
    [switch]$RunCoreProbe,
    # Additionally run the STEP and normals probe on data/cad-testdata/Test_model.STEP.
    [switch]$RunStepProbe,
    # Additionally run the STL and mesh outline probe on data/cad-testdata/Test_model.STL.
    [switch]$RunStlProbe,
    # Additionally run the silhouette topology probe on data/cad-testdata/Test_model.STEP.
    [switch]$RunSilhouetteProbe,
    # Additionally run the surfaces and tangents probe on data/cad-testdata/Test_model.STEP.
    [switch]$RunTangentsProbe,
    # Additionally run the C++ viewer entry point offscreen on data/cad-testdata/Test_model.ngc.
    [switch]$RunViewer,
    # Additionally run the viewer hotkey probe on the Russian and English layouts.
    [switch]$RunKeyProbe,
    # Configure only, without building.
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'modules\gcode-viewer\cpp'
$probeStep = Join-Path $root 'data\cad-testdata\Test_model.STEP'
$probeStl = Join-Path $root 'data\cad-testdata\Test_model.STL'
$probeNgc = Join-Path $root 'data\cad-testdata\Test_model.ngc'
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}

function Fail([string]$text) {
    Write-Host "build-viewer: FAIL $text" -ForegroundColor Red
    exit 1
}

# The first existing path from the list; an empty string if nothing was found.
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

# vcvars64.bat - a batch file: its environment is captured once into process variables,
# after that cmake and cl are launched directly from PowerShell. This way the repository path with Cyrillic
# reaches the tools without re-encoding (cmd.exe reads .cmd in the console encoding).
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

$env:VSLANG = '1033'                       # compiler messages in Latin script: readable in any console

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

$geomProbe = Join-Path $BuildDir 'geometry_probe.exe'
if (Test-Path -LiteralPath $geomProbe) {
    Write-Host ("build-viewer: пробник геометрий " + $geomProbe)
} else {
    Fail "геометрии не собраны: нет $geomProbe"
}

# Version probe: compares VERSION, projects.json and the viewerVersion property in QML.
# It needs neither Qt nor OCCT.
$versionProbe = Join-Path $BuildDir 'version_probe.exe'
if (Test-Path -LiteralPath $versionProbe) {
    Write-Host ("build-viewer: пробник версии " + $versionProbe)
} else {
    Fail "пробник версии не собран: нет $versionProbe"
}

$stepProbe = Join-Path $BuildDir 'step_probe.exe'
if (Test-Path -LiteralPath $stepProbe) {
    Write-Host ("build-viewer: пробник STEP и нормалей " + $stepProbe)
} else {
    Fail "слой STEP и нормалей не собран: нет $stepProbe"
}

$stlProbe = Join-Path $BuildDir 'stl_probe.exe'
if (Test-Path -LiteralPath $stlProbe) {
    Write-Host ("build-viewer: пробник STL и обводки " + $stlProbe)
} else {
    Fail "слой STL и обводки не собран: нет $stlProbe"
}

$silProbe = Join-Path $BuildDir 'silhouette_probe.exe'
if (Test-Path -LiteralPath $silProbe) {
    Write-Host ("build-viewer: пробник топологии силуэта " + $silProbe)
} else {
    Fail "топология силуэта не собрана: нет $silProbe"
}

$tanProbe = Join-Path $BuildDir 'tangents_probe.exe'
if (Test-Path -LiteralPath $tanProbe) {
    Write-Host ("build-viewer: пробник поверхностей и касательных " + $tanProbe)
} else {
    Fail "поверхности и касательные не собраны: нет $tanProbe"
}

# QML module of the C++ controller (stage 5.4a): the module directory is <repo>/build/qmlbackend/
# MockBackend, the import path is <repo>/build/qmlbackend. The path is predictable for the
# viewer entry point, that is why the directory does not match the build directory.
$backendImport = Join-Path $root 'build\qmlbackend'
$backendModule = Join-Path $backendImport 'MockBackend'
$backendQmldir = Join-Path $backendModule 'qmldir'
if (Test-Path -LiteralPath $backendQmldir) {
    Write-Host ("build-viewer: модуль MockBackend " + $backendModule)
    Write-Host ("build-viewer: import path контроллера: " + $backendImport)
} else {
    Fail "модуль MockBackend не собран: нет $backendQmldir"
}

# C++ viewer entry point (dropping Python in the runtime): <repo>/build/viewer/
# gcodeviewer.exe. It is what launches the viewer without Python; there is no Python
# backend in the runtime, so a missing exe is a build error, not a trifle.
$viewerExe = Join-Path $root 'build\viewer\gcodeviewer.exe'
if (Test-Path -LiteralPath $viewerExe) {
    Write-Host ("build-viewer: точка входа вьювера " + $viewerExe)
} else {
    Fail "точка входа вьювера не собрана: нет $viewerExe"
}

# Hotkey probe (viewer/keyprobe.cpp): loads the same GcodeViewer3D.qml and
# checks that in the Russian and English layouts every letter hotkey
# toggles its own property. It is built together with the entry point, the check is run by the guard
# (tools/check.ps1) - here only the presence of the probe and the run key are checked.
$keyProbeExe = Join-Path $root 'build\viewer\keyprobe.exe'
if (Test-Path -LiteralPath $keyProbeExe) {
    Write-Host ("build-viewer: пробник клавиш " + $keyProbeExe)
} else {
    Fail "пробник клавиш не собран: нет $keyProbeExe"
}

if ($RunProbe) {
    if (-not (Test-Path -LiteralPath $probe)) { Fail "пробника нет: $probe" }
    if (-not (Test-Path -LiteralPath $probeStep)) { Fail "нет тестовой модели: $probeStep" }
    $env:PATH = "$moduleDir;$OccDir\bin;$QtDir\bin;$env:PATH"   # OCCT, Qt and plugin DLLs
    & $probe $probeStep
    if ($LASTEXITCODE -ne 0) { Fail "пробник вернул $LASTEXITCODE" }
}

if ($RunCoreProbe) {
    if (-not (Test-Path -LiteralPath $probeNgc)) { Fail "нет тестовой УП: $probeNgc" }
    # The core probe needs no DLLs: gvcore is static, Qt and OCCT do not take part in it.
    $dumpDir = Join-Path $env:TEMP 'gvcore_build_probe'
    New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
    & $coreProbe $probeNgc $dumpDir
    if ($LASTEXITCODE -ne 0) { Fail "пробник ядра вернул $LASTEXITCODE" }
}

if ($RunStepProbe) {
    if (-not (Test-Path -LiteralPath $stepProbe)) { Fail "пробника нет: $stepProbe" }
    if (-not (Test-Path -LiteralPath $probeStep)) { Fail "нет тестовой модели: $probeStep" }
    # The probe needs only the OCCT DLLs: there is no Qt in the gvstep target.
    $env:PATH = "$OccDir\bin;$env:PATH"
    $dumpDir = Join-Path $env:TEMP 'gvstep_build_probe'
    New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
    & $stepProbe $probeStep $dumpDir
    if ($LASTEXITCODE -ne 0) { Fail "пробник STEP вернул $LASTEXITCODE" }
}

if ($RunStlProbe) {
    if (-not (Test-Path -LiteralPath $stlProbe)) { Fail "пробника нет: $stlProbe" }
    if (-not (Test-Path -LiteralPath $probeStl)) { Fail "нет тестовой модели: $probeStl" }
    # The STL probe needs no DLLs: it is entirely in gvcore, Qt and OCCT do not take part in it.
    $dumpDir = Join-Path $env:TEMP 'gvstl_build_probe'
    New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
    & $stlProbe $probeStl $dumpDir
    if ($LASTEXITCODE -ne 0) { Fail "пробник STL вернул $LASTEXITCODE" }
}

if ($RunSilhouetteProbe) {
    if (-not (Test-Path -LiteralPath $silProbe)) { Fail "пробника нет: $silProbe" }
    if (-not (Test-Path -LiteralPath $probeStep)) { Fail "нет тестовой модели: $probeStep" }
    # The probe needs only the OCCT DLLs: it takes the set from STEP through the gvstep target.
    $env:PATH = "$OccDir\bin;$env:PATH"
    $dumpDir = Join-Path $env:TEMP 'gvsilhouette_build_probe'
    New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
    & $silProbe $probeStep $dumpDir
    if ($LASTEXITCODE -ne 0) { Fail "пробник топологии силуэта вернул $LASTEXITCODE" }
}

if ($RunTangentsProbe) {
    if (-not (Test-Path -LiteralPath $tanProbe)) { Fail "пробника нет: $tanProbe" }
    if (-not (Test-Path -LiteralPath $probeStep)) { Fail "нет тестовой модели: $probeStep" }
    # The probe needs only the OCCT DLLs: it takes the set and the surfaces from STEP through
    # the gvstep target. There are no packed edges in this directory - the probe will run without
    # DROP marks, like the reference with packed_edges=None; the full comparison with the manifest
    # is done by the same probe with the --manifest key.
    $env:PATH = "$OccDir\bin;$env:PATH"
    $dumpDir = Join-Path $env:TEMP 'gvtangents_build_probe'
    New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
    & $tanProbe $probeStep $dumpDir
    if ($LASTEXITCODE -ne 0) { Fail "пробник поверхностей и касательных вернул $LASTEXITCODE" }
}

if ($RunViewer) {
    if (-not (Test-Path -LiteralPath $viewerExe)) { Fail "нет точки входа вьювера: $viewerExe" }
    if (-not (Test-Path -LiteralPath $probeNgc)) { Fail "нет тестовой УП: $probeNgc" }
    # Qt in PATH is required: the exe links against Qt6Core.dll. The plugin directory and the
    # OCCT DLLs the entry point adds to itself.
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATH = "$QtDir\bin;$env:PATH"
    & $viewerExe $probeNgc
    if ($LASTEXITCODE -ne 0) { Fail "точка входа вьювера вернула $LASTEXITCODE" }
}

if ($RunKeyProbe) {
    if (-not (Test-Path -LiteralPath $keyProbeExe)) { Fail "нет пробника клавиш: $keyProbeExe" }
    if (-not (Test-Path -LiteralPath $probeNgc)) { Fail "нет тестовой УП: $probeNgc" }
    # The probe needs Qt in PATH (the exe links against Qt6Core.dll); the plugin directory and the
    # OCCT DLLs it adds to itself, like the viewer entry point. The offscreen platform:
    # the probe does not need a live session or a foreground window.
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATH = "$QtDir\bin;$env:PATH"
    & $keyProbeExe $probeNgc
    if ($LASTEXITCODE -ne 0) { Fail "пробник клавиш вернул $LASTEXITCODE" }
}

Write-Host 'build-viewer: OK' -ForegroundColor Green
exit 0
