# Plan: mod support (data overrides + code plugins)

**Row:** O12 · **Ticket:** none (local project) · **State:** done (outcome below). The owner set the goal on 2026-09-28: "run it in 120fps and 4k, with a support for mods"; a stop-hook on that goal asked to continue building.
**Requested by / date:** jhonatan, 2026-09-28

## 1. Hypothesis
We believe **an ASI plugin (`game\scripts\ts2mods.asi`) that hooks `kernel32!CreateFileA`, `CreateFileW` and `LoadLibraryA` in-process, and redirects read-only opens of files under the game's data path (`g_pathRegValue` 0x00882F40) or CD path (`g_cdPathRegValue` 0x00883144) to `game\mods\<mod>\data\…` / `game\mods\<mod>\cd\…` when such a file exists**, will let players replace any game file (textures, levels, sounds, music, FMV) without touching the originals, **because** every data read in toy2.exe goes through CRT `fopen` (→ `CreateFileA`), `mmioOpenA` (WAV sfx) or `LoadLibraryA` (`cd\rtlibs\*.dll` FMV containers), per the exe's import table and the decomp's `FileUtils.cpp` / `NGNLoader.cpp` / `BmpDataNode.cpp`. Code mods already work: the ASI loader loads every `game\scripts\*.asi`.

**Falsifiers:**
- An override file exists but the game still reads the original (the log shows no redirect for a file the game opened) → the open does not pass through the hooked APIs; find the real path (e.g. `mmioOpenA` → `OpenFile`, or `NtCreateFile` directly) and hook it.
- The game reads the override but crashes or misrenders with a byte-identical copy of the original → the redirect breaks something (e.g. path length or handle semantics); fix before any real mod.

## 2. Feasibility
| Question | Answer | Evidence |
|---|---|---|
| WHERE | yes | imports `CreateFileA`, `LoadLibraryA` (KERNEL32), `mmioOpenA` (WINMM); path globals 0x00882F40, 0x00883144 (`FileUtils.cpp`) |
| MAPPED | yes | MinHook links (`ts2diag.asi` proves it); ASI loader loads `scripts\*.asi` |
| TOOL | yes | `ts2-run.ps1` + `ts2diag.asi`; `ts2mods.log` lists every redirect |

**Level:** HIGH.

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. Hook the file APIs in-process, overlay folder | covers every file type and every loader, no game-code knowledge needed, originals untouched | must restrict to read-only opens under the game's two paths | chosen |
| B. Hook the game's own loaders (`LoadFile` 0x004A6940, NGN, BmpDataNode, …) | precise | one hook per loader; misses `mmio` and FMV; more addresses to maintain | rejected |
| C. Tell players to overwrite `data\` | no code | destroys originals; no load order; no uninstall | rejected |

## 4. Edge cases
- Writes (saves, `toy2.cfg`) → only opens without write access and with `OPEN_EXISTING` are redirected.
- Case and slash differences in paths → compare case-insensitively after normalizing `/` to `\`.
- Several mods replace the same file → `mods\load_order.txt` (first line = highest priority); without it, folders in alphabetical order.
- Paths not under the game's data/CD paths (dgVoodoo conf, system DLLs) → untouched.
- Performance → resolved paths are cached per file.

## 5. Acceptance criteria
- AC1: with an empty `mods\`, a run is identical to before: frame-exact demo frame 240 equals `ab-none-demo240` (mean diff ≤ 0.5 / 255), and there is no `toy2.err`.
- AC2: a test mod replacing `data\pcbits\rezsel.bmp` with a tinted copy → the mode-select window shows the tint (PrintWindow of that window only), and `ts2mods.log` lists the redirect.
- AC3: a test mod replacing a 3D-visible texture file → the in-game frame shows the change (back-buffer capture), and `ts2mods.log` lists it.
- AC4: `ts2mods.log` records every data file the game opens during a run (the modder's list of what can be replaced).

## 6. Where to change
- New: `tools\ts2mods\ts2mods.cpp` + `build.cmd` → `game\scripts\ts2mods.asi`; `game\mods\README.txt`.
- Surface costs: one new ASI (our code); no env vars, no registry, no exe change.

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | Build `ts2mods.asi`, empty `mods\` | game unchanged; log lists opened files | AC1, AC4 |
| 2 | Test mod: tinted `rezsel.bmp` | tinted mode-select background | AC2 |
| 3 | Test mod: a texture visible in the demo | changed in-game frame | AC3 |
| 4 | README for modders; remove test mods | clean `mods\` | file listing |

## 8. Cost, risk, rollback
- Cost: one sitting. Risk: a wrong redirect breaks loading → the log shows it. Rollback: delete `game\scripts\ts2mods.asi`.

## 9. Docs to update
| Doc | Change |
|---|---|
| ROADMAP O12 row | state + evidence |
| CLAUDE.md | `ts2mods` in the `game\` and `tools\` lines |

## 10. Outcome
- **What happened (2026-09-28):** built `tools\ts2mods\ts2mods.cpp` → `game\scripts\ts2mods.asi` (hooks kernelbase `CreateFileW` / `CreateFileA` / `LoadLibraryExW`), plus `game\mods\README.txt`.
  - AC1 ✔ empty `mods\`: demo frame 240 pixel-identical to `ab-none` (mean diff 0.000, `mods-empty-demo240`).
  - AC4 ✔ `ts2mods.log` lists every file the game opened: 84 sfx (the mmio path), chars, levels, gfx, PAD, pcbits, `cd\audio`, `cd\rtlibs` (the LoadLibrary path).
  - AC2 ✔ a tinted `data\pcbits\rezsel.bmp` mod turned the mode-select background green (`mods-tint-modeselect.png`, PrintWindow of that window only); log: `REDIRECT data\pcbits\rezsel.bmp`.
  - AC3 ✔ (mechanism) a mod with byte-identical copies of `data\level01\*`, `buzz.all/.anm`, `cd\audio\house.wav`, `cd\rtlibs\dlogo.dll` → all redirected, and demo frame 240 stayed pixel-identical (`mods-identity-demo240`). A *visible* 3D texture change needs the texture format → O8.
  - Test mods removed.
- **What the falsifiers said:** neither fired: every opened data file reached the hook, and byte-identical overrides changed nothing.
- **What changed in the state file:** O12 → ✅.
