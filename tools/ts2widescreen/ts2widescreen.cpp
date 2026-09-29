// ts2widescreen.asi — the sky/backdrop moves with the world at widescreen resolutions (toy2.exe retail code).
// The backdrop is a flat picture the game tiles across the screen and scrolls by camera yaw
// (UpdateParallax 0x0048F230, RenderParallaxBackground 0x0048F410). The 3D world is a perspective projection:
// vertical field of view camera[0x40] = 1.25 rad, horizontal scale camera[0x44] (camera at [0x00B623FC]), which
// RibShark's Widescreen fix sets from 0.75 to 1/aspect (0.5625 at 16:9, a 104 degree horizontal view). A flat
// scroll cannot follow a perspective view: at 16:9 the world moves 2.6x faster at the screen edges than at the
// centre, while the backdrop moved at one speed (owner, 2026-09-28: the background "walk toghether with buss").
//  1. Sideways: each backdrop tile the game queues (Queue2DSprite 0x004B8CC0 with RENDER_PARALLAX_BG and a
//     texture) is drawn as narrow vertical strips, each showing the part of the backdrop at that screen column's
//     viewing angle, atan((2x - 1) * tan(half horizontal view)). The backdrop becomes a cylinder at infinity: it
//     turns exactly with the world everywhere on screen, and a tile is as wide at the screen centre as the
//     game drew it at 4:3 (one tile per 1/f radians, f = cot(vertical view / 2)). Static backdrops are untouched.
//  2. Tilting: UpdateParallax divides the camera pitch by camera[0x44] (constant 0x004DC0D0, read only there), so
//     since RibShark's change every camera tilt moved the backdrop 1.33x too far; the constant is scaled back.
//  3. Pop-in (owner: "objects apear from nowhere"): RibShark's IncreaseRenderDistance lifts only the level geometry's
//     far limit (0x005088B0). Characters/objects are drawn only within their own visibilityDistance (actor + 0x42;
//     test at 0x00447BF5: distance^2 < visibilityDistance^2 << 4) and sprites/effects fade out over the level's
//     cull band (1664, set at every level load at 0x0043E728 into g_type63CullDistance 0x0054BEF0). Owner asked for
//     "20k units" (2026-09-28). Characters/objects: the per-actor distances are 1280, 1800 or 3800 (all constant
//     writes to +0x42), and any multiplier that reaches far overflows the 32-bit square for the 3800 ones, so the
//     threshold becomes a fixed 2^30 (imul edi,edi -> or edi,-1; shl edi,4 -> shr edi,2): about a million world
//     units, beyond any level, i.e. everything in a visible area and on screen is drawn. Sprites/effects: 4096, the
//     most the band's 4095-entry table (0x00557C20..0x00559C1E, filled up to an equality test) allows.
//     (0x005088B4 is left alone: it is not a far limit but the distance below which far-detail shapes hide.)
//     The frustum tests themselves use the real 16:9 view (clip rectangle 3840x2160, measured), so they need no change.
//  4. Scenery popping near the camera (owner: enemies, allies and "textures … keep flicking and poping and changing"):
//     in play the game always applies distance preset 2 (0x004CDD10 called with 0x7FFFFFFF at 0x004410CC, 0x004410DE,
//     0x00441187; table 0x00508D38, 24 bytes per preset). Preset 2 alone hides the level shapes of kind 1 whenever
//     the camera is within 1000 units (0x005088B4, test 0x004BC5AF), so they blink as the follow camera swings;
//     presets 0 and 1 use 0 (never hidden). (Setting preset 2's 1000 to 0 was tried and reverted: the coarse copies
//     then drew near the camera too.) Detailed shapes cut off beyond 12000: see "Two versions" below.
//     (Treating every portal as visible was tried first: the traversal caps its area visits per frame (0x004BC4FD),
//     so areas and their actors dropped out on some frames and flickered. Removed 2026-09-28.)
//  5. Far objects (owner: "it is still happening"): the band variable is read by 19 instructions besides the loader's
//     table build (pickups/coins at 0x004412EA, another list at 0x0044163C as (band/4)^2; the placed-object renderers
//     compare linearly, some x4). Each of those reads becomes the constant 0x20000 (about 500k world units; (0x20000/4)^2
//     still fits 32 bits), while the variable itself stays 4096 so the loader's 4095-entry table stays in bounds.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <share.h>
#include <stdint.h>
#include <intrin.h>
#include "minhook.h"

