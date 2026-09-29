// ts2present64.exe <game pid> — shows toy2.exe's frames (sent by ts2bridge.asi through a shared texture) in a
// 64-bit D3D11 flip-model window laid exactly over the game window. Being 64-bit, NVIDIA Smooth Motion can load
// here (NvPresent64.dll) when its driver profile enables it: 60 game frames -> 120 displayed.
// The window never takes focus (WS_EX_NOACTIVATE), so keyboard input stays with the game; it hides whenever the
// game is not the foreground app (alt-tab) and exits with the game.
// "<name>@demo:<N>" lines in game\ts2diag\present.req save the frame received for attract-demo frame N to
// game\ts2diag\<name>.bmp (for comparing against ts2diag's own capture of that frame).
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <psapi.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <d3dcompiler.h>
#include "ts2bridge.h"

// Supersampled frames (dgVoodoo [DirectX] Resolution = 2x sends 7680x4320) are drawn scaled to the game window's
// size instead of presenting a swapchain of the frame's size: at 8K, Smooth Motion displayed 64-92 images/s instead
// of 120 (measured 2026-09-29, dispfps on DISPLAY1). At an exact 2:1 ratio, bilinear sampling at each output pixel's
// centre lands on the corner of a 2x2 source block: a box average. Frames of the window's size keep the plain copy.
static const char kScaleHlsl[] =
    "struct V { float4 p : SV_Position; float2 t : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { V o; o.t = float2((id << 1) & 2, id & 2); o.p = float4(o.t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
    "Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
    "float4 ps(V i) : SV_Target { return t0.Sample(s0, i.t); }\n";
static ID3D11VertexShader* g_vs; static ID3D11PixelShader* g_ps; static ID3D11SamplerState* g_smp;
static void Log(const char* fmt, ...);

static bool CreateScaler(ID3D11Device* dev)
{
    ID3DBlob *v = NULL, *p = NULL, *err = NULL;
    if (FAILED(D3DCompile(kScaleHlsl, sizeof(kScaleHlsl) - 1, "scale", NULL, NULL, "vs", "vs_5_0", 0, 0, &v, &err)) ||
        FAILED(D3DCompile(kScaleHlsl, sizeof(kScaleHlsl) - 1, "scale", NULL, NULL, "ps", "ps_5_0", 0, 0, &p, &err))) {
        Log("scaler shader compile failed: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return false;
    }
    dev->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), NULL, &g_vs);
    dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), NULL, &g_ps);
    v->Release(); p->Release();
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &g_smp);
    return g_vs && g_ps && g_smp;
}

static FILE* g_log;
static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log);
}

struct DemoShot { std::wstring name; LONG frame; };
static std::vector<DemoShot> g_shots;
static std::wstring g_diagDir;

static void ReadShotRequests()
{
    std::wstring req = g_diagDir + L"\\present.req";
    FILE* f = NULL;
    if (_wfopen_s(&f, req.c_str(), L"rt") != 0 || !f) return;
    wchar_t line[128];
    while (fgetws(line, 128, f)) {
        std::wstring s(line);
        s.erase(s.find_last_not_of(L" \r\n\t") + 1);
        size_t at = s.find(L"@demo:");
        if (at != std::wstring::npos) g_shots.push_back({ s.substr(0, at), _wtol(s.c_str() + at + 6) });
    }
    fclose(f);
    DeleteFileW(req.c_str());
}

