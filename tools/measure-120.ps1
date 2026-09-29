<#
Measures the 120 fps setup: launches the game (bridge + Smooth Motion presenter) for 30 s and counts how many
new images reach DISPLAY1 per second (tools\present64\dispfps.exe: Desktop Duplication METADATA only, no pixels
are read), next to how many frames the game itself rendered per second (ts2diag).
Leave the PC untouched for the 30 s: the game pauses whenever it loses focus.
Calibration (2026-09-28, 64-bit test app at 60 fps): Smooth Motion off -> 60 images/s, on -> 119-121 images/s.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$disp = Join-Path $PSScriptRoot 'present64\dispfps.exe'
$out = Join-Path $env:TEMP 'ts2-dispfps.txt'
& (Join-Path $PSScriptRoot 'nvprofile\ts2-smoothmotion.exe') show app=ts2present64.exe | Select-String 'Enable  '
$j = Start-Process -FilePath $disp -ArgumentList '32' -NoNewWindow -RedirectStandardOutput $out -PassThru
# The game pauses while it is not in front (ts2borderless) and the presenter shows only over the active game, so
# the game is brought to the front (ts2-drive focus) right after mode select, as a player clicking into it would.
& (Join-Path $PSScriptRoot 'ts2-run.ps1') -Tag measure120 -Seconds 0 -KeepRunning | Out-Null
& (Join-Path $PSScriptRoot 'ts2-drive.ps1') -Action focus
$j.WaitForExit()
& (Join-Path $PSScriptRoot 'ts2-drive.ps1') -Action stop | Out-Null
# the presenter exits with the game but holds its log open until then
Get-Process ts2present64 -ErrorAction SilentlyContinue | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
$shown = Select-String -Path (Join-Path $root 'game\ts2present64.log') -Pattern 'shown over the game' -Quiet
$displayed = ((Get-Content $out | Select-String 'per second:').Line -split ':')[1].Trim() -split ' ' | ForEach-Object { [int]$_ }
# the game's own frame log (ts2diag); ts2-run copies it to planning\evidence only when it closes the game itself, and
# this script keeps the game running, so read the live file (the game keeps it open: shared read)
$fs = [IO.File]::Open((Join-Path $root 'game\ts2diag\frames.csv'), 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $framesCsv = $sr.ReadToEnd(); $sr.Close()
$game = @(($framesCsv -split "`r?`n" | Where-Object { $_ }) | ConvertFrom-Csv | Group-Object { [Math]::Floor([double]$_.t_ms / 1000) } | ForEach-Object { $_.Count })
"presenter shown over the game: $shown"
"DISPLAY1 images/s: $($displayed -join ' ')"
"game frames/s:     $($game -join ' ')"
# the splash screens render at 60 fps: compare the best 5-second stretch of each
$best = 0; for ($i = 0; $i + 5 -le $displayed.Count; $i++) { $s = ($displayed[$i..($i + 4)] | Measure-Object -Sum).Sum / 5; if ($s -gt $best) { $best = $s } }
$bestGame = 0; for ($i = 0; $i + 5 -le $game.Count; $i++) { $s = ($game[$i..($i + 4)] | Measure-Object -Sum).Sum / 5; if ($s -gt $bestGame) { $bestGame = $s } }
"best 5 s: DISPLAY1 {0:N1} images/s vs game {1:N1} frames/s -> ratio {2:N2} (2.0 = frame generation doubling)" -f $best, $bestGame, ($(if ($bestGame) { $best / $bestGame } else { 0 }))
