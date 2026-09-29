# Toy Story 2 (PC, 1999) — local play and improvement workspace

State: `planning/ROADMAP.md`, read with /checkin, written with /update-map; plans from `planning/TEMPLATE.md`.
Local git repo (no remote; push only with the owner's OK): `.gitignore` is an allow-list of our sources, configs and
records; game content, the retail exe, the ISO, vendor binaries, captures and the nested repos never go in.

- `game\` — the playable install (`toy2.exe` = retail `023eb6a9…` with only the Large Address Aware header flag and
  checksum set: 4 header bytes, sha256 `e62e1576…`, identical to the community repack's exe; owner asked 2026-09-28 to
  lift the 2 GB limit. The retail original stays in `iso\Setup\Toy2\HD\toy2.exe`) plus:
  - RibShark ToyStory2Fix (`scripts\ToyStory2Fix.asi`); the ASI loader is DxWrapper v1.8.8600.25: stub `winmm.dll` + `dxwrapper.dll` + `dxwrapper.ini` (the old 2018 build is in `vendor\dxwrapper-1.0.2383.20\`)
  - DSOAL (`dsound.dll`, `dsoal-aldrv.dll` = OpenAL Soft, `alsoft.ini`: headphones + HRTF), from the community repack
  - dgVoodoo 2.87.5 (`DDraw.dll`, `D3DImm.dll`, `dgVoodoo.conf`: 2x supersampling, point filtering with auto mipmaps (`pointmip`, owner's choice), 24-bit depth)
  - our ASIs in `scripts\`: `ts2mods.asi` (mod loader, `mods\README.txt`; install paths from the exe folder, no registry), `ts2bridge.asi` + `ts2present64.exe` (120 fps via Smooth Motion in a 64-bit presenter; supersampled frames are scaled to the window there, else Smooth Motion drops to ~70/s), `ts2borderless.asi` (window switch: no minimize, pause/resume, videos never hang, lost surfaces restored at frame start, keyboard enabled), `ts2pad.asi` (DualSense: enabled, PS1 layout, d-pad, right stick camera), `ts2tex.asi` (32-bit textures, texture dump, HD replacement; the AI pack is switched off, moved to `work\texpack\hd-textures-off`), `ts2widescreen.asi` (backdrop drawn as a cylinder so it turns with the perspective world; tilt matched to RibShark's widened camera; characters/objects at any distance, effects farther, detailed level shapes to the far plane instead of the coarse copies beyond 12000, tile UVs inset from atlas cell edges; HUD, menus and videos at 4:3 proportions, `hud=stretch` / `fmv=stretch` in `ts2debug.txt` revert), `ts2fix60.asi` (60 fps fixes from AndetSTK's fork: Zurg/flying enemies, disk launcher, shiny textures), `ts2diag.asi` (test instrument)
  - the game's focus is fragile: dgVoodoo minimizes it and invalidates its surfaces on focus loss, and the input devices are enabled only if their startup Acquire succeeds. Test any window/input change with `ts2-drive.ps1 -Action blur`.
- `iso\` — pristine files extracted from the ISO on the Desktop; the reference for diffs and hashes.
- `vendor\` — third-party downloads and references with recorded hashes: dgVoodoo 2.78 (rollback) and 2.87.5, the community repack's non-data files (`repack-reference\`).
- `toy2-decomp\` — function/global addresses and structs for this exact exe (reccmp `// FUNCTION: TOY2 0x…` markers). The decomp types some fields as unsigned where the binary compares signed; check the disassembly before trusting a comparison.
- `ToyStory2Fix\` — RibShark's source; each fix locates its patch site by searching the exe for the original instruction bytes, so `toy2.exe`'s code must stay unmodified (the LAA header flag is outside the code).
- `tools\` — each component has its source and `build.cmd` (VS2022 Build Tools; MinHook from `toy2-decomp\external`):
  - `ts2mods\`, `bridge\`, `ts2borderless\`, `ts2pad\`, `ts2tex\`, `ts2fix60\`, `ts2diag\`: the ASIs above; `ts2tex\scan.py` / `upscale.py` build the texture pack (sources in `work\texpack\src`, keyed by content hash because every level names its textures texN)
  - `ts2-drive.ps1`: drive a running game (focus / blur / key / probe / shot / state / stop). Keys go in at the game's DirectInput read via ts2diag, because SendInput never reaches its DirectInput 7 keyboard
  - `present64\`: `dispfps`, `audiopeak`, `stackpeek` (where a thread is stuck), `dijoy` (joysticks as the game sees them)
  - `nvprofile\ts2-smoothmotion.exe`: NVIDIA driver profile, `show | on | off  app=<exe>`
  - `present64\smtest64.cpp`: the experiment showing Smooth Motion engages for 64-bit D3D11
  - `ts2-run.ps1`: launch, pick a mode, frame stats, frame-exact demo shots, `-BridgeCheckAt`
  - `ts2-modeselect-shot.ps1`, `dgconf.py`
- Testing: images come only from the game's own back buffer or the presenter's received frame, never screen pixels; ask before full-screen runs. The game stops drawing whenever it isn't the foreground app, so runs are unreliable while the owner uses the PC (`ts2-run` prints focus changes). The attract demo starts only after ~2 min without input at the title: a connected DualSense (ts2pad) keeps resetting that, so frame-exact demo runs set `scripts\ts2pad.asi` aside and put it back after. One invalid value in `dgVoodoo.conf` makes dgVoodoo fall back to its defaults (game on the primary display, 640x480 window after a switch): change it only with a script that checks the line it replaces.
