// ts2diag.asi — test instrument for toy2.exe (retail, sha256 023eb6a9...), loaded from game\scripts\ by the ASI loader.
// Hooks DrawingDevice::PresentFrame (0x004ABD40, called once per frame before the flip) to
//  - append one line per frame to game\ts2diag\frames.csv (frame, t_ms, dt_ms, demo, speed)
//  - when game\ts2diag\shot.req exists, read one request per line and save the finished back buffer to
//    game\ts2diag\<name>.bmp: "<name>" = next frame, "<name>@demo:<N>" = frame N of the attract demo
//    (the demo replays recorded input, so the same N is the same image in every run).
//    "<name>@burst:<N>" = the next N frames in a row at quarter size (4x4 averages), kept in memory and written
//    as <name>_000.bmp ... when done: catches frame-to-frame flicker that single shots miss.
//    The image can only contain the game's own frame.
// Addresses: toy2-decomp src/DrawingDevice.cpp (g_drawingDevice 0x00884008, m_pddsBackBuffer at +0x34),
// src/Toy2/Toy2.cpp g_demoMode 0x0052AD94, src/Renderer/Renderer.cpp 0x0052F2D4 (frame speed multiplier).
#define DIRECTDRAW_VERSION 0x0600
#include <windows.h>
#include <ddraw.h>
#include <psapi.h>
#include <intrin.h>
#include <stdio.h>
#include <share.h>
#include "minhook.h"

static const DWORD kPresentFrame = 0x004ABD40;
static void** const kDrawingDevice = (void**)0x00884008;
static const volatile LONG* const kDemoMode = (const volatile LONG*)0x0052AD94;
static const volatile LONG* const kSpeedMultiplier = (const volatile LONG*)0x0052F2D4;

struct ShotRequest { char name[64]; LONG demoFrame; };   // demoFrame < 0: next frame
static ShotRequest g_pending[16];
static int g_pendingCount;
static LONG g_demoFrame = -1;                           // frames since the attract demo started, -1 outside it

static char g_dir[MAX_PATH];
static FILE* g_frames;
static FILE* g_log;
static LARGE_INTEGER g_freq, g_start, g_prev;
static unsigned g_frame;

typedef HRESULT(__cdecl* PresentFrame_t)();
static PresentFrame_t oPresentFrame;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log);
}

static void MaskToShift(DWORD mask, int* shift, int* bits)
{
    *shift = 0; *bits = 0;
    if (!mask) return;
    while (!(mask & 1)) { mask >>= 1; ++*shift; }
    while (mask & 1) { mask >>= 1; ++*bits; }
}

