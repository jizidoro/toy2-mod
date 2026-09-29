<#
Drives a running toy2.exe (started with: ts2-run.ps1 -KeepRunning -Seconds 0) for smoke tests.
  -Action focus            bring the game to the foreground (it reads keys through DirectInput only while in front)
  -Action key -Keys ...    press keys, e.g. -Keys SPACE,UP:1500 (name[:hold ms], default 120 ms). Every key is sent
                           only after checking toy2.exe is the foreground app, so no keystroke can reach another app.
  -Action shot -Name x     save the game's own back buffer (ts2diag) as planning\evidence\<x>.png + a preview
  -Action state            Buzz position / health / lives / coins, demo flag, speed multiplier, frame count
  -Action stop             close the game
Memory addresses (toy2-decomp): g_buzzActor 0x0052F300 (pos x,y,z int32 at +0,+4,+8; health +0x96, lives +0x9A,
coins +0x9E, int16), g_demoMode 0x0052AD94, speed multiplier 0x0052F2D4.
#>
param(
    [Parameter(Mandatory)][ValidateSet('focus', 'blur', 'key', 'probe', 'shot', 'state', 'stop')][string]$Action,
    [int]$Seconds = 3,
    [string[]]$Keys = @(),
    [string]$Name = 'shot'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = Join-Path $root 'game'
$diag = Join-Path $game 'ts2diag'
$evidence = Join-Path $root 'planning\evidence'
$scratch = Join-Path $env:TEMP 'ts2-drive'
New-Item -ItemType Directory -Force $scratch | Out-Null

Add-Type -Namespace TS2 -Name Drive -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(int access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr h, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint pid);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hwnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr hwnd, int cmd);
[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr hwnd);
[DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
[StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public System.IntPtr extra; }
[StructLayout(LayoutKind.Explicit, Size = 40)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
'@

$proc = Get-Process toy2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc -and $Action -ne 'stop') { throw 'toy2.exe is not running (start it with ts2-run.ps1 -KeepRunning -Seconds 0)' }

function Test-GameForeground { $p = 0; [TS2.Drive]::GetWindowThreadProcessId([TS2.Drive]::GetForegroundWindow(), [ref]$p) | Out-Null; $p -eq $proc.Id }

# scan codes as DirectInput reads them (DIK_*); E = extended key
$scan = @{ SPACE = 0x39; ENTER = 0x1C; ESC = 0x01; UP = 'E48'; DOWN = 'E50'; LEFT = 'E4B'; RIGHT = 'E4D'; X = 0x2D; S = 0x1F; F = 0x21; A = 0x1E; D = 0x20; Q = 0x10; W = 0x11; TAB = 0x0F; SHIFT = 0x2A; ALT = 0x38; F24 = 0x76 }

# Windows only lets a process take the foreground right after an Alt press. Alt pressed and released ALONE puts the
# receiving window into menu mode, which blocks the game's message loop (the game froze at ~frame 300 that way), so
# an unused key (F24) is tapped while Alt is held.
function Get-Dik([string]$key) {
    $v = $scan[$key.ToUpper()]; if ($null -eq $v) { throw "unknown key $key" }
    if ($v -is [string]) { 0x80 + [Convert]::ToInt32($v.Substring(1), 16) } else { [int]$v }
}
function Take-Foreground([IntPtr]$hwnd) {
    Send-Scan ALT $false; [TS2.Drive]::SetForegroundWindow($hwnd) | Out-Null; Start-Sleep -Milliseconds 50
    Send-Scan F24 $false; Send-Scan F24 $true; Send-Scan ALT $true
}
function Send-Scan([string]$key, [bool]$up) {
    $v = $scan[$key.ToUpper()]; if ($null -eq $v) { throw "unknown key $key" }
    $ext = $v -is [string]; $code = if ($ext) { [Convert]::ToUInt16($v.Substring(1), 16) } else { [uint16]$v }
    $in = New-Object TS2.Drive+INPUT; $in.type = 1
    $in.ki.wScan = $code; $in.ki.dwFlags = 0x8 -bor ($(if ($ext) { 0x1 } else { 0 })) -bor ($(if ($up) { 0x2 } else { 0 }))
    [TS2.Drive]::SendInput(1, @($in), 40) | Out-Null
}

switch ($Action) {
    'focus' {
        $proc.Refresh(); $hwnd = $proc.MainWindowHandle
        if ([TS2.Drive]::IsIconic($hwnd)) { [TS2.Drive]::ShowWindow($hwnd, 9) | Out-Null }   # SW_RESTORE
        Take-Foreground $hwnd
        Start-Sleep -Milliseconds 300
        "game foreground: $(Test-GameForeground)"
    }
    'blur' {
        # simulate "the player clicks outside the game": a small window of ours on the primary monitor takes the
        # foreground for -Seconds, then closes
        Add-Type -AssemblyName System.Windows.Forms
        $f = New-Object System.Windows.Forms.Form
        $f.Text = 'ts2 focus test'; $f.StartPosition = 'Manual'; $f.Location = New-Object System.Drawing.Point(40, 40)
        $f.Size = New-Object System.Drawing.Size(260, 90); $f.TopMost = $true; $f.ShowInTaskbar = $false
        $f.Show(); [System.Windows.Forms.Application]::DoEvents()
        Take-Foreground $f.Handle
        $end = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $end) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
        $f.Close()
        "blurred for $Seconds s (game foreground now: $(Test-GameForeground))"
    }
    'key' {
        # Keys go in through ts2diag's hook on the game's DirectInput keyboard read (shared block
        # Local\ts2diag_keys_<pid>): Windows' SendInput never reaches this game's DirectInput 7 keyboard.
        $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\ts2diag_keys_$($proc.Id)")
        $acc = $mmf.CreateViewAccessor(0, 256)
        try {
            foreach ($k in $Keys) {
                $name, $hold = $k -split ':'; $hold = if ($hold) { [int]$hold } else { 150 }
                if (-not (Test-GameForeground)) { throw "stopped before '$k': toy2.exe is not the foreground app (no key sent)" }
                $code = Get-Dik $name
                $acc.Write($code, [byte]0x80); Start-Sleep -Milliseconds $hold; $acc.Write($code, [byte]0); Start-Sleep -Milliseconds 200
            }
        } finally { for ($i = 0; $i -lt 256; $i++) { $acc.Write($i, [byte]0) }; $acc.Dispose(); $mmf.Dispose() }
        "sent: $($Keys -join ', ')"
    }
    'probe' {
        # hold one key and read what the game's keyboard poll produced: g_inputStates[DIK] (0x00529A00)
        $k = $Keys[0]; $code = Get-Dik $k
        $h = [TS2.Drive]::OpenProcess(0x0410, $false, $proc.Id)
        if (-not (Test-GameForeground)) { throw 'toy2.exe is not the foreground app (no key sent)' }
        $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\ts2diag_keys_$($proc.Id)"); $acc = $mmf.CreateViewAccessor(0, 256)
        $b = New-Object byte[] 256; $r = 0
        [TS2.Drive]::ReadProcessMemory($h, [IntPtr]0x00529A00, $b, 256, [ref]$r) | Out-Null; $before = $b[$code]
        $acc.Write($code, [byte]0x80); Start-Sleep -Milliseconds 400
        [TS2.Drive]::ReadProcessMemory($h, [IntPtr]0x00529A00, $b, 256, [ref]$r) | Out-Null; $during = $b[$code]
        $acc.Write($code, [byte]0); $acc.Dispose(); $mmf.Dispose()
        "key $k (DIK 0x{0:X2}): game key state before {1}, while held {2}" -f $code, $before, $during
    }
    'shot' {
        $bmp = Join-Path $diag "$Name.bmp"; Remove-Item $bmp -ErrorAction SilentlyContinue
        Add-Content -Path (Join-Path $diag 'shot.req') -Value $Name
        $t = (Get-Date).AddSeconds(5); while (-not (Test-Path $bmp) -and (Get-Date) -lt $t) { Start-Sleep -Milliseconds 100 }
        if (-not (Test-Path $bmp)) { throw "no frame captured (is the game rendering? it pauses when it loses focus)" }
        Start-Sleep -Milliseconds 300
        Add-Type -AssemblyName System.Drawing
        $img = [System.Drawing.Image]::FromFile($bmp); $png = Join-Path $evidence "$Name.png"
        $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $small = New-Object System.Drawing.Bitmap 1280, 720; $g = [System.Drawing.Graphics]::FromImage($small); $g.InterpolationMode = 'HighQualityBicubic'
        $g.DrawImage($img, 0, 0, 1280, 720); $img.Dispose(); $prev = Join-Path $scratch "$Name-preview.png"; $small.Save($prev)
        "$png ($prev)"
    }
    'state' {
        $h = [TS2.Drive]::OpenProcess(0x0410, $false, $proc.Id)
        function Read-Bytes([long]$a, [int]$n) { $b = New-Object byte[] $n; $r = 0; [TS2.Drive]::ReadProcessMemory($h, [IntPtr]$a, $b, $n, [ref]$r) | Out-Null; $b }
        $b = Read-Bytes 0x0052F300 0xA0
        $pos = '{0},{1},{2}' -f [BitConverter]::ToInt32($b, 0), [BitConverter]::ToInt32($b, 4), [BitConverter]::ToInt32($b, 8)
        $demo = [BitConverter]::ToInt32((Read-Bytes 0x0052AD94 4), 0); $speed = [BitConverter]::ToInt32((Read-Bytes 0x0052F2D4 4), 0)
        # shared read: Get-Content returned stale line counts while ts2diag was writing
        $frames = try { $fs = [IO.File]::Open((Join-Path $diag 'frames.csv'), 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); @($sr.ReadToEnd() -split "`n" | Where-Object { $_ }).Count - 1; $sr.Close() } catch { -1 }
        "buzz pos $pos  health $([BitConverter]::ToInt16($b, 0x96))  lives $([BitConverter]::ToInt16($b, 0x9A))  coins $([BitConverter]::ToInt16($b, 0x9E))  demo $demo  speed $speed  frames $frames  foreground $(Test-GameForeground)"
    }
    'stop' { Get-Process toy2 -ErrorAction SilentlyContinue | Stop-Process -Force; 'stopped' }
}
