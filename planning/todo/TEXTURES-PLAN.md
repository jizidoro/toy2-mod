# Plan: HD textures (O8): 32-bit textures, dump, replace, AI first pass

**Row:** O8 · **Ticket:** none · **State:** stopped by the owner 2026-09-28 after steps 1–5 (harness kept; see Outcome)
**Requested by / date:** owner, 2026-09-28: "the only one missing is doing a proper texture pack, where i can get that"
(no pack exists for the PC version; searched TCRF, Textures Resource, GBAtemp, Emulation General wiki)

## 1. Hypothesis
We believe **one ASI (`ts2tex.asi`) hooking the game's single texture loader** will (a) remove the 16-bit colour
banding, (b) dump every texture the game loads, and (c) swap in higher-resolution replacements from
`mods\<pack>\textures\`, because every texture, level and HUD alike, goes through `LoadTextureByStream` 0x004B0A30
(toy2-decomp `Nu3D/BmpDataNode.cpp`). That function decodes a BMP into a square power-of-two RGBA buffer
(`texData`, capped by `g_maxTextureSize` 0x00508214 = 256), and `InitialiseTextureSurface` 0x004B0200 uploads it to a
**16-bit** surface (`FindSuitablePixelFormat` 0x004B0380 accepts only `bpp == 16`). The UVs span the whole
texture, so a bigger square texture maps the same way.

**Falsifiers:**
- Level geometry or the HUD draws wrong with a replacement of a different size (UVs or sprite sizes derived from
  `textureWidth`). If so, key sprites on `bitmapWidth` or exclude those textures, and record which.
- Hashes from the offline scan of `data\*.ngn` don't match the runtime dump for the same level. If so, only the runtime dump is trusted and the scan is dropped.
- toy2.exe (not Large Address Aware, 2 GB) runs out of memory, or dgVoodoo (`VRAM = 256`) refuses surfaces with
  the pack loaded. If so, cap the replacement size, then raise the dgVoodoo VRAM setting. LAA would need the owner's OK (it changes the exe).
- dgVoodoo enumerates no 32-bit texture formats. If so, that part is dropped (16-bit stays).

## 2. Feasibility
| Question | Answer | Evidence |
|---|---|---|
| WHERE to change? | yes | 0x004B0A30 LoadTextureByStream, 0x004B07A0 ProcessBmpPixelData, 0x004B0380 FindSuitablePixelFormat, 0x004AFF80 CopyTextureToSurface (writes uint16 only), 0x004B0200 InitialiseTextureSurface, malloc 0x004CF44C / free 0x004CEE5E; prologues checked in the exe |
| Requirements mapped? | yes | NGN textures: chunk 260 `ParseTextures` 0x004C4080, names `texN` repeat in every level, so the key is a content hash |
| Tool to observe? | yes | `ts2-run.ps1 -DemoFrames` frame-exact back-buffer shots (A/B), `ts2tex.log`, process memory |

**Level:** HIGH for 32-bit, dump and replace. MEDIUM for the AI pass quality (the sources are tiny and palettised).

## 3. Options
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| Hook the loader, content-hash key, PNG replacements in a mod folder | one choke point; any size; mods stack with load order | needs a PNG codec in the ASI | chosen (stb_image, public domain) |
| Replace BMPs inside `.ngn` files via ts2mods | no new hook | 146 MB of NGN rewritten per pack; still 256-capped and 16-bit | rejected |
| dgVoodoo / driver-level texture replacement | no game knowledge | dgVoodoo has none | rejected |

## 4. Edge cases
- Colour-key textures (flag 8: pure green = transparent): the dump carries alpha. The upscale step bleeds colour into
  transparent pixels, so the upscaler does not smear green into the edges.
- Tiling textures: upscale with wrap padding and then crop, so the tile edges stay seamless.
- Binary alpha (keyed or cut-out): the upscaled alpha is thresholded back to 0/255.
- Surfaces are lost on focus changes: the game restores from `texData`, and the replacement lives in `texData` itself.
- No mipmaps (the game creates none): dgVoodoo `Mipmapping = autogen_bilinear` if distant HD textures shimmer.

## 5. Acceptance criteria
- AC1: `ts2tex.log` shows 32-bit formats chosen, and a demo back-buffer shot shows no loss against the 16-bit shot, with less banding.
- AC2: with `game\texdump\` present, a demo run writes PNGs plus `index.csv`, and each image matches what the game shows.
- AC3: the offline scan reproduces the runtime hashes for the demo level (100 %).
- AC4: with the AI pack installed, the same demo frame shows the replaced textures, and nothing is misplaced (A/B shots).
- AC5: memory of toy2.exe with the pack stays under 1.6 GB, and 60 fps holds (frames.csv).

## 6. Where to change
- `tools\ts2tex\` (new): `ts2tex.cpp` + `build.cmd` → `game\scripts\ts2tex.asi`; `scan.py` (offline dump + parity);
  `upscale.py` (AI pass → `game\mods\hd-textures\textures\`).
- `vendor\stb\` (stb_image.h, stb_image_write.h, public domain) and `vendor\realesrgan-ncnn-vulkan\` (BSD-3), with hashes.
- `game\mods\README.txt`: texture mods section.
- Surface costs: no env var (dump is on while `game\texdump\` exists); two third-party downloads (above).

## 7. Steps
| # | Do | Expect | Verify |
|---|---|---|---|
| 1 | ts2tex: 32-bit formats + raised size cap | 32-bit chosen | log + A/B demo shot |
| 2 | ts2tex: dump | PNGs + index | demo run, open a few dumps |
| 3 | scan.py offline dump + parity | 100 % of runtime hashes | `scan.py --check` |
| 4 | ts2tex: replace | replaced textures on screen | demo shot with a test replacement (a solid-colour texture) |
| 5 | upscale.py on the demo level, then all | pack in mods\hd-textures | A/B demo shots, memory, fps |

## 8. Cost, risk, rollback
- Cost: this session and some GPU minutes. Risk: memory in a 2 GB process; texture visuals.
- Rollback: delete `game\scripts\ts2tex.asi` (and `game\mods\hd-textures`).

## 9. Docs to update
| Doc | Change |
|---|---|
| ROADMAP O8 | state + evidence |
| tools\README.md, CLAUDE.md, mods\README.txt | ts2tex component, texture mods |

## 10. Outcome (filled by /update-map when finished)
- What happened: all five steps built. The whole game has 229 distinct textures (415 NGN entries, mostly 256×256
  pages of 64×64 cells, plus 800×600 menu screens). Real-ESRGAN `x4plus-anime` was chosen over `x4plus` (the latter
  drew false grain lines on the wood); the 4× pass took 57 s. The owner played about 12 minutes with the pack (levels 1, 4 and 6):
  85 textures replaced, 0 failed, peak 487 MB private / 1.1 GB virtual, no crash. The owner then saw a "black square under
  buss": the upscaler's colour bleed in transparent pixels shows under colour blends (shadows use `ZERO/INVSRCCOLOR`);
  fixed in the loader (transparent pixels take the game's key colour), not yet observed in game. Owner verdict on quality:
  upscaling "will not change much, because the main issue is that they suck"; new art was declined as weeks of work.
- What the falsifiers said: UVs held with 4× textures (no misplaced textures reported; the square was the colour-bleed
  issue, not UVs). Offline hashes matched the runtime (9/9 keys, 8/8 pictures identical; 415/415 NGN textures found by
  the scan). Memory stayed far below 2 GB; LAA was set anyway at the owner's request. dgVoodoo offers A8R8G8B8 and X8R8G8B8.
- Attacks executed offline: pack completeness 229/229 and sizes correct; keyed alpha stays 0/255; all 229 PNGs decode
  with the ASI's own stb build and land in texData the right way up (largest 16 MB); tile seams: the brick wall tiles
  cleanly (the flagged cells were sprites or a mortar joint on the edge). Confirmed defect: tiny text gets garbled by the AI.
- Not done: A/B runs B and C (the owner was using the PC); see ROADMAP gaps.
- What changed in the state file: O8 ⛔ with the harness kept; new rows Textures and Backdrop at widescreen; ruled-out row.