static void SaveBackBuffer(const char* name)
{
    BYTE* dev = (BYTE*)*kDrawingDevice;
    if (!dev) { Log("shot %s: no drawing device\n", name); return; }
    IDirectDrawSurface4* bb = *(IDirectDrawSurface4**)(dev + 0x34);
    if (!bb) { Log("shot %s: no back buffer\n", name); return; }

    DDSURFACEDESC2 sd = {}; sd.dwSize = sizeof(sd);
    HRESULT hr = bb->Lock(NULL, &sd, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(hr)) { Log("shot %s: Lock failed %08lx\n", name, hr); return; }

    DWORD w = sd.dwWidth, h = sd.dwHeight, bpp = sd.ddpfPixelFormat.dwRGBBitCount;
    int rs, rb, gs, gb, bs, bbits;
    MaskToShift(sd.ddpfPixelFormat.dwRBitMask, &rs, &rb);
    MaskToShift(sd.ddpfPixelFormat.dwGBitMask, &gs, &gb);
    MaskToShift(sd.ddpfPixelFormat.dwBBitMask, &bs, &bbits);

    DWORD rowBytes = (w * 3 + 3) & ~3u;
    BYTE* out = (BYTE*)malloc((size_t)rowBytes * h);
    if (out && (bpp == 16 || bpp == 32) && rb && gb && bbits) {
        for (DWORD y = 0; y < h; ++y) {
            const BYTE* src = (const BYTE*)sd.lpSurface + (size_t)y * sd.lPitch;
            BYTE* dst = out + (size_t)(h - 1 - y) * rowBytes;   // BMP rows are bottom-up
            for (DWORD x = 0; x < w; ++x) {
                DWORD p = bpp == 32 ? ((const DWORD*)src)[x] : ((const WORD*)src)[x];
                DWORD r = (p >> rs) & ((1u << rb) - 1), g = (p >> gs) & ((1u << gb) - 1), b = (p >> bs) & ((1u << bbits) - 1);
                dst[x * 3 + 0] = (BYTE)(b * 255 / ((1u << bbits) - 1));
                dst[x * 3 + 1] = (BYTE)(g * 255 / ((1u << gb) - 1));
                dst[x * 3 + 2] = (BYTE)(r * 255 / ((1u << rb) - 1));
            }
        }
    }
    bb->Unlock(NULL);
    if (!out || !((bpp == 16 || bpp == 32) && rb && gb && bbits)) {
        Log("shot %s: unsupported format bpp=%lu masks %08lx %08lx %08lx\n", name, bpp,
            sd.ddpfPixelFormat.dwRBitMask, sd.ddpfPixelFormat.dwGBitMask, sd.ddpfPixelFormat.dwBBitMask);
        free(out);
        return;
    }

    char path[MAX_PATH];
    sprintf_s(path, "%s\\%s.bmp", g_dir, name);
    FILE* f = NULL;
    fopen_s(&f, path, "wb");
    if (f) {
        BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
        ih.biSize = sizeof(ih); ih.biWidth = (LONG)w; ih.biHeight = (LONG)h; ih.biPlanes = 1; ih.biBitCount = 24;
        ih.biSizeImage = rowBytes * h;
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
        fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(out, ih.biSizeImage, 1, f);
        fclose(f);
        Log("shot %s: %lux%lux%lu -> %s\n", name, w, h, bpp, path);
    }
    free(out);
}

// Burst: consecutive frames at quarter size.
static char g_burstName[64];
static int g_burstTotal, g_burstDone;
static BYTE* g_burstFrames[240];
static DWORD g_burstW, g_burstH, g_burstRow;

static void CaptureBurstFrame()
{
    BYTE* dev = (BYTE*)*kDrawingDevice;
    IDirectDrawSurface4* bb = dev ? *(IDirectDrawSurface4**)(dev + 0x34) : NULL;
    DDSURFACEDESC2 sd = {}; sd.dwSize = sizeof(sd);
    if (!bb || FAILED(bb->Lock(NULL, &sd, DDLOCK_WAIT | DDLOCK_READONLY, NULL))) { g_burstTotal = g_burstDone; return; }
    DWORD bpp = sd.ddpfPixelFormat.dwRGBBitCount;
    int rs, rb, gs, gb, bs, bbits;
    MaskToShift(sd.ddpfPixelFormat.dwRBitMask, &rs, &rb);
    MaskToShift(sd.ddpfPixelFormat.dwGBitMask, &gs, &gb);
    MaskToShift(sd.ddpfPixelFormat.dwBBitMask, &bs, &bbits);
    g_burstW = sd.dwWidth / 4; g_burstH = sd.dwHeight / 4; g_burstRow = (g_burstW * 3 + 3) & ~3u;
    BYTE* out = (bpp == 32 && rb && gb && bbits) ? (BYTE*)malloc((size_t)g_burstRow * g_burstH) : NULL;
    if (out)
        for (DWORD y = 0; y < g_burstH; ++y) {
            const DWORD* src = (const DWORD*)((const BYTE*)sd.lpSurface + (size_t)(y * 4) * sd.lPitch);
            BYTE* dst = out + (size_t)(g_burstH - 1 - y) * g_burstRow;
            for (DWORD x = 0; x < g_burstW; ++x) {                     // 4x4 average: what the eye sees, no capture aliasing
                DWORD r = 0, g = 0, b = 0;
                for (int yy = 0; yy < 4; ++yy) {
                    const DWORD* row = (const DWORD*)((const BYTE*)src + (size_t)yy * sd.lPitch) + x * 4;
                    for (int xx = 0; xx < 4; ++xx) { DWORD p = row[xx]; r += (p >> rs) & 0xFF; g += (p >> gs) & 0xFF; b += (p >> bs) & 0xFF; }
                }
                dst[x * 3 + 0] = (BYTE)(b / 16); dst[x * 3 + 1] = (BYTE)(g / 16); dst[x * 3 + 2] = (BYTE)(r / 16);
            }
        }
    bb->Unlock(NULL);
    if (!out) { Log("burst %s: unsupported format bpp=%lu\n", g_burstName, bpp); g_burstTotal = g_burstDone; return; }
    g_burstFrames[g_burstDone++] = out;
    if (g_burstDone < g_burstTotal) return;
    for (int i = 0; i < g_burstDone; ++i) {
        char path[MAX_PATH]; sprintf_s(path, "%s\\%s_%03d.bmp", g_dir, g_burstName, i);
        FILE* f = NULL; fopen_s(&f, path, "wb");
        if (f) {
            BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
            ih.biSize = sizeof(ih); ih.biWidth = (LONG)g_burstW; ih.biHeight = (LONG)g_burstH; ih.biPlanes = 1; ih.biBitCount = 24;
            ih.biSizeImage = g_burstRow * g_burstH;
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
            fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(g_burstFrames[i], ih.biSizeImage, 1, f);
            fclose(f);
        }
        free(g_burstFrames[i]); g_burstFrames[i] = NULL;
    }
    Log("burst %s: %d frames %lux%lu written\n", g_burstName, g_burstDone, g_burstW, g_burstH);
    g_burstTotal = g_burstDone = 0;
}

