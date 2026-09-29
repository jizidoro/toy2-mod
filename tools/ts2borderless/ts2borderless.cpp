// ts2borderless.asi — real borderless behavior: the game keeps running and stays visible when you click outside it.
// dgVoodoo's DirectDraw (game\DDraw.dll) emulates 1999 exclusive-fullscreen semantics even in its borderless
// ("fake fullscreen") mode: on focus loss it calls ShowWindow(game window, SW_SHOWMINNOACTIVE). The game loop
// sleeps while minimized, so to the player the game "dies" on every click outside it (seen 2026-09-28:
// ts2diag logged "ShowWindow(..., 7) from DDRAW.dll+0x3cfd" at the moment of focus loss).
// This hook ignores minimize requests made by DDraw.dll only; minimizes from anywhere else still work.
//
// Second focus bug: InputManager::Init (0x004152E0) enables the keyboard (g_directInputSuccess 0x00529D3C) only if
// DirectInput's Acquire succeeds at startup, which fails when the game window is not in front at that instant
// (e.g. the player clicked elsewhere during mode select); the keyboard then stays dead all session. The game
// re-acquires before every read anyway (UpdateInputState), so once the device exists the flag is set here.
#include <windows.h>
#include <ddraw.h>
#include <intrin.h>
#include <stdio.h>
#include <share.h>
#include "minhook.h"

typedef BOOL(WINAPI* ShowWindow_t)(HWND, int);
static ShowWindow_t oShowWindow;
static HMODULE g_ddraw;
static FILE* g_log;
static unsigned g_blocked;

// Called by DDraw.dll? Checks the first few frames of the call stack, not just the immediate caller, so another
// hook on ShowWindow in the same process (whose code would sit between) does not hide DDraw.
static HMODULE ModuleOf(void* addr)
{
    HMODULE m = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m);
    return m;
}

static bool CalledByDDraw(void* ret)
{
    if (!g_ddraw) g_ddraw = GetModuleHandleW(L"ddraw.dll");
    if (!g_ddraw) return false;
    if (ModuleOf(ret) == g_ddraw) return true;          // direct call (the usual case)
    void* frames[8];                                     // fallback when frame pointers allow a walk
    USHORT n = CaptureStackBackTrace(1, 8, frames, NULL);
    for (USHORT i = 0; i < n; ++i) if (ModuleOf(frames[i]) == g_ddraw) return true;
    return false;
}

static BOOL WINAPI hShowWindow(HWND h, int cmd)
{
    bool minimize = cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE;
    void* ret = _ReturnAddress();
    bool byDDraw = minimize && CalledByDDraw(ret);
    if (minimize && g_log) {
        wchar_t name[MAX_PATH] = L"?"; HMODULE m = ModuleOf(ret);
        if (m) GetModuleFileNameW(m, name, MAX_PATH);
        fprintf(g_log, "minimize request ShowWindow(%p, %d) from %ls -> %s\n", h, cmd, wcsrchr(name, L'\\') ? wcsrchr(name, L'\\') + 1 : name, byDDraw ? "ignored" : "allowed");
        fflush(g_log);
    }
    if (byDDraw) {
        if (g_log && g_blocked++ < 50) { fprintf(g_log, "kept window %p visible (DDraw asked ShowWindow %d)\n", h, cmd); fflush(g_log); }
        return IsWindowVisible(h);      // what ShowWindow returns: the previous visibility
    }
    return oShowWindow(h, cmd);
}

// Third focus bug, the one behind "the game dies when I switch windows": on a focus loss dgVoodoo applies
// exclusive-fullscreen semantics — it minimizes the window (see hShowWindow) and invalidates the DirectDraw surfaces.
// (A subclass swallowing WM_ACTIVATEAPP / WM_ACTIVATE did not stop that: dgVoodoo learns of the focus loss some
// other way; removed 2026-09-28.) The videos and the frame start below handle what the invalidation breaks.

