<#
Launches toy2.exe, captures ONLY the "Screen Mode Select" window with PrintWindow (the window's own content,
never screen pixels), then closes the game. Used to verify mods that change the mode-select screen.
#>
param([string]$Out = (Join-Path (Split-Path $PSScriptRoot -Parent) 'planning\evidence\modeselect.png'))
$ErrorActionPreference = 'Stop'
$game = Join-Path (Split-Path $PSScriptRoot -Parent) 'game'
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace TS2 -Name MS -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
public struct RECT { public int L, T, R, B; }
'@
Get-Process toy2 -ErrorAction SilentlyContinue | Stop-Process -Force
$proc = Start-Process -FilePath (Join-Path $game 'toy2.exe') -WorkingDirectory $game -PassThru
$deadline = (Get-Date).AddSeconds(20)
do { Start-Sleep -Milliseconds 250; $proc.Refresh() } until ($proc.HasExited -or $proc.MainWindowTitle -eq 'Screen Mode Select' -or (Get-Date) -gt $deadline)
if ($proc.MainWindowTitle -ne 'Screen Mode Select') { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; throw "no mode-select window" }
Start-Sleep -Seconds 2
$r = New-Object TS2.MS+RECT; [TS2.MS]::GetClientRect($proc.MainWindowHandle, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
$ok = [TS2.MS]::PrintWindow($proc.MainWindowHandle, $hdc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
Stop-Process -Id $proc.Id -Force
if (-not $ok) { throw 'PrintWindow failed' }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"mode-select window $($bmp.Width)x$($bmp.Height) -> $Out"
