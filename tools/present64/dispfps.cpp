// dispfps.exe <seconds> [\\.\DISPLAYn] — counts how many new images reach a monitor per second, using the
// Desktop Duplication API's frame METADATA only (AccumulatedFrames, LastPresentTime). Each frame is released
// immediately; no pixel is ever mapped, copied or saved, so nothing on screen can be captured by this tool.
// Used to verify frame generation: the displayed rate of the game's monitor, not the game's own rate.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <vector>

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    double seconds = argc > 1 ? _wtof(argv[1]) : 10;
    const wchar_t* want = argc > 2 ? argv[2] : L"\\\\.\\DISPLAY1";

    IDXGIFactory1* fac; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fac);
    IDXGIAdapter1* ad = NULL; IDXGIOutput* out = NULL;
    for (UINT i = 0; !out && fac->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
        IDXGIOutput* o;
        for (UINT j = 0; ad->EnumOutputs(j, &o) != DXGI_ERROR_NOT_FOUND; ++j) {
            DXGI_OUTPUT_DESC od; o->GetDesc(&od);
            if (!_wcsicmp(od.DeviceName, want)) { out = o; break; }
            o->Release();
        }
        if (!out) ad->Release();
    }
    if (!out) { wprintf(L"%ls not found\n", want); return 1; }
    ID3D11Device* dev; ID3D11DeviceContext* ctx;
    if (FAILED(D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx))) { printf("no device\n"); return 1; }
    IDXGIOutput1* out1; out->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
    IDXGIOutputDuplication* dup = NULL;
    HRESULT hr = out1->DuplicateOutput(dev, &dup);
    if (FAILED(hr)) { printf("DuplicateOutput failed %08lx\n", hr); return 1; }
    unsigned reconnects = 0;

    LARGE_INTEGER freq, t0, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    std::vector<unsigned> perSecond((size_t)seconds + 1, 0);
    unsigned total = 0;
    for (;;) {
        QueryPerformanceCounter(&now);
        double t = double(now.QuadPart - t0.QuadPart) / freq.QuadPart;
        if (t >= seconds) break;
        DXGI_OUTDUPL_FRAME_INFO fi; IDXGIResource* res = NULL;
        hr = dup->AcquireNextFrame(100, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (hr == DXGI_ERROR_ACCESS_LOST) {          // mode change / full-screen switch: re-create the duplication
            dup->Release(); dup = NULL;
            while (FAILED(out1->DuplicateOutput(dev, &dup))) Sleep(50);
            ++reconnects;
            continue;
        }
        if (FAILED(hr)) { printf("AcquireNextFrame %08lx at %.1f s\n", hr, t); break; }
        if (fi.LastPresentTime.QuadPart) { perSecond[(size_t)t] += fi.AccumulatedFrames; total += fi.AccumulatedFrames; }
        if (res) res->Release();          // never mapped or copied
        dup->ReleaseFrame();
    }
    wprintf(L"%ls new images per second:", want);
    for (size_t i = 0; i < perSecond.size() && i < (size_t)seconds; ++i) printf(" %u", perSecond[i]);
    printf("\ntotal %u in %.0f s (duplication re-created %u times)\n", total, seconds, reconnects);
    return 0;
}
