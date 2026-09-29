// ts2pad.asi — makes a modern pad (tested: DualSense) work in toy2.exe. Addresses: toy2-decomp InputManager.cpp /
// SaveManager.cpp.
//  1. InputManager::Init (0x004152E0) enables joystick 0 (g_isInputDeviceValid[0], 0x00529CA0) only if its
//     exclusive Acquire succeeds at startup; when the game window is not in front at that instant the pad is ignored
//     all session (seen 2026-09-28: DualSense devices created, all valid flags 0). UpdateInputState re-acquires
//     before every read, so once the device exists the flag is set.
//  2. The game reads buttons through its mapping table (g_save99Data 0x00529B08: 38 x {dInputCode, gameControlId};
//     "joy N" = 0x3FF + N = DirectInput button N-1). Its defaults put jump on button 0 (Square on a DualSense) and
//     pause on button 8 (Create). If the pad is a DualSense / DualShock ("Wireless Controller") and the joystick
//     entries are still the defaults, they are replaced by the PlayStation layout below. A mapping the player
//     changed is left alone.
//  3. The game moves Buzz only from the left stick (lX/lY) and never reads the d-pad (POV); while the stick is
//     centered, the d-pad is fed in as full stick deflection. The right stick turns the camera (via L2 / R2).
#define DIRECTINPUT_VERSION 0x0500
#include <windows.h>
#include <dinput.h>
#include <math.h>
#include <stdio.h>
#include <share.h>
#include "minhook.h"

static void* volatile* const kPad0 = (void* volatile*)0x005298D0;       // g_directInputDevices[0] (IDirectInputDevice2A)
static volatile LONG* const kPad0Valid = (volatile LONG*)0x00529CA0;    // g_isInputDeviceValid[0]
struct Mapping { LONG code, control; };
static Mapping* const kMappings = (Mapping*)(0x00529B08 + 8);           // g_save99Data.saveStructs[38]
static const int kMappingCount = 38;

enum { UP = 0x10, MENU = 0x8, SECRET = 0x1, CAM_L = 0x100, CAM_R = 0x200, VISOR = 0x400, TARGET = 0x800, CANCEL = 0x1000, SPIN = 0x2000, JUMP = 0x4000, FIRE = 0x8000 };
static const Mapping kDefaultJoy[] = { {0x409, CANCEL}, {0x400, JUMP}, {0x401, FIRE}, {0x402, SPIN}, {0x407, CAM_R}, {0x406, CAM_L}, {0x404, TARGET}, {0x403, VISOR}, {0x409, SECRET}, {0x408, MENU} };
// DualSense / DualShock DirectInput buttons: 0 Square, 1 Cross, 2 Circle, 3 Triangle, 4 L1, 5 R1, 6 L2, 7 R2, 8 Create/Share, 9 Options.
// The game's control ids are the PS1 pad bits (Square 0x8000, Cross 0x4000, Circle 0x2000, Triangle 0x1000, L1 0x400,
// R1 0x800, L2 0x100, R2 0x200, Start 0x8, Select 0x1), so each button maps to its own PS1 bit: the original layout
// (Triangle = back/"cancel", L1 = visor, R1 = target lock, L2/R2 = camera, Options = Start, Create = Select).
static const Mapping kPlayStation[] = { {0x400, FIRE}, {0x401, JUMP}, {0x402, SPIN}, {0x403, CANCEL}, {0x404, VISOR}, {0x405, TARGET}, {0x406, CAM_L}, {0x407, CAM_R}, {0x408, SECRET}, {0x409, MENU} };

static FILE* g_log;
static void Log(const char* fmt, ...) { if (!g_log) return; va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }

typedef HRESULT(__stdcall* GetDeviceState_t)(void*, DWORD, void*);
static GetDeviceState_t oGetDeviceState;
static unsigned g_dpadUses;
static volatile bool g_isPlayStation;                                    // right stick is lZ / lRz only on these

