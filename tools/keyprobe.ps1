# tools/keyprobe.ps1 — проба горячих клавиш вьювера на разных раскладках.
#
# Запуск: powershell -NoProfile -ExecutionPolicy Bypass -File tools/keyprobe.ps1
#         powershell -NoProfile -ExecutionPolicy Bypass -File tools/keyprobe.ps1 -Layouts ru
#
# Что делает: поднимает живой вьювер (build/viewer/gcodeviewer.exe) с включённой
# диагностикой клавиш (CNC_KEY_DEBUG=1, печатает GcodeViewer3D.qml), переключает
# раскладку окна на заданную, посылает физические нажатия и читает журнал.
# Нажатие посылается сканом (SendInput, KEYEVENTF_SCANCODE): систему скан-код в VK и
# символ переводит сама, поэтому проба воспроизводит то, что видит вьювер на русской
# раскладке. Синтетическое событие (PostMessage с готовым VK) для этого не годится:
# оно задаёт VK напрямую и не воспроизводит трансляцию раскладки.
#
# Результат - по существу: клавиша считается сработавшей, если после нажатия
# состояние переключателей вьювера изменилось (строка state в журнале).
#
# Нужна живая сессия рабочего стола: окно вьювера обязано быть передним, иначе
# нажатия ушли бы в чужое окно - и проба их не посылает вовсе. Поэтому в сторож
# (tools/check.ps1) проба не входит, её гоняют руками.
#
# Прогон на машине сборки: выбор окна, переключение раскладки (ru и en встают) и
# разбор журнала проверены; доставку посланных нажатий подтвердить не удалось -
# SendInput в живой сессии до окна вьювера не доходил (у консольного exe два окна
# верхнего уровня: консоль и окно Qt). Воспроизводимая проверка слоя - шаг сторожа
# build/viewer/keyprobe.exe (offscreen, раскладки ru и en).
#
# Код возврата: 0 - все клавиши сработали; 1 - часть не сработала; 2 - пробы не было
# (нет exe, нет окна, окно не вышло на передний план, нет рабочего стола).

[CmdletBinding()]
param(
    # Точка входа вьювера.
    [string]$Exe = '',
    # УП для загрузки, относительно корня репозитория: на пустом вьювере P и T молчат.
    [string]$Program = 'data\cad-testdata\Test_model.ngc',
    # Раскладки целевого окна: ru (00000419), en (00000409).
    [string[]]$Layouts = @('ru', 'en'),
    # Физические клавиши горячих клавиш вьювера.
    [string[]]$Keys = @('P', 'S', 'M', 'L', 'T', 'E', 'D', 'X', 'F'),
    # Ожидание окна вьювера, мс.
    [int]$WaitMs = 15000,
    # Пауза после подъёма окна (вьювер грузит модель и УП), мс.
    [int]$SettleMs = 6000,
    # Каталог Qt (внутри ожидается bin/Qt6Core.dll).
    [string]$QtDir = $env:CNC_QT_DIR,
    # Каталог OCCT (внутри ожидается bin).
    [string]$OccDir = $env:CNC_OCC_DIR
)

$ErrorActionPreference = 'Stop'
$Keys = @($Keys -split ',')
$Layouts = @($Layouts -split ',')

$repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $repo 'build\viewer\gcodeviewer.exe' }
if (-not $QtDir) { $QtDir = 'C:\Qt\6.12.0\msvc2022_64' }
if (-not $OccDir) {
    $OccDir = if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }
              else { 'C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library' }
}

function Skip([string]$text) {
    Write-Host "keyprobe: SKIP $text" -ForegroundColor Yellow
    exit 2
}

if (-not (Test-Path -LiteralPath $Exe)) {
    Skip "нет точки входа вьювера $Exe - соберите: scripts/build-viewer.ps1"
}
$qtBin = Join-Path $QtDir 'bin'
if (-not (Test-Path -LiteralPath (Join-Path $qtBin 'Qt6Core.dll'))) {
    Skip "нет Qt: $qtBin\Qt6Core.dll (задайте CNC_QT_DIR)"
}
$env:PATH = "$qtBin;$(Join-Path $OccDir 'bin');$env:PATH"
$env:CNC_KEY_DEBUG = '1'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class K {
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
        public int dx; public int dy; public uint mouseData; public uint dwFlags;
        public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] public struct HARDWAREINPUT {
        public uint uMsg; public ushort wParamL; public ushort wParamH; }
    [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION {
        [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public HARDWAREINPUT hi; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION u; }
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] p, int size);
    [DllImport("user32.dll")] public static extern ushort MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
    [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint idThread);
    [DllImport("user32.dll")] public static extern IntPtr ActivateKeyboardLayout(IntPtr hkl, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr LoadKeyboardLayout(string id, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}
'@

$inputSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][K+INPUT])

