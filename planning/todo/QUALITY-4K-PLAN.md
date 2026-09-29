# Plan: verified 4K play with the best image quality

**Row:** O1 (verified 4K play session) + O2 (best image quality) · **Ticket:** none (local project) · **State:** in progress. Approved 2026-09-28 (owner: run now, monitor = 4K DISPLAY1, PresentMon approved, then replaced by ts2diag at the owner's choice). Steps 1–4b done; open: AC4 owner playtest, SSAA eye check, `/adversarial`, `/smoke`.
**Requested by / date:** jhonatan, 2026-09-28: "continue with the improvements we need to have the best version ever possible"

## 1. Hypothesis
We believe **running the unmodified retail `toy2.exe` at 3840×2160×32 with RibShark ToyStory2Fix,
and replacing dgVoodoo 2.78 with 2.87.5 set to `Antialiasing = 8x`, `Filtering = 16`,
`ForceVerticalSync = true`** will produce **a stable 60 fps 4K image with smooth polygon edges and
sharp textures at a distance** because **dgVoodoo implements the game's DirectDraw / Direct3D IM
(`game\DDraw.dll` + `game\D3DImm.dll`, which the game loads instead of the system copies) on
D3D11/12, and applies these `[DirectX]` settings there; RibShark's `FixFramerate` already caps the
simulation at 60 Hz, so vsync on the 240 Hz monitor adds no speed change.**

**Falsifiers:**
- The game crashes, shows a black screen, or aborts (`toy2.err` appears) after picking 3840×2160×32
  under dgVoodoo → remove `DDraw.dll`/`D3DImm.dll` and retry on native DirectDraw. If native works,
  dgVoodoo is the fault: try 2.78 vs 2.87.5, and drop dgVoodoo if both fail.
- Forced 8x MSAA produces seams, lines or halos on HUD sprites and text → 4x, then `appdriven`.
  Image quality then comes from `Resolution = 2x` (supersampling) instead.
- Level 1 falls below 60 fps at 4K with 8x → lower MSAA. The simulation is capped at 60 Hz, so
  any drop means the renderer is the bottleneck.
- 2.87.5 renders anything worse than 2.78 at the same settings → roll back to 2.78 and keep the
  settings.

## 2. Feasibility, derived from the row
| Question | Answer | Evidence |
|---|---|---|
| Do we know WHERE to change? | yes | `game\DDraw.dll`, `game\D3DImm.dll`, `game\dgVoodoo.conf` `[DirectX]` keys (values documented in the conf: `Antialiasing` off/appdriven/2x/4x/8x/16x; `Filtering` 1–16 = anisotropic; `Resolution` unforced / `2x` / `max…`) |
| Is every requirement MAPPED? | missing: owner OK for full-screen runs; monitor choice; dgVoodoo 2.87.5 download | ROADMAP O1 / O2 rows and the "Known gaps" table |
| Is there a TOOL to apply and observe it? | adapt | window-only capture (`shot.ps1`, uses the window rect only); `toy2.err` / `dsound-toy2.log`; fps: PresentMon (new download, owner decides) or eyeball + capture |

**Level:** MEDIUM. The unknowns are bounded owner decisions (OK to run, which monitor, fps tool) plus one download. The mechanism is config-only.

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. dgVoodoo 2.87.5, forced 8x MSAA + 16x AF + vsync | newest fixes; all quality at the D3D11/12 layer; no code | new third-party binary; AV false positives noted in its release | chosen |
| B. Keep dgVoodoo 2.78 with the same settings | already in hand | four years of fixes missing | fallback if A regresses |
| C. Native DirectDraw + NVIDIA control-panel MSAA/AF | no wrapper | driver overrides do not reliably reach the legacy DX6 path; Windows' D3DIM emulation is the weakest link | rejected |
| D. DxWrapper features (already present as `dsound.dll`) | no new file | would be a third rendering layer next to dgVoodoo and Windows | rejected |
| E. Supersampling via `Resolution = 2x` (renders 7680×4320, downsamples to 4K) | best edge and texture shimmer quality; works where MSAA breaks 2D | 4× pixel cost; untested with this game's 2D sprites | try after A if A's image still aliases or MSAA breaks sprites |

## 4. Edge cases
- **Monitor:** dgVoodoo `FullScreenOutput = default` means the primary ultrawide (5120×2160). 3840×2160 there would be scaled or pillarboxed. Either set `FullScreenOutput` to the 4K output's ordinal, or pick 5120×2160 in the game. The owner chooses; 21:9 makes the HUD stretch (O3) worse.
- **Config version:** the 2.78 conf has `Version = 0x278`. 2.87.5 may migrate or ignore unknown keys. Start from the conf its `dgVoodooCpl.exe` writes, re-apply our keys, and diff.
- **Antivirus:** Defender may quarantine new dgVoodoo DLLs. Check that the files exist after extraction, before the run.
- **Alt-tab / exit:** a full-screen 1999 game may not restore cleanly. The run ends with `Stop-Process` on the pid and a desktop-resolution check.
- **Owner on a call:** ask before each full-screen session. The game takes a monitor and plays sound.
- **Watermark:** keep `dgVoodooWatermark = false`.

## 5. Acceptance criteria (observations)
- AC1: `Get-FileHash game\DDraw.dll, game\D3DImm.dll` equals the x86 `MS\` DLLs inside `dgVoodoo2_87_5.zip`.
- AC2: after picking 3840×2160×32, the game window rect reports `3840x2160`, level 1 renders in a window-only capture, and no `toy2.err` appears within 5 minutes.
- AC3: 1:1 crops of the same scene, baseline (2.78 `appdriven`) vs final, show smoothed polygon edges and sharper ground textures at distance. Both PNGs are saved under `planning/evidence/`.
- AC4 (if PresentMon approved): 60 s in level 1, average ≥ 59 fps and 1% low ≥ 55 fps. Otherwise: owner playtest reports no stutter.
- AC5: HUD / text / menus show no MSAA artifacts in the capture (seams, halos, missing glyphs).

## 6. Where to change
- `game\DDraw.dll`, `game\D3DImm.dll`: replace with 2.87.5 (x86, `MS\` folder of the release zip).
- `game\dgVoodoo.conf`: `[DirectX] Antialiasing = 8x`, `Filtering = 16`, `ForceVerticalSync = true`; `[General] FullScreenOutput` per the owner's monitor choice.
- `vendor\dgVoodoo-2.78\`: the current three files, kept for rollback.
- Surface costs the owner decides: **new third-party binary version** (dgVoodoo 2.87.5, GitHub `dege-diosg/dgVoodoo2`); **optional PresentMon download** for fps numbers. No env vars, no code, no gates.

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | Baseline (O1): current setup (2.78, `appdriven`); full-screen 3840×2160×32 on the chosen monitor; play level 1 ~2 min | game runs; note HUD stretch, fps feel, music, FMV intro, pad | window-only capture → `planning/evidence/baseline.png`; process alive; no `toy2.err` |
| 2 | Download `dgVoodoo2_87_5.zip`; extract `MS\x86\DDraw.dll`, `D3DImm.dll`, `dgVoodooCpl.exe` | files present, not quarantined | hashes recorded |
| 3 | Move 2.78 files to `vendor\dgVoodoo-2.78\`; drop in 2.87.5; keep settings `appdriven` | same image as baseline | AC1, AC2 |
| 4 | Set `Antialiasing = 8x`, `Filtering = 16`, `ForceVerticalSync = true` | smoother edges, crisp distance textures, 60 fps | AC3, AC4, AC5 |
| 5 | If AC5 fails: 4x, then `appdriven` + `Resolution = 2x`. If AC4 fails: step MSAA down | artifacts or fps recovered | re-run AC3–AC5 |
| 6 | `/update-map`: O1 and O2 rows with evidence; O3 / O4 / O6 / O7 rows updated from what step 1 observed | roadmap current | rows cite the capture files |

### Step results
- **Step 1 (2026-09-28, 17:06): partial.** `tools\ts2-run.ps1 -Tag baseline`: selected `3840 x 2160 x 32` from game memory after 33 steps; dgVoodoo 2.78 with `FullScreenOutput = 2` put the game window at `-3840,0 3840x2160` (DISPLAY1); alive after 60 s; no `toy2.err`. At 35 s the window was minimized (focus loss), restored by 55 s.
  - **Fail, capture:** the rect-limited screen copy returned the owner's desktop (a video call on DISPLAY1), not the game. dgVoodoo's full-screen output is not composited. Images deleted; the script now uses `PrintWindow(PW_RENDERFULLCONTENT)`, which can only return the game window's own content or black.
  - **Fail, fps:** PresentMon wrote no CSV. Cause not yet known: elevation for ETW, or the quoted `--output_file` path.
  - Not observed: HUD, FMV, audio, gameplay image. Needs a run while DISPLAY1 is free.
- **Step 1 re-run: pass (2026-09-28).** Plan change: PresentMon needs admin or the "Performance Log Users" group ("failed to start trace session: access denied"), so it was removed (`vendor\presentmon` deleted). The owner chose option (c), `game\scripts\ts2diag.asi` (source `tools\ts2diag\`), which hooks `PresentFrame` 0x004ABD40 for per-frame times and saves the back buffer on request. `tools\ts2-run.ps1 -Tag baseline-long -Seconds 150`:
  - render `3840x2160`, fullscreen=1, alive 150 s, no `toy2.err`
  - sequence: splash / ESRB (60 fps) → intro FMV (~25 fps; plays, but pixelated and stretched to 16:9) → title → copyright → attract demo at a flat 30 fps (33.3 ms, RibShark forces 30 in demo mode)
  - load hitches 1.2–2.4 s
  - evidence: `planning/evidence/baseline-long-*.png`, `baseline-long-frames.csv`
  - observed: 3D aspect correct (RibShark widescreen); polygon edges aliased; 4:3 2D art (ESRB, title) stretched to 16:9 → O3 confirmed
  - gameplay fps not measurable without input (the demo is capped at 30) → AC4 needs an owner playtest
  - audio not observable from here
- **Step 2: pass.** `dgVoodoo2_87_5.zip` sha256 `5ffde692…` = GitHub asset digest; extracted `MS\x86\DDraw.dll` (`612a2440…`), `D3DImm.dll` (`93c534f2…`), root `dgVoodooCpl.exe` (x86) to `vendor\dgVoodoo-2.87.5\`; not quarantined.
- **Step 3: pass, after one fix.** The conf is 2.87.5's default with 7 keys carried from the 2.78 conf: `Adapters = 1`, `FullScreenOutput = 2`, `dgVoodooWatermark = false`, `VideoCard = ati_radeon_8500`, `FastVideoMemoryAccess = true`, `Default3DRenderFormat = argb8888`, `DitheringEffect = pure32bit`. AC1 ✔ (game DLL hashes = 2.87.5).
  - The first run (`v2875-default`) captured 3D frames (title, demo) as black; FMV and text frames were identical to the baseline (mean diff 0.00).
  - With `FastVideoMemoryAccess = false` (the 2.87.5 default), the title and demo capture correctly (`v2875-fvma-off-*.png`); pacing is unchanged (36.1 avg / 28.7 1% low vs baseline 37.6 / 28.5).
  - Kept at default `false`. AC2 ✔ (render 3840×2160, alive 140 s, no `toy2.err`).
  - Owner added mid-run (2026-09-28): "we also need borderless fullscreen and 120 fps and if possible a hd texture pack". Borderless becomes step 4b here (`[GeneralExt] FullscreenAttributes = fake`, one conf line); 120 fps and HD textures become roadmap rows O11 / O8.

- **Step 4: falsifiers fired (2026-09-28).** Method: `ts2diag.asi` now captures frame-exact attract-demo frames (`@demo:N`, using `g_demoMode` 0x0052AD94); the same frame differs by only 0.1–2.4 / 255 between configs, so all differences come from the settings. Runs `ab-none`, `ab-aa8`, `ab-aa8af16`, demo frames 60 / 240 / 400.
  - **MSAA works:** the none→aa8 difference mask traces polygon silhouettes (0.294% of pixels).
  - **MSAA also exposes cracks:** it traces internal edges between coplanar triangles; at 4× zoom, aa8 shows dashed dark seam lines where none is clean (`seam-zoom`). Cause: the game submits pre-transformed TL vertices computed per polygon, so shared edges are not bit-identical, and extra coverage samples land in the hairline cracks. Falsifier "MSAA produces seams/lines" → fired.
  - **Forced 16x filtering is worse:** a dark band across polygon joins (texel bleed across UV edges); it also softens the 2D HUD / text (4.9% of pixels differ vs aa8). Falsifier fired.
  - Pacing is identical in all three (37.6–37.7 avg / 28.7 1% low after 75 s), so the GPU is not a factor.
  - Adjustment per step 5: filtering `appdriven`, MSAA off, try supersampling `Resolution = 2x`. Possible later fix for the cracks themselves: snap TL vertex XY to a 1/16-pixel grid in an ASI hook on the game's draw call (new row, not this plan).
- **Step 5: unverifiable by capture.** `Resolution = 2x` (`ab-ssaa2x`) differs from none by only 761–1,714 px (MSAA: 24–44k). The back-buffer Lock returns dgVoodoo's app-resolution copy, not the supersampled output, so the capture cannot show whether SSAA reaches the monitor. Left `unforced`; SSAA goes to the owner's visual check.
- **Step 4b (owner request): pass.** `[GeneralExt] FullscreenAttributes = fake`; run `borderless`:
  - window style `0x94000000` (`WS_POPUP`, no caption / thickframe), covering DISPLAY1 exactly (the non-DPI-aware runner reports 2560×1440 at −2560,0 = 3840×2160 physical at 150%)
  - render 3840×2160, capture 3840×2160, alive, no `toy2.err`
- **Final config** (vs 2.87.5 defaults): `Adapters = 1`, `FullScreenOutput = 2`, `FullscreenAttributes = fake`, `VideoCard = ati_radeon_8500`, `ForceVerticalSync = true`, `dgVoodooWatermark = false`, `DitheringEffect = pure32bit`, `Default3DRenderFormat = argb8888`; `Antialiasing` / `Filtering` / `Resolution` at defaults.

### Acceptance status
- AC1 ✔ hashes. AC2 ✔ 3840×2160, alive, no `toy2.err` (every run).
- AC3 revised: forced MSAA / AF made the image worse (seams); no-force is the verified clean baseline; SSAA unverified.
- AC4 ⏳ gameplay 60 fps needs an owner playtest (the demo is capped at 30); `ts2diag.asi` stays installed so the playtest writes `game\ts2diag\frames.csv`.
- AC5 ✔ for the chosen config (no forced filtering, so the 2D text stays crisp).
- Not yet run: `/adversarial`, `/smoke`. The plan is not done until AC4 and both of these are.

## 8. Cost, risk, rollback
- Cost: one sitting; about 4 short full-screen runs; one ~9 MB download (+ PresentMon if approved).
- Risk: full-screen runs take over a monitor and play sound; a dgVoodoo regression; AV quarantine.
- Rollback: `Copy-Item C:\toy_story_2\vendor\dgVoodoo-2.78\* C:\toy_story_2\game -Force`

## 9. Docs to update
| Doc | Change |
|---|---|
| `planning/ROADMAP.md` | O1, O2 rows → shipped with evidence; What-is-live dgVoodoo row |
| `CLAUDE.md` | dgVoodoo version in the `game\` line |
| memory `ts2-4k-setup` | dgVoodoo version and settings decision |

## 10. Outcome (filled by /update-map when finished)
- What happened:
- What the falsifiers said:
- What changed in the state file:
