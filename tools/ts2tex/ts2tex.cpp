// ts2tex.asi — textures for toy2.exe (retail, sha256 023eb6a9...). Addresses: toy2-decomp Nu3D/BmpDataNode.cpp.
// Every texture, level and HUD alike, is loaded by LoadTextureByStream 0x004B0A30: a BMP becomes a square power-of-two
// BGRA buffer (texData, bottom row first), which InitialiseTextureSurface 0x004B0200 uploads to a D3D surface.
//  1. 32-bit: FindSuitablePixelFormat 0x004B0380 accepts only 16-bit formats (colour banding; alpha textures get 4 bits
//     per channel). A 32-bit format is preferred, 16-bit stays the fallback; CopyTextureToSurface 0x004AFF80 writes
//     uint16 only, so 32-bit surfaces are filled here.
//  2. Size cap: g_maxTextureSize 0x00508214 256 -> 2048, so a source bitmap larger than 256 keeps its resolution.
//  3. Dump: while game\texdump\ exists, each texture is written there once as <key>.png (what the game shows, at the
//     source bitmap's resolution, top row first, with its alpha), plus a line in texdump\index.csv.
//  4. Replace: game\mods\<mod>\textures\<key>*.png replaces the texture with that key, at any resolution (stretched to
//     the next power-of-two square, the way the game stretches its own bitmaps). Mods in the ts2mods order:
//     game\mods\load_order.txt, else alphabetical; the first one with a key wins.
// Key: the first 16 hex digits of SHA-1 over width, height and bits per pixel (3 little-endian int32), then the
// 1024-byte palette for 8-bit, then the pixel bytes the game reads. Level textures are all named texN in every level,
// so the name can't be the key; tools\ts2tex\scan.py computes the same key from the data files.
#define NOMINMAX
#include <windows.h>
#include <ddraw.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdint.h>
#include <share.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "minhook.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "stb_image_write.h"

struct BmpDataNode {
    IDirectDrawSurface4* surface; void* d3dTexture; DDSURFACEDESC2 surfaceDesc;
    uint32_t* texData; int32_t textureWidth, textureHeight; uint32_t bitmapWidth, bitmapHeight; HBITMAP bitmapHandle;
    int32_t unk[4]; char texName[80]; int32_t unk5; int32_t flags; int32_t refCount; BmpDataNode* next; BmpDataNode* prev;
};
static_assert(sizeof(BmpDataNode) == 0x110, "BmpDataNode");
struct FindPixelFormat { uint32_t bpp; int32_t minAlphaBits; uint32_t needAlpha; uint32_t valid; DDPIXELFORMAT* out; };

typedef BmpDataNode* (__cdecl* LoadTex_t)(void* file, const char* name, int32_t flags);
typedef uint32_t* (__cdecl* ProcessPixels_t)(HBITMAP mainBmp, HBITMAP alphaBmp, int32_t flags);
typedef LONG (WINAPI* FindFormat_t)(DDPIXELFORMAT* pf, void* ctx);
typedef void (__cdecl* CopyTex_t)(BmpDataNode* node);
typedef int32_t (__cdecl* InitSurface_t)(BmpDataNode* node);
static const InitSurface_t InitialiseTextureSurface = (InitSurface_t)0x004B0200;
static void* (__cdecl* const GameMalloc)(size_t) = (void* (__cdecl*)(size_t))0x004CF44C;   // texData is freed by the game
static void (__cdecl* const GameFree)(void*) = (void (__cdecl*)(void*))0x004CEE5E;
static const uint8_t* const kLastPalette = (const uint8_t*)0x00884044;                     // g_lastBmpPalette
static int32_t* const kMaxTextureSize = (int32_t*)0x00508214;
static char* const* const kCurFileName = (char* const*)0x00AAD7AC;                          // NGN file being parsed
static const int32_t kNewMaxTextureSize = 2048, kMaxReplacementSize = 4096;

static LoadTex_t oLoadTex; static ProcessPixels_t oProcessPixels; static FindFormat_t oFindFormat; static CopyTex_t oCopyTex;

