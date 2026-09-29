<#
Launches toy2.exe, picks a display mode on the game's "Screen Mode Select" window, lets the game run
(no in-game input is sent), then closes it. Frame times and screenshots come from game\scripts\ts2diag.asi
(tools\ts2diag), which hooks the game's PresentFrame: screenshots are the game's own back buffer, never
screen pixels.

Keys are posted to the mode-select window only (its WndProc stores WM_KEYDOWN in g_keyDown), so nothing
can reach another application. The selected mode is read from game memory, not guessed from pixels:
  g_selectionState           0x0053C5B0  0 = driver, 1 = render method, 2 = resolution, 4 = committed
  g_ddAppSelectedDisplayMode 0x0053C574  -> DisplayMode, modeText at +0x7C (e.g. "3840 x 2160 x 32")
(addresses from toy2-decomp src/ModeSelect.cpp, retail exe sha256 023eb6a9...)
#>
param(
    [string]$Mode = '3840 x 2160 x 32',
    [int]$Seconds = 60,
    [int[]]$ShotsAt = @(),
    [int[]]$DemoFrames = @(),       # frame-exact shots of the attract demo (same image in every run)
    [int]$BridgeCheckAt = 0,        # 120 fps bridge: at this second, save one frame at its source and as received by the presenter
    [switch]$PresenterForceShow,    # 120 fps bridge: show the presenter even while the game lacks focus (measurement runs)
    [switch]$KeepRunning,           # leave the game running after -Seconds (driven further by tools\ts2-drive.ps1)
    [int]$StatsFrom = 10,
    [string]$Tag = 'run'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = Join-Path $root 'game'
$diag = Join-Path $game 'ts2diag'
$evidence = Join-Path $root 'planning\evidence'
New-Item -ItemType Directory -Force $evidence | Out-Null
if (-not (Test-Path (Join-Path $game 'scripts\ts2diag.asi'))) { throw 'game\scripts\ts2diag.asi missing: run tools\ts2diag\build.cmd' }

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace TS2 -Name Native -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(int access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr h, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr hwnd, uint msg, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern int GetWindowLong(System.IntPtr hwnd, int index);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
public struct RECT { public int L, T, R, B; }
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint pid);
'@

function Read-Bytes($h, [long]$addr, [int]$n) {
    $buf = New-Object byte[] $n; $read = 0
    if (-not [TS2.Native]::ReadProcessMemory($h, [IntPtr]$addr, $buf, $n, [ref]$read)) { return $null }
    $buf
}
function Read-Int($h, [long]$addr) { $b = Read-Bytes $h $addr 4; if ($b) { [BitConverter]::ToInt32($b, 0) } }
function Read-ModeText($h) {
    $p = Read-Int $h 0x0053C574
    if (-not $p) { return '' }
    $b = Read-Bytes $h ($p + 0x7C) 40
    if (-not $b) { return '' }
    [Text.Encoding]::ASCII.GetString($b).Split([char]0)[0]
}
function Send-Key($hwnd, [int]$vk) { [TS2.Native]::PostMessage($hwnd, 0x100, [IntPtr]$vk, [IntPtr]::Zero) | Out-Null }

# Records whether toy2 is the foreground app (process ids only, nothing else) and every change, so a run
# disturbed by someone using the PC can be told apart. Windows usually keeps a process started from a
# background console out of the foreground; the game renders anyway, so "not foreground" alone is normal.
$script:focusLog = @(); $script:hadFocus = $null
function Wait-Tracked([double]$seconds) {
    $until = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $until) {
        $fgPid = 0; [TS2.Native]::GetWindowThreadProcessId([TS2.Native]::GetForegroundWindow(), [ref]$fgPid) | Out-Null
        $has = $fgPid -eq $proc.Id
        if ($has -ne $script:hadFocus) { $script:focusLog += ('{0:N1}s {1}' -f ((Get-Date) - $start).TotalSeconds, $(if ($has) { 'game foreground' } else { 'game NOT foreground' })); $script:hadFocus = $has }
        Start-Sleep -Milliseconds 250
    }
}

Get-Process toy2 -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item (Join-Path $game 'toy2.err') -ErrorAction SilentlyContinue
Remove-Item $diag -Recurse -Force -ErrorAction SilentlyContinue
$proc = Start-Process -FilePath (Join-Path $game 'toy2.exe') -WorkingDirectory $game -PassThru
$h = [TS2.Native]::OpenProcess(0x0410, $false, $proc.Id)   # PROCESS_VM_READ | PROCESS_QUERY_INFORMATION

$deadline = (Get-Date).AddSeconds(20)
do { Start-Sleep -Milliseconds 250; $proc.Refresh() } until ($proc.HasExited -or $proc.MainWindowTitle -eq 'Screen Mode Select' -or (Get-Date) -gt $deadline)
if ($proc.MainWindowTitle -ne 'Screen Mode Select') { throw "no mode-select window (title '$($proc.MainWindowTitle)', exited=$($proc.HasExited))" }
$msHwnd = $proc.MainWindowHandle