static void ReadShotRequests()
{
    char req[MAX_PATH];
    sprintf_s(req, "%s\\shot.req", g_dir);
    if (GetFileAttributesA(req) == INVALID_FILE_ATTRIBUTES) return;
    FILE* f = NULL;
    fopen_s(&f, req, "rb");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f) && g_pendingCount < 16) {
            line[strcspn(line, "\r\n ")] = 0;
            if (!line[0]) continue;
            char* burst = strstr(line, "@burst:");
            if (burst) {
                if (!g_burstTotal) {
                    int n = atoi(burst + 7); *burst = 0;
                    g_burstTotal = n < 1 ? 1 : n > 240 ? 240 : n; g_burstDone = 0;
                    strcpy_s(g_burstName, line[0] ? line : "burst");
                    Log("burst %s: %d consecutive frames\n", g_burstName, g_burstTotal);
                }
                continue;
            }
            ShotRequest* r = &g_pending[g_pendingCount++];
            r->demoFrame = -1;
            char* at = strstr(line, "@demo:");
            if (at) { r->demoFrame = atol(at + 6); *at = 0; }
            strcpy_s(r->name, line[0] ? line : "shot");
            Log("request %s at %s %ld\n", r->name, r->demoFrame < 0 ? "next frame" : "demo frame", r->demoFrame);
        }
        fclose(f);
    }
    DeleteFileA(req);
}

static void ServeShotRequests()
{
    for (int i = 0; i < g_pendingCount;) {
        ShotRequest* r = &g_pending[i];
        if (r->demoFrame < 0 || r->demoFrame == g_demoFrame) {
            SaveBackBuffer(r->name);
            g_pending[i] = g_pending[--g_pendingCount];
        } else {
            ++i;
        }
    }
}

static void LogModules()
{
    HMODULE mods[512]; DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return;
    Log("modules at frame %u:", g_frame);
    for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 512; ++i) {
        char name[MAX_PATH];
        if (K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], name, MAX_PATH)) Log(" %s", name);
    }
    Log("\n");
}

// Test input: Windows' injected keystrokes (SendInput) never reach this game's DirectInput 7 keyboard (checked
// 2026-09-28: g_inputStates stayed 0 at the title screen). ts2-drive.ps1 marks keys in the shared 256-byte block
// Local\ts2diag_keys_<pid>; the keyboard's GetDeviceState ORs them in, so everything from the game's key mapping on
// runs for real. Only when the real read succeeded (DirectInput reads the keyboard only while the game is in front).
static void** const kKeyboardDevice = (void**)0x00529C9C;   // InputManager.cpp g_directInputDevice
static BYTE* g_keys;
typedef HRESULT(__stdcall* GetDeviceState_t)(void*, DWORD, void*);
static GetDeviceState_t oGetDeviceState;
static bool g_keyboardHooked;

