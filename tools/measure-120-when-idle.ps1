<#
Waits for a moment when nobody is using the PC, then runs tools\measure-120.ps1.
Runs only when the system has had no keyboard/mouse input for -IdleSeconds AND no window title contains
Meet / Zoom / Teams (a call may be on the game's monitor). Reads only the idle time and window titles.
A run whose focus log shows the game losing focus after having it (it pauses then), or where the presenter never
showed, is treated as disturbed and retried.
#>
param([int]$IdleSeconds = 60, [int]$MaxMinutes = 9, [int]$MaxAttempts = 3)
Add-Type -Namespace TS2 -Name Idle2 -MemberDefinition '[DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO p); public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }'
function Get-IdleSeconds { $li = New-Object TS2.Idle2+LASTINPUTINFO; $li.cbSize = 8; [TS2.Idle2]::GetLastInputInfo([ref]$li) | Out-Null; ([Environment]::TickCount - $li.dwTime) / 1000 }
function Test-CallOpen { @(Get-Process | Where-Object { $_.MainWindowTitle -match 'Meet|Zoom|Teams' }).Count -gt 0 }

$deadline = (Get-Date).AddMinutes($MaxMinutes)
for ($attempt = 1; $attempt -le $MaxAttempts; ) {
    if ((Get-Date) -gt $deadline) { "gave up after $MaxMinutes min: the PC was never idle for $IdleSeconds s without a call window"; exit 2 }
    if ((Get-IdleSeconds) -lt $IdleSeconds -or (Test-CallOpen)) { Start-Sleep -Seconds 5; continue }
    "attempt ${attempt}: idle $([int](Get-IdleSeconds)) s, no call window, running at $(Get-Date -Format HH:mm:ss)"
    $out = & (Join-Path $PSScriptRoot 'measure-120.ps1') 2>&1 | Out-String
    $out
    $focus = ($out -split "`n" | Where-Object { $_ -match '^focus:' }) -join ''
    $disturbed = ($focus -match 'NOT foreground' -and $focus -match '\d+\.\ds game foreground;.*NOT foreground') -or ($out -notmatch 'presenter shown over the game: True')
    if (-not $disturbed) { 'result: undisturbed run'; exit 0 }
    'result: disturbed run (focus changed or presenter not shown), waiting for the next idle moment'
    $attempt++
}
"no undisturbed run in $MaxAttempts attempts"; exit 3
