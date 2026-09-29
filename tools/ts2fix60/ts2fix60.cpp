// ts2fix60.asi — game behaviour that breaks at 60 fps (RibShark's FixFramerate halves g_speedMultiplier 0x0052F2D4
// from 2 to 1), and a texture glitch on shiny models. Reimplemented from AndetSTK's fork of ToyStory2Fix (GPLv3,
// https://github.com/AndetSTK/ToyStory2Fix, source/dllmain.cpp at 0f7a05c6, 2022-12-15), which the community repack
// ships as a binary. RibShark's own ASI (and its 13 patches) stays as it is; none of these sites overlaps it.
// Each site is written only if the retail bytes are there.
//  1. ZurgFix (Zurg and other flying enemies twice as fast at 60 fps): Toy2::Actor::UpdateAIMovement 0x004076F0 moves
//     the actor 1/16 of the remaining distance each frame (0x00407F8E x, 0x00407FB0 z: sar reg,4; sub ecx,reg), with no
//     frame-rate term. Now (d * g_speedMultiplier) >> 5: identical at 30 fps (x2), half the step per frame at 60.
//  2. DiskFix (disk launcher broken at 60 fps): Nu3D::Particles::Update 0x00410F40 turns the projectile toward its
//     target by (angle difference * g_speedMultiplier) / divisor, integer; at multiplier 1 small corrections truncate
//     to 0. The multiplier read at 0x00411099 becomes the constant 2 (AndetSTK's choice).
//  3. TextureFix (texture glitches on the Tin Robot, Zurg and other shiny objects): before
//     Nu3D::PatchVertices::CreateVertexBuffer 0x004B2ED0 copies a 36-byte vertex array (FVF 0x152) into its Direct3D
//     buffer (0x004B300E), every vertex gets normal.z -= 2^-16, u += 2^-16, v += 2^-16. AndetSTK's reason is not
//     documented; the normal nudge points at environment-mapped surfaces with degenerate normals.
#include <windows.h>
#include <stdio.h>
#include <share.h>
#include <stdint.h>

static FILE* g_log;

static bool Patch(uintptr_t at, const void* original, const void* patch, size_t n, const char* what)
{
    bool ok = memcmp((void*)at, original, n) == 0;
    DWORD old;
    if (ok && VirtualProtect((void*)at, n, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)at, patch, n);
        VirtualProtect((void*)at, n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)at, n);
    } else ok = false;
    if (g_log) { fprintf(g_log, "%s: %s\n", what, ok ? "changed" : "NOT changed (unexpected bytes)"); fflush(g_log); }
    return ok;
}

static bool PatchCall(uintptr_t at, const char* original, void* target, const char* what)   // 5-byte call rel32
{
    uint8_t call[5] = { 0xE8 };
    int32_t rel = (int32_t)((uintptr_t)target - (at + 5));
    memcpy(call + 1, &rel, 4);
    return Patch(at, original, call, 5, what);
}

// 1. ZurgFix. Called in place of "sar reg,4; sub ecx,reg"; the following code reads no flags and no stack slot the
//    call's return address could disturb (mov ebx,[esp+24h] runs after the ret).
__declspec(naked) static void ZurgStepX()
{
    __asm {
        imul ebx, dword ptr ds:[0x0052F2D4]
        sar ebx, 5
        sub ecx, ebx
        ret
    }
}
__declspec(naked) static void ZurgStepZ()
{
    __asm {
        imul edx, dword ptr ds:[0x0052F2D4]
        sar edx, 5
        sub ecx, edx
        ret
    }
}

// 3. TextureFix. At 0x004B300E: ecx = byte count of the array, esi = the vertices, edi = the locked buffer.
static void __cdecl NudgeVertices(uint8_t* v, uint32_t bytes)
{
    const float e = 1.0f / 65536.0f;
    for (uint32_t i = 0; i + 36 <= bytes; i += 36) {
        *(float*)(v + i + 0x14) -= e;      // normal.z
        *(float*)(v + i + 0x1C) += e;      // u
        *(float*)(v + i + 0x20) += e;      // v
    }
}
__declspec(naked) static void CopyVerticesNudged()
{
    __asm {
        mov edx, ecx                      // the two instructions this call replaces
        shr ecx, 2
        pushad
        push edx
        push esi
        call NudgeVertices
        add esp, 8
        popad
        ret                               // back to rep movsd
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    wcscpy_s(wcsrchr(path, L'\\') + 1, MAX_PATH - (wcsrchr(path, L'\\') + 1 - path), L"ts2fix60.log");
    g_log = _wfsopen(path, L"w", _SH_DENYNO);
    int ok = 0;
    ok += PatchCall(0x00407F8E, "\xC1\xFB\x04\x2B\xCB", (void*)ZurgStepX, "ZurgFix x (sar ebx,4; sub ecx,ebx -> step * speed >> 5)");
    ok += PatchCall(0x00407FB0, "\xC1\xFA\x04\x2B\xCA", (void*)ZurgStepZ, "ZurgFix z (sar edx,4; sub ecx,edx -> step * speed >> 5)");
    ok += Patch(0x00411099, "\xA1\xD4\xF2\x52\x00", "\xB8\x02\x00\x00\x00", 5, "DiskFix (mov eax,[speed] -> mov eax,2)");
    ok += PatchCall(0x004B300E, "\x8B\xD1\xC1\xE9\x02", (void*)CopyVerticesNudged, "TextureFix (36-byte vertices nudged before the buffer copy)");
    if (g_log) { fprintf(g_log, "ts2fix60 loaded: %d of 4 sites changed\n", ok); fflush(g_log); }
    return TRUE;
}