static unsigned g_kbCalls, g_kbFails;
static volatile unsigned g_markRequests, g_marksWritten;   // F11 presses (keyboard read) -> MARK lines in draws.csv
static HRESULT __stdcall hGetDeviceState(void* self, DWORD cb, void* data)
{
    HRESULT hr = oGetDeviceState(self, cb, data);
    static unsigned anyCalls;
    if (++anyCalls <= 3 || anyCalls % 1200 == 0) Log("GetDeviceState call %u: self %p (keyboard %p) cb %lu hr %08lx\n", anyCalls, self, *kKeyboardDevice, cb, hr);
    if (cb == 256 && self == *kKeyboardDevice) {
        ++g_kbCalls; if (FAILED(hr)) ++g_kbFails;
        if (g_kbCalls == 1 || g_kbCalls % 600 == 0) Log("keyboard reads %u, failed %u (last hr %08lx)\n", g_kbCalls, g_kbFails, hr);
    }
    if (SUCCEEDED(hr) && cb == 256 && self == *kKeyboardDevice && g_keys)
        for (int i = 0; i < 256; ++i) if (g_keys[i]) ((BYTE*)data)[i] = 0x80;
    if (SUCCEEDED(hr) && cb == 256 && self == *kKeyboardDevice) {            // F11 (DIK_F11 0x57): the player marks a moment
        static bool wasDown;
        bool down = (((BYTE*)data)[0x57] & 0x80) != 0;
        if (down && !wasDown) g_markRequests++;
        wasDown = down;
    }
    return hr;
}

static void HookKeyboard()
{
    void* dev = *kKeyboardDevice;
    if (!dev || g_keyboardHooked) return;
    g_keyboardHooked = true;
    void* fn = (*(void***)dev)[9];                                  // IDirectInputDeviceA::GetDeviceState
    MH_STATUS s = MH_CreateHook(fn, (void*)hGetDeviceState, (void**)&oGetDeviceState);
    if (s == MH_OK) s = MH_EnableHook(fn);
    Log("keyboard GetDeviceState hook (test input): %d\n", s);
}

// Level-draw probe (owner, 2026-09-28: "please put a probe for textures and how they are being loaded").
// Every placed level shape (gscale set 0 = detailed, set 1 = coarse; DynamicScaler 0x94 bytes, matrix at +0x30) is
// drawn through DrawShape 0x004B8490(shape, &instance->matrix, flags), from the portal traversal 0x004BC460 or the
// grid pass 0x004BC720. Per frame the probe marks which instances were drawn and by which pass, and writes
//   draws.csv          one line per instance that appears (+) or disappears (-) between two frames
//   draws-summary.csv  once a second: instances drawn per set and pass, other draws, texture binds (SetTexture
//                      0x004AC1A0), distinct textures, textures loaded (g_bmpDataHead 0x00884444 list length).
// Level image [0x00B62410]: dynamicScalers[2] at +0x08, shapeCounts[2] at +0x10.
typedef void (__cdecl* DrawShape_t)(void* shape, char* matrix, int flags);
typedef void (__cdecl* Traverse_t)(char* img, int area, int set, void* portal);
typedef void (__cdecl* GridDraw_t)(int radius, int set, char* img);
typedef HRESULT (__cdecl* SetTexture_t)(int stage, void* node);
static DrawShape_t oDrawShape; static Traverse_t oTraverse; static GridDraw_t oGridDraw; static SetTexture_t oSetTexture;
static int g_pass;                                          // 0 other, 1 portal traversal, 2 grid pass
static const int kMaxShapes = 20000;
static BYTE g_drawn[2][kMaxShapes], g_prevDrawn[2][kMaxShapes];
static char* g_probeImg; static char* g_probeSet[2]; static int g_probeN[2];
static unsigned g_drawCount[2][3], g_otherDraws, g_texBinds, g_eventsThisSecond;
static void* g_texSeen[1024]; static unsigned g_texDistinct;
static FILE* g_draws; static FILE* g_drawSummary;

