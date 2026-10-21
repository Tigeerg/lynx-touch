# scripts/collect-occt.ps1 - the minimal set of OCCT DLLs for distribution.
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/collect-occt.ps1 `
#              -OutDir D:\cnc-toolchain\dist-occt
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/collect-occt.ps1 `
#              -OutDir D:\cnc-toolchain\dist-occt -IncludeQt
#
# The set is not written by hand: the script walks the real dependency graph of the built
# artifacts (dumpbin /dependents, recursively) and takes only the DLLs that
# are actually loaded. The source of the OCCT DLLs is the -OccDir directory (by default
# $env:CNC_OCC_DIR), the build directory is -BuildDir (by default <repo>\build),
# the result is in -OutDir: it is mandatory, the script does not write to the repository.
# Qt DLLs ($env:CNC_QT_DIR\bin) are counted separately and are not copied by default:
# they are added by the -IncludeQt key.
#
# dumpbin is taken from the MSVC environment, which is captured with a single vcvars64.bat call,
# as in scripts/build-viewer.ps1 (the path is searched for in the same place: CNC_VCVARS, vswhere,
# VS directories). A DLL is first searched for in the OCCT directory, then in Qt, then in the
# build directories and System32; a dependency not found anywhere is printed as a finding, not
# silently hidden. The script is idempotent: it does not touch files outside -OutDir.
#
# Exit code: 0 - the set is assembled; 1 - a tool or directory was not found.

[CmdletBinding()]
param(
    # OCCT directory (a bin with DLLs and lib/cmake/opencascade/OpenCASCADEConfig.cmake are expected inside).
    [string]$OccDir = $env:CNC_OCC_DIR,
    # Build directory relative to the repository root.
    [string]$BuildDir = '',
    # Result directory; mandatory and it must not be inside the repository.
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    # Qt6 directory (a bin with Qt DLLs is expected inside).
    [string]$QtDir = $env:CNC_QT_DIR,
    # Also copy into the set the DLLs resolved from the Qt directory.
    [switch]$IncludeQt
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build' }
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path (Get-Location).Path $OutDir }

function Fail([string]$text) {
    Write-Host "collect-occt: FAIL $text" -ForegroundColor Red
    exit 1
}

function First-Existing([string[]]$paths) {
    foreach ($p in $paths) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return ''
}

