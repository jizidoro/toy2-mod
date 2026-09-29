# Plan: 3D audio through OpenAL Soft (HRTF for headphones)

**Row:** O15 · **Ticket:** none · **State:** built and verified by measurement; waiting for the owner's ears (AC4)
**Requested by / date:** owner, 2026-09-28 (the repack as a reference) and 2026-09-29 "do it all".

## 1. Hypothesis
We believe **moving our plugin loader from `dsound.dll` to `winmm.dll` and installing DSOAL as `dsound.dll`** will
give the game's DirectSound 3D audio OpenAL Soft's HRTF rendering (as the community repack does), because:
- toy2.exe imports `WINMM.dll`, `DINPUT.dll` and `DSOUND.dll` (import table, retail exe);
- our `dsound.dll` is DxWrapper 1.0.2383.20 (sha256 `09665143…`), used only as the ASI loader (`dsound.ini`:
  `LoadPlugins = 1`, `LoadFromScriptsOnly = 1`); DxWrapper also wraps `winmm.dll` (its version info) and reads
  `<its name>.ini`;
- the repack runs DSOAL (`dsound.dll` "Wine DirectSound" 5.3.1.904, sha256 `62faaab1…`) + OpenAL Soft 1.24.3
  (`dsoal-aldrv.dll`, sha256 `c731b8b1…`) + `alsoft.ini` (stereo, headphones, HRTF), with Ultimate ASI Loader as
  `dinput.dll` for its plugins.

**Falsifiers:**
- the loader log (`winmm-toy2.log`) does not list our 9 ASIs → the rename does not load plugins; use the repack's
  Ultimate ASI Loader as `dinput.dll` instead.
- `ts2diag`/`audiopeak` shows no audio output from toy2.exe with DSOAL → DSOAL does not work here; roll back.
- owner hears worse positioning or artefacts than the plain DirectSound build → keep DirectSound (rollback).

## 2. Feasibility
| Question | Answer | Evidence |
|---|---|---|
| WHERE | yes | `game\dsound.dll`, `game\dsound.ini`, three repack files |
| MAPPED | yes | import table; DxWrapper log "Reading config file: dsound.ini", "Loading ASI Plugins" |
| TOOL | yes | `ts2-run.ps1` (a run to the title/demo), loader log, `tools\present64\audiopeak`, owner's ears |

**Level:** HIGH.

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. DxWrapper renamed `winmm.dll` + DSOAL `dsound.dll` | same loader binary we run today | none known | **chosen** |
| B. Ultimate ASI Loader as `dinput.dll` (repack) | proven with DSOAL in the repack | a new binary between the game and DirectInput (ts2pad/ts2diag hook DirectInput) | fallback |
| C. DSOAL loaded some other way (e.g. by an ASI) | — | the game resolves `DSOUND.dll` by name at start-up | rejected |

## 4. Edge cases
- Speakers instead of headphones: `alsoft.ini` `stereo-mode=headphones` + HRTF sounds wrong on speakers → the owner
  says which; for speakers set `stereo-mode=speakers`, `stereo-encoding=uhj` or remove the HRTF lines.
- Window switch: ts2borderless pauses the game; audio must pause/resume the same way (blur test).
- 120 fps presenter and FMVs use their own audio paths? FMVs play through the game's video path (check sound in an FMV).

## 5. Acceptance criteria
- AC1: `game\winmm-toy2.log` lists all ASIs loaded; `game\ts2*.log` written as before.
- AC2: `audiopeak` shows toy2.exe audio output at the title and in the demo.
- AC3: `ts2-drive.ps1 -Action blur` during play: game pauses and resumes with sound.
- AC4: owner: sound works, directional audio in headphones is at least as good.

## 6. Where to change
- `game\dsound.dll` → `game\winmm.dll`, `game\dsound.ini` → `game\winmm.ini` (renames, same bytes).
- New in `game\`: `dsound.dll`, `dsoal-aldrv.dll`, `alsoft.ini` copied from `vendor\repack-reference\Toy Story 2\Game\`.
- Surface costs: two third-party binaries (already in vendor with hashes); no env vars, no new code.

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | rename loader, copy DSOAL files | — | hashes listed |
| 2 | run to the demo (ts2-run) | AC1, AC2 | logs, audiopeak |
| 3 | blur test | AC3 | ts2borderless log + audiopeak |
| 4 | owner listens | AC4 | owner's words |

## 8. Cost, risk, rollback
- Cost: one run (~3 min), owner listening.
- Risk: no sound or a crash at start-up (DSOAL); plugins not loaded (loader rename).
- Rollback: delete `game\dsound.dll`, `dsoal-aldrv.dll`, `alsoft.ini`; rename `winmm.dll`/`winmm.ini` back to `dsound.*`.

## 9. Docs to update
| Doc | Change |
|---|---|
| `planning/ROADMAP.md` | O15 row, What is live (loader name) |
| `C:\toy_story_2\CLAUDE.md` | RibShark line: loader is `winmm.dll`; DSOAL files |

## 10. Outcome (filled by /update-map when finished)
- What happened: loader renamed (`winmm.dll`/`winmm.ini`, same bytes `09665143…`/`a516f527…`), DSOAL files copied (hashes match the repack). `winmm-toy2.log`: all ASIs loaded; toy2.exe modules include `game\DSOUND.dll` and `dsoal-aldrv.dll`; audiopeak over 140 s: peaks 0.08–0.94 through title and demo; blur 4 s: 0.00 while away, 0.08–0.10 after focus (AC1–AC3).
- What the falsifiers said: none fired.
- What changed in the state file: O15 🟡 (owner listening pending); What is live: loader is `winmm.dll`.