// Videos: the game advances a video with a synchronous IStreamSample::Update (0x004DB950: flags 2, no event).
// When dgVoodoo invalidates the DirectDraw surfaces on a window switch, that call blocks until the whole video's
// length has passed (main thread stuck in amstream.dll). While the game is not in front, the update is answered
// with MS_S_ENDOFSTREAM so the game stops the video as at its natural end (0x004DBA01..) and carries on.
static const HRESULT kEndOfStream = 0x00040003;                                  // MS_S_ENDOFSTREAM
typedef HRESULT(STDMETHODCALLTYPE* SampleUpdate_t)(void*, DWORD, HANDLE, void*, DWORD_PTR);
typedef int(__cdecl* MovieStep_t)(void*);
static SampleUpdate_t oSampleUpdate;
static MovieStep_t oMovieStep;
static bool g_sampleHooked;
static unsigned g_videoAborts;

static bool GameInFront() { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid == GetCurrentProcessId(); }

// The game's flags (2 = SSUPDATE_CONTINUOUS) make an *asynchronous* update signal its event only at the end of the
// whole video, so converting the call cut every video at the 3 s limit (owner: "now it's cutting all the fmv's",
// 2026-09-28). With the game in front the original synchronous call runs untouched; only when it is not in front
// does the video end instead of hanging.
// A focus loss during a video invalidates its surface for good (the game never restores it), so even after the
// player is back the next synchronous update would block until the video's natural end: once the game has been
// out of front during a video, that video ends at its next update.
static volatile LONG g_focusLosses;            // bumped by the pause in hPresentFrame
static void* g_curMovie;                       // the video being played (reset by its close, 0x004DB8C0)
static LONG g_lossesAtMovieStart;

static HRESULT STDMETHODCALLTYPE hSampleUpdate(void* self, DWORD flags, HANDLE ev, void* apc, DWORD_PTR data)
{
    bool away = !GameInFront(), wasAway = g_curMovie && g_focusLosses != g_lossesAtMovieStart;
    if ((!away && !wasAway) || ev || apc || (flags & 1)) return oSampleUpdate(self, flags, ev, apc, data);
    if (g_log && g_videoAborts++ < 50) { fprintf(g_log, "video ended: %s\n", away ? "the game is not in front" : "the game was out of front during it"); fflush(g_log); }
    return kEndOfStream;
}

typedef void(__cdecl* MovieClose_t)(void*);
static MovieClose_t oMovieClose;
static void __cdecl hMovieClose(void* movie) { if (movie == g_curMovie) g_curMovie = NULL; oMovieClose(movie); }

static int __cdecl hMovieStep(void* movie)
{
    if (movie != g_curMovie) { g_curMovie = movie; g_lossesAtMovieStart = g_focusLosses; }
    if (!g_sampleHooked && movie) {
        g_sampleHooked = true;
        void* sample = *(void**)((BYTE*)movie + 0x10);                       // the video's IDirectDrawStreamSample
        void* update = sample ? (*(void***)sample)[6] : NULL;                // IStreamSample::Update
        MH_STATUS s = update ? MH_CreateHook(update, (void*)hSampleUpdate, (void**)&oSampleUpdate) : MH_ERROR_NOT_EXECUTABLE;
        if (s == MH_OK) s = MH_EnableHook(update);
        if (g_log) { fprintf(g_log, "video sample Update hook: %d\n", s); fflush(g_log); }
    }
    return oMovieStep(movie);
}

// Pause while the game is not in front. With the minimize suppressed the game logic kept running unfocused
// (its old pause was dgVoodoo sleeping inside DirectDraw while minimized) while dgVoodoo stopped showing
// frames: in a level Buzz would stand defenceless behind a frozen picture. So the game waits at its per-frame
// present (DrawingDevice::PresentFrame 0x004ABD40) until it is in front again, keeping its window responsive by
// dispatching its messages (a WM_QUIT is re-posted for the game's own loop). The window stays visible.
typedef HRESULT(__cdecl* PresentFrame_t)();
static PresentFrame_t oPresentFrame;
static unsigned g_pauses;

// Waits while the game is not in front; false when a WM_QUIT arrived (re-posted for the game's own loop).
static bool PauseWhileAway()
{
    if (GameInFront()) return true;
    InterlockedIncrement(&g_focusLosses);
    if (g_log && g_pauses++ < 50) { fprintf(g_log, "paused: the game is not in front\n"); fflush(g_log); }
    DWORD start = GetTickCount();
    while (!GameInFront()) {
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); return false; }
            TranslateMessage(&m); DispatchMessageA(&m);
        }
        Sleep(30);
    }
    if (g_log && g_pauses <= 50) { fprintf(g_log, "resumed after %.1f s\n", (GetTickCount() - start) / 1000.0); fflush(g_log); }
    return true;
}

