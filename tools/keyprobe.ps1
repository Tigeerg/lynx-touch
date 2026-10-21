# tools/keyprobe.ps1 - viewer hotkey probe on different layouts.
#
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File tools/keyprobe.ps1
#         powershell -NoProfile -ExecutionPolicy Bypass -File tools/keyprobe.ps1 -Layouts ru
#
# What it does: starts a live viewer (build/viewer/gcodeviewer.exe) with key
# diagnostics enabled (CNC_KEY_DEBUG=1, printed by GcodeViewer3D.qml), switches
# the window layout to the given one, sends physical key presses and reads the log.
# A press is sent as a scan code (SendInput, KEYEVENTF_SCANCODE): the system itself
# translates a scan code into VK and a character, so the probe reproduces what the
# viewer sees on the Russian layout. A synthetic event (PostMessage with a ready VK)
# is not suitable for this: it sets the VK directly and does not reproduce the
# layout translation.
#
# The result is substantive: a key counts as fired if after the press the state of
# the viewer toggles changed (the state line in the log).
#
# A live desktop session is required: the viewer window must be foreground, otherwise
# the presses would go to a foreign window - and the probe does not send them at all.
# That is why the probe is not part of the guard (tools/check.ps1), it is run by hand.
#
# A run on the build machine: window selection, layout switching (ru and en take hold)
# and log parsing are verified; delivery of the sent presses could not be confirmed -
# SendInput in a live session did not reach the viewer window (a console exe has two
# top-level windows: the console and the Qt window). A reproducible check of the layer is
# the guard step build/viewer/keyprobe.exe (offscreen, layouts ru and en).
#
# Return code: 0 - all keys fired; 1 - some did not fire; 2 - there was no probe
# (no exe, no window, the window did not come to the foreground, no desktop).

[CmdletBinding()]
param(
    # Viewer entry point.
    [string]$Exe = '',
    # G-code to load, relative to the repository root: on an empty viewer P and T stay silent.
    [string]$Program = 'data\cad-testdata\Test_model.ngc',
    # Layouts of the target window: ru (00000419), en (00000409).
    [string[]]$Layouts = @('ru', 'en'),
    # Physical keys of the viewer hotkeys.
    [string[]]$Keys = @('P', 'S', 'M', 'L', 'T', 'E', 'D', 'X', 'F'),
    # Waiting for the viewer window, ms.
    [int]$WaitMs = 15000,
    # Pause after the window is up (the viewer loads the model and the g-code), ms.
    [int]$SettleMs = 6000,
    # Qt directory (bin/Qt6Core.dll is expected inside).
    [string]$QtDir = $env:CNC_QT_DIR,
    # OCCT directory (bin is expected inside).
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

# Windows of the process: for a console exe MainWindowHandle points to the console, so the
# windows are enumerated and the window of class Qt (QQuickView) is taken, not ConsoleWindowClass.
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

# Foreground: the window is brought to the foreground once, after that the foreground is only
# checked - showing and switching the window on every key drops the delivery of presses.
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

# Physical press: scan code, the system translates it into VK and a character according to the window layout.
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

# Window layout: LoadKeyboardLayout for its own thread plus a request to the window
# (WM_INPUTLANGCHANGEREQUEST). Attachment to the window queue (AttachThreadInput) is not needed:
# the thread has its own layout, and the system translates the scan code into a character according to the layout
# of the window that received the press.
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

# Log: the process stdout (entry point) and stderr (console.log from QML).
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
    # GetKeyboardLayout returns an HKL: the low word is the language (0x0419 Russian, 0x0409 English),
    # the high word is the device descriptor, it depends on the load and is not good for comparison.
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
        # The press is repeated until the event appears in the log: a live session may
        # take away the foreground in the middle of the probe.
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