static float* const kTiltScale = (float*)0x004DC0D0;      // 0.2387 (3 / 4pi)
static char** const kCamera = (char**)0x00B623FC;
static const int* const kStaticBackdrop = (const int*)0x0055A0E0;   // Toy2 g_hasStaticBackdrop
static const float kNarrow = 0.75f;                        // camera[0x44] at 4:3, as the game sets it (0x004CE08F)
static const uint32_t kParallaxBG = 0x80000000;            // RenderType.h RENDER_PARALLAX_BG
static const int kStrips = 96;                             // strips per screen width
static float g_tilt0, g_lastS = 1.0f, g_yaw;               // g_yaw: camera yaw in radians, from UpdateParallax
static unsigned g_stripFrames;
static FILE* g_log;

typedef void (__cdecl* UpdateParallax_t)(int pitch, int yaw);
typedef void (__cdecl* Queue2DSprite_t)(float x, float y, float w, float h, const float* uvTopLeft, const float* uvBottomRight,
                                        int textureIndex, uint32_t color, uint32_t flags);
static UpdateParallax_t oUpdateParallax;
static Queue2DSprite_t oQueue2DSprite;

// RibShark's IncreaseRenderDistance sets the squared geometry far limit 0x005088B0 to infinity. The per-polygon
// near/far split (0x004BD325: far1 + (far2 - far1) * 0.3, far1 = sqrt of that limit) then becomes NaN, every
// polygon lands in the "far" draw buckets (+0x44C, 0x004BC9D6), not only the distant ones. A finite 1e14
// (distance 1e7, beyond any level) keeps everything drawn and the split meaningful. Written every frame because
// RibShark's own write happens at its start-up, in an order this plugin does not control.
static float* const kFarLimitSquared = (float*)0x005088B0;
static const float kFarLimit = 1e14f;

// Two versions of the same object (owner, 2026-09-28: the pillar showed 4 rows of big blocks, then 8 rows of bricks;
// far fences and sand looked coarse): each level places its shapes twice, set 0 (detailed, kind 0) and set 1 (a coarse
// copy at the same pivot, kind 1). Nu3D::Scene::RenderWorldGeometry (0x004CDDD0) draws them in two passes through the
// same portal traversal (0x004BC460): first the coarse pass, whose camera gets an extra near clip plane at preset p[0]
// (camera +0x50), then the detailed pass, whose camera gets an extra FAR clip plane at preset p[1] (camera +0x54;
// Camera::CalculateFrustumPlanes 0x004BA420 adds it only while it is nearer than the far plane +0x4C, 48000). A shape
// whose bounding sphere lies beyond p[1] fails the frustum test (0x004BC5D2) and only its coarse copy is drawn.
// RibShark's IncreaseRenderDistance lifts only the per-shape distance test (0x005088B0), not this plane. Measured with
// ts2diag's draw log (2026-09-29, construction level, owner playing): 13 set-1 shapes per frame drawn without their
// detailed twin (pillar 0#16 dropped at frame 5097 while 1#18 stayed). p[1] becomes the far plane, so the detailed
// copy is drawn as far as anything is; where both are drawn the detailed one wins the depth test (its pass projects
// with near plane 60 against the coarse pass's 40, which puts it in front at equal distance up to the far plane), and
// coarse-only shapes (a set-1 shape without a twin) still show, so nothing leaves a hole.
// Preset table 0x00508D28, 6 floats per preset: coarse near clip, detailed far clip, near planes x2, shape distances x2.
static float* const kPresetDetailFar[3] = { (float*)0x00508D2C, (float*)0x00508D44, (float*)0x00508D5C };
static const float kPresetDetailFar0[3] = { 3500.0f, 6500.0f, 12000.0f };
static const float kViewFar = 48000.0f;                     // far plane of both passes (0x00508D04 / 0x00508D0C)