# The first file found by name: the order of the directories sets the DLL source.
function Resolve-Dependency([string]$name, [string[]]$dirs) {
    foreach ($dir in $dirs) {
        $candidate = Join-Path $dir $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    return ''
}

# Names of the DLLs from the "dependencies" section of the dumpbin /dependents output.
function Get-Dependents([string]$file) {
    $names = New-Object System.Collections.Generic.List[string]
    $lines = & $script:dumpbin /nologo /dependents $file 2>$null
    if ($null -eq $lines) { return $names }
    $inDep = $false
    foreach ($line in $lines) {
        if ($line -match 'dependencies:\s*$') { $inDep = $true; continue }
        if (-not $inDep) { continue }
        # After the header dumpbin prints an empty line, then the DLL names;
        # the list ends at the first non-empty line that is not a DLL name ("Summary").
        if ($line -match '^\s*(\S+\.dll)\s*$') {
            $names.Add($matches[1])
        } elseif ($line.Trim() -ne '') {
            $inDep = $false
        }
    }
    return $names
}

if (-not $OccDir) { Fail 'OCCT не найден: задайте CNC_OCC_DIR=<каталог OCCT>/Library или -OccDir' }
if (-not (Test-Path -LiteralPath (Join-Path $OccDir 'lib\cmake\opencascade\OpenCASCADEConfig.cmake'))) {
    Fail "не найдено $OccDir/lib/cmake/opencascade/OpenCASCADEConfig.cmake — проверьте CNC_OCC_DIR"
}
$occBin = Join-Path $OccDir 'bin'
if (-not (Test-Path -LiteralPath $occBin)) { Fail "нет каталога DLL OCCT: $occBin" }

$outFull = [System.IO.Path]::GetFullPath($OutDir)
$rootFull = [System.IO.Path]::GetFullPath($root)
if ($outFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    Fail "каталог результата внутри репозитория: $outFull — в репозиторий писать нельзя"
}
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

# vcvars64.bat - a batch file: its environment is captured once into process
# variables, after that dumpbin is launched directly from PowerShell (the repository path with
# Cyrillic reaches it without re-encoding).
$vsRoot = ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = (& $vswhere -latest -products * -property installationPath 2>$null | Out-String).Trim()
}
$vcvars = First-Existing @(
    $env:CNC_VCVARS,
    $(if ($vsRoot) { Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat' }),
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
)
if (-not $vcvars) { Fail 'vcvars64.bat не найден: задайте CNC_VCVARS=<полный путь>' }

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

$script:dumpbin = (Get-Command 'dumpbin.exe' -ErrorAction SilentlyContinue).Source
if (-not $script:dumpbin) { Fail 'dumpbin не найден: проверьте CNC_VCVARS' }

# Search directories: first OCCT, then Qt, then the build directories and System32. The DLL source
# is needed to separate the OCCT DLLs (copied) from Qt (counted separately) and
# system ones (not copied).
$qtBin = ''
if ($QtDir) { $qtBin = Join-Path $QtDir 'bin' }
$searchDirs = @($occBin)
$origins = @{ ([System.IO.Path]::GetFullPath($occBin)) = 'OCCT' }
if ($qtBin -and (Test-Path -LiteralPath $qtBin)) {
    $searchDirs += $qtBin
    $origins[[System.IO.Path]::GetFullPath($qtBin)] = 'Qt'
}
$buildDirs = @(
    (Join-Path $BuildDir 'viewer'),
    (Join-Path $BuildDir 'cpp'),
    (Join-Path $BuildDir 'cpp\exp'),
    (Join-Path $BuildDir 'edges'),
    (Join-Path $BuildDir 'edges\CNC\Edges'),
    (Join-Path $BuildDir 'qmlbackend'),
    (Join-Path $BuildDir 'qmlbackend\MockBackend')
)
foreach ($dir in $buildDirs) {
    if (Test-Path -LiteralPath $dir) {
        $searchDirs += $dir
        $origins[[System.IO.Path]::GetFullPath($dir)] = 'Build'
    }
}
$sys32 = Join-Path $env:WINDIR 'System32'
$searchDirs += $sys32
$origins[[System.IO.Path]::GetFullPath($sys32)] = 'System'

$artifacts = @(
    (Join-Path $BuildDir 'viewer\gcodeviewer.exe'),
    (Join-Path $BuildDir 'edges\CNC\Edges\cncedgesplugin.dll'),
    (Join-Path $BuildDir 'qmlbackend\MockBackend\gvqmlbackend.dll')
)

Write-Host "collect-occt: OCCT   = $OccDir" -ForegroundColor Cyan
Write-Host "collect-occt: Qt     = $QtDir"
Write-Host "collect-occt: build  = $BuildDir"
Write-Host "collect-occt: вывод  = $OutDir"
Write-Host "collect-occt: dumpbin= $script:dumpbin"

# Graph walk: the artifacts are counted first, then their dependencies. System directories are not
# walked: their DLLs are not copied anyway, and the tree is large.
$resolved = @{}                                   # name (lowercase) -> path
$dependencyOf = @{}                               # name -> source directory
$unresolved = @{}                                 # names not found anywhere
$scanned = @{}
$queue = New-Object System.Collections.Generic.Queue[string]

foreach ($artifact in $artifacts) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        Write-Host ("collect-occt: пропуск (нет файла): " + $artifact) -ForegroundColor DarkYellow
        continue
    }
    $queue.Enqueue($artifact)
}