# Окна процесса: у консольного exe MainWindowHandle указывает на консоль, поэтому окна
# перебираются и берётся окно класса Qt (QQuickView), а не ConsoleWindowClass.
function Find-ViewerWindow([int]$processId, [int]$timeoutMs) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        $script:winList = @()
        $callback = [K+EnumWindowsProc] {
            param($h, $p)
            $owner = [uint32]0
            [void][K]::GetWindowThreadProcessId($h, [ref]$owner)
            if ($owner -eq $processId -and [K]::IsWindowVisible($h)) {
                $cls = New-Object System.Text.StringBuilder 256
                [void][K]::GetClassName($h, $cls, 256)
                $script:winList += [pscustomobject]@{ hwnd = $h; cls = $cls.ToString() }
            }
            return $true
        }
        [void][K]::EnumWindows($callback, [IntPtr]::Zero)
        $hit = $script:winList | Where-Object { $_.cls -like 'Qt*' } | Select-Object -First 1
        if ($hit) { return $hit }
        Start-Sleep -Milliseconds 250
    }
    return $null
}

# Передний план: окно выводится на передний план один раз, дальше передний план только
# проверяется - показ и переключение окна на каждой клавише роняют доставку нажатий.
function Set-Foreground([IntPtr]$hwnd, [uint32]$myTid, [bool]$strong) {
    $fg = [K]::GetForegroundWindow()
    if ($fg -eq $hwnd) { return $true }
    $fgOwner = [uint32]0
    $fgTid = [K]::GetWindowThreadProcessId($fg, [ref]$fgOwner)
    [void][K]::AttachThreadInput($myTid, $fgTid, $true)
    if ($strong) {
        [void][K]::ShowWindow($hwnd, 9)      # SW_RESTORE
    }
    [void][K]::BringWindowToTop($hwnd)
    [void][K]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 400
    [void][K]::AttachThreadInput($myTid, $fgTid, $false)
    return ([K]::GetForegroundWindow() -eq $hwnd)
}

# Физическое нажатие: скан-код, перевод в VK и символ делает система по раскладке окна.
function Send-Scan([int]$scan) {
    $down = New-Object K+INPUT
    $down.type = 1
    $down.u.ki.wVk = 0
    $down.u.ki.wScan = [uint16]$scan
    $down.u.ki.dwFlags = 0x0008           # KEYEVENTF_SCANCODE
    $up = $down
    $up.u.ki.dwFlags = 0x000A             # KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP
    $sent = [K]::SendInput(1, @($down), $inputSize)
    Start-Sleep -Milliseconds 40
    $sent += [K]::SendInput(1, @($up), $inputSize)
    if ($sent -ne 2) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Host "keyprobe: SendInput отказал, ошибка $err"
    }
    Start-Sleep -Milliseconds 120
}

# Раскладка окна: LoadKeyboardLayout для своего потока плюс запрос окну
# (WM_INPUTLANGCHANGEREQUEST). Привязка к очереди окна (AttachThreadInput) не нужна:
# раскладка у потока своя, а перевод скан-кода в символ система делает по раскладке
# окна, которое получило нажатие.
function Set-Layout([IntPtr]$hwnd, [uint32]$tid, [uint32]$myTid, [string]$tag) {
    $klid = if ($tag -eq 'ru') { '00000419' } else { '00000409' }
    $hkl = [K]::LoadKeyboardLayout($klid, 0x00000001)
    [void][K]::ActivateKeyboardLayout($hkl, 0)
    for ($i = 0; $i -lt 8; $i++) {
        [void][K]::PostMessage($hwnd, 0x0050, [IntPtr]::Zero, $hkl)   # WM_INPUTLANGCHANGEREQUEST
        Start-Sleep -Milliseconds 250
        if ([K]::GetKeyboardLayout($tid) -eq $hkl) { break }
    }
    return [K]::GetKeyboardLayout($tid)
}

$vkOf = @{ P = 0x50; S = 0x53; M = 0x4D; L = 0x4C; T = 0x54; E = 0x45; D = 0x44; X = 0x58; F = 0x46 }