static HRESULT __cdecl hPresentFrame()
{
    PauseWhileAway();
    return oPresentFrame();
}

// Fourth focus bug, the freeze after a window switch (owner, 2026-09-28: "crashed when changed windows during game"):
// the game repairs lost surfaces only when presenting fails (Renderer::EndScene 0x004B2DE0). When the loss shows up
// first at the start of a frame (DrawingDevice::BeginScene 0x004ABA90), the game skips the frame, never presents and
// never repairs: its loop runs on (RibShark's limiter at 60 Hz) behind a picture that never changes, with
// PresentFrame never called again. Here a failed BeginScene waits for the game to be in front, restores the lost
// surfaces the way EndScene does (front, back, z-buffer of g_drawingDevice 0x00884008) and tries once more.
typedef HRESULT(__cdecl* BeginScene_t)();
static BeginScene_t oBeginScene;
static unsigned g_restores;

static HRESULT __cdecl hBeginScene()
{
    HRESULT hr = oBeginScene();
    if (SUCCEEDED(hr)) return hr;
    if (!PauseWhileAway()) return hr;
    char* device = *(char**)0x00884008;                                   // DrawingDevice::g_drawingDevice
    static const int kSurfaces[] = { 0x30, 0x34, 0x3C };                  // m_pddsFrontBuffer, m_pddsBackBuffer, m_pddsZBuffer
    if (device)
        for (int off : kSurfaces) {
            IDirectDrawSurface4* s = *(IDirectDrawSurface4**)(device + off);
            if (s && s->IsLost() == DDERR_SURFACELOST) s->Restore();
        }
    HRESULT again = oBeginScene();
    if (g_log && g_restores++ < 50) { fprintf(g_log, "frame start failed (%08lx): surfaces restored, retry %08lx\n", (unsigned long)hr, (unsigned long)again); fflush(g_log); }
    return again;
}

static DWORD WINAPI KeyboardWatch(LPVOID)
{
    void* volatile* const keyboardDevice = (void* volatile*)0x00529C9C;   // InputManager.cpp g_directInputDevice
    volatile LONG* const keyboardEnabled = (volatile LONG*)0x00529D3C;     // InputManager.cpp g_directInputSuccess
    for (;;) {
        if (*keyboardDevice && *keyboardEnabled == 0) {
            Sleep(200);                                  // let Init finish its own Acquire attempt first
            if (*keyboardDevice && *keyboardEnabled == 0) {
                InterlockedExchange(keyboardEnabled, 1);
                if (g_log) { fprintf(g_log, "keyboard enabled (its startup Acquire had failed: game window was not in front)\n"); fflush(g_log); }
            }
        }
        Sleep(250);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    wcscpy_s(wcsrchr(path, L'\\') + 1, MAX_PATH - (wcsrchr(path, L'\\') + 1 - path), L"ts2borderless.log");
    g_log = _wfsopen(path, L"w", _SH_DENYNO);
    MH_STATUS s = MH_Initialize();
    if (s == MH_OK) s = MH_CreateHookApi(L"user32", "ShowWindow", (void*)hShowWindow, (void**)&oShowWindow);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004DB950, (void*)hMovieStep, (void**)&oMovieStep);   // video step
    if (s == MH_OK) s = MH_CreateHook((void*)0x004ABD40, (void*)hPresentFrame, (void**)&oPresentFrame);   // pause
    if (s == MH_OK) s = MH_CreateHook((void*)0x004DB8C0, (void*)hMovieClose, (void**)&oMovieClose);       // video close
    if (s == MH_OK) s = MH_CreateHook((void*)0x004ABA90, (void*)hBeginScene, (void**)&oBeginScene);       // frame start
    if (s == MH_OK) s = MH_EnableHook(MH_ALL_HOOKS);
    if (g_log) { fprintf(g_log, "ts2borderless loaded, ShowWindow hook %d\n", s); fflush(g_log); }
    CloseHandle(CreateThread(NULL, 0, KeyboardWatch, NULL, 0, NULL));
    return TRUE;
}