// Tile edges without the neighbour's colour (owner 2026-09-29: red / steel-blue / grey lines along ground tiles).
// Level textures are 256x256 pages of 64x64 cells with no gap (e.g. level04 page 2539fa53: the dirt cell has a red
// cell to its left and a navy cell above). A tile maps one cell with UVs on the cell's edges, and bilinear sampling
// there blends half a texel of the neighbour: near the camera one texel is ~15 px at 4K, a visible band. At level
// load every UV coordinate lying on a 32-texel grid line at the edge of its triangle's cell region moves half a texel
// into the cell, before the vertices are copied into their Direct3D buffer. Only the level's own shapes
// (NGNLoader::ParseGeometry 0x004C35C0 calls Primitive::CreateAllVertexBuffers 0x004B32E0 at 0x004C367B, returning
// to 0x004C3680); characters and objects come through 0x004CA6D7 and are left alone.
// Primitive: listNext +0x08, materialIndex +0x10, PatchVertices +0x14 (FVF, count +0x18, vertices +0x1C),
// headerCount +0x28, headers +0x2C (0x10 each: drawType, indexCount, uint16* indices). Material array 0x00A4CC98,
// 0x84 each, texDataIndex +0x68; NGNTextureData array 0x009F6224, 0x1C each, BmpDataNode* +0x10; BmpDataNode
// bitmapWidth/Height +0x90/+0x94 (textureWidth/Height +0x88/+0x8C). The vertex layouts the buffer copy handles
// (0x004B2F99): FVF 0x152 (36 bytes, UV at +28), 0x112 and 0x1C4 (32 bytes, UV at +24).
static const float kCellGrid = 32.0f, kInsetTexels = 0.5f, kOnLine = 0.05f;
struct UvInsetStats { int prims, tris, edgeCoords, moved, conflicts, outside, noTexture, badFormat, assumed256; };
static UvInsetStats g_uv;
static bool g_uvPending;
static bool g_dbgUvInsetOff;

// A material without a texture (texDataIndex 0) has nothing to bleed. Some scenes load their geometry before their
// textures exist (measured 2026-09-29: the first scene after start-up, 658 of 736 primitives with no BmpDataNode yet);
// those take 256x256, the size of nearly every level page: on a smaller page the half-texel inset is a smaller
// fraction of its texel, on a larger one about a texel, still inside the cell.
static bool TexelSize(int materialId, float* w, float* h)
{
    if (materialId < 0 || materialId >= 3000) return false;
    int texDataIndex = *(int*)(0x00A4CC98 + materialId * 0x84 + 0x68);
    if (texDataIndex <= 0 || texDataIndex >= 4096) return false;
    char* bmp = *(char**)(0x009F6224 + texDataIndex * 0x1C + 0x10);
    int bw = bmp ? *(int*)(bmp + 0x90) : 0, bh = bmp ? *(int*)(bmp + 0x94) : 0;
    if (bmp && (bw < 8 || bw > 4096 || bh < 8 || bh > 4096)) { bw = *(int*)(bmp + 0x88); bh = *(int*)(bmp + 0x8C); }
    if (bw < 8 || bw > 4096 || bh < 8 || bh > 4096) { bw = bh = 256; ++g_uv.assumed256; }
    *w = (float)bw; *h = (float)bh;
    return true;
}