static std::wstring g_gameDir, g_dumpDir;
static FILE* g_log;
static std::unordered_map<std::string, std::wstring> g_pack;       // key -> replacement png
static std::unordered_set<std::string> g_dumped;
static std::unordered_set<std::string> g_formatsSeen;
static unsigned g_loaded, g_replaced, g_dumpedCount, g_copied32, g_copied16, g_failed;
static DWORD g_lastSummary;
static BCRYPT_ALG_HANDLE g_sha1;

// What ProcessBmpPixelData saw for the texture being loaded (the game loads on one thread).
static struct { bool valid; std::string key; int w, h, bpp; std::vector<uint8_t> rgba; } g_src;

static void Log(const char* fmt, ...) { if (!g_log) return; va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
static std::string Narrow(const std::wstring& w) { char b[1024]; WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, b, sizeof(b), NULL, NULL); return b; }

static std::string Key(int w, int h, int bpp, const uint8_t* palette, const uint8_t* bits, size_t size)
{
    BCRYPT_HASH_HANDLE hh = NULL; uint8_t d[20] = {};
    int32_t head[3] = { w, h, bpp };
    if (!g_sha1 && BCryptOpenAlgorithmProvider(&g_sha1, BCRYPT_SHA1_ALGORITHM, NULL, 0) != 0) return "";   // not in DllMain: loads DLLs
    if (BCryptCreateHash(g_sha1, &hh, NULL, 0, NULL, 0, 0) != 0) return "";
    BCryptHashData(hh, (PUCHAR)head, sizeof(head), 0);
    if (palette) BCryptHashData(hh, (PUCHAR)palette, 1024, 0);
    BCryptHashData(hh, (PUCHAR)bits, (ULONG)size, 0);
    BCryptFinishHash(hh, d, sizeof(d), 0);
    BCryptDestroyHash(hh);
    char hex[17]; for (int i = 0; i < 8; ++i) sprintf_s(hex + 2 * i, 3, "%02x", d[i]);
    return hex;
}

// SampleBitmapPixel 0x004B0870 at the bitmap's own resolution; memory row 0 is the bottom row.
static void BuildDumpImage(const DIBSECTION& m, const DIBSECTION* a, int flags, std::vector<uint8_t>& out)
{
    int w = m.dsBm.bmWidth, h = m.dsBm.bmHeight, bpp = m.dsBm.bmBitsPixel;
    out.assign((size_t)w * h * 4, 0);
    for (int j = 0; j < h; ++j) {
        int row = h - 1 - j;
        const uint8_t* src = (const uint8_t*)m.dsBm.bmBits + (size_t)m.dsBm.bmWidthBytes * row;
        for (int x = 0; x < w; ++x) {
            uint8_t b, g, r, al = 0xFF;
            if (bpp == 8) { const uint8_t* p = kLastPalette + 4 * src[x]; b = p[0]; g = p[1]; r = p[2]; }
            else { const uint8_t* p = src + 3 * x; b = p[0]; g = p[1]; r = p[2]; }
            uint32_t v = 0xFF000000u | (r << 16) | (g << 8) | b;
            bool zero = false;
            if (flags & 2) al = v != 0xFFFFFFFFu ? 0xFF : 0;
            else if (flags & 4) al = v != 0xFF000000u ? 0xFF : 0;
            else if ((flags & 8) && v == 0xFF00FF00u) zero = true;
            if (zero) { r = g = b = al = 0; }
            if (bpp == 24 && a && a->dsBm.bmBitsPixel == 8) {
                int ax = x * a->dsBm.bmWidth / w, ay = row * a->dsBm.bmHeight / h;
                al = *((const uint8_t*)a->dsBm.bmBits + (size_t)a->dsBm.bmWidthBytes * ay + ax);
            }
            uint8_t* o = &out[((size_t)j * w + x) * 4]; o[0] = r; o[1] = g; o[2] = b; o[3] = al;
        }
    }
}

