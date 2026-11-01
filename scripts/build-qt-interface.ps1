# scripts/build-qt-interface.ps1 - build of the standalone Qt panel (apps/qt-interface).
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-qt-interface.ps1
#
# The panel needs Qt 6 (Core, Gui, Qml, Quick, Quick3D) and MSVC; neither OCCT nor the viewer build is
# required to compile it, but the built viewer modules (build/qmlbackend, build/edges) are needed at
# run time and the launcher adds their import paths. SDK paths come from CNC_QT_DIR, CNC_VCVARS,
# CNC_CMAKE and CNC_NINJA, with the same defaults as scripts/build-viewer.ps1.
#
# Built: build/qt-interface/lynx-panel.exe - a normal window, no console.
# Exit code: 0 - built; 1 - a tool is missing or the build failed.

[CmdletBinding()]
param(
    [string]$QtDir = $env:CNC_QT_DIR,
    [string]$OccDir = $env:CNC_OCC_DIR,
    [string]$BuildDir = 'build/qt-interface'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'apps\qt-interface'
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}

function Fail([string]$text) {
    Write-Host "build-qt-interface: FAIL $text" -ForegroundColor Red
    exit 1
}

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
    Fail "нет $QtDir/lib/cmake/Qt6/Qt6Config.cmake - проверьте CNC_QT_DIR"
}

$vsRoot = ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = (& $vswhere -latest -products * -property installationPath 2>$null | Out-String).Trim()
}
$vcvars = Find-Tool $env:CNC_VCVARS @(
    $(if ($vsRoot) { Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat' }),
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
) 'vcvars64.bat'
if (-not $vcvars) { Fail 'vcvars64.bat не найден: задайте CNC_VCVARS' }

$cmake = Find-Tool $env:CNC_CMAKE @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
) 'cmake.exe'
$ninja = Find-Tool $env:CNC_NINJA @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
) 'ninja.exe'
if (-not $cmake) { Fail 'cmake не найден: задайте CNC_CMAKE' }
if (-not $ninja) { Fail 'ninja не найден: задайте CNC_NINJA' }

Write-Host "build-qt-interface: Qt    = $QtDir"
Write-Host "build-qt-interface: cmake = $cmake"
Write-Host "build-qt-interface: build = $BuildDir"

$envText = & cmd.exe /c ('call "' + $vcvars + '" >nul && set')
if ($LASTEXITCODE -ne 0) { Fail "vcvars64.bat не отработал: $vcvars" }
foreach ($line in $envText) {
    $k = $line.IndexOf('=')
    if ($k -lt 1) { continue }
    [Environment]::SetEnvironmentVariable($line.Substring(0, $k), $line.Substring($k + 1))
}
$env:VSLANG = '1033'

& $cmake -S $srcDir -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_PREFIX_PATH="$QtDir" `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE -ne 0) { Fail "конфигурирование не прошло (код $LASTEXITCODE)" }

& $cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { Fail "сборка не прошла (код $LASTEXITCODE)" }

$exe = Join-Path $BuildDir 'lynx-panel.exe'
if (-not (Test-Path -LiteralPath $exe)) { Fail "панель не собрана: нет $exe" }

# A double click from Explorer has no Qt in PATH: deploy the Qt DLLs, the platform plugins and the
# QML modules next to the exe. The viewer plugins (MockBackend, CNC.Edges) stay in the repository
# and main.cpp adds their import paths; their Qt dependencies are found in the exe directory.
$deploy = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deploy)) { Fail "нет $deploy - проверьте CNC_QT_DIR" }
& $deploy --release --no-translations --compiler-runtime --qmldir $srcDir $exe
if ($LASTEXITCODE -ne 0) { Fail "windeployqt не прошёл (код $LASTEXITCODE)" }
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'Qt6Core.dll'))) {
    Fail "Qt6Core.dll не скопирован рядом с exe"
}

# The viewer controller links OCCT (BRep, STEP, mesh); a double click has no CNC_OCC_DIR. The
# 147-DLL novtk set is enough for MockBackend and keeps the debug package small.
if (-not $OccDir) {
    $OccDir = First-Existing @(
        'D:\cnc-toolchain\occt-novtk\Library',
        'D:\cnc-toolchain\occt-env\Library',
        $(if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }),
        'C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library')
}
if ($OccDir -and (Test-Path -LiteralPath (Join-Path $OccDir 'bin'))) {
    $occtOut = Join-Path $BuildDir 'occt'
    # Clear the folder before the copy: an older deployment may have put a full occt-env set here,
    # and Copy-Item -Force would leave its extra DLLs behind (over a gigabyte).
    if (Test-Path -LiteralPath $occtOut) { Remove-Item -LiteralPath $occtOut -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $occtOut | Out-Null
    Copy-Item -Path (Join-Path $OccDir 'bin\*.dll') -Destination $occtOut -Force
    Write-Host ("build-qt-interface: OCCT " + $OccDir + " -> " + $occtOut)
} else {
    Write-Host 'build-qt-interface: OCCT не найден - exe будет искать CNC_OCC_DIR в PATH' -ForegroundColor Yellow
}

Write-Host ("build-qt-interface: OK " + $exe) -ForegroundColor Green