static void InsetPrimitiveUVs(char* prim)
{
    int fvf = *(int*)(prim + 0x14), count = *(int*)(prim + 0x18);
    char* verts = *(char**)(prim + 0x1C);
    int stride, uvAt;
    if (fvf == 0x152) { stride = 36; uvAt = 28; } else if (fvf == 0x112 || fvf == 0x1C4) { stride = 32; uvAt = 24; }
    else { ++g_uv.badFormat; return; }
    if (!verts || count <= 0 || count > 65535) { ++g_uv.badFormat; return; }
    float tw, th;
    if (!TexelSize(*(int*)(prim + 0x10), &tw, &th)) { ++g_uv.noTexture; return; }
    ++g_uv.prims;
    // per vertex and axis: 0 = untouched, +1 / -1 = move in, 2 = conflicting (left as it is)
    static signed char want[65536][2];
    memset(want, 0, count * 2);
    int headerCount = *(int*)(prim + 0x28);
    char* headers = *(char**)(prim + 0x2C);
    for (int hI = 0; headers && hI < headerCount && hI < 64; ++hI) {
        char* hd = headers + hI * 0x10;
        int type = *(int*)hd, n = *(int*)(hd + 4);
        const uint16_t* idx = *(const uint16_t**)(hd + 8);
        // Nu3D draw types (g_drawTypeConversion 0x00508A6C maps the file's 1..6 to 0,2,1,3,5,4): 0 = triangle list
        // (the loader turns quads into lists for the hardware path, 0x004CBC90), 1 = strip, 2 = fan; others skipped.
        if (!idx || n < 3 || type < 0 || type > 2) continue;
        int tris = type == 0 ? n / 3 : n - 2;
        for (int t = 0; t < tris; ++t) {
            int a, b, c;
            if (type == 0) { a = idx[t * 3]; b = idx[t * 3 + 1]; c = idx[t * 3 + 2]; }
            else if (type == 1) { a = idx[t]; b = idx[t + 1]; c = idx[t + 2]; }
            else { a = idx[0]; b = idx[t + 1]; c = idx[t + 2]; }
            if (a >= count || b >= count || c >= count) continue;
            ++g_uv.tris;
            int v[3] = { a, b, c };
            for (int axis = 0; axis < 2; ++axis) {
                float size = axis ? th : tw, p[3];
                for (int k = 0; k < 3; ++k) p[k] = *(float*)(verts + v[k] * stride + uvAt + axis * 4) * size;
                float lo = min(p[0], min(p[1], p[2])), hi = max(p[0], max(p[1], p[2]));
                if (lo < -kOnLine || hi > size + kOnLine) { ++g_uv.outside; continue; }   // repeats or wraps: leave it
                if (hi - lo < 1.0f) continue;                                              // degenerate along this axis
                float cellLo = floorf((lo + kOnLine) / kCellGrid) * kCellGrid, cellHi = ceilf((hi - kOnLine) / kCellGrid) * kCellGrid;
                for (int k = 0; k < 3; ++k) {
                    signed char d = fabsf(p[k] - cellLo) < kOnLine ? 1 : fabsf(p[k] - cellHi) < kOnLine ? -1 : 0;
                    if (!d) continue;
                    ++g_uv.edgeCoords;
                    signed char& w = want[v[k]][axis];
                    w = w == 0 ? d : (w == d ? w : 2);
                }
            }
        }
    }
    for (int i = 0; i < count; ++i)
        for (int axis = 0; axis < 2; ++axis) {
            signed char w = want[i][axis];
            if (w == 2) { ++g_uv.conflicts; continue; }
            if (!w) continue;
            float* uv = (float*)(verts + i * stride + uvAt + axis * 4);
            *uv += w * kInsetTexels / (axis ? th : tw);
            ++g_uv.moved;
        }
}

static wchar_t g_dbgPath[MAX_PATH];
static bool DebugSwitchSet(const char* name)
{
    FILE* f = NULL; bool on = false;
    if (_wfopen_s(&f, g_dbgPath, L"rt") == 0 && f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) if (strstr(line, name)) on = true;
        fclose(f);
    }
    return on;
}

static void FlushUvStats()                                    // one line per level load, written once the level runs
{
    if (!g_uvPending) return;
    if (g_log) {
        if (g_dbgUvInsetOff) fprintf(g_log, "uv inset: off for this level (uvinset=off)\n");
        else fprintf(g_log, "uv inset: %d primitives (%d with the page size assumed 256), %d triangles, %d cell-edge coordinates, "
                     "%d moved, %d conflicts kept, %d repeating/wrapping kept; skipped %d without texture, %d other vertex formats\n",
                     g_uv.prims, g_uv.assumed256, g_uv.tris, g_uv.edgeCoords, g_uv.moved, g_uv.conflicts, g_uv.outside,
                     g_uv.noTexture, g_uv.badFormat);
        fflush(g_log);
    }
    memset(&g_uv, 0, sizeof(g_uv)); g_uvPending = false;
}

typedef int (__cdecl* CreateAllVertexBuffers_t)(char* prim, int flags);
static CreateAllVertexBuffers_t oCreateAllVertexBuffers;
static int __cdecl hCreateAllVertexBuffers(char* prim, int flags)
{
    if (_ReturnAddress() == (void*)0x004C3680) {                 // a level shape (NGNLoader::ParseGeometry)
        if (!g_uvPending) { g_dbgUvInsetOff = DebugSwitchSet("uvinset=off"); g_uvPending = true; }
        if (!g_dbgUvInsetOff)
            for (char* p = prim; p; p = *(char**)(p + 0x08)) InsetPrimitiveUVs(p);
    }
    return oCreateAllVertexBuffers(prim, flags);
}

// Live debug switches for experiments (read once a second from game\ts2debug.txt; the file absent = the defaults):
//   set0=hide     all detailed shapes hidden (far limit 0)          set1=hide  all coarse shapes hidden (preset 2)
//   portals=all   every portal passes (PortalVisible 0x004BACF0 -> 1, full view kept)
//   uvinset=off   the next level loads without the tile-edge inset (A/B)
//   hud=stretch   2D drawn stretched over 16:9 as the game does (HUD/menus at 4:3 off)
static bool g_dbgHideSet0, g_dbgHideSet1, g_dbgAllPortals, g_dbgHudStretch;
static unsigned g_dbgTick;
static float* const kPreset2Hide = (float*)0x00508D6C;       // preset 2's near-hide distance for set 1 (1000)

