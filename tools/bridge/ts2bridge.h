// Shared between ts2bridge.asi (32-bit, inside toy2.exe) and ts2present64.exe (64-bit).
#pragma once
#include <windows.h>

// Named objects, all suffixed with the game's process id:
//   Local\ts2bridge_info_<pid>          file mapping holding BridgeInfo
//   Local\ts2bridge_frame_<pid>         auto-reset event, set after each frame copied into the shared texture
//   Local\ts2bridge_tex_<pid>_<gen>     shared NT-handle texture (keyed mutex: game writes with key 0 -> 1,
//                                        presenter reads with key 1 -> 0)
struct BridgeInfo {
    volatile LONG generation;   // bumps when the shared texture is (re)created
    UINT width, height, format; // DXGI_FORMAT
    DWORD gameHwnd;             // HWND of the window dgVoodoo presents to (32-bit value)
    volatile LONG frameId;      // bumps per frame copied
    volatile LONG demoFrame;    // attract-demo frame number of the last copied frame, -1 outside the demo
    wchar_t texName[96];
    // Transfer check: "game\ts2diag\bridge.req" (content = a name) makes the bridge pick captureFrameId =
    // frameId + 60; the bridge saves the source back buffer as <name>-source.bmp and the presenter saves what it
    // received for that frame as <name>-received.bmp. Identical images = the transfer is lossless.
    volatile LONG captureFrameId;   // 0 = none
    wchar_t captureName[64];
};