// Polygon sorter InsertPolygon 0x004BC980: drops a polygon when this frame's count [0x00DBB094] has reached 15000
// (0x3A98) or its depth bucket reaches 30000 (0x7530). Counted here: submitted, dropped by the cap, dropped by depth.
typedef void (__cdecl* InsertPoly_t)(int a, int b, int c, int d, int e);
static InsertPoly_t oInsertPoly;
static unsigned g_polySubmitted, g_polyDropCap, g_polyDropDepth, g_polyMaxInFrame;
static void __cdecl hInsertPoly(int a, int b, int c, int d, int e)
{
    volatile int* count = (volatile int*)0x00DBB094;
    int before = *count;
    oInsertPoly(a, b, c, d, e);
    ++g_polySubmitted;
    if (before >= 0x3A98) ++g_polyDropCap;
    else if (*count == before) ++g_polyDropDepth;
    if ((unsigned)*count > g_polyMaxInFrame) g_polyMaxInFrame = (unsigned)*count;
}

static void __cdecl hTraverse(char* img, int area, int set, void* portal) { int o = g_pass; g_pass = 1; oTraverse(img, area, set, portal); g_pass = o; }
static void __cdecl hGridDraw(int radius, int set, char* img) { int o = g_pass; g_pass = 2; oGridDraw(radius, set, img); g_pass = o; }

static void __cdecl hDrawShape(void* shape, char* matrix, int flags)
{
    char* inst = matrix - 0x30;
    bool mine = false;
    for (int s = 0; s < 2 && !mine; ++s) {
        char* base = g_probeSet[s];
        if (base && inst >= base && inst < base + g_probeN[s] * 0x94 && (inst - base) % 0x94 == 0) {
            int i = (int)((inst - base) / 0x94);
            g_drawn[s][i] = (BYTE)(1 + g_pass);
            ++g_drawCount[s][g_pass];
            mine = true;
        }
    }
    if (!mine) ++g_otherDraws;
    oDrawShape(shape, matrix, flags);
}

static HRESULT __cdecl hSetTexture(int stage, void* node)
{
    ++g_texBinds;
    if (node) {
        unsigned h = ((unsigned)(uintptr_t)node >> 4) & 1023;
        for (unsigned k = 0; k < 1024; ++k, h = (h + 1) & 1023) {
            if (g_texSeen[h] == node) break;
            if (!g_texSeen[h]) { g_texSeen[h] = node; ++g_texDistinct; break; }
        }
    }
    return oSetTexture(stage, node);
}

