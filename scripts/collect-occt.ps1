# scripts/collect-occt.ps1 — минимальный набор DLL OCCT для поставки.
#
# Запуск:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/collect-occt.ps1 `
#              -OutDir D:\cnc-toolchain\dist-occt
#          powershell -NoProfile -ExecutionPolicy Bypass -File scripts/collect-occt.ps1 `
#              -OutDir D:\cnc-toolchain\dist-occt -IncludeQt
#
# Набор не пишут руками: скрипт обходит настоящий граф зависимостей собранных
# артефактов (dumpbin /dependents, рекурсивно) и берёт только те DLL, которые
# действительно грузятся. Источник DLL OCCT — каталог -OccDir (по умолчанию
# $env:CNC_OCC_DIR), каталог сборки — -BuildDir (по умолчанию <repo>\build),
# результат — в -OutDir: он обязателен, в репозиторий скрипт не пишет.
# DLL Qt ($env:CNC_QT_DIR\bin) считаются отдельно и по умолчанию не копируются:
# их добавляет ключ -IncludeQt.
#
# dumpbin берётся из окружения MSVC, которое снимается одним вызовом vcvars64.bat,
# как в scripts/build-viewer.ps1 (путь ищется там же: CNC_VCVARS, vswhere,
# каталоги VS). Сначала ищется DLL в каталоге OCCT, затем в Qt, затем в каталогах
# сборки и System32; зависимость, не найденная нигде, печатается как находка, а не
# молча прячется. Скрипт идемпотентен: файлы вне -OutDir он не трогает.
#
# Код возврата: 0 — набор собран; 1 — инструмент или каталог не найден.

[CmdletBinding()]
param(
    # Каталог OCCT (внутри ожидается bin с DLL и lib/cmake/opencascade/OpenCASCADEConfig.cmake).
    [string]$OccDir = $env:CNC_OCC_DIR,
    # Каталог сборки относительно корня репозитория.
    [string]$BuildDir = '',
    # Каталог результата; обязателен и не может лежать внутри репозитория.
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    # Каталог Qt6 (внутри ожидается bin с DLL Qt).
    [string]$QtDir = $env:CNC_QT_DIR,
    # Копировать в набор и DLL, разрешённые из каталога Qt.
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

# Первый найденный по имени файл: порядок каталогов задаёт источник DLL.
function Resolve-Dependency([string]$name, [string[]]$dirs) {
    foreach ($dir in $dirs) {
        $candidate = Join-Path $dir $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    return ''
}

# Имена DLL из раздела «dependencies» вывода dumpbin /dependents.
function Get-Dependents([string]$file) {
    $names = New-Object System.Collections.Generic.List[string]
    $lines = & $script:dumpbin /nologo /dependents $file 2>$null
    if ($null -eq $lines) { return $names }
    $inDep = $false
    foreach ($line in $lines) {
        if ($line -match 'dependencies:\s*$') { $inDep = $true; continue }
        if (-not $inDep) { continue }
        # После заголовка dumpbin печатает пустую строку, дальше — имена DLL;
        # список кончается первой непустой строкой не из имени DLL («Summary»).
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

# vcvars64.bat — batch-файл: его окружение снимается один раз в переменные
# процесса, дальше dumpbin запускается прямо из PowerShell (путь репозитория с
# кириллицей доезжает без перекодировки).
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

# Каталоги поиска: сначала OCCT, потом Qt, потом сборка и System32. Источник DLL
# нужен, чтобы отделить DLL OCCT (копируются) от Qt (считаются отдельно) и
# системных (не копируются).
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

# Обход графа: считаем артефакты, дальше — их зависимости. Системные каталоги не
# обходим: их DLL всё равно не копируются, а дерево велико.
$resolved = @{}                                   # имя (в нижнем регистре) -> путь
$dependencyOf = @{}                               # имя -> каталог-источник
$unresolved = @{}                                 # имена, не найденные нигде
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

# Конда-пакет OCCT держит в своём bin и копии служебных DLL: наборы API Windows
# (api-ms-win-*) и рантайм MSVC (msvcp140*, vcruntime140*, vcomp140*, concrt140*).
# В поставку их даёт система или распространяемый пакет MSVC, а не OCCT, поэтому в
# набор они не идут: без них замыкание равно настоящему, а с ними раздувается на 18
# файлов. Тест — по имени, а не списком, чтобы набор оставался воспроизводимым.
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