while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    $fileKey = [System.IO.Path]::GetFullPath($file)
    if ($scanned.ContainsKey($fileKey)) { continue }
    $scanned[$fileKey] = $true

    foreach ($name in (Get-Dependents $file)) {
        $key = $name.ToLowerInvariant()
        if (-not $resolved.ContainsKey($key)) {
            $hit = Resolve-Dependency $name $searchDirs
            if (-not $hit) {
                $unresolved[$key] = $name
                continue
            }
            $dir = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($hit))
            $origin = 'Other'
            if ($origins.ContainsKey($dir)) { $origin = $origins[$dir] }
            $resolved[$key] = $hit
            $dependencyOf[$key] = $origin
            if ($origin -ne 'System') { $queue.Enqueue($hit) }
        }
    }
}

# The conda OCCT package keeps copies of auxiliary DLLs in its bin as well: the Windows API sets
# (api-ms-win-*) and the MSVC runtime (msvcp140*, vcruntime140*, vcomp140*, concrt140*).
# At distribution time they are provided by the system or by the MSVC redistributable, not by OCCT, so
# they do not go into the set: without them the closure equals the real one, and with them it grows by 18
# files. The test is by name, not by a list, so that the set stays reproducible.
function Test-SystemDependency([string]$name) {
    return $name -match '^(api-ms-win-|ucrtbase|msvcp|vcruntime|vcomp|concrt)'
}

$occAll = @($resolved.Keys | Where-Object { $dependencyOf[$_] -eq 'OCCT' } | ForEach-Object { $resolved[$_] } | Sort-Object)
$occDlls = @($occAll | Where-Object { -not (Test-SystemDependency ([System.IO.Path]::GetFileName($_))) })
$occRuntime = @($occAll | Where-Object { Test-SystemDependency ([System.IO.Path]::GetFileName($_)) })
$qtDlls = @($resolved.Keys | Where-Object { $dependencyOf[$_] -eq 'Qt' } | ForEach-Object { $resolved[$_] } | Sort-Object)

foreach ($src in $occDlls) {
    Copy-Item -LiteralPath $src -Destination (Join-Path $OutDir ([System.IO.Path]::GetFileName($src))) -Force
}
if ($IncludeQt) {
    foreach ($src in $qtDlls) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $OutDir ([System.IO.Path]::GetFileName($src))) -Force
    }
}

$copied = @($occDlls)
if ($IncludeQt) { $copied += $qtDlls }
$total = 0
foreach ($src in $copied) { $total += (Get-Item -LiteralPath $src).Length }

Write-Host ''
Write-Host '--- DLL, скопированные из OCCT ---'
foreach ($src in $occDlls) {
    $size = (Get-Item -LiteralPath $src).Length
    Write-Host ("  {0,10:N0}  {1}" -f $size, [System.IO.Path]::GetFileName($src))
}
if ($IncludeQt -and $qtDlls.Count -gt 0) {
    Write-Host '--- DLL, скопированные из Qt ---'
    foreach ($src in $qtDlls) {
        $size = (Get-Item -LiteralPath $src).Length
        Write-Host ("  {0,10:N0}  {1}" -f $size, [System.IO.Path]::GetFileName($src))
    }
}

Write-Host ''
Write-Host ("collect-occt: DLL OCCT скопировано: " + $occDlls.Count)
if ($occRuntime.Count -gt 0) {
    Write-Host ("collect-occt: служебные DLL (api-ms-win-*, MSVC runtime) не копируются: " + $occRuntime.Count)
}
Write-Host ("collect-occt: DLL Qt найдено (не копировались): " + $qtDlls.Count)
Write-Host ("collect-occt: всего скопировано: " + $copied.Count)
Write-Host ("collect-occt: суммарный размер: {0:N0} байт ({1:N1} МиБ)" -f $total, ($total / 1MB))

if ($unresolved.Count -gt 0) {
    $names = @($unresolved.Values | Sort-Object)
    Write-Host ("collect-occt: НЕ разрешены (" + $names.Count + "): " + ($names -join ', ')) -ForegroundColor Yellow
} else {
    Write-Host 'collect-occt: все зависимости разрешены'
}

Write-Host 'collect-occt: OK' -ForegroundColor Green
exit 0
