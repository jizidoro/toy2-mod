# tools — build, run and smoke-test the Toy Story 2 build

**Target:** the local install `C:\toy_story_2\game` (retail `toy2.exe` sha256 `023eb6a9…`) on this PC; the game
runs on DISPLAY1 (4K) through dgVoodoo. No auth. Full-screen runs take DISPLAY1 and play sound: ask first.
The game stops drawing whenever it loses focus after having had it, so runs are unreliable while someone uses the PC.

## Smoke battery (golden path first)
| Script | What it proves |
|---|---|
| `ts2-run.ps1 -KeepRunning -Seconds 0` then `ts2-drive.ps1 -Action focus / key / shot / state / stop` | golden path: mode select → menus → level gameplay; keys enter at the game's DirectInput read (ts2diag) because SendInput never reaches it; Buzz position / health / lives / coins read from game memory |
| `ts2-drive.ps1 -Action blur -Seconds N` | a window switch: the game must pause, stay visible and resume (check `game\ts2borderless.log`) |
| `ts2-run.ps1 -Seconds N -DemoFrames …` | frame-exact attract-demo shots + frame pacing (`planning\evidence\<tag>-frames.csv`) |
| `ts2-run.ps1 -BridgeCheckAt N` | 120 fps bridge transfer is lossless (source vs received frame) |
| `measure-120.ps1` / `measure-120-when-idle.ps1` | images/s reaching DISPLAY1 vs game frames/s (2.0 = Smooth Motion doubling); the idle variant waits until the PC is idle and no call window is open |
| `ts2-modeselect-shot.ps1` | captures the mode-select window (PrintWindow of that window only), e.g. to see a `pcbits\rezsel.bmp` mod |

## Components (each `build.cmd` needs VS2022 Build Tools; MinHook from `toy2-decomp\external`)
| Path | Builds / is |
|---|---|
| `ts2diag\` | `game\scripts\ts2diag.asi`: frame times, back-buffer shots (`shot.req`: `name`, `name@demo:N`, `name@burst:N` = N consecutive frames at quarter size, for flicker), module list, level-draw probe (`draws.csv`: each level shape shown/hidden with set, pass and Buzz position; `draws-summary.csv`: per second shapes drawn per pass, texture binds, textures loaded) |
| `ts2mods\` | `game\scripts\ts2mods.asi`: mod loader (`game\mods\README.txt`); install paths from the exe folder when `data\` and `cd\validate.tta` sit beside it (`ValidateInstall` 0x4A6390 hooked, the registry is not read) |
| `ts2fix60\` | `game\scripts\ts2fix60.asi`: 60 fps game-logic fixes reimplemented from AndetSTK's fork of ToyStory2Fix (GPLv3): ZurgFix 0x407F8E/0x407FB0, DiskFix 0x411099, TextureFix 0x4B300E; log `game\ts2fix60.log` |
| `ts2borderless\` | `game\scripts\ts2borderless.asi`: window switches never kill the game (no minimize, pause/resume, videos end instead of hanging, lost surfaces restored when a frame fails to start, keyboard enabled) |
| `ts2pad\` | `game\scripts\ts2pad.asi`: DualSense/DualShock enabled, PS1 button layout, d-pad as stick, right stick turns the camera |
| `ts2widescreen\` | `game\scripts\ts2widescreen.asi`: the sky/backdrop is drawn as a cylinder at infinity (narrow strips per viewing angle, `Queue2DSprite` 0x4B8CC0) so it turns exactly with the perspective world, and its tilt is scaled back to 4:3 (`UpdateParallax` 0x48F230); HUD and menus squeezed to 4:3 proportions around the centre (full-width quads and lens flares untouched; `hud=stretch` reverts live); characters/objects at any distance, sprites/effects to 4096 (0x447BFD/0x447C09, 0x43E728), detailed level shapes to the far plane (each distance preset's detailed far clip 0x508D2C/44/5C → 48000, so the coarse copies no longer replace them beyond 12000); level-tile UVs on atlas cell edges moved half a texel inward at level load (no neighbour-colour lines at tile joins; `Primitive::CreateAllVertexBuffers` 0x4B32E0); debug switches in `game\ts2debug.txt` (`set0=hide`, `set1=hide`, `portals=all`, `hud=stretch` read once a second; `uvinset=off` read at level load); log `game\ts2widescreen.log` |
| `ts2tex\` | `game\scripts\ts2tex.asi`: 32-bit textures, size cap 2048, dump (while `game\texdump\` exists), HD replacement from `mods\<pack>\textures\<key>.png`. `scan.py`: every texture from the data files, same key (`--check <dump dir>`: parity with the game). `upscale.py`: AI first pass (Real-ESRGAN, `vendor\realesrgan-ncnn-vulkan`) into `mods\hd-textures` |
| `present64\audiopeak.cpp` | a process's audio output peak level per second (no audio recorded) |
| `present64\stackpeek.cpp` | return addresses on a (WOW64) thread's stack: where the game is stuck |
| `present64\dijoy.cpp` | joysticks in the game's DirectInput order; with seconds, prints button presses of joystick 0 |
| `bridge\` | `game\scripts\ts2bridge.asi` + `game\ts2present64.exe`: 120 fps via Smooth Motion in a 64-bit presenter; a frame larger than the game window (dgVoodoo supersampling) is drawn scaled to the window (bilinear 2:1 = box average) so Smooth Motion keeps 120 |
| `nvprofile\ts2-smoothmotion.cpp` | NVIDIA driver profile `show / on [bars] / off  app=<exe>` |
| `present64\dispfps.cpp` | images/s on a monitor from Desktop Duplication metadata (no pixels read) |
| `present64\smtest64.cpp` | experiment: Smooth Motion on a 64-bit D3D11 window (60 → 120 images/s) |
| `dgconf.py` | set dgVoodoo keys by section, keeping the file's CRLF |
