# Plan: everything still open after 2026-09-29

**Rows:** O1, O2 (fps in play), O3 (video option), O13, O14, O16 (reopen), O17 step 6, O18 (new: local git repo),
O19 (new: true 120 Hz logic, research) · **Ticket:** none · **State:** approved except W3 (owner 2026-09-29: "defer number 3 for now, and execute the other ones")
**Requested by / date:** owner, 2026-09-29: "ok, create a plan to implement everthyng that is missing".

Seven workstreams, in the recommended order. W1–W4 are small and known; W5 starts with a measurement that may close it
without code; W6 and W7 are research with a stop condition, and the recommendation is to stop there unless the
research finds a cheap path (supersampling already smooths edges, Smooth Motion already shows 120).

## 1. Hypotheses and falsifiers

**W1 Housekeeping.** We believe removing ts2diag's temporary focus-call probes, fixing `measure-120.ps1`'s frame
source and closing O1 leaves the tools correct and the map true, because:
- the probes are six user32 API hooks (`GetForegroundWindow`, `GetActiveWindow`, `GetFocus`, `GetGUIThreadInfo`,
  `SetWinEventHook`, `SetWindowsHookExW`) installed only to diagnose yesterday's focus bug (`tools\ts2diag\ts2diag.cpp`
  ~488–495, log "focus-call probes enabled");
- `tools\measure-120.ps1` reads `planning\evidence\measure120-frames.csv`, which `ts2-run.ps1` writes only when it
  closes the game itself; the script runs it with `-KeepRunning`, so the game rate comes from a stale file. It should
  read `game\ts2diag\frames.csv` with shared access (the game keeps it open), as `scratchpad\fps120.ps1` does;
- O1 already has its observation: owner session 2026-09-29 ~01:37, 3600 play frames, mean 16.79 ms, 99th pct 17.70 ms.
Falsifiers: the window-switch test (`ts2-drive -Action blur`, 3 times) fails after the probes go → they were masking
something: restore and investigate. The fixed script's game rate disagrees with the frame count ts2-run prints → wrong
file or window: re-check.