static void SaveBmp(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src, const std::wstring& name)
{
    D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ID3D11Texture2D* st = NULL;
    if (FAILED(dev->CreateTexture2D(&d, NULL, &st))) return;
    ctx->CopyResource(st, src);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        DWORD row = (d.Width * 3 + 3) & ~3u;
        std::vector<BYTE> out((size_t)row * d.Height);
        for (UINT y = 0; y < d.Height; ++y) {
            const BYTE* s = (const BYTE*)m.pData + (size_t)y * m.RowPitch;
            BYTE* o = &out[(size_t)(d.Height - 1 - y) * row];
            for (UINT x = 0; x < d.Width; ++x) {
                o[x * 3 + 0] = s[x * 4 + (rgba ? 2 : 0)];
                o[x * 3 + 1] = s[x * 4 + 1];
                o[x * 3 + 2] = s[x * 4 + (rgba ? 0 : 2)];
            }
        }
        ctx->Unmap(st, 0);
        std::wstring path = g_diagDir + L"\\" + name + L".bmp";
        FILE* f = NULL;
        if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
            BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
            ih.biSize = sizeof(ih); ih.biWidth = d.Width; ih.biHeight = d.Height; ih.biPlanes = 1; ih.biBitCount = 24; ih.biSizeImage = (DWORD)out.size();
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
            fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(out.data(), out.size(), 1, f); fclose(f);
            Log("saved %ls (%ux%u)\n", path.c_str(), d.Width, d.Height);
        }
    }
    st->Release();
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 1;
    DWORD pid = wcstoul(argv[1], NULL, 10);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL, dir, MAX_PATH); *wcsrchr(dir, L'\\') = 0;
    g_diagDir = std::wstring(dir) + L"\\ts2diag";
    _wfopen_s(&g_log, (std::wstring(dir) + L"\\ts2present64.log").c_str(), L"w");
    Log("ts2present64 for game pid %lu\n", pid);

    HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, pid);
    wchar_t name[64];
    swprintf_s(name, L"Local\\ts2bridge_info_%lu", pid);
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    swprintf_s(name, L"Local\\ts2bridge_frame_%lu", pid);
    HANDLE evt = OpenEventW(SYNCHRONIZE, FALSE, name);
    const BridgeInfo* info = map ? (const BridgeInfo*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(BridgeInfo)) : NULL;
    if (!game || !info || !evt) { Log("cannot open the game's bridge objects\n"); return 1; }

    ID3D11Device* dev0 = NULL; ID3D11DeviceContext* ctx = NULL;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &dev0, NULL, &ctx))) { Log("no D3D11 device\n"); return 1; }
    ID3D11Device1* dev = NULL; dev0->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev);
    IDXGIDevice* dxdev; dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
    IDXGIAdapter* ad; dxdev->GetAdapter(&ad);
    IDXGIFactory2* fac; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);

    WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"ts2present64";
    wc.hCursor = NULL;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Toy Story 2 (120)",
                                WS_POPUP, 0, 0, 16, 16, NULL, NULL, wc.hInstance, NULL);

    LONG gen = 0, lastFrame = 0;
    ID3D11Texture2D* tex = NULL; IDXGIKeyedMutex* km = NULL; ID3D11ShaderResourceView* srv = NULL;
    IDXGISwapChain1* sc = NULL; ID3D11Texture2D* bb = NULL; ID3D11RenderTargetView* rtv = NULL;
    UINT scW = 0, scH = 0;
    bool scaler = CreateScaler(dev);
    Log("scaler for supersampled frames: %s\n", scaler ? "ready" : "unavailable (frames larger than the window are presented at their own size)");
    bool shown = false;
    unsigned presented = 0, lastPresented = 0;
    DWORD lastReport = GetTickCount(), lastShotCheck = 0;
    bool reportedLayer = false;

    for (;;) {
        HANDLE waits[2] = { evt, game };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 100);
        if (w == WAIT_OBJECT_0 + 1) { Log("game exited\n"); break; }
        MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);

        // Show only while the game is the foreground app and not minimized; sit exactly on its window.
        HWND gameHwnd = (HWND)(LONG_PTR)(LONG)info->gameHwnd;
        DWORD fgPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        RECT r = {};
        // Test-only override (ts2-run -PresenterForceShow): show whenever the game window itself is visible and not
        // minimized, even without focus, so measurements work while someone else is using the PC.
        bool forceShow = GetFileAttributesW((g_diagDir + L"\\present.forceshow").c_str()) != INVALID_FILE_ATTRIBUTES;
        bool want = gameHwnd && IsWindowVisible(gameHwnd) && !IsIconic(gameHwnd) && (fgPid == pid || forceShow) && GetWindowRect(gameHwnd, &r);
        if (want) {
            SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | (shown ? 0 : SWP_SHOWWINDOW));
            if (!shown) Log("shown over the game at %ld,%ld %ldx%ld\n", r.left, r.top, r.right - r.left, r.bottom - r.top);
            shown = true;
        } else if (shown) {
            ShowWindow(hwnd, SW_HIDE); shown = false; Log("hidden (game not foreground or minimized)\n");
        }

        if (info->generation != gen && info->generation) {
            if (srv) { srv->Release(); srv = NULL; }
            if (km) { km->Release(); km = NULL; }
            if (tex) { tex->Release(); tex = NULL; }
            gen = info->generation;
            HRESULT hr = dev->OpenSharedResourceByName(info->texName, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, __uuidof(ID3D11Texture2D), (void**)&tex);
            if (SUCCEEDED(hr)) tex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km);
            if (SUCCEEDED(hr) && scaler) dev->CreateShaderResourceView(tex, NULL, &srv);
            Log("opened %ls: %08lx (%ux%u fmt %u)\n", info->texName, hr, info->width, info->height, info->format);
        }
        if (!tex || !km) continue;

        // The swapchain has the game window's size when the frame is larger (supersampling) and the scaler is ready;
        // otherwise the frame's own size, as before.
        UINT tw = info->width, th = info->height;
        RECT cr;
        if (srv && gameHwnd && GetClientRect(gameHwnd, &cr) && cr.right > 0 && cr.bottom > 0 &&
            (UINT)cr.right < info->width && (UINT)cr.bottom < info->height) { tw = cr.right; th = cr.bottom; }
        if (!sc || scW != tw || scH != th) {
            if (rtv) { rtv->Release(); rtv = NULL; }
            if (bb) { bb->Release(); bb = NULL; }
            if (sc) { sc->Release(); sc = NULL; }
            DXGI_SWAP_CHAIN_DESC1 sd = {};
            sd.Width = tw; sd.Height = th; sd.Format = (DXGI_FORMAT)info->format;
            sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling = DXGI_SCALING_STRETCH;
            HRESULT hr = fac->CreateSwapChainForHwnd(dev, hwnd, &sd, NULL, NULL, &sc);
            Log("swapchain %ux%u fmt %u for frames %ux%u (%s): %08lx\n", tw, th, info->format, info->width, info->height,
                tw == info->width ? "copied" : "scaled", hr);
            if (FAILED(hr)) { sc = NULL; continue; }
            sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
            if (tw != info->width) dev->CreateRenderTargetView(bb, NULL, &rtv);
            scW = tw; scH = th;
        }

        if (GetTickCount() - lastShotCheck > 250) { ReadShotRequests(); lastShotCheck = GetTickCount(); }
        if (w == WAIT_OBJECT_0 && info->frameId != lastFrame && km->AcquireSync(1, 50) == S_OK) {
            lastFrame = info->frameId;
            LONG demoFrame = info->demoFrame;
            if (rtv && srv) {                                     // supersampled frame: draw it at the window's size
                D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)scW, (float)scH, 0.0f, 1.0f };
                ctx->OMSetRenderTargets(1, &rtv, NULL);
                ctx->RSSetViewports(1, &vp);
                ctx->IASetInputLayout(NULL);
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ctx->VSSetShader(g_vs, NULL, 0);
                ctx->PSSetShader(g_ps, NULL, 0);
                ctx->PSSetShaderResources(0, 1, &srv);
                ctx->PSSetSamplers(0, 1, &g_smp);
                ctx->Draw(3, 0);
                ID3D11ShaderResourceView* none = NULL;
                ctx->PSSetShaderResources(0, 1, &none);
            } else {
                ctx->CopyResource(bb, tex);
            }
            for (size_t i = 0; i < g_shots.size();) {
                if (g_shots[i].frame == demoFrame) { SaveBmp(dev, ctx, tex, g_shots[i].name); g_shots.erase(g_shots.begin() + i); }
                else ++i;
            }
            if (info->captureFrameId && lastFrame == info->captureFrameId)
                SaveBmp(dev, ctx, tex, std::wstring(info->captureName) + L"-received");
            km->ReleaseSync(0);
            sc->Present(1, 0);
            ++presented;
        }

        if (GetTickCount() - lastReport >= 5000) {
            bool layer = GetModuleHandleW(L"NvPresent64.dll") != NULL;
            Log("presented %u frames in the last %.1f s (%.1f fps), window %s, NvPresent64.dll %s\n", presented - lastPresented,
                (GetTickCount() - lastReport) / 1000.0, (presented - lastPresented) * 1000.0 / (GetTickCount() - lastReport),
                shown ? "shown" : "hidden", layer ? "LOADED (Smooth Motion active)" : "not loaded");
            lastPresented = presented; lastReport = GetTickCount();
        }
    }
    return 0;
}
