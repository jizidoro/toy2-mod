# Plan: tile edges without coloured lines

**Row:** O17 (tile-edge lines) · **Ticket:** none (local project) · **State:** built, owner-accepted in play ("it's much better!!", 2026-09-29 ~01:08); steps 4-6 open
**Requested by / date:** owner, 2026-09-29 00:48, with screenshots 00:39:21 (visor view on the dirt) and 00:47:50
(sand mound, water, dirt in the construction yard): "try to plan how to make those lines that separate tiles looks
better".

## 1. Hypothesis
We believe **moving every level-shape texture coordinate that sits exactly on an atlas cell edge half a texel into
its own cell, once at level load** will remove the coloured lines along ground-tile edges, because:
- each level's textures are 256×256 pages of 64×64 cells packed with no gap (`work\texpack\src\index.csv`,
  construction yard = `level04\level*.ngn`, e.g. page `2539fa53`); a tile polygon maps one cell with its UVs on the
  cell's edges;
- bilinear sampling at an edge UV blends half a texel of the NEIGHBOUR cell. Near the camera one texel covers
  about 15 screen pixels at 4K, so that half texel is a visible band. Its colour is the neighbour's: in page
  `2539fa53` the dirt cell has a red cell to its left and a navy cell above, the owner's reddish and steel-blue lines;
- the UVs live in each `Nu3D::Primitive`'s vertex array (`PatchVertices`: format, count, `Vertex*`/`VertexTL*`,
  D3D vertex buffer; `toy2-decomp\src\Nu3D\Primitive.h`, `Patch.h`), filled at level load
  (`Primitive::CreateAllVertexBuffers`), so an inset applied there costs nothing per frame.

What it does not fix on its own: dgVoodoo's automatic mipmaps (`Mipmapping = autogen_bilinear`) are built over the
whole page, so at mip level n a cell edge blends 2^(n-1) texels of the neighbour. That affects distant tiles only
(each level is used where its texels are about one pixel), so those lines are thin; step 6 handles them if they
remain visible.

**Falsifiers:**
- F1: in memory, the dirt/sand/water tile vertices of level04 do not have UVs on 32-texel multiples (they are
  already inset, or wrap across a whole page) → the lines are not edge sampling; look at cracks between polygons
  instead (O16's TL-vertex path) and re-plan.
- F2: with the inset on, a back-buffer crop of a near tile edge (texels magnified) still shows the neighbour's colour
  → the colour does not come from mip-0 edge sampling; isolate with `Mipmapping = appdriven` before changing more.
- F3: a line's colour does not match the atlas neighbour across that edge (e.g. the dirt's left edge not red) → not
  atlas bleed; re-plan.

## 2. Feasibility, derived from the row
| Question | Answer | Evidence |
|---|---|---|
| Do we know WHERE to change? | partly | `Primitive` / `PatchVertices` layout from toy2-decomp; `CreateAllVertexBuffers` is a stub in the decomp (address and vertex format still to find: dig D1); the texture size per material (`Material.texDataIndex`) still to map (dig D2) |
| Is every requirement MAPPED? | missing: D1, D2 | O17 row |
| Is there a TOOL to apply and observe it? | yes | `ts2diag` back-buffer shots, frame-exact attract demo (`ts2-run -DemoFrames`), `ts2-drive` (focus/keys/shot), memory reads from PowerShell, `game\ts2debug.txt` switches (read by `ts2widescreen`) |

**Level:** MEDIUM. Two bounded digs (D1 vertex format and fill point, D2 texture size per material), each a
disassembly read with a stop condition.

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. Half-texel UV inset at level load (edge UVs only) | removes the near-camera bands at the source; no per-frame cost; textures untouched; one hook | tiles lose half a texel at each edge (a 1-texel pattern step across a seam, no colour); needs D1/D2 | **chosen** |
| B. Per-cell mip chains made by ts2tex (mips never cross a cell), `Mipmapping = appdriven` | fixes the distant lines too | ts2tex must create mipmapped surfaces; cell layout per page needed | **only if F2-far** (step 6) |
| C. Gutters: ts2tex re-packs each page at 2× with 1-texel borders + UV remap | the textbook fix | needs A's UV machinery anyway plus a page re-pack; most surface | rejected unless A+B fall short |
| D. Mipmaps off (`Mipmapping = appdriven`) | no mip bleed | brings back the distant shimmer the owner reported (the reason mips were enabled, 2026-09-28) | rejected alone |
| E. Point sampling | no bleed at all | blocky 64-px textures at 4K | rejected |
| F. Texture address clamp | — | clamps to the page, not the cell | does not apply |
| G. Back to `Filtering = 16` | sharper at grazing angles | known texel bleed at joins (Ruled out, 2026-09-28) | re-test only after A (step 5) |

## 4. Edge cases
- A texture meant to repeat (UVs span more than one cell or leave [0,1]) → left alone: the inset applies only
  when a triangle's UV box fits one grid-aligned region.
- A vertex shared by triangles in two different cells → left alone (logged count).
- Animated UVs (water scrolling via `Material.horzOffset` / `vertOffset`) → left alone if the material animates.
- Pages that are not 256×256 (128×128, 192×128) → the texel size comes from each page's own size (D2).
- Characters, sprites, fonts, HUD → not touched: only level-shape primitives (sets 0 and 1) are processed.
- The coarse copies (set 1) → same treatment, so both copies match.
- A texture pack with larger pages (ts2tex replacement) → half a texel of the original is still inside the cell.
- UVs as integers or fixed point in the vertex format → D1 establishes the type before any write.