static uint32_t* __cdecl hProcessPixels(HBITMAP mainBmp, HBITMAP alphaBmp, int32_t flags)
{
    g_src.valid = false;
    DIBSECTION m = {}, a = {};
    if (GetObjectA(mainBmp, sizeof(m), &m) == sizeof(m) && m.dsBm.bmBits && (m.dsBm.bmBitsPixel == 8 || m.dsBm.bmBitsPixel == 24)) {
        int w = m.dsBm.bmWidth, h = m.dsBm.bmHeight, bpp = m.dsBm.bmBitsPixel;
        size_t size = bpp == 8 ? (size_t)w * h : (size_t)w * (h * 24) / 8;                // the game's fread size
        g_src.key = Key(w, h, bpp, bpp == 8 ? kLastPalette : NULL, (const uint8_t*)m.dsBm.bmBits, size);
        g_src.w = w; g_src.h = h; g_src.bpp = bpp; g_src.valid = !g_src.key.empty();
        g_src.rgba.clear();
        if (g_src.valid && !g_dumpDir.empty() && !g_dumped.count(g_src.key)) {
            bool haveAlpha = alphaBmp && GetObjectA(alphaBmp, sizeof(a), &a) == sizeof(a) && a.dsBm.bmBits;
            BuildDumpImage(m, haveAlpha ? &a : NULL, flags, g_src.rgba);
        }
    }
    return oProcessPixels(mainBmp, alphaBmp, flags);
}