static void ReadDebugSwitches()
{
    bool h0 = false, h1 = false, ap = false, hs = false;
    FILE* f = NULL;
    if (_wfopen_s(&f, g_dbgPath, L"rt") == 0 && f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "set0=hide")) h0 = true;
            if (strstr(line, "set1=hide")) h1 = true;
            if (strstr(line, "portals=all")) ap = true;
            if (strstr(line, "hud=stretch")) hs = true;
        }
        fclose(f);
    }
    if (h0 != g_dbgHideSet0 || h1 != g_dbgHideSet1 || ap != g_dbgAllPortals || hs != g_dbgHudStretch) {
        g_dbgHideSet0 = h0; g_dbgHideSet1 = h1; g_dbgAllPortals = ap; g_dbgHudStretch = hs;
        *kPreset2Hide = h1 ? 1e7f : 1000.0f;
        if (g_log) { fprintf(g_log, "debug: set0 %s, set1 %s, portals %s, hud %s\n", h0 ? "hidden" : "normal", h1 ? "hidden" : "normal",
                             ap ? "all" : "normal", hs ? "stretched" : "4:3"); fflush(g_log); }
    }
}

// HUD and menus at 4:3 (O3). Every 2D quad goes through Queue2DSprite 0x004B8CC0 as (x, y, w, h) normalised 0..1 of
// the viewport; the HUD and menu layouts are PS1-style 4:3 (virtual 320x256 in play, 512x256 in menus,
// g_virtualScreenWidth 0x004F7414) stretched over the whole 16:9 screen. Each quad that does not span the full width
// is squeezed around the centre by s = (4/3) / (DestW/DestH) (GetDestWidth/Height 0x004ABD90 / 0x004ABDA0), after
// cropping it to the visible [0, 1] so panels sliding in from off-screen (HUD::TickSlide 0x0049FC60) stay hidden.
// Full-width quads keep the whole screen: tints (DrawTintOverlay 0x0044DF69), fades and transitions
// (DrawBackdropTransition 0x0049D847), cinematic bars (0x00440F2C), full-screen art. Lens flares are 3D points
// projected into 2D (LensFlare::RenderSlot 0x0044F580 -> DrawScaled) and must stay where the sun is: that function
// sets a guard. World sprites, the static menu backdrop (BltFast) and FMV do not go through Queue2DSprite.
typedef int (__cdecl* GetDestSize_t)();
static volatile int g_inLensFlare;
static void* g_lensFlareReturn;
static void* oLensFlareSlot;
__declspec(naked) static void hLensFlareSlot()                // any argument list: the caller's stack is passed on as is
{
    __asm {
        pop g_lensFlareReturn
        mov g_inLensFlare, 1
        call oLensFlareSlot
        mov g_inLensFlare, 0
        push g_lensFlareReturn
        ret
    }
}


typedef int (__cdecl* PortalVisible_t)(void* camera, void* portal);
static PortalVisible_t oPortalVisible;
static int __cdecl hPortalVisible(void* camera, void* portal) { return g_dbgAllPortals ? 1 : oPortalVisible(camera, portal); }

static void __cdecl hUpdateParallax(int pitch, int yaw)
{
    if (g_dbgTick++ % 60 == 0) ReadDebugSwitches();
    FlushUvStats();
    float farLimit = g_dbgHideSet0 ? 0.0f : kFarLimit;
    if (*kFarLimitSquared != farLimit) {
        static bool logged;
        if (!logged && g_log) { fprintf(g_log, "geometry far limit %g -> %g (no NaN in the near/far split)\n", *kFarLimitSquared, farLimit); fflush(g_log); logged = true; }
        *kFarLimitSquared = farLimit;
    }
    g_yaw = (float)(yaw & 0xFFFF) * (6.2831853f / 65536.0f);        // the angle UpdateParallax itself uses
    char* cam = *kCamera;
    if (cam) {
        float s = *(float*)(cam + 0x44) / kNarrow;
        if (s > 0.2f && s < 2.0f && s != g_lastS) {
            DWORD old;
            if (VirtualProtect(kTiltScale, 4, PAGE_READWRITE, &old)) {
                *kTiltScale = g_tilt0 / s;
                VirtualProtect(kTiltScale, 4, old, &old);
                g_lastS = s;
                if (g_log) { fprintf(g_log, "backdrop tilt scale %.4f (camera horizontal scale %.4f)\n", s, s * kNarrow); fflush(g_log); }
            }
        }
    }
    oUpdateParallax(pitch, yaw);
}

