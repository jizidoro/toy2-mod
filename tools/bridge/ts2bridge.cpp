// ts2bridge.asi — sends each finished game frame to ts2present64.exe (64-bit), whose window NVIDIA Smooth Motion
// can frame-generate (the driver's NvPresent64.dll does not exist for 32-bit processes like toy2.exe).
// Hooks IDXGISwapChain::Present / Present1 (dgVoodoo presents the game through D3D11), copies the back buffer into
// a shared NT-handle texture guarded by a keyed mutex, signals the presenter, then presents as usual.
// The copy uses a 0 ms mutex acquire: if the presenter is busy or not running, that frame is simply not sent.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include "minhook.h"
#include "ts2bridge.h"

static const volatile LONG* const kDemoMode = (const volatile LONG*)0x0052AD94;   // toy2-decomp Toy2.cpp g_demoMode

typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1_t)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static Present_t oPresent;
static Present1_t oPresent1;

static wchar_t g_dir[MAX_PATH];
static FILE* g_log;
static HWND g_dummyHwnd;
static BridgeInfo* g_info;
static HANDLE g_event;
static ID3D11Texture2D* g_shared;
static IDXGIKeyedMutex* g_mutex;
static HANDLE g_sharedHandle;
static bool g_presenterStarted;
static LONG g_demoFrame = -1;
static unsigned g_sent, g_dropped;
static thread_local bool t_inPresent;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log);
}

static void StartPresenter()
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32];
    swprintf_s(exe, L"%ls\\ts2present64.exe", g_dir);
    swprintf_s(cmd, L"\"%ls\" %lu", exe, GetCurrentProcessId());
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
    // CREATE_NO_WINDOW: a console window would take the foreground, and the game minimizes when it loses focus.
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_dir, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        Log("presenter started, pid %lu\n", pi.dwProcessId);
    } else {
        Log("presenter %ls could not start: error %lu\n", exe, GetLastError());
    }
    g_presenterStarted = true;
}

static bool EnsureShared(ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& bb, HWND hwnd)
{
    if (g_shared && g_info->width == bb.Width && g_info->height == bb.Height && g_info->format == (UINT)bb.Format) return true;
    if (g_mutex) { g_mutex->Release(); g_mutex = NULL; }
    if (g_shared) { g_shared->Release(); g_shared = NULL; }
    if (g_sharedHandle) { CloseHandle(g_sharedHandle); g_sharedHandle = NULL; }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = bb.Width; td.Height = bb.Height; td.MipLevels = 1; td.ArraySize = 1; td.Format = bb.Format;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    HRESULT hr = dev->CreateTexture2D(&td, NULL, &g_shared);
    if (FAILED(hr)) { Log("CreateTexture2D(shared %ux%u fmt %u) failed %08lx\n", bb.Width, bb.Height, bb.Format, hr); return false; }
    g_shared->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&g_mutex);
    IDXGIResource1* r1 = NULL;
    g_shared->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
    LONG gen = g_info->generation + 1;
    swprintf_s(g_info->texName, L"Local\\ts2bridge_tex_%lu_%ld", GetCurrentProcessId(), gen);
    hr = r1 ? r1->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, g_info->texName, &g_sharedHandle) : E_NOINTERFACE;
    if (r1) r1->Release();
    if (FAILED(hr) || !g_mutex) { Log("CreateSharedHandle failed %08lx\n", hr); g_shared->Release(); g_shared = NULL; return false; }
    g_info->width = bb.Width; g_info->height = bb.Height; g_info->format = bb.Format;
    g_info->gameHwnd = (DWORD)(ULONG_PTR)hwnd;
    InterlockedExchange(&g_info->generation, gen);
    Log("shared texture %ls: %ux%u format %u (back buffer samples %u)\n", g_info->texName, bb.Width, bb.Height, bb.Format, bb.SampleDesc.Count);
    return true;
}

static void SaveSourceBmp(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src, const wchar_t* name)
{
    D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0; d.SampleDesc.Count = 1;
    ID3D11Texture2D* st = NULL;
    if (FAILED(dev->CreateTexture2D(&d, NULL, &st))) return;
    ctx->CopyResource(st, src);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        DWORD row = (d.Width * 3 + 3) & ~3u;
        BYTE* out = (BYTE*)malloc((size_t)row * d.Height);
        for (UINT y = 0; out && y < d.Height; ++y) {
            const BYTE* s = (const BYTE*)m.pData + (size_t)y * m.RowPitch;
            BYTE* o = out + (size_t)(d.Height - 1 - y) * row;
            for (UINT x = 0; x < d.Width; ++x) { o[x*3] = s[x*4 + (rgba ? 2 : 0)]; o[x*3+1] = s[x*4+1]; o[x*3+2] = s[x*4 + (rgba ? 0 : 2)]; }
        }
        ctx->Unmap(st, 0);
        wchar_t path[MAX_PATH]; swprintf_s(path, L"%ls\\ts2diag\\%ls-source.bmp", g_dir, name);
        FILE* f = NULL;
        if (out && _wfopen_s(&f, path, L"wb") == 0 && f) {
            BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
            ih.biSize = sizeof(ih); ih.biWidth = d.Width; ih.biHeight = d.Height; ih.biPlanes = 1; ih.biBitCount = 24; ih.biSizeImage = row * d.Height;
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
            fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(out, ih.biSizeImage, 1, f); fclose(f);
            Log("saved source frame %ld -> %ls\n", g_info->captureFrameId, path);
        }
        free(out);
    }
    st->Release();
}

