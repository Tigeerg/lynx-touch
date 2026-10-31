# scripts/build-quickprog.ps1 - build of the quick programming demo on Windows
# (apps/quickprog, PLAN-QUICKPROG, 7.2).
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-quickprog.ps1
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-quickprog.ps1 -RunCheck
#
# The demo needs neither Qt nor OCCT: C++17 and the standard library are enough,
# so this script does not look for SDK directories. It needs only MSVC, which the
# less-common tutorial build (PLAN-QUICKPROG, 7.2) implies: the environment is
# captured with a single call to vcvars64.bat, after that cmake and cl are
# launched directly from PowerShell, so a repository path with Cyrillic reaches
# the tools without re-encoding (cmd.exe reads .cmd in the console encoding and
# corrupts non-ASCII paths).
#
# Tools: vcvars64.bat is found by vswhere, then among the known Visual Studio
# paths; CNC_VCVARS, CNC_CMAKE, CNC_NINJA override the search. vswhere is not
# mandatory: without it the known paths are tried. The build directory is
# -BuildDir (relative to the repository root), by default build/quickprog.
#
# The round-trip cases of quickprog_check link the viewer core parser (see
# apps/quickprog/CMakeLists.txt): the sources are read from
# modules/gcode-viewer/cpp/core, no SDK, no Qt, no OCCT.
#
# Built: <build>/quickprog.exe (the console demo) and <build>/quickprog_check.exe
# (the self-check, exit code 0 - all cases passed).
#
# Exit code: 0 - built, the artifacts are in place; 1 - tool not found or build
# error.

[CmdletBinding()]
param(
    # Build directory relative to the repository root.
    [string]$BuildDir = 'build/quickprog',
    # Additionally run the self-check after the build.
    [switch]$RunCheck,
    # Configure only, without building.
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'apps\quickprog'
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}

function Fail([string]$text) {
    Write-Host "build-quickprog: FAIL $text" -ForegroundColor Red
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

# vcvars64.bat: vswhere first (not mandatory), then the known Visual Studio paths.
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

Write-Host "build-quickprog: cmake=$cmake"
Write-Host "build-quickprog: ninja=$ninja"
Write-Host "build-quickprog: build=$BuildDir"

# vcvars64.bat is a batch file: its environment is captured once into process
# variables, after that cmake and cl are launched directly from PowerShell.
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
    -DCMAKE_MAKE_PROGRAM="$ninja" `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE -ne 0) { Fail "конфигурирование не прошло (код $LASTEXITCODE)" }

if (-not $ConfigureOnly) {
    & $cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { Fail "сборка не прошла (код $LASTEXITCODE)" }
}

$app = Join-Path $BuildDir 'quickprog.exe'
$check = Join-Path $BuildDir 'quickprog_check.exe'

if ($ConfigureOnly) {
    Write-Host 'build-quickprog: OK (только конфигурирование)' -ForegroundColor Green
    exit 0
}

if (-not (Test-Path -LiteralPath $app)) { Fail "демо не собрано: нет $app" }
if (-not (Test-Path -LiteralPath $check)) { Fail "проверка не собрана: нет $check" }

Write-Host ("build-quickprog: демо     " + $app)
Write-Host ("build-quickprog: проверка " + $check)

if ($RunCheck) {
    & $check
    if ($LASTEXITCODE -ne 0) { Fail "проверка вернула $LASTEXITCODE" }
}

Write-Host 'build-quickprog: OK' -ForegroundColor Green
exit 0
