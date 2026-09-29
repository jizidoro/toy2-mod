// dijoy.exe [seconds] — the joysticks toy2.exe sees, in the order it sees them (DirectInput 0x0500, 32-bit,
// EnumDevices(DIDEVTYPE_JOYSTICK, attached only) like InputManager::Init 0x004152E0; the game uses the first one).
// With a number of seconds, also polls device 0 (DIJOYSTATE, 80 bytes like the game) and prints what changes,
// so a button press on the pad shows up as "button N".
#define DIRECTINPUT_VERSION 0x0500
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

typedef HRESULT(WINAPI* DICreateA_t)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
static GUID g_first; static int g_count;

static BOOL CALLBACK Enum(LPCDIDEVICEINSTANCEA d, LPVOID)
{
    printf("  joystick %d: \"%s\" / \"%s\"  type 0x%08lx\n", g_count, d->tszInstanceName, d->tszProductName, d->dwDevType);
    if (g_count++ == 0) g_first = d->guidInstance;
    return DIENUM_CONTINUE;
}

int main(int argc, char** argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 0;
    HMODULE di = LoadLibraryA("dinput.dll");
    DICreateA_t create = (DICreateA_t)GetProcAddress(di, "DirectInputCreateA");
    LPDIRECTINPUTA dinput = NULL;
    if (!create || FAILED(create(GetModuleHandleA(NULL), 0x0500, &dinput, NULL))) { printf("DirectInputCreateA failed\n"); return 1; }
    printf("DirectInput 0x0500 joysticks (attached), in the game's order:\n");
    dinput->EnumDevices(DIDEVTYPE_JOYSTICK, Enum, NULL, DIEDFL_ATTACHEDONLY);
    if (!g_count || !seconds) return 0;

    LPDIRECTINPUTDEVICEA dev = NULL;
    dinput->CreateDevice(g_first, &dev, NULL);
    dev->SetDataFormat(&c_dfDIJoystick);
    dev->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    HRESULT hr = dev->Acquire();
    printf("polling joystick 0 for %d s (Acquire %08lx) — press buttons / move sticks:\n", seconds, hr);
    DIJOYSTATE prev = {}, s = {};
    IDirectInputDevice2A* dev2 = NULL; dev->QueryInterface(IID_IDirectInputDevice2A, (void**)&dev2);
    if (dev2) dev2->Poll();
    if (SUCCEEDED(dev->GetDeviceState(sizeof(prev), &prev)))
        printf("    at rest: lX %ld lY %ld lZ %ld lRx %ld lRy %ld lRz %ld\n", prev.lX, prev.lY, prev.lZ, prev.lRx, prev.lRy, prev.lRz);
    DWORD end = GetTickCount() + seconds * 1000;
    while (GetTickCount() < end) {
        if (dev2) dev2->Poll();
        if (SUCCEEDED(dev->GetDeviceState(sizeof(s), &s))) {
            for (int b = 0; b < 32; ++b) if ((s.rgbButtons[b] ^ prev.rgbButtons[b]) & 0x80) printf("    button %d %s\n", b, s.rgbButtons[b] & 0x80 ? "down" : "up");
            if (s.rgdwPOV[0] != prev.rgdwPOV[0]) printf("    POV %ld\n", (long)s.rgdwPOV[0]);
            if (labs(s.lX - prev.lX) > 8000 || labs(s.lY - prev.lY) > 8000) printf("    left stick %ld,%ld\n", s.lX, s.lY);
            if (labs(s.lZ - prev.lZ) > 8000 || labs(s.lRz - prev.lRz) > 8000) printf("    lZ,lRz (right stick) %ld,%ld\n", s.lZ, s.lRz);
            prev = s;
        }
        Sleep(10);
    }
    return 0;
}
