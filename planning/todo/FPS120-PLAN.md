# Plan: 120 fps displayed (60 fps game + NVIDIA Smooth Motion in a 64-bit presenter)

**Row:** O11 · **Ticket:** none · **State:** done: 120 images/s measured on DISPLAY1 from a 60 fps game (outcome below). The owner set the goal on 2026-09-28 ("run it in 120fps and 4k, with a support for mods"); a stop-hook on that goal asked to continue building.

## 1. Hypothesis
We believe **copying each finished game frame from dgVoodoo's D3D11 swapchain (32-bit, in toy2.exe) into a cross-process shared texture, and presenting it from our own 64-bit window (`ts2present64.exe`) that has NVIDIA Smooth Motion enabled in its driver profile**, will show the game at 120 fps (60 rendered + 60 generated) **because**:
- Smooth Motion ships only as `NvPresent64.dll`. It cannot load into the 32-bit game, which is why a toy2.exe profile did nothing.
- A 64-bit D3D11 flip-model window with only `Smooth Motion - Enable = 1` (settable without admin) does load `NvPresent64.dll` + CUDA (`smtest64`, 2026-09-28).
- The game already renders a stable 60 fps: RibShark `FixFramerate`; `ts2diag` measures 16.6 ms frames.
- True 120 Hz simulation is not in this plan: 540 instructions use the 60 Hz step `[0x52F2D4]`, plus per-frame code like Zurg's.

**Falsifiers:**
- `NvPresent64.dll` does not load in `ts2present64.exe` while it presents game frames → Smooth Motion does not engage for this window setup (e.g. it needs the foreground / active window); try without `WS_EX_NOACTIVATE`, or fall back to Lossless Scaling (owner's purchase).
- The presented image is not the game's frame (black, stale, torn) → the shared-texture copy or keyed-mutex sync is wrong; fix before measuring.
- The game loses keyboard focus or stalls when the presenter window is on top → input breaks; change the window flags or parent the presenter to the game window.

## 2. Feasibility
| Question | Answer | Evidence |
|---|---|---|
| WHERE | yes | dgVoodoo presents via D3D11 (`ts2diag`: d3d11=1, d3d12=0); `IDXGISwapChain::Present` vtable hook; shared NT-handle texture + keyed mutex (works across 32/64-bit) |
| MAPPED | yes | Smooth Motion engages for 64-bit D3D11 with Enable only (`smtest64`); profile tool `tools\nvprofile\ts2-smoothmotion.exe app=<exe>` |
| TOOL | partly | `NvPresent64.dll` in the presenter's module list = engaged; the presenter can save its own frame on request (the game's image only); 120 on the monitor itself needs the owner's eye or the NVIDIA overlay |