static HRESULT __stdcall hGetDeviceState(void* self, DWORD cb, void* data)
{
    HRESULT hr = oGetDeviceState(self, cb, data);
    if (SUCCEEDED(hr) && cb == sizeof(DIJOYSTATE) && self == *kPad0) {
        DIJOYSTATE* s = (DIJOYSTATE*)data;
        DWORD pov = s->rgdwPOV[0];
        bool centered = labs(s->lX - 32767) < 8000 && labs(s->lY - 32767) < 8000;
        if (LOWORD(pov) != 0xFFFF && centered) {
            double a = pov / 100.0 * 3.14159265358979 / 180.0;              // 0 = up, 9000 = right (hundredths of a degree)
            s->lX = (LONG)(32767 + 32767 * sin(a));
            s->lY = (LONG)(32767 - 32767 * cos(a));
            if (g_dpadUses++ == 0) Log("d-pad used as stick\n");
        }
        // The game never reads the right stick; on a DualSense / DualShock its left/right (lZ) presses the camera
        // buttons L2 / R2 (DirectInput buttons 6 / 7), so it turns the camera through the same mapping entries.
        if (g_isPlayStation && s->lZ < 32767 - 14000) s->rgbButtons[6] |= 0x80;
        if (g_isPlayStation && s->lZ > 32767 + 14000) s->rgbButtons[7] |= 0x80;
    }
    return hr;
}

static bool JoyEntriesAreDefault()
{
    int n = 0; bool match = true;
    for (int i = 0; i < kMappingCount; ++i) {
        if (kMappings[i].code < 0x400 || kMappings[i].code > 0x41F) continue;
        bool found = false;
        for (const Mapping& d : kDefaultJoy) if (d.code == kMappings[i].code && d.control == kMappings[i].control) found = true;
        if (!found) match = false;
        ++n;
    }
    return match && n == (int)(sizeof(kDefaultJoy) / sizeof(kDefaultJoy[0]));
}

static void ApplyPlayStationLayout()
{
    int k = 0;
    for (int i = 0; i < kMappingCount && k < (int)(sizeof(kPlayStation) / sizeof(kPlayStation[0])); ++i)
        if (kMappings[i].code >= 0x400 && kMappings[i].code <= 0x41F) kMappings[i] = kPlayStation[k++];
    Log("PS1 button layout applied (Cross jump, Square fire, Circle spin, Triangle back, L1 visor, R1 target lock, L2/R2 and right stick camera, Options start, Create select)\n");
}

static DWORD WINAPI Watch(LPVOID)
{
    bool hooked = false, isPlayStation = false, checkedName = false;
    for (;;) {
        void* pad = *kPad0;
        if (pad) {
            if (*kPad0Valid == 0) { Sleep(200); if (*kPad0 && *kPad0Valid == 0) { InterlockedExchange(kPad0Valid, 1); Log("pad enabled (its startup Acquire had failed: game window was not in front)\n"); } }
            if (!hooked) {
                hooked = true;
                void* fn = (*(void***)pad)[9];                                   // IDirectInputDevice2A::GetDeviceState
                MH_STATUS s = MH_CreateHook(fn, (void*)hGetDeviceState, (void**)&oGetDeviceState);
                if (s == MH_OK) s = MH_EnableHook(fn);
                Log("pad GetDeviceState hook (d-pad): %d\n", s);
            }
            if (!checkedName) {
                checkedName = true;
                DIDEVICEINSTANCEA info = {}; info.dwSize = sizeof(info);
                HRESULT hr = ((IDirectInputDevice2A*)pad)->GetDeviceInfo(&info);
                isPlayStation = SUCCEEDED(hr) && (strstr(info.tszProductName, "DualSense") || strstr(info.tszProductName, "Wireless Controller"));
                Log("pad 0: \"%s\" -> %s\n", SUCCEEDED(hr) ? info.tszProductName : "?", isPlayStation ? "PlayStation layout" : "game's own layout");
                g_isPlayStation = isPlayStation;
            }
            if (isPlayStation && JoyEntriesAreDefault()) ApplyPlayStationLayout();   // also after the game (re)loads its defaults
        }
        Sleep(250);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    wcscpy_s(wcsrchr(path, L'\\') + 1, MAX_PATH - (wcsrchr(path, L'\\') + 1 - path), L"ts2pad.log");
    g_log = _wfsopen(path, L"w", _SH_DENYNO);
    Log("ts2pad loaded, MinHook %d\n", MH_Initialize());
    CloseHandle(CreateThread(NULL, 0, Watch, NULL, 0, NULL));
    return TRUE;
}