static void ProbeEndFrame(double tms)
{
    char* img = *(char**)0x00B62410;
    char* s0 = img ? *(char**)(img + 0x08) : NULL; char* s1 = img ? *(char**)(img + 0x0C) : NULL;
    int n0 = img ? *(int*)(img + 0x10) : 0, n1 = img ? *(int*)(img + 0x14) : 0;
    if (n0 < 0 || n0 > kMaxShapes) n0 = 0;
    if (n1 < 0 || n1 > kMaxShapes) n1 = 0;
    if (img != g_probeImg || s0 != g_probeSet[0] || s1 != g_probeSet[1] || n0 != g_probeN[0] || n1 != g_probeN[1]) {
        g_probeImg = img; g_probeSet[0] = s0; g_probeSet[1] = s1; g_probeN[0] = n0; g_probeN[1] = n1;
        memset(g_drawn, 0, sizeof(g_drawn)); memset(g_prevDrawn, 0, sizeof(g_prevDrawn));
        if (g_draws) { fprintf(g_draws, "%u,%.0f,level,set0=%d,set1=%d,,,,,,,,\n", g_frame, tms, n0, n1); fflush(g_draws); }
        return;
    }
    const volatile int* buzz = (const volatile int*)0x0052F300;
    {   // the pad's right-stick click (R3, DirectInput button 11; the game does not use it) also marks a moment
        static bool r3WasDown;
        bool r3 = (*(const volatile BYTE*)(0x00529CD8 + 0x30 + 11) & 0x80) != 0;    // g_joystickState.rgbButtons[11]
        if (r3 && !r3WasDown) g_markRequests++;
        r3WasDown = r3;
    }
    if (g_marksWritten != g_markRequests && g_draws) {
        g_marksWritten = g_markRequests;
        fprintf(g_draws, "%u,%.0f,MARK,,,,,,,,%d,%d,%d\n", g_frame, tms, buzz[0], buzz[1], buzz[2]);
        fflush(g_draws);
        Log("MARK at frame %u\n", g_frame);
    }
    unsigned events = 0;
    for (int s = 0; s < 2; ++s) {
        char* base = g_probeSet[s];
        for (int i = 0; i < g_probeN[s]; ++i) {
            BYTE now = g_drawn[s][i], before = g_prevDrawn[s][i];
            if ((now != 0) != (before != 0) && g_draws && events < 200 && g_eventsThisSecond < 2000) {
                const float* t = (const float*)(base + i * 0x94 + 0x0C);
                int shapeId = *(int*)(base + i * 0x94 + 0x70);
                const char* pass = (now ? now : before) == 3 ? "grid" : (now ? now : before) == 2 ? "portal" : "other";
                fprintf(g_draws, "%u,%.0f,%s,%d,%d,%d,%s,%.0f,%.0f,%.0f,%d,%d,%d\n", g_frame, tms, now ? "+" : "-", s, i, shapeId, pass,
                        t[0], t[1], t[2], buzz[0], buzz[1], buzz[2]);
                ++events; ++g_eventsThisSecond;
            }
        }
    }
    memcpy(g_prevDrawn, g_drawn, sizeof(g_drawn)); memset(g_drawn, 0, sizeof(g_drawn));
    if (g_frame % 60 == 0) {
        unsigned loaded = 0;
        for (char* n = *(char**)0x00884444; n && loaded < 100000; n = *(char**)(n + 0x108)) ++loaded;   // BmpDataNode.next
        if (g_drawSummary) {
            fprintf(g_drawSummary, "%u,%.0f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", g_frame, tms,
                    g_drawCount[0][1] + g_drawCount[0][2] + g_drawCount[0][0], g_drawCount[0][1], g_drawCount[0][2],
                    g_drawCount[1][1] + g_drawCount[1][2] + g_drawCount[1][0], g_drawCount[1][1], g_drawCount[1][2],
                    g_otherDraws, g_texBinds, g_texDistinct, loaded, g_eventsThisSecond,
                    g_polySubmitted, g_polyDropCap, g_polyDropDepth, g_polyMaxInFrame);
            fflush(g_drawSummary);
        }
        if (g_draws) fflush(g_draws);
        g_eventsThisSecond = 0;
    }
    if (g_frame % 60 == 0) { g_polySubmitted = g_polyDropCap = g_polyDropDepth = g_polyMaxInFrame = 0; memset(g_drawCount, 0, sizeof(g_drawCount)); g_otherDraws = g_texBinds = 0; memset(g_texSeen, 0, sizeof(g_texSeen)); g_texDistinct = 0; }
}