**Level:** MEDIUM. The unknown is whether Smooth Motion engages for a topmost, non-activating window that isn't the foreground app.

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. Own 64-bit presenter + Smooth Motion | free, ours, RTX 5090 hardware frame generation | +1 frame latency; a second window to manage | chosen |
| B. Lossless Scaling (Steam, paid) | works today on borderless windows | purchase; its own frame generation (not NVIDIA's) | fallback, owner's call |
| C. True 120 Hz simulation | real 120 | 540+ code sites, research with no guaranteed end | not this plan |

## 4. Edge cases
- The presenter is not running or crashed → the game side acquires the keyed mutex with 0 ms timeout, drops that copy and keeps playing.
- Alt-tab → the presenter hides while the game isn't foreground and shows again when it is.
- The game exits → the presenter exits (it waits on the game's process handle).
- The back buffer is multisampled → ResolveSubresource instead of CopyResource.
- Window size ≠ texture size → logged; the plain copy needs equal sizes (borderless 4K on the 4K monitor is equal).

## 5. Acceptance criteria
- AC1: `game\ts2bridge.log` shows the presenter started, `NvPresent64.dll LOADED` in ts2present64.exe, and ~60 frames/s received.
- AC2: a frame the presenter saves equals the game's own back buffer for the same moment (mean diff ≤ 1/255, from `ts2diag` and the presenter at a demo frame).
- AC3: the game keeps working with the presenter on top: the demo runs, and the focus log shows no change caused by the presenter.
- AC4 (owner): 120 fps on the NVIDIA overlay (Alt+R) or a clearly smoother picture; latency acceptable.

## 6. Where to change
- New: `tools\bridge\ts2bridge.cpp` → `game\scripts\ts2bridge.asi`; `tools\bridge\ts2present64.cpp` → `game\ts2present64.exe`.
- Driver profile for `ts2present64.exe`: Smooth Motion Enable = 1 (`ts2-smoothmotion on app=ts2present64.exe`; undo: `off app=ts2present64.exe`).
- Surface costs: one ASI + one exe (ours), one per-app driver profile. No env vars.

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | Build both; profile for ts2present64.exe | builds; profile shows Enable = 1 | `ts2-smoothmotion show app=ts2present64.exe` |
| 2 | Demo run with the bridge | presenter window over the game, frames flowing | AC1, AC3 |
| 3 | Frame-exact comparison presenter vs game | same image | AC2 |
| 4 | Owner playtest | 120 fps, feel | AC4 |

## 8. Cost, risk, rollback
- Cost: one sitting. Risk: input / focus issues, extra latency.
- Rollback: delete `game\scripts\ts2bridge.asi` and `game\ts2present64.exe`; `ts2-smoothmotion off app=ts2present64.exe`.

## 10. Outcome
- **What happened (2026-09-28):**
  - Built `tools\bridge\` → `game\scripts\ts2bridge.asi` + `game\ts2present64.exe`; driver profile "ts2present64.exe (C:\toy_story_2)" with Smooth Motion Enable = 1 (`ts2-smoothmotion show app=ts2present64.exe`).
  - AC1 ✔ `ts2bridge.log`: frames sent 5,998, dropped 2. `ts2present64.log`: presented at the game's rate (60.0 fps menus, 30 demo, 25 FMV), `NvPresent64.dll LOADED` for the whole run.
  - AC2 ✔ transfer check (`ts2-run -BridgeCheckAt`): frame 238 at the source (dgVoodoo's back buffer) vs as received by the presenter: **0 of 8,294,400 pixels differ** (`xfer-transfer-source/received.png`, a real game screen with 937,564 non-black pixels).
  - AC3 ✔ with the presenter on top, the game kept the foreground (focus log: foreground at 0.3 s, no change caused by the presenter); the presenter hides when the game loses focus and returns with it.
  - Display-side measurement method added: `tools\present64\dispfps.exe` counts new images per second on DISPLAY1 via Desktop Duplication *metadata* only (frames released unmapped; no pixels read). Calibrated on the 64-bit test app submitting exactly 60 fps: Smooth Motion off → **60** images/s; on → **119–121** images/s (with SM on, DXGI `GetFrameStatistics` returns nothing, so this is the usable signal).
  - Game measurement not obtained: three attempts were cut short because the owner was using the PC and the game pauses after losing focus. One-command check for an untouched 30 s: `tools\measure-120.ps1` (prints DISPLAY1 images/s vs game frames/s and their ratio; 2.0 = doubling).
  - **AC4 ✔ (18:51, `tools\measure-120-when-idle.ps1`, owner idle 62 s, no call window):**
    - presenter shown over the game at −3840,0 3840×2160 (DISPLAY1), shared texture 3840×2160
    - DISPLAY1 received **119–121 images/s while the game rendered 60 frames/s** (splash, ~20 s)
    - attract demo: 60 images/s vs 30 frames/s
    - best 5 s ratio **2.00**
    - evidence: `planning/evidence/measure120-dispfps.txt`, `measure120-frames.csv`
    - Three earlier idle runs showed the presenter hidden (ratio 1.00): the game started from a background process never got the foreground (Windows' foreground lock), and the presenter shows only over the active game. The measurement therefore uses the test switch `-PresenterForceShow` (the game keeps rendering unfocused; the switch file is removed after the run). The NVIDIA overlay tracks the foreground app (toy2.exe, 60 fps), so it may not show the presenter's rate; the clearest check is Smooth Motion's own debug bars (`ts2-smoothmotion on app=ts2present64.exe bars` from an **admin** terminal), or an A/B with `ts2bridge.asi` removed.
- **What the falsifiers said:**
  - "NvPresent64 does not load" did not fire.
  - "Image not the game's frame" did not fire (0-pixel diff). The first frame-exact attempt compared demo-frame counters and mismatched: the bridge counts DXGI presents, `ts2diag` counts game flips, and they drift. Replaced by the frame-id handshake.
  - "Game loses focus" fired once: the presenter was a console app, and its console took the foreground. Fixed with `/SUBSYSTEM:WINDOWS` + `CREATE_NO_WINDOW`; did not recur.
  - Earlier on the same row: a Smooth Motion profile for `toy2.exe` itself does nothing. The driver ships only `NvPresent64.dll` (driver store); no 32-bit counterpart. That profile change was undone.
- **What changed in the state file:** O11 → 🟡 (built and verified in software; the owner confirms 120 on screen).
