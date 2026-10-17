# SPDX-License-Identifier: MIT
# Copyright (c) 2026 KimHerV

<#
Captures the README screenshots from two portable foobar2000 copies. See docs/SCREENSHOTS.md for the
one-time setup of those copies and for what each picture must show.

  powershell -ExecutionPolicy Bypass -File tools\screenshots.ps1 -DefaultUi <dir> -ColumnsUi <dir> -Album <dir>

Writes docs\main.png, panel.png, panel-compact.png, export.png and columns-ui.png.
Only the two portable copies are started and closed; any other foobar2000 is left alone.
#>
param(
	[Parameter(Mandatory)] [string]$DefaultUi,   # portable foobar2000 (Default UI) with the prepared layout
	[Parameter(Mandatory)] [string]$ColumnsUi,   # portable foobar2000 with Columns UI and the panel in the toolbar
	[Parameter(Mandatory)] [string]$Album,       # folder with the album to show playing
	[int]$Track = 2,                             # track that is playing in the pictures
	[string]$Out,                                # default: docs
	[string]$Build                               # default: build\x64\foo_slowed_reverb.dll
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path   # $PSScriptRoot is empty in param defaults on PowerShell 5.1
if (-not $Out) { $Out = Join-Path $root "..\docs" }
if (-not $Build) { $Build = Join-Path $root "..\build\x64\foo_slowed_reverb.dll" }

Add-Type -ReferencedAssemblies System.Drawing @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Drawing;
public static class Shots {
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
	[DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
	[DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
	[DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	[DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
	[DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
	[DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
	[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
	[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
	[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
	[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
	[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	public struct RECT { public int L, T, R, B; }

	// Visible top-level window of the process with exactly this title (null title: any).
	public static IntPtr Top(int pid, string title) {
		IntPtr f = IntPtr.Zero;
		EnumWindows((h, l) => { int p; GetWindowThreadProcessId(h, out p); var s = new StringBuilder(512); GetWindowText(h, s, 512);
			if (p == pid && IsWindowVisible(h) && (title == null || s.ToString() == title)) { f = h; return false; } return true; }, IntPtr.Zero);
		return f;
	}
	// First visible descendant with this control ID.
	public static IntPtr Child(IntPtr parent, int id) {
		IntPtr f = IntPtr.Zero;
		EnumChildWindows(parent, (h, l) => { if (GetDlgCtrlID(h) == id && IsWindowVisible(h)) { f = h; return false; } return true; }, IntPtr.Zero);
		return f;
	}
	public static string Text(IntPtr h) { var s = new StringBuilder(256); GetWindowText(h, s, 256); return s.ToString(); }
	public static Bitmap Capture(IntPtr h) {
		RECT r; GetWindowRect(h, out r);
		var b = new Bitmap(r.R - r.L, r.B - r.T);
		using (var g = Graphics.FromImage(b)) { var dc = g.GetHdc(); PrintWindow(h, dc, 2); g.ReleaseHdc(dc); }
		return b;
	}
	// Part of a window's capture covered by one of its descendants.
	public static Bitmap Crop(Bitmap full, IntPtr window, IntPtr part) {
		RECT w, p; GetWindowRect(window, out w); GetWindowRect(part, out p);
		return full.Clone(new Rectangle(p.L - w.L, p.T - w.T, p.R - p.L, p.B - p.T), full.PixelFormat);
	}
}

// Mutes the audio session of one process, so the copies stay silent while they "play".
[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator { int NotImpl1(); int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice dev); }
[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice { int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o); }
[Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 { int NotImpl1(); int NotImpl2(); int GetSessionEnumerator(out IAudioSessionEnumerator e); }
[Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator { int GetCount(out int n); int GetSession(int i, out IAudioSessionControl2 s); }
[Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
	int N1(); int N2(); int N3(); int N4(); int N5(); int N6(); int N7(); int N8(); int N9(); int N10(); int N11();
	int GetProcessId(out uint pid);
}
[Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ISimpleAudioVolume { int SetMasterVolume(float v, ref Guid ctx); int GetMasterVolume(out float v); int SetMute(bool m, ref Guid ctx); }
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}
public static class Silence {
	public static bool Mute(int pid) {
		var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
		IMMDevice dev; en.GetDefaultAudioEndpoint(0, 1, out dev);
		var iid = typeof(IAudioSessionManager2).GUID; object o; dev.Activate(ref iid, 23, IntPtr.Zero, out o);
		IAudioSessionEnumerator sessions; ((IAudioSessionManager2)o).GetSessionEnumerator(out sessions);
		int n; sessions.GetCount(out n); bool found = false;
		for (int i = 0; i < n; i++) {
			IAudioSessionControl2 s; sessions.GetSession(i, out s); uint p; s.GetProcessId(out p);
			if (p == pid) { var g = Guid.Empty; ((ISimpleAudioVolume)s).SetMute(true, ref g); found = true; }
		}
		return found;
	}
}
'@
[Shots]::SetProcessDPIAware() | Out-Null
$Out = (Resolve-Path $Out).Path
$tracks = Get-ChildItem -LiteralPath $Album -Filter *.flac | Sort-Object Name | ForEach-Object { '"' + $_.FullName + '"' }
if (-not $tracks) { throw "No .flac files in $Album" }

function Get-Fb2k($dir) { Get-Process foobar2000 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $dir "foobar2000.exe") } }

function Stop-Fb2k($dir) {
	if (Get-Fb2k $dir) { Start-Process (Join-Path $dir "foobar2000.exe") -ArgumentList "/exit"; Start-Sleep 4 }
}

function Start-Fb2k($dir) {
	Stop-Fb2k $dir
	Copy-Item $Build (Join-Path $dir "profile\user-components-x64\foo_slowed_reverb\") -Force
	# Hide ffmpeg from this copy so the export dialog shows the no-ffmpeg WAV state and no personal path.
	$oldPath = $env:PATH; $oldLocal = $env:LOCALAPPDATA
	$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch 'ffmpeg|WinGet\\Links' }) -join ';'
	$env:LOCALAPPDATA = Join-Path $dir "profile"
	try { Start-Process (Join-Path $dir "foobar2000.exe") } finally { $env:PATH = $oldPath; $env:LOCALAPPDATA = $oldLocal }
	Start-Sleep 5
	$p = Get-Fb2k $dir
	$main = $p.MainWindowHandle
	[Shots]::ShowWindow($main, 4) | Out-Null          # restore without taking focus
	[Shots]::MoveWindow($main, 40, 40, 1400, 860, $true) | Out-Null
	# Load the album (replaces the playlist and starts playing), then skip to the wanted track.
	Start-Process (Join-Path $dir "foobar2000.exe") -ArgumentList $tracks
	for ($t = 0; $t -lt 40 -and -not [Silence]::Mute($p.Id); $t++) { Start-Sleep -Milliseconds 100 }
	Start-Sleep 2
	for ($i = 1; $i -lt $Track; $i++) { Start-Process (Join-Path $dir "foobar2000.exe") -ArgumentList "/next"; Start-Sleep 1 }
	return $p
}

function Save-Png($bitmap, $name) { $bitmap.Save((Join-Path $Out $name), [System.Drawing.Imaging.ImageFormat]::Png); $bitmap.Dispose(); "  $name" }

function Click($h) { [Shots]::SendMessage($h, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }

# Picks a preset in the panel's combo box (IDC_PRESET) the way a user would; returns the panel dialog.
function Set-Preset($main, $name) {
	$combo = [Shots]::Child($main, 1101)
	if ($combo -eq [IntPtr]::Zero) { throw "No Slowed & Reverb panel in this layout (see docs/SCREENSHOTS.md)" }
	$panel = [Shots]::GetParent($combo)
	$i = [Shots]::SendMessage($combo, 0x014C, [IntPtr](-1), $name)   # CB_FINDSTRING: "Classic *" matches too
	[Shots]::SendMessage($combo, 0x014E, $i, [IntPtr]::Zero) | Out-Null
	[Shots]::SendMessage($panel, 0x0111, [IntPtr]((1 -shl 16) -bor 1101), $combo) | Out-Null
	return $panel
}

# ---------------------------------------------------------------- Default UI
"Default UI"
$p = Start-Fb2k $DefaultUi
$main = $p.MainWindowHandle
# Preset Classic, effect on, "Reverb shape" open.
$panel = Set-Preset $main "Classic"
$fold = [Shots]::GetDlgItem($panel, 1040)
if ([Shots]::SendMessage([Shots]::GetDlgItem($panel, 1106), 0x00F0, [IntPtr]::Zero, [IntPtr]::Zero) -eq [IntPtr]::Zero) { Click ([Shots]::GetDlgItem($panel, 1106)) }
if ([Shots]::Text($fold).StartsWith([string][char]0x25B8)) { Click $fold }
Start-Sleep 15  # some playback progress in the seek bar and visualisations

$full = [Shots]::Capture($main)
Save-Png ([Shots]::Crop($full, $main, [Shots]::GetParent($panel))) "panel.png"   # the tab control around the panel
Save-Png $full "main.png"

# Compact layout: the View > Slowed & Reverb popup keeps its own (compact) layout.
Start-Process (Join-Path $DefaultUi "foobar2000.exe") -ArgumentList '"/command:Slowed & Reverb"'
Start-Sleep 2
$popup = [Shots]::Top($p.Id, "Slowed & Reverb")
if ([Shots]::Child($popup, 1104) -eq [IntPtr]::Zero) { Write-Warning "The popup is not in the compact layout (right-click it > Compact layout)" }
Save-Png ([Shots]::Capture($popup)) "panel-compact.png"
[Shots]::PostMessage($popup, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

# Export dialog for the whole album: playback settings, default suffix, WAV, output folder empty.
# Cancelled afterwards, so none of this is saved.
Start-Process (Join-Path $DefaultUi "foobar2000.exe") -ArgumentList (@('"/context_command:Export..."') + $tracks)
Start-Sleep 3
$dlg = [Shots]::Top($p.Id, "Export with Slowed & Reverb")
$fmt = [Shots]::GetDlgItem($dlg, 1201)
[Shots]::SendMessage($fmt, 0x014E, [IntPtr]4, [IntPtr]::Zero) | Out-Null
[Shots]::SendMessage($dlg, 0x0111, [IntPtr]((1 -shl 16) -bor 1201), $fmt) | Out-Null
[Shots]::SendMessage([Shots]::GetDlgItem($dlg, 1202), 0x000C, [IntPtr]::Zero, "") | Out-Null
[Shots]::SendMessage([Shots]::GetDlgItem($dlg, 1208), 0x000C, [IntPtr]::Zero, "(Slowed & Reverb)") | Out-Null
[Shots]::SendMessage([Shots]::GetDlgItem($dlg, 1204), 0x000C, [IntPtr]::Zero, "[%artist% - ]%title% '(Slowed & Reverb)'") | Out-Null
Click ([Shots]::GetDlgItem($dlg, 1105))   # Use playback settings
Start-Sleep 1
Save-Png ([Shots]::Capture($dlg)) "export.png"
[Shots]::SendMessage($dlg, 0x0111, [IntPtr]2, [IntPtr]::Zero) | Out-Null
Stop-Fb2k $DefaultUi

# ---------------------------------------------------------------- Columns UI
"Columns UI"
$p = Start-Fb2k $ColumnsUi
Set-Preset $p.MainWindowHandle "Classic" | Out-Null
# Give the panel the rest of the first toolbar row (band 1, right after the menu), so its sliders are not cramped.
$rebar = [Shots]::Child($p.MainWindowHandle, 2100)
if ($rebar -ne [IntPtr]::Zero) { [Shots]::SendMessage($rebar, 0x0400 + 31, [IntPtr]1, [IntPtr]::Zero) | Out-Null }   # RB_MAXIMIZEBAND
Start-Sleep 8
Save-Png ([Shots]::Capture($p.MainWindowHandle)) "columns-ui.png"
Stop-Fb2k $ColumnsUi
"done"
