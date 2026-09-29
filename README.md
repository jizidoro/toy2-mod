# toy2-mod — Toy Story 2 (PC, 1999) in 4K at 120 fps

A set of small plugins and a tuned configuration that make the original PC release of
*Toy Story 2: Buzz Lightyear to the Rescue* run well on a modern Windows PC:

- **Resolution and speed:** 4K widescreen, 60 fps game logic, and **120 frames per second on screen**.
- **Image quality:** supersampled edges, crisp textures without seams, and full detail at every distance.
- **Presentation:** the sky turns correctly with the camera, and the HUD, menus and videos keep their 4:3 proportions.
- **Gameplay and devices:** fixes for gameplay that breaks at 60 fps, 3D headphone audio, and DualSense support.
- **Stability:** switching windows doesn't break the game.
- **Portability and mods:** a mod loader, and a portable install that doesn't need the registry.

Nothing in this repository is game content. You need your own copy of the game (the retail CD or its ISO).
The retail executable is never modified in code. RibShark's fixes locate their patch sites by the original bytes,
and every change here is applied at run time by plugins (`.asi` files).

Tested on Windows 11 with an NVIDIA RTX 5090 and a 3840×2160 240 Hz monitor.

---

## Contents
1. [What you get](#what-you-get)
2. [Requirements](#requirements)
3. [Install](#install)
4. [Playing and settings](#playing-and-settings)
5. [How it works — what we did](#how-it-works--what-we-did)
6. [Tried and ruled out](#tried-and-ruled-out)
7. [Troubleshooting](#troubleshooting)
8. [Repository layout and developer tools](#repository-layout-and-developer-tools)
9. [Credits and licences](#credits-and-licences)

---

## What you get

| Area | Result | Where |
|---|---|---|
| Resolution | Any resolution up to 4K, 32-bit colour, correct widescreen 3D | RibShark ToyStory2Fix + dgVoodoo |
| Frame rate | Game logic at a steady 60 fps; **120 images/s on screen** with NVIDIA Smooth Motion (measured 119–122/s) | `ts2bridge.asi` + `ts2present64.exe` |
| Anti-aliasing | 2× supersampling (renders 7680×4320, shown at 4K) | dgVoodoo + presenter scaling |
| Textures | Crisp point filtering with mipmaps; 32-bit textures; no coloured lines at tile edges | dgVoodoo, `ts2tex.asi`, `ts2widescreen.asi` |
| Draw distance | Detailed geometry to the horizon; characters, enemies and pickups at any distance; no prop fade-in | `ts2widescreen.asi` |
| Sky | The backdrop turns with the camera (a cylinder instead of a flat scroll) | `ts2widescreen.asi` |
| HUD, menus, videos | 4:3 proportions, centred, instead of stretched to 16:9 | `ts2widescreen.asi` |
| 60 fps gameplay | Zurg and flying enemies at their intended speed, working disk launcher, shiny-texture glitch fix | `ts2fix60.asi` |
| Audio | DirectSound 3D through OpenAL Soft with HRTF for headphones | DSOAL |
| Controller | DualSense/DualShock: PlayStation layout, d-pad, right stick turns the camera | `ts2pad.asi` |
| Window switching | No minimising, the game pauses and resumes, videos never hang, lost surfaces restored | `ts2borderless.asi` |
| Memory | Large Address Aware (4 GB instead of 2 GB) | one header flag (optional) |
| Mods | Replace any game file from `game\mods\<mod>\` | `ts2mods.asi` |
| Portable | Runs from any folder, no registry entry needed | `ts2mods.asi` |

## Requirements

- **The game:** the original *Toy Story 2* PC CD (or its ISO). The retail `toy2.exe` has SHA-256
  `023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0`.
- **Windows 10 or 11** and a GPU with Direct3D 11.
- **For 120 fps:** an NVIDIA RTX 40 or 50 series card with a driver that has *Smooth Motion*. Without it, the
  game runs at 60 fps and everything else still works.
- **To build the plugins:** *Visual Studio 2022 Build Tools* with the C++ workload (the `build.cmd` scripts call
  `vcvars32.bat` / `vcvars64.bat` from the default install path).
- **Third-party files** (download them yourself; the versions and hashes we run are listed):

| Component | Version | Files you need | SHA-256 of our copies |
|---|---|---|---|
| [ToyStory2Fix](https://github.com/RibShark/ToyStory2Fix/releases) by RibShark | v1.1.0 | `scripts\ToyStory2Fix.asi` (not its `dsound.dll`) | `01896b24…` |
| [dgVoodoo 2](http://dege.freeweb.hu/dgVoodoo2/) by Dege | 2.87.5 | `MS\x86\DDraw.dll`, `MS\x86\D3DImm.dll` | `612a2440…`, `93c534f2…` |
| [DxWrapper](https://github.com/elishacloud/dxwrapper/releases) by Elisha Riedlinger | v1.8.8600.25 | `Stub\winmm.dll`, `dxwrapper.dll` | `7c843006…`, `ec42e51c…` |
| [DSOAL](https://github.com/kcat/dsoal) + [OpenAL Soft](https://openal-soft.org/) by kcat | DSOAL (Wine dsound 5.3.1.904) + OpenAL Soft 1.24.3 | `dsound.dll`, OpenAL Soft's 32-bit DLL renamed `dsoal-aldrv.dll` | `62faaab1…`, `c731b8b1…` |
| [toy2-decomp](https://github.com/0danny/toy2-decomp) by 0danny | any recent | only for building: `external\include\minhook.h`, `external\libs\minhook_x86.lib` | — |
| [stb](https://github.com/nothings/stb) by Sean Barrett | stb_image 2.30, stb_image_write 1.16 | included in `vendor\stb\` (public domain), used by `ts2tex` | — |

## Install

The steps below build the same folder we run: `game\` next to `tools\`, inside this repository.

1. **Clone this repository**, and clone [toy2-decomp](https://github.com/0danny/toy2-decomp) into a folder named
   `toy2-decomp` inside it. The build scripts take MinHook from there.
   ```
   git clone https://github.com/jizidoro/toy2-mod.git
   cd toy2-mod
   git clone https://github.com/0danny/toy2-decomp.git
   ```
2. **Copy the game files from your CD/ISO** into `game\`. The configuration files already in `game\` stay:
   - everything in `Setup\Toy2\HD\` (`toy2.exe` and the `data` folder) goes to `game\`;
   - everything in `Setup\Toy2\CD\` (`audio`, `rtlibs`, `validate.tta`) goes to `game\cd\`.

   The result is `game\toy2.exe`, `game\data\…` and `game\cd\validate.tta`. With this layout the game finds its
   files by itself, and no installer or registry entry is needed.
3. **Add the third-party files** from the table above:
   - to `game\`: `DDraw.dll`, `D3DImm.dll`, `winmm.dll` (DxWrapper's stub), `dxwrapper.dll`, `dsound.dll` (DSOAL)
     and `dsoal-aldrv.dll`;
   - to `game\scripts\`: `ToyStory2Fix.asi`.

   `dgVoodoo.conf`, `dxwrapper.ini`, `alsoft.ini` and `scripts\ToyStory2Fix.ini` come with this repository.
4. **Build our plugins.** From the repository root, in a normal command prompt:
   ```
   for /d %d in (tools\*) do @if exist %d\build.cmd call %d\build.cmd
   ```
   Each `build.cmd` writes its output into the game folder:
   - the `.asi` plugins go to `game\scripts\`;
   - `ts2present64.exe` goes to `game\`.
5. **Choose your monitor.** In `game\dgVoodoo.conf`, `FullScreenOutput = 2` puts the game on our second display.
   Set it to your display's number (`0` = the primary one), or use `dgVoodooCpl.exe` from the dgVoodoo zip.
6. **High-DPI setting (only if Windows display scaling is above 100%).** Right-click `game\toy2.exe` →
   *Properties* → *Compatibility* → *Change high DPI settings*. Tick *Override high DPI scaling behaviour* and choose
   *Application*. Without it Windows may stretch the game's window as a scaled bitmap.
7. **Optional — lift the 2 GB memory limit.** In a *Developer Command Prompt for VS*, run:
   ```
   editbin /LARGEADDRESSAWARE /RELEASE game\toy2.exe
   ```
   This changes only a header flag and the checksum, not the code, and gives SHA-256 `e62e1576…`. Keep a copy of the
   original file.
8. **Optional — 120 fps (NVIDIA RTX 40/50).** Build the profile tool 32-bit, because it loads the 32-bit
   `nvapi.dll`. Then switch Smooth Motion on for the presenter:
   ```
   "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
   cl /nologo /O2 /EHsc tools\nvprofile\ts2-smoothmotion.cpp /Fe:tools\nvprofile\ts2-smoothmotion.exe
   tools\nvprofile\ts2-smoothmotion.exe on app=ts2present64.exe
   ```
   `ts2-smoothmotion.exe show app=ts2present64.exe` checks the setting, and `off` removes it.

## Playing and settings

Start `game\toy2.exe`. The game's *Screen Mode Select* window appears first: pick your resolution (for example
`3840 x 2160 x 32`). The 120 fps presenter starts by itself and sits over the game window while the game is in
front.

**Live switches.** Create `game\ts2debug.txt` with any of these lines to go back to the original behaviour:

| Line | Effect | When it is read |
|---|---|---|
| `hud=stretch` | HUD and menus stretched to 16:9 as the original does | at game start, and within a second during levels |
| `fmv=stretch` | Videos stretched to 16:9 | when a video starts |
| `uvinset=off` | No tile-edge seam fix | at the next level load |

**dgVoodoo settings** (`game\dgVoodoo.conf`, section `[DirectX]`):
- `Resolution = 2x`: supersampling. Set `unforced` to render at native resolution.
- `Filtering = pointmip`: crisp textures. `trilinear` gives the smooth look.
- `Mipmapping = autogen_bilinear`: keeps distant textures calm.
- `[DirectXExt] DepthBuffersBitDepth = forcemin24bit`: prevents distant z-fighting.

Forced MSAA stays off: it shows seams (see below).

**Audio** (`game\alsoft.ini`): set up for headphones (`stereo-mode=headphones`, HRTF). On speakers, set
`stereo-mode=speakers` and remove the `stereo-encoding=hrtf` line.

**Mods:** put replacement files in `game\mods\<mod name>\data\…` or `…\cd\…`, with the same paths as the
originals. `game\mods\README.txt` explains the load order, and `game\mods\ts2mods.log` lists every file the game
opens.

## How it works — what we did

The game is a 32-bit DirectX 6 title. dgVoodoo turns its DirectDraw/Direct3D calls into Direct3D 11, RibShark's
ToyStory2Fix makes 32-bit modes, widescreen 3D and a 60 fps timing possible, and DxWrapper loads our plugins. Our
plugins hook the retail code at addresses taken from
[toy2-decomp](https://github.com/0danny/toy2-decomp) and from our own disassembly. Each one checks the original bytes
before changing anything.

- **120 fps without touching the 60 fps logic.** NVIDIA's Smooth Motion frame generation ships only as a 64-bit driver
  layer, and the game is 32-bit. `ts2bridge.asi` copies every finished frame into a shared texture. The 64-bit
  `ts2present64.exe` presents it in a window laid exactly over the game, where Smooth Motion can run. Supersampled
  frames (7680×4320) are scaled down to the window in the presenter first. At 8K, Smooth Motion managed only
  64–92 images/s; at window size it keeps 120.
- **Detail at every distance.** Each level stores its scenery twice: a detailed copy and a coarse stand-in. The game
  draws the coarse copy beyond 10,000 units and clips the detailed pass at 12,000 units with an extra frustum plane
  (distance preset p[1], camera +0x54). RibShark's render-distance fix never touched that plane, so from afar you saw
  the rough copies: blocky walls, blurry fences, flat sand. `ts2widescreen` moves that plane to the far plane, so the
  detailed copy is drawn everywhere, and it wins the depth test wherever both copies exist.
- **No seams between tiles.** Level textures are 256×256 pages of 64×64 tiles packed with no gap. Near the camera,
  bilinear filtering at a tile's edge blended half a texel of the neighbouring tile, which showed as a coloured line.
  At level load, `ts2widescreen` moves every texture coordinate that sits on a tile boundary half a texel inward,
  before the geometry reaches its Direct3D buffer. Point filtering then removes the rest.
- **Characters and objects everywhere.** Actor visibility distances, the effect cull band, the prop fade table and the
  creature activation radius are all raised. Pickups get the raised effect band (4096)
  but not the unlimited range, because they are drawn without a wall test and would show through walls.
- **A sky that turns with the world.** The backdrop was a flat picture scrolled by the camera's yaw, which cannot
  follow a wide perspective view. It is now drawn as narrow strips, each at its screen column's viewing angle: a
  cylinder at infinity. The tilt is scaled back to match RibShark's widened camera.
- **4:3 HUD, menus and videos.** Every 2D quad passes one function. Quads that do not span the whole screen are
  squeezed to 4:3 around the centre; full-screen tints, fades and bars stay full width, and lens flares are left
  alone. Videos get a centred 4:3 rectangle with black bars.
- **60 fps gameplay fixes.** Reimplemented from AndetSTK's fork of ToyStory2Fix:
  - Zurg and other flying enemies moved 1/16 of the remaining distance per frame, twice as fast at 60 fps; the step
    now scales with the frame rate;
  - the disk launcher's homing truncated to zero at 60 fps and now turns as it did at 30;
  - shiny models' vertices get a tiny normal/UV nudge that avoids texture glitches.
- **Window switching.** dgVoodoo minimises the game and invalidates its surfaces when it loses focus.
  `ts2borderless` keeps the window, pauses and resumes cleanly, ends videos instead of hanging on them, and restores
  lost surfaces when a frame fails to start.
- **Portable install.** The game reads its install and CD paths from the registry in one function. When `data\` and
  `cd\validate.tta` sit beside the exe, `ts2mods` fills those paths from the exe folder and skips the registry.
- **Audio.** DSOAL gives the game's DirectSound 3D sounds to OpenAL Soft, which renders them with HRTF for headphones.
  DxWrapper loads the plugins through `winmm.dll`, so `dsound.dll` is free for DSOAL.

Every change was checked on the game's own frames: back-buffer captures, frame-exact attract-demo comparisons,
frame-time logs and displayed-frame counts. The record, with addresses and measurements, is in
`planning/ROADMAP.md`.

## Tried and ruled out

- **HD texture pack by AI upscaling.** The sources are 64×64 tiles, so upscaling adds no real detail. The harness in
  `ts2tex` can still dump and replace textures by content hash.
- **Forced MSAA.** It shows dashed seams along edges. They are T-junctions: 84% sit between separately placed pieces
  of geometry that share their mesh data (698 of 832 on Level 1). Repairing them would need per-instance geometry, so
  supersampling is used instead.
- **Forced 16× anisotropic filtering.** It causes dark bands where tiles join.
- **True 120 Hz game logic.** The time step is an integer (`1` per frame at 60 fps) read at 540 places in 131
  functions, with no single choke point. Smooth Motion gives 120 on screen instead.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Toy Story 2 is not correctly installed" | Check the layout: `game\data\` and `game\cd\validate.tta` next to `toy2.exe` |
| The game opens on the wrong monitor | `FullScreenOutput` in `game\dgVoodoo.conf` |
| After a config edit the game opens on the primary display in a small window | A value in `dgVoodoo.conf` is invalid, so dgVoodoo fell back to its defaults: undo the edit |
| No 120 fps | NVIDIA RTX 40/50 needed; run `ts2-smoothmotion.exe show app=ts2present64.exe`; `game\ts2present64.log` should say `NvPresent64.dll LOADED` |
| The attract demo never starts | It needs about 2 minutes without any input at the title screen; a connected controller can keep resetting that |
| Sound is strange on speakers | `game\alsoft.ini`: `stereo-mode=speakers` |
| Anything else | Logs: `game\*.log`, `game\ts2diag\`, `game\mods\ts2mods.log` |

## Repository layout and developer tools

```
game\                  configuration only (dgVoodoo.conf, dxwrapper.ini, alsoft.ini, scripts\*.ini, mods\README.txt)
tools\<plugin>\        source + build.cmd for each plugin: ts2widescreen, ts2fix60, ts2mods, ts2borderless, ts2pad,
                       ts2tex, ts2diag, bridge (ts2bridge.asi + ts2present64.exe)
tools\*.ps1            ts2-run (launch, pick a mode, frame stats, frame-exact demo shots), ts2-drive (focus, keys,
                       back-buffer shots, state), measure-120 (displayed vs game fps)
tools\present64\       dispfps (displayed images per second, metadata only), audiopeak, stackpeek, dijoy
tools\nvprofile\       ts2-smoothmotion (NVIDIA driver profile)
planning\              ROADMAP.md (what is live, with evidence), plans
```

`ts2diag.asi` is the test instrument. It records frame times (`game\ts2diag\frames.csv`) and captures back-buffer
images on request (`shot.req`: `name`, `name@demo:N`, `name@burst:N`). It also logs which level shapes are drawn
(`draws.csv`). All images come from the game's own back buffer, never the screen.

## Credits and licences

- **ToyStory2Fix** by RibShark (GPLv3), and **AndetSTK**'s fork, whose three 60 fps fixes `ts2fix60` reimplements.
- **dgVoodoo 2** by Dege (freeware).
- **DxWrapper** by Elisha Riedlinger (redistribution with attribution).
- **DSOAL** and **OpenAL Soft** by Chris Robinson (kcat), LGPL.
- **MinHook** by Tsuda Kageyu (BSD 2-clause), taken from **toy2-decomp** by 0danny, whose function map and structs
  made this work possible.
- **stb_image / stb_image_write** by Sean Barrett (public domain), included in `vendor\stb\`.
- NVIDIA NvAPI profile access follows Orbmu2k's **nvidiaProfileInspector**.

*Toy Story 2* is © Disney/Pixar; the game is by Traveller's Tales and Activision. This project contains no game
content and is not affiliated with them.

The licence for this repository's own code has not been chosen yet. `ts2fix60` reimplements GPLv3 code, so a
GPLv3-compatible licence fits.