static HRESULT __cdecl hPresentFrame()
{
    if (!g_keyboardHooked) HookKeyboard();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_frame == 0) {
        g_start = g_prev = now;
        BYTE* dev = (BYTE*)*kDrawingDevice;
        if (dev) Log("first frame: fullscreen=%ld render %ldx%ld\n", *(LONG*)(dev + 4), *(LONG*)(dev + 8), *(LONG*)(dev + 0xC));
        Log("present runtimes loaded: d3d11=%d d3d12=%d d3d9=%d dxgi=%d\n", GetModuleHandleA("d3d11.dll") != NULL,
            GetModuleHandleA("d3d12.dll") != NULL, GetModuleHandleA("d3d9.dll") != NULL, GetModuleHandleA("dxgi.dll") != NULL);
    }
    LONG demo = *kDemoMode, speed = *kSpeedMultiplier;
    g_demoFrame = demo == 1 ? g_demoFrame + 1 : -1;
    if (g_frames) {
        double t = (now.QuadPart - g_start.QuadPart) * 1000.0 / g_freq.QuadPart;
        double dt = (now.QuadPart - g_prev.QuadPart) * 1000.0 / g_freq.QuadPart;
        fprintf(g_frames, "%u,%.3f,%.3f,%ld,%ld\n", g_frame, t, dt, g_demoFrame, speed);
        if (g_frame % 60 == 0) fflush(g_frames);   // the process is usually killed, so flush as we go
    }
    g_prev = now;
    if (g_frame == 600) LogModules();   // ~10 s in: shows what the driver injected (e.g. frame-generation modules)
    if (g_frame % 15 == 0) ReadShotRequests();
    if (g_pendingCount) ServeShotRequests();
    if (g_burstTotal) CaptureBurstFrame();
    ProbeEndFrame((now.QuadPart - g_start.QuadPart) * 1000.0 / g_freq.QuadPart);
    ++g_frame;
    return oPresentFrame();
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    GetModuleFileNameA(NULL, g_dir, MAX_PATH);             // ...\game\toy2.exe
    *strrchr(g_dir, '\\') = 0;
    strcat_s(g_dir, "\\ts2diag");
    CreateDirectoryA(g_dir, NULL);

    char path[MAX_PATH];
    sprintf_s(path, "%s\\ts2diag.log", g_dir);
    g_log = _fsopen(path, "w", _SH_DENYNO);   // readable while the game runs
    sprintf_s(path, "%s\\frames.csv", g_dir);
    g_frames = _fsopen(path, "w", _SH_DENYNO);
    if (g_frames) fprintf(g_frames, "frame,t_ms,dt_ms,demo,speed\n");
    QueryPerformanceFrequency(&g_freq);
    wchar_t keysName[64];
    swprintf_s(keysName, L"Local\\ts2diag_keys_%lu", GetCurrentProcessId());
    HANDLE keysMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 256, keysName);
    if (keysMap) g_keys = (BYTE*)MapViewOfFile(keysMap, FILE_MAP_ALL_ACCESS, 0, 0, 256);

    MH_STATUS s = MH_Initialize();
    if (s == MH_OK) s = MH_CreateHook((void*)kPresentFrame, (void*)hPresentFrame, (void**)&oPresentFrame);
    if (s == MH_OK) s = MH_EnableHook((void*)kPresentFrame);
    Log("ts2diag loaded, PresentFrame hook: %d\n", s);
    sprintf_s(path, "%s\\draws.csv", g_dir);
    g_draws = _fsopen(path, "w", _SH_DENYNO);
    if (g_draws) fprintf(g_draws, "frame,t_ms,event,set,index,shape,pass,x,y,z,buzz_x,buzz_y,buzz_z\n");
    sprintf_s(path, "%s\\draws-summary.csv", g_dir);
    g_drawSummary = _fsopen(path, "w", _SH_DENYNO);
    if (g_drawSummary) fprintf(g_drawSummary, "frame,t_ms,set0_drawn,set0_portal,set0_grid,set1_drawn,set1_portal,set1_grid,other_draws,texture_binds,textures_distinct,textures_loaded,events,polys_submitted,polys_dropped_cap,polys_dropped_depth,polys_max_in_frame\n");
    MH_STATUS p = MH_CreateHook((void*)0x004B8490, (void*)hDrawShape, (void**)&oDrawShape);
    if (p == MH_OK) p = MH_CreateHook((void*)0x004BC460, (void*)hTraverse, (void**)&oTraverse);
    if (p == MH_OK) p = MH_CreateHook((void*)0x004BC720, (void*)hGridDraw, (void**)&oGridDraw);
    if (p == MH_OK) p = MH_CreateHook((void*)0x004AC1A0, (void*)hSetTexture, (void**)&oSetTexture);
    if (p == MH_OK) p = MH_CreateHook((void*)0x004BC980, (void*)hInsertPoly, (void**)&oInsertPoly);
    Log("level-draw probe hooks: %d\n", p);
    Log("level-draw probe hooks enabled: %d\n", MH_EnableHook(MH_ALL_HOOKS));
    return TRUE;
}