**W2 Local git repository (O18).** We believe a local repo at `C:\toy_story_2` holding only our own sources, configs and
records (about a few MB) is ready to push to the owner's future repo, because everything else is either third-party,
copyrighted game content or regenerable: `game\data` 177 MB and `game\cd` 372 MB (game content), `iso\` 551 MB, the
retail exe, `vendor\` 132 MB (downloads with recorded hashes), `game\ts2diag` 356 MB and `planning\evidence` ~536 MB
(test captures), `work\` 230 MB, and three folders that are already their own repos (`toy2-decomp`, `ToyStory2Fix`,
`Toy-Story-2-Modding`).
Falsifiers: `git status --ignored` lists any game content, exe/dll, ISO file or capture as tracked → the ignore rules
are wrong: fix before the first commit. The repo's `.git` exceeds ~20 MB → something large slipped in.

**W3 Owner play-checks and fps in a heavy level (O2, O13, O14, backdrop).** We believe one play session with a
metadata-only measurement closes the open checks, because the fixes are built and verified by logs; what is missing is
the owner meeting Zurg / the disk launcher, using Triangle and the right stick, and a heavy scene at 2x supersampling.
Falsifiers: DISPLAY1 shows fewer than 110 images/s while the game renders ~60 in a heavy level → the 8K render or the
scaler costs too much there: options are 1.5x supersampling or supersampling off (A/B). Zurg still twice as fast / the
disk still misses → ts2fix60's reading of AndetSTK's fix is wrong for that case: compare with his binary in a copy.

**W4 Videos at 4:3 (O3 option).** We believe the FMVs play centred at 4:3 with black side bars if
`Nu3D_FMV_SetDimensions` 0x4DB910 `(fmv, x, y, w, h)` receives `x + (w − w·s)/2, y, w·s, h` with
s = (4/3)/(w/h), because `Nu3D_FMV_PlayMovie` 0x4CE5F0 calls it with `(0, 0, DestW, DestH)` (0x4CE621) and
`UpdateAndRenderFMV` 0x4DB950 Blts each frame into that rectangle; `ShowBlackFrames` 0x4CE5B0 clears both buffers first.
Falsifiers: the side bars show stale or garbage pixels → the buffers are not cleared every frame: fill the bars
ourselves before the Blt. The video is offset or cropped → the rectangle is interpreted differently: log the four values.
(The static menu backdrop is a separate path, `Glue::BackdropBltFast` 0x4CE4D0 over a screen-sized surface; not in this
plan unless the owner asks after seeing the videos.)

**W5 Distant tile lines (O17 step 6).** We believe no code is needed, because with point filtering each lookup takes one
texel from one mip level, and dgVoodoo's generated mips average aligned 2×2 blocks, which cannot straddle the 64-texel
(and 32-texel) cell boundaries until a cell is smaller than one texel (below 64×64 page size, far past where tiles are
visible). If lines do show far away, the fallback is `Mipmapping = autogen_point` (config), then per-cell mip chains in
ts2tex (a new surface-creation hook, dig first).
Falsifiers: a far view of the construction yard shows neighbour-coloured lines along distant tile edges → the mips do
mix cells: try `autogen_point`, then the ts2tex dig. The lines appear only with supersampling → the scaler, not the mips.

**W6 Seam-free MSAA (O16 reopen), research.** We believe the 8x-MSAA dashes are T-junctions and can only be removed by
splitting edges at load, because snapping transformed vertices did not change them (1620 → 1588 px). Whether that is
feasible depends on where the T-junctions are: inside one shape's mesh (fixable in the existing level-load hook
`Primitive::CreateAllVertexBuffers` 0x4B32E0, per shape) or between separately placed instances (world space, shared
shapes, per-instance geometry: not feasible at this size).
Stop condition: map the seam pixels of demo frame 240 (`b1-aa8` vs `b1-base`) to the shapes drawn there (ts2diag draw
log / a per-shape ID render); if most seams lie between two instances, stop and keep O16 ⛔ with that evidence.
Falsifier of the fix (if built): demo-frame dark-pixel count under 8x MSAA does not drop below ~10% of 1620.

**W7 True 120 Hz game logic (O19), research.** We believe it is not feasible within this project, because the game's
per-frame step is an integer (`g_speedMultiplier` 0x52F2D4 = 1 at 60 fps, 2 at 30), read at about 540 sites, so a
120 Hz step would need 0.5 everywhere; and interpolating only the camera would make objects judder against it.
Stop condition: one session sampling the 540 uses (how many multiply positions, timers, animation frames) and whether
any single choke point exists; if none, record it ruled out with the counts. Smooth Motion already displays 120.

## 2. Feasibility, derived from the rows
| Workstream | WHERE | MAPPED | TOOL | Level |
|---|---|---|---|---|
| W1 housekeeping | yes: ts2diag.cpp, measure-120.ps1, ROADMAP | yes | build + blur test + a measure run | HIGH |
| W2 git repo | yes: repo root, `.gitignore` | yes (sizes measured) | `git status --ignored`, `du` | HIGH |
| W3 play-checks | n/a (owner plays) | yes | dispfps + frames.csv (metadata only), ts2fix60/ts2pad logs | HIGH (needs the owner) |
| W4 videos 4:3 | yes: 0x4DB910 in ts2widescreen | yes | back-buffer shot during the intro FMV | HIGH |
| W5 distant lines | measurement first | yes | back-buffer/presenter shot of a far view | HIGH for the measurement; MEDIUM if ts2tex work is needed |
| W6 T-junctions | no: depends on the mapping | missing: where the seams are | demo A/B, draw log | LOW (research, stop condition) |
| W7 120 Hz logic | no | missing: the multiplier's use map | xref / capstone | LOW (research, stop condition) |

## 3. Options
| Choice | Options | Verdict |
|---|---|---|
| Git: nested repos | submodules / ignore them / copy their files in | ignore now; submodules once the remote exists (their URLs are public: 0danny/toy2-decomp, RibShark/ToyStory2Fix) |
| Git: build outputs (our .asi/.exe) | track / ignore | ignore: every tool has its source and `build.cmd` |
| Git: planning evidence images | track / ignore / keep a few | ignore the folder; the ROADMAP cites file names and numbers |
| Videos | pillarbox 4:3 / stretched as now | pillarbox behind a switch `fmv=stretch`; owner picks after seeing it |
| Heavy-level fps short of 120 | 1.5x supersampling / off / keep | only if W3's falsifier fires; A/B |
| Distant lines | nothing / `autogen_point` / ts2tex per-cell mips | measure first; config before code |

## 4. Edge cases
- W1: other tools may rely on the focus probes' log lines → grep `tools\` for "focus-call" before removing.
- W2: the repo must never contain `toy2.exe`, game data, the ISO or third-party binaries (licences); `.gitignore` is
  written first and checked with `git status --ignored` before `git add`. No remote is added.
- W3: the owner's DualSense must stay connected for play (ts2pad); measurements read metadata and frame times only.
- W4: videos also play inside menus (movie viewer) → same function, same result; the intro skip (Space) must still work.
- W5: point filtering exposes the half-texel UV inset as a 1-texel pattern step at tile joins → acceptable, but look.
- W6/W7: time-boxed; no game code changes unless the research finds a bounded path and the owner approves a new plan.

## 5. Acceptance criteria (observations)
- AC1 (W1): ts2diag.log has no "focus-call probes" line; blur test ×3 passes (window stays, frames advance);
  `measure-120.ps1` prints game frames/s from the live frames.csv (~60 at the splash screens) and DISPLAY1 ~120.
- AC2 (W1): ROADMAP O1 ✅ with the session numbers; the Known gaps table has only open gaps.
- AC3 (W2): `git status --ignored` shows only sources/configs/records as tracked; `git count-objects -vH` under 20 MB;
  no remote (`git remote -v` empty).
- AC4 (W3): owner's words for Zurg, the disk launcher, Triangle/right stick and the backdrop; dispfps in a heavy level
  ≥ 115 images/s with the game at ~60.
- AC5 (W4): a back-buffer shot during the intro FMV shows the video centred at 4:3 with black bars; `fmv=stretch`
  restores full width; Space still skips.
- AC6 (W5): far-view shot of the construction yard with and without supersampling: no neighbour-coloured lines at
  distant tile edges, or the fallback's shot without them.
- AC7 (W6, W7): a findings note with counts in the ROADMAP row (feasible with a bounded next plan, or ruled out).

## 6. Where to change
- `tools\ts2diag\ts2diag.cpp` (remove probes), `tools\measure-120.ps1` (frame source).
- `C:\toy_story_2\.gitignore` (new), `.git\` (new, local only).
- `tools\ts2widescreen\ts2widescreen.cpp` (W4: one hook on 0x4DB910, switch `fmv=stretch`).
- `game\dgVoodoo.conf` only if W3/W5 falsifiers fire.
- `planning\ROADMAP.md`, `tools\README.md`, `CLAUDE.md`.
- Surface costs the owner decides: one new hook (W4); a git repository (W2). No env vars, no new ASI, no exe change.

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | W1: grep for probe users; remove the six hooks; rebuild ts2diag; blur test ×3 | window stable, frames advance | AC1 |
| 2 | W1: measure-120.ps1 reads `game\ts2diag\frames.csv` shared after the run; one run | ~120 vs ~60 | AC1 |
| 3 | W1: O1 ✅, gaps table cleaned | — | AC2 |
| 4 | W2: write `.gitignore`, `git init`, `git status --ignored`, review, first local commit (owner's OK is this plan's approval) | small repo, no content | AC3 |
| 5 | W4: FMV hook + switch; run to the intro FMV; shots with and without `fmv=stretch` | pillarbox / stretch | AC5 |
| 6 | W5: far view in the construction yard (owner's save, or ts2-drive walk), shots with/without supersampling | no lines | AC6; fallback only if lines |
| 7 | W3: owner plays (Zurg if reachable, disk launcher, Triangle, right stick, a heavy level); I run dispfps + frames.csv for 60 s during it | ≥ 115/s, owner OK | AC4 |
| 8 | W6: seam-to-instance mapping on demo frame 240; decide | stop or a bounded plan | AC7 |
| 9 | W7: multiplier use map; decide | ruled out or a bounded plan | AC7 |
| 10 | `/adversarial` on the W1/W4 diffs, `/smoke` run, `/update-map` | — | reports |

## 8. Cost, risk, rollback
- Cost: W1 ~0.5 session; W2 ~0.5; W4 ~0.5 + one run; W5 one run (+0.5–2 sessions only if lines show); W3 ~20 min of the
  owner's play; W6 and W7 about one session each of research. All test runs follow "run as needed".
- Risk: W1 probe removal could re-expose a focus issue (blur test guards it); W2 could capture copyrighted files if the
  ignore rules slip (checked before the first add, never pushed without OK); W4 could leave garbage in the bars.
- Rollback: W1 previous ts2diag.asi / script from git; W2 delete `.git` and `.gitignore`; W4 `fmv=stretch` or the
  previous ts2widescreen.asi; config changes are single lines in `dgVoodoo.conf`.

## 9. Docs to update
| Doc | Change |
|---|---|
| `planning/ROADMAP.md` | O1 ✅; O18 (git), O19 (120 Hz logic) rows; O3/O13/O14/O16/O17 states; gaps table |
| `tools/README.md` | ts2diag (no focus probes), measure-120, ts2widescreen (`fmv=stretch`) |
| `C:\toy_story_2\CLAUDE.md` | the repo and what it excludes |

## 10. Outcome (filled by /update-map when finished)
- What happened: W1: six user32 focus probes removed from ts2diag (log now "level-draw probe hooks enabled: 0"); blur test ×3 passed (window stays at DISPLAY1, frames advance); measure-120.ps1 reads the live frames.csv shared and waits for the presenter before reading its log: "best 5 s: DISPLAY1 120.0 images/s vs game 60.0 frames/s -> ratio 2.00"; O1 closed; gaps table cleaned. W2: repo at C:\toy_story_2, commit ad5de6a, 50 files, 148.5 KiB, no remote. W4: video rectangle 480,0 2880x2160 (4:3) with black bars (max 0), fmv=stretch gives 0,0 3840x2160. W5: no code; demo frames under pointmip show no neighbour lines; the construction-yard long view moves to W3. W6: 698 of 832 T-junction vertices are between instances: stop. W7: 540 references / 131 functions / no choke point: ruled out. W3 deferred by the owner.
- What the falsifiers said: none fired for W1, W2, W4. W6 stop condition fired (84% between instances). W7 stop condition fired (no choke point). W5 inconclusive at distance (the demo has no long views): owner check.
- What changed in the state file: O1 ✅, O18 ✅, O19 ⛔, O16 ⛔ with the counts, O3 videos, O17 step 6 note; Now = the deferred W3 checks.