// Stretch an RGBA image (top row first) to size x size, bilinear, into texData order (bottom row first, BGRA).
static void FillTexData(const uint8_t* img, int W, int H, int size, uint32_t* dst)
{
    for (int Y = 0; Y < size; ++Y) {
        uint32_t* row = dst + (size_t)(size - 1 - Y) * size;
        float v = (Y + 0.5f) * H / size - 0.5f; if (v < 0) v = 0;
        int y0 = (int)v, y1 = std::min(y0 + 1, H - 1); float fy = v - y0;
        for (int X = 0; X < size; ++X) {
            float u = (X + 0.5f) * W / size - 0.5f; if (u < 0) u = 0;
            int x0 = (int)u, x1 = std::min(x0 + 1, W - 1); float fx = u - x0;
            const uint8_t *p00 = img + ((size_t)y0 * W + x0) * 4, *p01 = img + ((size_t)y0 * W + x1) * 4;
            const uint8_t *p10 = img + ((size_t)y1 * W + x0) * 4, *p11 = img + ((size_t)y1 * W + x1) * 4;
            uint8_t c[4];
            for (int k = 0; k < 4; ++k)
                c[k] = (uint8_t)((p00[k] * (1 - fx) + p01[k] * fx) * (1 - fy) + (p10[k] * (1 - fx) + p11[k] * fx) * fy + 0.5f);
            row[X] = ((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
        }
    }
}

static void Replace(BmpDataNode* node, const std::wstring& path)
{
    FILE* f = NULL;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) { ++g_failed; Log("replace %s: cannot open %s\n", g_src.key.c_str(), Narrow(path).c_str()); return; }
    int W = 0, H = 0, n = 0;
    uint8_t* img = stbi_load_from_file(f, &W, &H, &n, 4);
    fclose(f);
    if (!img) { ++g_failed; Log("replace %s: not a PNG (%s)\n", g_src.key.c_str(), stbi_failure_reason()); return; }
    int size = 1; while (size < W || size < H) size <<= 1;
    size = std::min(size, kMaxReplacementSize);
    uint32_t* data = (uint32_t*)GameMalloc((size_t)size * size * 4);
    if (!data) { ++g_failed; stbi_image_free(img); Log("replace %s: out of memory for %dx%d\n", g_src.key.c_str(), size, size); return; }
    FillTexData(img, W, H, size, data);
    stbi_image_free(img);
    // Fully transparent pixels get the colour the game's own keyed pixels have (SampleBitmapPixel), because some
    // effects blend by colour and ignore alpha: shadows darken by ZERO / INVSRCCOLOR, glows add ONE / ONE. A
    // replacement's colour there (the upscaler's bleed) would draw the whole quad, e.g. a dark square under Buzz.
    // Key 8: transparent black. Key 4 / 2: black / white. No key (tex14, 36, 37): the pure green the game shows.
    uint32_t clear = (node->flags & 8) ? 0x00000000u : (node->flags & 4) ? 0x00000000u : (node->flags & 2) ? 0x00FFFFFFu : 0xFF00FF00u;
    if (!(node->flags & 1))
        for (size_t i = 0, n = (size_t)size * size; i < n; ++i)
            if ((data[i] >> 24) == 0) data[i] = clear;
    GameFree(node->texData);
    node->texData = data;
    node->textureWidth = node->textureHeight = size;
    if (InitialiseTextureSurface(node)) ++g_replaced;
    else { ++g_failed; Log("replace %s (%s): surface %dx%d not created\n", g_src.key.c_str(), node->texName, size, size); }
}

static void Dump(const char* name, int flags)
{
    std::wstring png = g_dumpDir + L"\\" + std::wstring(g_src.key.begin(), g_src.key.end()) + L".png";
    FILE* f = NULL;
    if (_wfopen_s(&f, png.c_str(), L"wb") == 0 && f) {
        stbi_write_png_to_func([](void* ctx, void* data, int size) { fwrite(data, 1, size, (FILE*)ctx); }, f,
                               g_src.w, g_src.h, 4, g_src.rgba.data(), g_src.w * 4);
        fclose(f);
    }
    FILE* idx = _wfsopen((g_dumpDir + L"\\index.csv").c_str(), L"a", _SH_DENYNO);
    if (idx) {
        fseek(idx, 0, SEEK_END);
        if (ftell(idx) == 0) fprintf(idx, "key,name,width,height,bpp,flags,file\n");
        const char* file = *kCurFileName ? *kCurFileName : "";
        fprintf(idx, "%s,%s,%d,%d,%d,%d,%s\n", g_src.key.c_str(), name ? name : "", g_src.w, g_src.h, g_src.bpp, flags, file);
        fclose(idx);
    }
    g_dumped.insert(g_src.key);
    ++g_dumpedCount;
}

static BmpDataNode* __cdecl hLoadTex(void* file, const char* name, int32_t flags)
{
    g_src.valid = false;
    BmpDataNode* node = oLoadTex(file, name, flags);
    if (node && g_src.valid) {
        ++g_loaded;
        if (!g_src.rgba.empty()) Dump(name, flags);
        auto it = g_pack.find(g_src.key);
        if (it != g_pack.end() && !(node->flags & 0x40) && node->texData) Replace(node, it->second);
    }
    g_src.valid = false;
    if (GetTickCount() - g_lastSummary > 3000) {
        g_lastSummary = GetTickCount();
        Log("textures loaded %u, replaced %u, dumped %u, failed %u; surfaces filled 32-bit %u, 16-bit %u\n",
            g_loaded, g_replaced, g_dumpedCount, g_failed, g_copied32, g_copied16);
    }
    return node;
}

static LONG WINAPI hFindFormat(DDPIXELFORMAT* pf, void* ctx)
{
    FindPixelFormat* f = (FindPixelFormat*)ctx;
    if (pf && g_formatsSeen.size() < 64) {
        char d[96]; sprintf_s(d, "%lu bpp flags %08lx A %08lx R %08lx G %08lx B %08lx", pf->dwRGBBitCount, pf->dwFlags,
                              pf->dwRGBAlphaBitMask, pf->dwRBitMask, pf->dwGBitMask, pf->dwBBitMask);
        if (g_formatsSeen.insert(d).second) Log("texture format offered: %s\n", d);
    }
    if (!pf || !f || f->bpp != 16) return oFindFormat(pf, ctx);
    f->bpp = 32;
    LONG r = oFindFormat(pf, ctx);                          // 0 = a 32-bit match, copied out: stop enumerating
    f->bpp = 16;
    if (r == 0) return 0;
    if (!f->valid) oFindFormat(pf, ctx);                   // the first 16-bit match is kept as the fallback
    return DDENUMRET_OK;                                    // and a 32-bit format is still looked for
}

static int Shift(DWORD mask, uint8_t c)                    // an 8-bit channel into the bits of mask
{
    if (!mask) return 0;
    unsigned long low = 0; _BitScanForward(&low, mask);
    int bits = __popcnt(mask);
    return (int)(((uint32_t)c >> (8 - std::min(bits, 8))) << low) & mask;
}

static void __cdecl hCopyTex(BmpDataNode* node)
{
    if (node->surfaceDesc.ddpfPixelFormat.dwRGBBitCount != 32) { ++g_copied16; oCopyTex(node); return; }
    DDSURFACEDESC2 sd = {}; sd.dwSize = sizeof(sd);
    HRESULT hr;
    do hr = node->surface->Lock(NULL, &sd, DDLOCK_NOSYSLOCK, NULL); while (hr == DDERR_WASSTILLDRAWING);
    if (FAILED(hr)) { Log("lock failed %08lx (%s)\n", hr, node->texName); return; }
    const DDPIXELFORMAT& pf = sd.ddpfPixelFormat;
    bool alpha = (node->flags & 0xF) != 0;
    int w = node->textureWidth, h = node->textureHeight;
    for (int row = 0; row < h; ++row) {
        const uint8_t* s = (const uint8_t*)&node->texData[(size_t)w * (h - row - 1)];
        uint32_t* d = (uint32_t*)((uint8_t*)sd.lpSurface + (size_t)row * sd.lPitch);
        for (int x = 0; x < w; ++x, s += 4)
            d[x] = Shift(pf.dwRBitMask, s[2]) | Shift(pf.dwGBitMask, s[1]) | Shift(pf.dwBBitMask, s[0]) |
                   Shift(pf.dwRGBAlphaBitMask, alpha ? s[3] : 0xFF);
    }
    node->surface->Unlock(NULL);
    ++g_copied32;
}

static void ReadPack()
{
    std::wstring mods = g_gameDir + L"\\mods";
    std::vector<std::wstring> order;
    FILE* f = NULL;
    if (_wfopen_s(&f, (mods + L"\\load_order.txt").c_str(), L"rt") == 0 && f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
            s.erase(0, s.find_first_not_of(" \t"));
            if (s.empty() || s[0] == '#') continue;
            wchar_t w[512]; MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, 512);
            order.push_back(w);
        }
        fclose(f);
    } else {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((mods + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') order.push_back(fd.cFileName);
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(order.begin(), order.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    }
    for (const auto& mod : order) {
        std::wstring dir = mods + L"\\" + mod + L"\\textures";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        unsigned n = 0;
        do {
            std::string key;
            for (int i = 0; i < 16 && iswxdigit(fd.cFileName[i]); ++i) key += (char)towlower(fd.cFileName[i]);
            if (key.size() == 16 && g_pack.emplace(key, dir + L"\\" + fd.cFileName).second) ++n;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        Log("mod %s: %u textures\n", Narrow(mod).c_str(), n);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    *wcsrchr(exe, L'\\') = 0;
    g_gameDir = exe;
    g_log = _wfsopen((g_gameDir + L"\\ts2tex.log").c_str(), L"w", _SH_DENYNO);
    DWORD attr = GetFileAttributesW((g_gameDir + L"\\texdump").c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) g_dumpDir = g_gameDir + L"\\texdump";
    ReadPack();
    Log("ts2tex loaded; replacements %u; dump %s; size cap %d -> %d\n", (unsigned)g_pack.size(),
        g_dumpDir.empty() ? "off (no game\\texdump folder)" : "on", *kMaxTextureSize, kNewMaxTextureSize);
    *kMaxTextureSize = kNewMaxTextureSize;

    MH_STATUS s = MH_Initialize();
    if (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED) s = MH_CreateHook((void*)0x004B0A30, (void*)hLoadTex, (void**)&oLoadTex);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004B07A0, (void*)hProcessPixels, (void**)&oProcessPixels);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004B0380, (void*)hFindFormat, (void**)&oFindFormat);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004AFF80, (void*)hCopyTex, (void**)&oCopyTex);
    if (s == MH_OK) s = MH_EnableHook(MH_ALL_HOOKS);
    Log("hooks: %s (%d)\n", s == MH_OK ? "ok" : "FAILED", (int)s);
    return TRUE;
}