$logDir = Join-Path $env:TEMP ("cnc_keyprobe\" + $PID)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$outLog = Join-Path $logDir 'stdout.txt'
$errLog = Join-Path $logDir 'stderr.txt'
Remove-Item -LiteralPath $outLog, $errLog -ErrorAction SilentlyContinue
Write-Host "keyprobe: журнал $logDir"

# Журнал: stdout процесса (точка входа) и stderr (console.log из QML).
function Get-LogText {
    $text = ''
    foreach ($file in $outLog, $errLog) {
        if (Test-Path -LiteralPath $file) { $text += (Get-Content -LiteralPath $file -Encoding UTF8 -Raw) }
    }
    return $text
}
function Get-StateLines([string]$text) {
    return @($text -split "`r?`n" | Where-Object { $_ -match '\[key\] state' })
}
function Get-KeyLines([string]$text) {
    return @($text -split "`r`n" | Where-Object { $_ -match '\[key\] key=' })
}

Write-Host "keyprobe: вьювер $Exe" -ForegroundColor Cyan
$proc = Start-Process -FilePath $Exe -ArgumentList $Program -WorkingDirectory $repo -PassThru `
    -RedirectStandardOutput $outLog -RedirectStandardError $errLog
$window = Find-ViewerWindow $proc.Id $WaitMs
if (-not $window) {
    if (-not $proc.HasExited) { $proc.Kill() }
    Skip "окно вьювера не появилось (нужна живая сессия рабочего стола)"
}
$hwnd = $window.hwnd
$ownerPid = [uint32]0
$tid = [K]::GetWindowThreadProcessId($hwnd, [ref]$ownerPid)
$myTid = [K]::GetCurrentThreadId()
Write-Host ("keyprobe: окно {0} класс {1} поток {2}" -f $hwnd, $window.cls, $tid)
Start-Sleep -Milliseconds $SettleMs

if (-not (Set-Foreground $hwnd $myTid $true)) {
    if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 800; $proc.Kill() }
    Skip "окно вьювера не вышло на передний план: нажатия ушли бы в чужое окно"
}

$fired = 0
$total = 0
foreach ($tag in $Layouts) {
    $hkl = Set-Layout $hwnd $tid $myTid $tag
    # GetKeyboardLayout отдаёт HKL: младшее слово - язык (0x0419 русский, 0x0409 английский),
    # старшее - описатель устройства, он от загрузки зависит и для сверки не годится.
    $langWanted = if ($tag -eq 'ru') { 0x0419 } else { 0x0409 }
    $langGot = $hkl.ToInt64() -band 0xFFFF
    if ($langGot -ne $langWanted) {
        Write-Host ("keyprobe: раскладка {0} не встала (язык потока 0x{1:X4}) - пропуск" -f $tag, $langGot)
        continue
    }
    Write-Host ("keyprobe: раскладка {0}, язык потока окна 0x{1:X4}" -f $tag, $langGot) -ForegroundColor Cyan
    foreach ($key in $Keys) {
        $total++
        $vk = $vkOf[$key]
        $scan = [K]::MapVirtualKey($vk, 0)
        $stateBefore = @(Get-StateLines (Get-LogText))
        $keysBefore = (Get-KeyLines (Get-LogText)).Count
        $line = ''
        # Нажатие повторяется, пока событие не появится в журнале: живая сессия может
        # отобрать передний план на середине пробы.
        for ($try = 1; $try -le 6; $try++) {
            if (-not (Set-Foreground $hwnd $myTid $false)) {
                Write-Host ("keyprobe: [{0}] {1}: окно потеряло передний план, нажатие не послано" -f $tag, $key)
                break
            }
            Send-Scan $scan
            $lines = @(Get-KeyLines (Get-LogText))
            if ($lines.Count -gt $keysBefore) { $line = $lines[$lines.Count - 1]; break }
        }
        $stateAfter = @(Get-StateLines (Get-LogText))
        $changed = ($stateAfter.Count -gt $stateBefore.Count) -and
                   ($stateAfter[$stateAfter.Count - 1] -ne $stateBefore[$stateBefore.Count - 1])
        if ($changed) { $fired++ }
        Write-Host ("keyprobe: [{0}] {1} vk=0x{2:X2} scan=0x{3:X2} -> {4}" -f $tag, $key, $vk, $scan,
            $(if ($changed) { 'сработала' } else { 'НЕ сработала' })) `
            -ForegroundColor $(if ($changed) { 'Green' } else { 'Red' })
        if ($line) { Write-Host "          $line" }
    }
}

$text = Get-LogText
Write-Host ''
Write-Host '--- журнал вьювера, признаки клавиш (CNC_KEY_DEBUG=1) ---'
foreach ($l in Get-KeyLines $text) { Write-Host $l }
Write-Host ''
Write-Host '--- журнал вьювера, состояние переключателей ---'
foreach ($l in Get-StateLines $text) { Write-Host $l }
Write-Host ''
Write-Host '--- журнал вьювера, точка входа ---'
foreach ($l in ($text -split "`r?`n" | Where-Object { $_ -match '^gcodeviewer:' })) { Write-Host $l }

if (-not $proc.HasExited) {
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 900
    if (-not $proc.HasExited) { $proc.Kill() }
}

Write-Host ''
if ($fired -eq $total -and $total -gt 0) {
    Write-Host "RESULT: OK ($fired из $total)" -ForegroundColor Green
    exit 0
}
Write-Host "RESULT: FAIL (сработало $fired из $total)" -ForegroundColor Red
exit 1