# driver (0) and render method (1): accept the defaults
foreach ($state in 0, 1) {
    $t = (Get-Date).AddSeconds(3)
    while ((Read-Int $h 0x0053C5B0) -eq $state -and (Get-Date) -lt $t) { Send-Key $msHwnd 0x20; Start-Sleep -Milliseconds 300 }
}
if ((Read-Int $h 0x0053C5B0) -ne 2) { throw "mode select did not reach the resolution list (state $(Read-Int $h 0x0053C5B0))" }

# resolution (2): step down until the wanted mode is selected
$seen = @()
for ($i = 0; $i -lt 120; $i++) {
    $text = Read-ModeText $h
    if ($text -eq $Mode) { break }
    if ($seen.Count -and $seen[0] -eq $text) { throw "mode '$Mode' not in list: $($seen -join ' | ')" }
    $seen += $text
    Send-Key $msHwnd 0x28
    $t = (Get-Date).AddMilliseconds(800)
    while ((Read-ModeText $h) -eq $text -and (Get-Date) -lt $t) { Start-Sleep -Milliseconds 30 }
}
if ((Read-ModeText $h) -ne $Mode) { throw "could not select '$Mode'; list seen: $($seen -join ' | ')" }
Send-Key $msHwnd 0x20
"selected: $Mode (after $($seen.Count) steps)"

$start = Get-Date
if ($PresenterForceShow) { Set-Content -Path (Join-Path $diag 'present.forceshow') -Value '1' }
$req = Join-Path $diag 'shot.req'
foreach ($n in $DemoFrames) { Add-Content -Path $req -Value "$Tag-demo$n@demo:$n" }
# the 120 fps presenter (ts2present64.exe, if the bridge is installed) saves the frame it received for the same demo frames
foreach ($n in $DemoFrames) { Add-Content -Path (Join-Path $diag 'present.req') -Value "$Tag-presenter-demo$n@demo:$n" }
foreach ($at in $ShotsAt) {
    $wait = $at - ((Get-Date) - $start).TotalSeconds
    if ($wait -gt 0) { Wait-Tracked $wait }
    if ($proc.HasExited) { break }
    Add-Content -Path $req -Value "$Tag-${at}s"
}
if ($BridgeCheckAt -gt 0) {
    $wait = $BridgeCheckAt - ((Get-Date) - $start).TotalSeconds
    if ($wait -gt 0) { Wait-Tracked $wait }
    Set-Content -Path (Join-Path $diag 'bridge.req') -Value "$Tag-transfer" -NoNewline
}
$rest = $Seconds - ((Get-Date) - $start).TotalSeconds
if ($rest -gt 0) { Wait-Tracked $rest }

if ($KeepRunning) { "game left running: pid $($proc.Id)"; return }
"alive after ${Seconds}s: $(-not $proc.HasExited)"
if (-not $proc.HasExited) {
    $proc.Refresh(); $hw = $proc.MainWindowHandle; $r = New-Object TS2.Native+RECT
    [TS2.Native]::GetWindowRect($hw, [ref]$r) | Out-Null
    $style = [TS2.Native]::GetWindowLong($hw, -16); $ex = [TS2.Native]::GetWindowLong($hw, -20)
    "window: {0}x{1} at {2},{3}  style 0x{4:X8} (caption {5}, thickframe {6})  exstyle 0x{7:X8} (topmost {8})" -f ($r.R - $r.L), ($r.B - $r.T), $r.L, $r.T, $style, [bool]($style -band 0x00C00000), [bool]($style -band 0x00040000), $ex, [bool]($ex -band 0x8)
}
# The game takes focus shortly after it starts; only changes after the first 3 s mean someone used the PC.
$late = @($script:focusLog | Where-Object { [double]($_ -split 's ')[0] -gt 3 })
"focus: $($script:focusLog -join '; ')$(if ($late.Count) { '  <- changed during the run: someone used the PC' })"
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Milliseconds 500 }
$err = Join-Path $game 'toy2.err'
if (Test-Path $err) { "toy2.err: $((Get-Content $err) -join ' / ')" }
if (Test-Path "$diag\ts2diag.log") { Get-Content "$diag\ts2diag.log" | ForEach-Object { "ts2diag: $_" } }

foreach ($bmp in Get-ChildItem $diag -Filter *.bmp -ErrorAction SilentlyContinue) {
    $png = Join-Path $evidence ($bmp.BaseName + '.png')
    $img = [System.Drawing.Image]::FromFile($bmp.FullName)
    $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()
    "shot: $png"
}

$csv = Join-Path $diag 'frames.csv'
if (Test-Path $csv) {
    Copy-Item $csv (Join-Path $evidence "$Tag-frames.csv") -Force
    $rows = @(Import-Csv $csv | Where-Object { [double]$_.t_ms -ge $StatsFrom * 1000 })
    if ($rows.Count -gt 1) {
        $dts = @($rows | ForEach-Object { [double]$_.dt_ms } | Sort-Object)
        $span = ([double]$rows[-1].t_ms - [double]$rows[0].t_ms) / 1000
        $p99 = $dts[[int][Math]::Floor($dts.Count * 0.99) - 1]
        "frames after ${StatsFrom}s: $($rows.Count) over {0:N1}s, avg {1:N1} fps, 1% low {2:N1} fps, worst frame {3:N1} ms" -f $span, ($rows.Count / $span), (1000 / $p99), $dts[-1]
    } else { "frames after ${StatsFrom}s: none" }
} else { 'frames.csv: missing (ts2diag not loaded?)' }