static void __cdecl hQueue2DSprite(float x, float y, float w, float h, const float* uvTL, const float* uvBR, int tex, uint32_t color, uint32_t flags)
{
    if (flags != kParallaxBG) {                                          // HUD / menus / text / tints (see O3 above)
        int dw = ((GetDestSize_t)0x004ABD90)(), dh = ((GetDestSize_t)0x004ABDA0)();
        float s = dw > 0 && dh > 0 ? (4.0f / 3.0f) * (float)dh / (float)dw : 1.0f;
        float left = w >= 0.0f ? x : x + w, right = w >= 0.0f ? x + w : x;
        if (g_dbgHudStretch || g_inLensFlare || !(s > 0.2f && s < 0.999f) || (left <= 0.001f && right >= 0.999f)) {
            oQueue2DSprite(x, y, w, h, uvTL, uvBR, tex, color, flags);
            return;
        }
        if (w > 0.0f && (x < 0.0f || x + w > 1.0f) && uvTL && uvBR) {     // crop to the screen the layout was made for
            float a = x < 0.0f ? 0.0f : x, b = x + w > 1.0f ? 1.0f : x + w;
            if (b <= a) return;
            float u0 = uvTL[0], u1 = uvBR[0];
            float tl[2] = { u0 + (u1 - u0) * (a - x) / w, uvTL[1] }, br[2] = { u0 + (u1 - u0) * (b - x) / w, uvBR[1] };
            oQueue2DSprite(0.5f + (a - 0.5f) * s, y, (b - a) * s, h, tl, br, tex, color, flags);
            return;
        }
        oQueue2DSprite(0.5f + (x - 0.5f) * s, y, w * s, h, uvTL, uvBR, tex, color, flags);
        return;
    }
    char* cam = *kCamera;
    if (!tex || *kStaticBackdrop || !cam || !(w > 0.0f)) { oQueue2DSprite(x, y, w, h, uvTL, uvBR, tex, color, flags); return; }
    float f = 1.0f / tanf(*(float*)(cam + 0x40) * 0.5f), hscale = *(float*)(cam + 0x44);
    if (!(f > 0.1f && f < 20.0f && hscale > 0.1f && hscale < 2.0f)) { oQueue2DSprite(x, y, w, h, uvTL, uvBR, tex, color, flags); return; }
    float tanHalf = 1.0f / (hscale * f);
    auto phase = [&](float sx) { return (atanf((2.0f * sx - 1.0f) * tanHalf) - g_yaw) * f; };   // in tiles

    // This call is one tile of the game's flat row covering [x, x + w]; its part of the screen gets the strips.
    float a = x > 0.0f ? x : 0.0f, b = x + w < 1.0f ? x + w : 1.0f;
    float s0 = a, p0 = phase(a);
    while (s0 < b) {
        float s1 = (floorf(s0 * kStrips + 1e-4f) + 1.0f) / kStrips;
        if (s1 > b) s1 = b;
        float p1 = phase(s1), tile = floorf(p0);
        if (p1 > tile + 1.0f) {                                   // a tile edge inside this strip: end the strip there
            float edge = tile + 1.0f;
            s1 = s0 + (edge - p0) / (p1 - p0) * (s1 - s0);
            p1 = edge;
        }
        float uvA[2] = { p0 - tile, uvTL[1] }, uvB[2] = { p1 - tile, uvBR[1] };
        oQueue2DSprite(s0, y, s1 - s0, h, uvA, uvB, tex, color, flags);
        s0 = s1; p0 = p1;                                         // after a tile edge p0 is a whole number: u starts at 0
    }
    if (g_log && g_stripFrames++ == 0) { fprintf(g_log, "backdrop drawn as a cylinder: view %.1f deg wide\n", 2.0f * atanf(tanHalf) * 57.29578f); fflush(g_log); }
}

