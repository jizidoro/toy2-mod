// smtest64.exe — does NVIDIA Smooth Motion engage for a plain 64-bit D3D11 flip-model window, and does it
// double the frames the system presents? Shows a borderless color-cycling window on \\.\DISPLAY1 for 8 s,
// submitting exactly 60 frames/s (timer-paced like the game), then reports:
//  - whether the driver loaded its frame-generation present layer (NvPresent64.dll) into this process
//  - app Present calls vs DXGI frame statistics (PresentCount = presents the system processed)
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <psapi.h>
#include <stdio.h>
#include <math.h>

static RECT g_mon;
static BOOL CALLBACK FindMon(HMONITOR m, HDC, LPRECT, LPARAM found)
{
    MONITORINFOEXW mi = {}; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(m, &mi);
    if (!_wcsicmp(mi.szDevice, L"\\\\.\\DISPLAY1")) { g_mon = mi.rcMonitor; *(bool*)found = true; return FALSE; }
    return TRUE;
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool found = false;
    EnumDisplayMonitors(NULL, NULL, FindMon, (LPARAM)&found);
    if (!found) { printf("DISPLAY1 not found\n"); return 1; }

    WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"smtest64";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"smtest64", WS_POPUP | WS_VISIBLE, g_mon.left, g_mon.top,
                                g_mon.right - g_mon.left, g_mon.bottom - g_mon.top, NULL, NULL, wc.hInstance, NULL);
    ID3D11Device* dev; ID3D11DeviceContext* ctx;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx))) return 1;
    IDXGIDevice* dxdev; dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
    IDXGIAdapter* ad; dxdev->GetAdapter(&ad);
    IDXGIFactory2* fac; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = g_mon.right - g_mon.left; sd.Height = g_mon.bottom - g_mon.top; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* sc;
    if (FAILED(fac->CreateSwapChainForHwnd(dev, hwnd, &sd, NULL, NULL, &sc))) return 1;
    ID3D11Texture2D* bb; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    ID3D11RenderTargetView* rtv; dev->CreateRenderTargetView(bb, NULL, &rtv);

    LARGE_INTEGER freq, t0, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    unsigned presents = 0, statsOk = 0;
    DXGI_FRAME_STATISTICS first = {}, last = {};
    bool haveFirst = false;
    for (;;) {
        QueryPerformanceCounter(&now);
        double t = double(now.QuadPart - t0.QuadPart) / freq.QuadPart;
        if (t >= 8.0) break;
        // pace to exactly 60 submissions per second, like the game
        double due = presents / 60.0;
        if (t < due) { Sleep(1); continue; }
        MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        float c[4] = { 0.5f + 0.5f * sinf((float)t * 2), 0.5f + 0.5f * sinf((float)t * 2 + 2), 0.5f + 0.5f * sinf((float)t * 2 + 4), 1 };
        ctx->OMSetRenderTargets(1, &rtv, NULL);
        ctx->ClearRenderTargetView(rtv, c);
        sc->Present(0, 0);
        ++presents;
        DXGI_FRAME_STATISTICS st;
        if (SUCCEEDED(sc->GetFrameStatistics(&st))) { ++statsOk; if (!haveFirst && t > 1.0) { first = st; haveFirst = true; } last = st; }
        if (t > 1.0 && !haveFirst) {}
    }
    printf("NvPresent64.dll %s\n", GetModuleHandleW(L"NvPresent64.dll") ? "LOADED" : "not loaded");
    printf("app presents: %u in 8 s (%.1f/s); frame statistics available: %u times\n", presents, presents / 8.0, statsOk);
    if (haveFirst) {
        double secs = double(last.SyncQPCTime.QuadPart - first.SyncQPCTime.QuadPart) / freq.QuadPart;
        printf("DXGI stats over %.2f s: PresentCount +%u (%.1f/s), PresentRefreshCount +%u, SyncRefreshCount +%u (%.1f Hz)\n", secs,
               last.PresentCount - first.PresentCount, (last.PresentCount - first.PresentCount) / secs,
               last.PresentRefreshCount - first.PresentRefreshCount, last.SyncRefreshCount - first.SyncRefreshCount,
               (last.SyncRefreshCount - first.SyncRefreshCount) / secs);
    }
    return 0;
}
