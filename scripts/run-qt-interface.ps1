# scripts/run-qt-interface.ps1 - start the Qt port (apps/qt-interface/main.qml) without a console.
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-qt-interface.ps1
#
# qmlscene is a console-subsystem developer tool: started by Start-Process or by a
# double click it opens a console window next to the panel. The script creates the
# process with CREATE_NO_WINDOW, so only the panel window appears; the Qt window
# itself is not touched by the flag.
#
# The build must exist: build/qmlbackend (MockBackend, target gvqmlbackend) and
# build/edges (CNC.Edges) are produced by scripts/build-viewer.ps1. Qt and OCCT are
# taken from CNC_QT_DIR / CNC_OCC_DIR with the same defaults as the build script.
#
# Exit code: 0 - the process is started; 1 - a directory or tool is missing.

[CmdletBinding()]
param(
    [string]$QtDir = $env:CNC_QT_DIR,
    [string]$OccDir = $env:CNC_OCC_DIR,
    # Main QML file; by default the port panel.
    [string]$Qml = 'apps\qt-interface\main.qml'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$text) {
    Write-Host "run-qt-interface: FAIL $text" -ForegroundColor Red
    exit 1
}

function First-Existing([string[]]$paths) {
    foreach ($p in $paths) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return ''
}

if (-not $QtDir) {
    $QtDir = First-Existing @('C:\Qt\6.12.0\msvc2022_64', 'C:\Qt\6.10.0\msvc2022_64', 'C:\Qt\6.9.0\msvc2022_64')
}
if (-not $QtDir) { Fail 'Qt6 не найден: задайте CNC_QT_DIR=<каталог Qt6>/msvc2022_64' }
$qmlscene = Join-Path $QtDir 'bin\qmlscene.exe'

if (-not $OccDir) {
    $OccDir = First-Existing @(
        $(if ($env:CONDA_PREFIX) { Join-Path $env:CONDA_PREFIX 'Library' }),
        'D:\cnc-toolchain\occt-env\Library',
        'C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library')
}
if (-not $OccDir) { Fail 'OCCT не найден: задайте CNC_OCC_DIR=<каталог OCCT>/Library' }

$pluginDir = Join-Path $root 'build\qmlbackend\MockBackend'
if (-not (Test-Path -LiteralPath $pluginDir)) {
    Fail "нет $pluginDir - соберите вьювер: scripts/build-viewer.ps1"
}
$qmlFile = if ([System.IO.Path]::IsPathRooted($Qml)) { $Qml } else { Join-Path $root $Qml }
if (-not (Test-Path -LiteralPath $qmlFile)) { Fail "нет QML-файла $qmlFile" }

# qmlscene resolves gvqmlbackend.dll and the CNC.Edges plugin through PATH, the same
# way the viewer entry point does.
$env:PATH = "$pluginDir;$(Join-Path $root 'build\edges\CNC\Edges');$(Join-Path $QtDir 'bin');$(Join-Path $OccDir 'bin');$env:PATH"

# The standalone panel is the normal launch for debugging; qmlscene is the fallback for a machine
# where the executable has not been built yet.
$panel = Join-Path $root 'build\qt-interface\lynx-panel.exe'
if (Test-Path -LiteralPath $panel) {
    $started = Start-Process -FilePath $panel -WorkingDirectory $root -PassThru
    Write-Host "run-qt-interface: lynx-panel pid=$($started.Id)"
    exit 0
}
if (-not (Test-Path -LiteralPath $qmlscene)) {
    Fail "нет $panel и нет $qmlscene - соберите панель: scripts/build-qt-interface.ps1"
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class QtPortProcess {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct STARTUPINFO { public int cb; public string lpReserved; public string lpDesktop; public string lpTitle; public int dwX; public int dwY; public int dwXSize; public int dwYSize; public int dwXCountChars; public int dwYCountChars; public int dwFillAttribute; public int dwFlags; public short wShowWindow; public short cbReserved2; public IntPtr lpReserved2; public IntPtr hStdInput; public IntPtr hStdOutput; public IntPtr hStdError; }
  [StructLayout(LayoutKind.Sequential)]
  public struct PROCESS_INFORMATION { public IntPtr hProcess; public IntPtr hThread; public int dwProcessId; public int dwThreadId; }
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, int flags, IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  public static int Start(string app, string args, string dir) {
    var si = new STARTUPINFO();
    si.cb = Marshal.SizeOf(si);
    PROCESS_INFORMATION pi;
    // CREATE_NO_WINDOW: no console for the console-subsystem qmlscene; its Qt window
    // is created by the process itself and is not affected.
    if (!CreateProcess(app, "\"" + app + "\" " + args, IntPtr.Zero, IntPtr.Zero, false, 0x08000000, IntPtr.Zero, dir, ref si, out pi))
      throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    return pi.dwProcessId;
  }
}
"@

# The import path and the panel: the same arguments the viewer build documents.
$arguments = '-I build\qmlbackend -I build\edges "' + $qmlFile + '"'
$id = [QtPortProcess]::Start($qmlscene, $arguments, $root)
Write-Host "run-qt-interface: qmlscene pid=$id"