// Writes 'patch' over 'original' at 'at' only if the original bytes are there (retail exe, nobody patched it first).
static bool Patch(uintptr_t at, const char* original, const char* patch, size_t n, const char* what)
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

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    wcscpy_s(wcsrchr(path, L'\\') + 1, MAX_PATH - (wcsrchr(path, L'\\') + 1 - path), L"ts2widescreen.log");
    g_log = _wfsopen(path, L"w", _SH_DENYNO);
    wcscpy_s(g_dbgPath, path);
    wcscpy_s(wcsrchr(g_dbgPath, L'\\') + 1, MAX_PATH - (wcsrchr(g_dbgPath, L'\\') + 1 - g_dbgPath), L"ts2debug.txt");
    g_tilt0 = *kTiltScale;
    if (Patch(0x00447BFD, "\x0F\xAF\xFF", "\x83\xCF\xFF", 3, "character/object draw distance, part 1 (imul edi,edi -> or edi,-1)"))
        Patch(0x00447C09, "\xC1\xE7\x04", "\xC1\xEF\x02", 3, "character/object draw distance, part 2 (shl edi,4 -> shr edi,2: unlimited)");
    Patch(0x0043E728, "\xB8\x80\x06\x00\x00", "\xB8\x00\x10\x00\x00", 5, "sprite/effect cull band (1664 -> 4096)");
    // Every reader of the band except the loader's table build (0x0043E735) gets 0x20000 instead of the variable.
    static const struct { uint32_t at; uint8_t n; const char* orig; const char* patch; } kBandReads[] = {
        // Not the pickup lists at 0x004412EA / 0x0044163C: pickups are drawn without a wall test and rely on their
        // range, so unlimited showed coins through walls (owner, 2026-09-28); they keep the band (4096).
        { 0x00449990, 5, "\xA1\xF0\xBE\x54\x00", "\xB8\x00\x00\x02\x00" },       // these compare linearly
        { 0x0044AA98, 5, "\xA1\xF0\xBE\x54\x00", "\xB8\x00\x00\x02\x00" },
        { 0x0044BD04, 5, "\xA1\xF0\xBE\x54\x00", "\xB8\x00\x00\x02\x00" },
        { 0x0044D3C2, 5, "\xA1\xF0\xBE\x54\x00", "\xB8\x00\x00\x02\x00" },
        { 0x00492628, 6, "\x8B\x0D\xF0\xBE\x54\x00", "\xB9\x00\x00\x02\x00\x90" },
        { 0x00492F73, 5, "\xA1\xF0\xBE\x54\x00", "\xB8\x00\x00\x02\x00" },
        { 0x00495CDE, 6, "\x8B\x1D\xF0\xBE\x54\x00", "\xBB\x00\x00\x02\x00\x90" },
        { 0x00495E21, 6, "\x8B\x1D\xF0\xBE\x54\x00", "\xBB\x00\x00\x02\x00\x90" },
        { 0x00495F51, 6, "\x8B\x1D\xF0\xBE\x54\x00", "\xBB\x00\x00\x02\x00\x90" },
        { 0x00496081, 6, "\x8B\x15\xF0\xBE\x54\x00", "\xBA\x00\x00\x02\x00\x90" },
        { 0x0049653D, 6, "\x8B\x1D\xF0\xBE\x54\x00", "\xBB\x00\x00\x02\x00\x90" },
        { 0x0049667C, 6, "\x8B\x1D\xF0\xBE\x54\x00", "\xBB\x00\x00\x02\x00\x90" },
        { 0x004967B4, 6, "\x8B\x1D\xF0\xBE\x54\x00", "\xBB\x00\x00\x02\x00\x90" },
        { 0x004968EF, 6, "\x8B\x35\xF0\xBE\x54\x00", "\xBE\x00\x00\x02\x00\x90" },
        { 0x0049751C, 6, "\x8B\x0D\xF0\xBE\x54\x00", "\xB9\x00\x00\x02\x00\x90" },
        { 0x004979AC, 6, "\x8B\x0D\xF0\xBE\x54\x00", "\xB9\x00\x00\x02\x00\x90" },
        { 0x00497CC6, 6, "\x8B\x0D\xF0\xBE\x54\x00", "\xB9\x00\x00\x02\x00\x90" },
    };
    int bandOk = 0;
    for (const auto& r : kBandReads) {
        char what[64]; sprintf_s(what, "object distance read at %08X (band -> 0x20000)", r.at);
        bandOk += Patch(r.at, r.orig, r.patch, r.n, what);
    }
    if (g_log) { fprintf(g_log, "object distance reads changed: %d of %d\n", bandOk, (int)(sizeof(kBandReads) / sizeof(kBandReads[0]))); fflush(g_log); }
    // Placed props: 5 renderers index the fade table by avg(corner depth >> 2) >> 2 (sar eax,4) and skip at 4096; the
    // corner depths are int16, which overflow at twice that depth, so >> 5 doubles the prop distance to that ceiling.
    static const uint32_t kPropIndex[] = { 0x00443726, 0x004448D7, 0x004452A0, 0x00446347, 0x00446CF1 };
    int propOk = 0;
    for (uint32_t at : kPropIndex) {
        char what[64]; sprintf_s(what, "prop depth index at %08X (sar 4 -> 5)", at);
        propOk += Patch(at, "\xC1\xF8\x04", "\xC1\xF8\x05", 3, what);
    }
    if (g_log) { fprintf(g_log, "prop renderers changed: %d of 5\n", propOk); fflush(g_log); }
    // Props fade in over 3 steps near the end of their range (owner: "the texture looks like have 3 layer … when we
    // aproach it, it loads the other layers"): the loader fills the depth table 0x00557C20 with 0 near, then 0x600,
    // 0x400 and 0x200 (ORed into each prop's draw command). All entries become 0, the near value: fully drawn at
    // every depth. The fill values are the two immediates and the tail store of bp (512) -> ax (0 after the first).
    int fadeOk = Patch(0x0043E793, "\xB8\x00\x06\x00\x06", "\xB8\x00\x00\x00\x00", 5, "prop fade table, 0x600 step -> 0");
    fadeOk += Patch(0x0043E7B3, "\xB8\x00\x04\x00\x04", "\xB8\x00\x00\x00\x00", 5, "prop fade table, 0x400 step -> 0");
    if (fadeOk == 2) Patch(0x0043E7CE, "\x66\x89\x2A", "\x66\x89\x02", 3, "prop fade table, 0x200 tail -> 0");
    // Allies and foes: each frame the active list is rebuilt from the 64 creature slots, taking those within
    // (362 * visibilityDistance >> 9) + radius/8 (units of 256) of Buzz (0x00408749); an inactive creature is not
    // drawn at all. >> 6 instead of >> 9: 8x farther (squares stay far below 2^31; the list cannot overflow).
    Patch(0x00408749, "\xC1\xFF\x09", "\xC1\xFF\x06", 3, "creature activation radius (sar 9 -> 6: 8x)");
    // Detailed shapes at every distance (see the two versions above): each preset's detailed far clip -> the far plane.
    // (Tried before and reverted 2026-09-29, owner: "this is awful": hiding the coarse twins, forcing the detailed pass
    //  through the grid pass (0x004CDF8B / 0x004CDFD7), a clamped grid cell. All treated the missing detailed copy as an
    //  area/portal problem; this level has one area for both sets, and the copy was cut by this plane.)
    int farOk = 0;
    for (int i = 0; i < 3; ++i) {
        char what[80]; sprintf_s(what, "preset %d detailed far clip %.0f -> %.0f (the far plane)", i, kPresetDetailFar0[i], kViewFar);
        farOk += Patch((uintptr_t)kPresetDetailFar[i], (const char*)&kPresetDetailFar0[i], (const char*)&kViewFar, 4, what);
    }
    if (g_log) { fprintf(g_log, "detailed shapes drawn to the far plane: %d of 3 presets\n", farOk); fflush(g_log); }
    MH_STATUS s = MH_Initialize();
    if (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED) s = MH_CreateHook((void*)0x0048F230, (void*)hUpdateParallax, (void**)&oUpdateParallax);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004B8CC0, (void*)hQueue2DSprite, (void**)&oQueue2DSprite);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004BACF0, (void*)hPortalVisible, (void**)&oPortalVisible);   // debug switch only
    if (s == MH_OK) s = MH_CreateHook((void*)0x004B32E0, (void*)hCreateAllVertexBuffers, (void**)&oCreateAllVertexBuffers);
    if (s == MH_OK) s = MH_CreateHook((void*)0x0044F580, (void*)hLensFlareSlot, (void**)&oLensFlareSlot);
    if (s == MH_OK) s = MH_EnableHook(MH_ALL_HOOKS);
    ReadDebugSwitches();                                      // switches that matter before the first level frame
    if (g_log) { fprintf(g_log, "ts2widescreen loaded; tilt constant %.6f; hooks %d\n", g_tilt0, (int)s); fflush(g_log); }
    return TRUE;
}