static void CheckTransferRequest()
{
    wchar_t req[MAX_PATH]; swprintf_s(req, L"%ls\\ts2diag\\bridge.req", g_dir);
    FILE* f = NULL;
    if (g_info->captureFrameId || _wfopen_s(&f, req, L"rt") != 0 || !f) return;
    wchar_t name[64] = L"transfer";
    if (fgetws(name, 64, f)) name[wcscspn(name, L"\r\n ")] = 0;
    fclose(f);
    DeleteFileW(req);
    wcscpy_s(g_info->captureName, name[0] ? name : L"transfer");
    InterlockedExchange(&g_info->captureFrameId, g_info->frameId + 60);
    Log("transfer check %ls requested for frame %ld\n", g_info->captureName, g_info->captureFrameId);
}

static void SendFrame(IDXGISwapChain* sc)
{
    if ((g_sent + g_dropped) % 30 == 0) CheckTransferRequest();
    DXGI_SWAP_CHAIN_DESC d;
    if (FAILED(sc->GetDesc(&d)) || d.OutputWindow == g_dummyHwnd) return;
    LONG demo = *kDemoMode;
    g_demoFrame = demo == 1 ? g_demoFrame + 1 : -1;

    ID3D11Texture2D* bb = NULL;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    ID3D11Device* dev = NULL; bb->GetDevice(&dev);
    if (dev && EnsureShared(dev, bd, d.OutputWindow)) {
        if (!g_presenterStarted) StartPresenter();
        ID3D11DeviceContext* ctx = NULL; dev->GetImmediateContext(&ctx);
        if (g_mutex->AcquireSync(0, 0) == S_OK) {
            if (bd.SampleDesc.Count > 1) ctx->ResolveSubresource(g_shared, 0, bb, 0, bd.Format);
            else ctx->CopyResource(g_shared, bb);
            LONG id = g_info->frameId + 1;
            // the source is dgVoodoo's own back buffer, so the check covers the GPU copy and the cross-process read
            if (g_info->captureFrameId && id == g_info->captureFrameId && bd.SampleDesc.Count == 1) SaveSourceBmp(dev, ctx, bb, g_info->captureName);
            g_mutex->ReleaseSync(1);
            g_info->demoFrame = g_demoFrame;
            InterlockedIncrement(&g_info->frameId);
            SetEvent(g_event);
            ++g_sent;
        } else {
            ++g_dropped;
        }
        if ((g_sent + g_dropped) % 600 == 0) Log("frames sent %u, dropped %u (presenter busy or absent)\n", g_sent, g_dropped);
        ctx->Release();
    }
    if (dev) dev->Release();
    bb->Release();
}

static HRESULT STDMETHODCALLTYPE hPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    if (!t_inPresent && !(flags & DXGI_PRESENT_TEST)) SendFrame(sc);
    t_inPresent = true;
    HRESULT hr = oPresent(sc, sync, flags);
    t_inPresent = false;
    return hr;
}

static HRESULT STDMETHODCALLTYPE hPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p)
{
    if (!t_inPresent && !(flags & DXGI_PRESENT_TEST)) SendFrame(sc);
    t_inPresent = true;
    HRESULT hr = oPresent1(sc, sync, flags, p);
    t_inPresent = false;
    return hr;
}

// Present lives in dxgi.dll and is shared by every swapchain: find it through a throwaway swapchain, once
// dgVoodoo has loaded d3d11.dll (never inside DllMain).
static DWORD WINAPI InstallHooks(LPVOID)
{
    while (!GetModuleHandleW(L"d3d11.dll")) Sleep(50);
    WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"ts2bridge_dummy";
    RegisterClassW(&wc);
    g_dummyHwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 16, 16, NULL, NULL, wc.hInstance, NULL);
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1; sd.BufferDesc.Width = 16; sd.BufferDesc.Height = 16; sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = g_dummyHwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = NULL; ID3D11Device* dev = NULL; ID3D11DeviceContext* ctx = NULL;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx);
    if (FAILED(hr)) { Log("dummy swapchain failed %08lx\n", hr); return 0; }
    void** vt = *(void***)sc;
    MH_STATUS s1 = MH_CreateHook(vt[8], (void*)hPresent, (void**)&oPresent);
    MH_STATUS s2 = MH_ERROR_NOT_CREATED;
    IDXGISwapChain1* sc1 = NULL;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1))) {
        s2 = MH_CreateHook((*(void***)sc1)[22], (void*)hPresent1, (void**)&oPresent1);
        sc1->Release();
    }
    MH_STATUS se = MH_EnableHook(MH_ALL_HOOKS);
    Log("Present hook %d, Present1 hook %d, enable %d\n", s1, s2, se);
    sc->Release(); ctx->Release(); dev->Release();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    GetModuleFileNameW(NULL, g_dir, MAX_PATH);
    *wcsrchr(g_dir, L'\\') = 0;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%ls\\ts2bridge.log", g_dir);
    _wfopen_s(&g_log, path, L"w");

    wchar_t name[64];
    swprintf_s(name, L"Local\\ts2bridge_info_%lu", GetCurrentProcessId());
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(BridgeInfo), name);
    g_info = map ? (BridgeInfo*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeInfo)) : NULL;
    swprintf_s(name, L"Local\\ts2bridge_frame_%lu", GetCurrentProcessId());
    g_event = CreateEventW(NULL, FALSE, FALSE, name);
    if (!g_info || !g_event) { Log("shared objects failed\n"); return TRUE; }
    g_info->demoFrame = -1;

    MH_Initialize();
    CloseHandle(CreateThread(NULL, 0, InstallHooks, NULL, 0, NULL));
    Log("ts2bridge loaded\n");
    return TRUE;
}