## 5. Acceptance criteria (observations)
- AC1: after a level loads, `game\ts2widescreen.log` has `uv inset: <level> <P> primitives, <M> coordinates moved,
  <S> skipped (repeat/shared/animated)`, M > 0 in level04.
- AC2: attract demo frames 60/240/400/700, inset off vs on (`ts2debug.txt` `uvinset=off`), 4× crops of tile edges:
  the neighbour-colour bands are gone at near range; outside tile edges the difference mask is empty (the demo is
  frame-exact).
- AC3: the owner, in the construction yard (visor view on the dirt, the sand mound, the water edge): no coloured lines
  near the camera.
- AC4: HUD, text, Buzz and enemies are identical between the two demo runs (difference mask outside level geometry = 0).
- AC5: mean frame time unchanged (`frames.csv`, 16.7 ms).

## 6. Where to change
- `tools\ts2widescreen\ts2widescreen.cpp`: one hook at the level-load vertex fill (D1), the inset (about 60 lines),
  a `uvinset=off` line in the existing `game\ts2debug.txt` switch reader. Reuses the ASI that already owns
  level-shape fixes and the debug file.
- `game\dgVoodoo.conf`: `[DirectX] Filtering` stays `trilinear` (set 2026-09-29 00:44) unless step 5 shows 16× is
  clean with the inset.
- Surface costs the owner decides: one new hook in ts2widescreen. No new ASI, env var, gate or dependency.
- Step 6 only (if needed): mipmapped surfaces in `tools\ts2tex\ts2tex.cpp`.

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | D1: disassemble the vertex-buffer creation for primitives (callers of `PatchVertices::CreateVertexBuffer`) and the per-frame path for level shapes; D2: the material → texture page size | vertex format (Vertex 32 bytes: xyz, normal, tu/tv floats?) and the point where UVs are final | addresses recorded in this plan; stop and re-plan if UVs are regenerated per frame from data we cannot reach |
| 2 | Single target, read-only: dump level04's level-shape primitives from memory (PowerShell), histogram of UV coordinates on 32-texel multiples, triangles per cell, repeats | most ground-tile UVs on cell edges (F1 check); the dirt/sand/water materials identified | counts + the dirt tile's 4 UVs quoted |
| 3 | Implement the inset in ts2widescreen at the D1 point, switch `uvinset=off` | AC1 log line on each level load | log after a level load |
| 4 | A/B: attract demo twice (off / on), frame-exact shots (ask first: full-screen runs ~5 min) | AC2, AC4, AC5 | crops + masks + frame times |
| 5 | Filtering re-test with the inset: `trilinear` vs `16` on the same demo frames | pick the sharper one that stays clean | crops |
| 6 | Only if distant lines remain (visible in the owner's check or the demo crops): per-cell mip chains in ts2tex | distant edges clean | same crops at far range |
| 7 | Owner check at the construction yard (AC3); `/adversarial`, `/smoke`, `/update-map` | | owner's words recorded in O17 |

## 8. Cost, risk, rollback
- Cost: about one session: two digs, one hook, two demo runs (~5 min of the screen, owner asked first).
- Risk: a wrong vertex format read writes garbage into level UVs (textures scrambled on that level). Mitigated by
  step 2's read-only dump first and by the switch. Only visuals are affected: collision uses separate data.
- Rollback: `uvinset=off` in `game\ts2debug.txt` (instant, next level load), or the previous `ts2widescreen.asi`.

## 9. Docs to update
| Doc | Change |
|---|---|
| `planning/ROADMAP.md` | O17 row: state and evidence; `Filtering = 16` ruled-out row gets the 2026-09-28 23:23 re-enable and the 2026-09-29 revert |
| `tools/README.md` | ts2widescreen line: UV inset and the `uvinset=off` switch |
| `C:\toy_story_2\CLAUDE.md` | ts2widescreen description |

## 10. Outcome (filled by /update-map when finished)
- What happened: D1: `Primitive::CreateAllVertexBuffers` 0x004B32E0 copies each `PatchVertices` array into its D3D buffer (FVF 0x152 36 bytes UV +28; 0x112 / 0x1C4 32 bytes UV +24); level shapes come from `NGNLoader::ParseGeometry` (call 0x004C367B, returns to 0x004C3680). Header draw types are Nu3D's own codes (0 = triangle list, the hardware quad conversion), not D3D's: the first build read them as D3D and touched 95 triangles; fixed. D2: material 0x00A4CC98 + id*0x84, texDataIndex +0x68; NGNTextureData 0x009F6224 + i*0x1C, BmpDataNode +0x10, bitmap size +0x90/+0x94. The first scene after start-up loads its geometry before its textures (658 of 736 primitives without a BmpDataNode), so an unknown size now counts as 256x256. Log of the diagnostic run (demo level, 01:05): "772 primitives, 24170 triangles, 49962 cell-edge coordinates, 33771 moved, 0 conflicts kept, 0 repeating/wrapping kept; skipped 171 without texture". Owner: "it's much better!!".
- What the falsifiers said: F1 did not fire (49962 UV coordinates on 32-texel cell edges in one level). F2/F3 not run as crops: the frame-exact demo A/B (step 4) never reached the demo (demo flag stayed -1; frames drawn only 62 of 130 s), so AC2/AC4 are NOT EXECUTED; the owner's eye check replaced them.
- What changed in the state file: O17 🟡 with this evidence; open: the construction-yard log line with the final build, distant mip bleed (step 6). Step 5 closed differently: the owner chose point filtering with mipmaps (`pointmip`) over trilinear after the filter A/B (2026-09-29), which never blends neighbouring cells at mip 0.
