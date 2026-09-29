# Draft: issue for 0danny/toy2-decomp (O9) — NOT POSTED

**State:** not to be posted upstream (owner 2026-09-29: "we will create our own repo, the other devs don't like llm");
kept as the record of the finding for the owner's own repo. Checked against toy2-decomp `ac1135b2` (2026-07-25).

---

**Title:** ModeSelect VRAM check: `vidMemFree` / `freeTextureMem` are compared signed in the binary (explains the `$BUG` at ModeSelect.cpp:400)

**Body:**

`src/DrawingDevice.h` declares the DirectDraw memory fields as `DWORD`:

```cpp
DWORD vidMemFree;      // +0x360
DWORD vidMemTotal;     // +0x364
DWORD freeTextureMem;  // +0x368
DWORD totalTextureMem; // +0x36C
```

but the retail exe (sha256 `023eb6a9…`) compares them **signed** in the display-mode enumeration callback
(`ModeSelect.cpp` ~line 403, the block under the `$BUG` comment):

```
004ACAC4  mov  ecx, [edx+0x150]      ; ddAppParent
004ACACA  mov  edx, [ecx+0x364]      ; vidMemTotal
004ACAD0  mov  esi, [ecx+0x36C]      ; totalTextureMem
004ACAD6  cmp  edx, esi
004ACAD8  je   0x4ACAED
004ACADA  cmp  [ecx+0x368], eax      ; freeTextureMem vs texMem
004ACAE0  jge  0x4ACB08              ; signed
...
004ACAED  mov  ecx, [ecx+0x360]      ; vidMemFree
004ACAF3  sub  ecx, eax              ; - texMem
004ACAF5  cmp  ecx, 0x80000
004ACAFB  jge  0x4ACB08              ; signed
```

With `DWORD` fields the decomp compiles these to unsigned comparisons (`jae`), so it neither matches the binary nor
reproduces the bug. With the fields as `int32_t` it does, and the `$BUG` becomes explainable: on a GPU whose
DirectDraw free-memory report is 2 GiB or more (common today, e.g. via dgVoodoo or modern drivers), the value is
negative as a signed 32-bit int, `freeTextureMem < texMem` (or `vidMemFree - texMem < 0x80000`) is true, every
mode is skipped and the game reports it cannot find a suitable device. RibShark's ToyStory2Fix `IgnoreVRAM` works by
turning exactly these two `jge` into `jmp`.

Suggested change: declare the four fields as `int32_t` (and the `$BUG` comment can name the cause). The same
pattern exists in `D3DApp.cpp` (`int32_t texVidMemFree = curDevice->texVidMemFree; if (texVidMemFree < 0x200000)`),
where the decomp already uses a signed local.

---

Posting (after the owner's OK): `gh issue create --repo 0danny/toy2-decomp --title "…" --body-file <this body>`.
