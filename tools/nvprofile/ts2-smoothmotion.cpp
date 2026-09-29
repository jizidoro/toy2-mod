// ts2-smoothmotion.exe — manages an NVIDIA driver profile for an exe (default toy2.exe; "app=<exe>" to pick one).
// NOTE: Smooth Motion's driver layer ships only as NvPresent64.dll, so it cannot engage in 32-bit toy2.exe; the
// 120 fps setup uses it for the 64-bit presenter:  ts2-smoothmotion on app=ts2present64.exe that turns on Smooth Motion
// (driver frame generation: each rendered frame gets a generated one, 60 fps -> 120 fps displayed).
//   ts2-smoothmotion show         print the Smooth Motion settings of the profile that holds toy2.exe
//   ts2-smoothmotion on [bars]    create/update that profile: Smooth Motion on, DX11 allowed ("bars": NVIDIA's
//                                 on-screen debug bars, to see that frame generation is active)
//   ts2-smoothmotion off          remove what "on" did (deletes our profile, or restores the settings of a
//                                 predefined NVIDIA profile)
// dgVoodoo presents the game through D3D11, which Smooth Motion supports on RTX 40/50.
// NvAPI DRS function ids and structs: NVIDIA NvAPI SDK, as used by Orbmu2k/nvidiaProfileInspector
// (Native/NVAPI/NvapiDrsWrapper.cs). Setting ids: its CustomSettingNames.xml.
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int NvAPI_Status;
typedef void* NvDRSSessionHandle;
typedef void* NvDRSProfileHandle;
typedef wchar_t NvUnicode[2048];

struct NVDRS_PROFILE { DWORD version; NvUnicode profileName; DWORD gpuSupport; DWORD isPredefined, numOfApps, numOfSettings; };
struct NVDRS_APPLICATION_V4 { DWORD version, isPredefined; NvUnicode appName, userFriendlyName, launcher, fileInFolder; DWORD bits; NvUnicode commandLine; };
union NVDRS_VALUE { DWORD u32; BYTE raw[4100]; };
struct NVDRS_SETTING { DWORD version; NvUnicode settingName; DWORD settingId, settingType, settingLocation, isCurrentPredefined, isPredefinedValid; NVDRS_VALUE predefinedValue, currentValue; };

static_assert(sizeof(NVDRS_PROFILE) == 0x1014, "NVDRS_PROFILE size");
static_assert(sizeof(NVDRS_APPLICATION_V4) == 0x500C, "NVDRS_APPLICATION_V4 size");
static_assert(sizeof(NVDRS_SETTING) == 0x3020, "NVDRS_SETTING size");

static const DWORD kSmoothMotionEnable = 0xB0D384C0;   // 0 off, 1 on
static const DWORD kSmoothMotionApis = 0xB0CC0875;     // bit0 DX12, bit1 DX11, bit2 Vulkan
static const DWORD kSmoothMotionDebugBars = 0xB01B8B02; // 0 off, 1 on
static const wchar_t* kApp = L"toy2.exe";                      // "app=<exe>" on the command line overrides
static wchar_t kProfileName[512] = L"Toy Story 2 (C:\\toy_story_2)";

typedef void* (__cdecl* QueryInterface_t)(unsigned);
static QueryInterface_t Q;
template <class F> static F Fn(unsigned id) { return (F)Q(id); }

static void Check(NvAPI_Status s, const char* what)
{
    if (s != 0) { printf("%s failed: NvAPI status %d\n", what, s); ExitProcess(1); }
}

// The newer DRS entry points (0xEA99498D / 0x8A2CF5F5, two extra arguments) accept settings the classic
// GetSetting / SetSetting (0x73BF8338 / 0x577DD202) reject with NVAPI_SETTING_NOT_FOUND (-160), which is what
// the classic ones return for the Smooth Motion ids. Same order as nvidiaProfileInspector: newer first.
static NvAPI_Status GetSettingAny(NvDRSSessionHandle ses, NvDRSProfileHandle prof, DWORD id, NVDRS_SETTING* s)
{
    if (auto get2 = Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, DWORD, NVDRS_SETTING*, DWORD*)>(0xEA99498D)) {
        DWORD x = 0;
        return get2(ses, prof, id, s, &x);
    }
    return Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, DWORD, NVDRS_SETTING*)>(0x73BF8338)(ses, prof, id, s);
}

static NvAPI_Status SetSettingAny(NvDRSSessionHandle ses, NvDRSProfileHandle prof, NVDRS_SETTING* s)
{
    if (auto set2 = Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*, DWORD, DWORD)>(0x8A2CF5F5))
        return set2(ses, prof, s, 0, 0);
    return Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*)>(0x577DD202)(ses, prof, s);
}

static void PrintSetting(NvDRSSessionHandle ses, NvDRSProfileHandle prof, DWORD id, const char* name)
{
    NVDRS_SETTING s = {}; s.version = sizeof(s) | (1 << 16);
    NvAPI_Status r = GetSettingAny(ses, prof, id, &s);
    if (r == 0) printf("  %-32s 0x%08lX = %lu%s\n", name, id, s.currentValue.u32, s.isCurrentPredefined ? " (driver default)" : "");
    else printf("  %-32s 0x%08lX not set (status %d)\n", name, id, r);
}

static void SetDword(NvDRSSessionHandle ses, NvDRSProfileHandle prof, DWORD id, DWORD value, const char* name)
{
    NVDRS_SETTING s = {}; s.version = sizeof(s) | (1 << 16);
    s.settingId = id; s.settingType = 0; /* DWORD */ s.currentValue.u32 = value;
    Check(SetSettingAny(ses, prof, &s), name);
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const wchar_t* cmd = argc > 1 ? argv[1] : L"show";
    bool bars = false;
    for (int i = 2; i < argc; ++i) {
        if (!_wcsicmp(argv[i], L"bars")) bars = true;
        else if (!_wcsnicmp(argv[i], L"app=", 4)) { kApp = argv[i] + 4; swprintf_s(kProfileName, L"%ls (C:\\toy_story_2)", kApp); }
    }

    HMODULE nv = LoadLibraryW(L"nvapi.dll");
    if (!nv) { printf("nvapi.dll not found (no NVIDIA driver?)\n"); return 1; }
    Q = (QueryInterface_t)GetProcAddress(nv, "nvapi_QueryInterface");
    Check(Fn<NvAPI_Status(__cdecl*)()>(0x0150E828)(), "NvAPI_Initialize");

    NvDRSSessionHandle ses = NULL;
    Check(Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle*)>(0x0694D52E)(&ses), "DRS_CreateSession");
    Check(Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>(0x375DBD6B)(ses), "DRS_LoadSettings");

    auto FindApp = Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, const wchar_t*, NvDRSProfileHandle*, NVDRS_APPLICATION_V4*)>(0xEEE566B2);
    auto GetProfileInfo = Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_PROFILE*)>(0x61CD6FD6);
    auto SaveSettings = Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>(0xFCBC7E14);

    NvDRSProfileHandle prof = NULL;
    NVDRS_APPLICATION_V4 app = {}; app.version = sizeof(app) | (4 << 16);
    NvAPI_Status found = FindApp(ses, kApp, &prof, &app);
    NVDRS_PROFILE info = {}; info.version = sizeof(info) | (1 << 16);
    if (found == 0) {
        Check(GetProfileInfo(ses, prof, &info), "DRS_GetProfileInfo");
        wprintf(L"%ls is in profile \"%ls\" (%ls)\n", kApp, info.profileName, info.isPredefined ? L"predefined by NVIDIA" : L"user-created");
    } else {
        wprintf(L"%ls is in no driver profile (status %d)\n", kApp, found);
    }

    if (!_wcsicmp(cmd, L"show")) {
        if (found == 0) {
            PrintSetting(ses, prof, kSmoothMotionEnable, "Smooth Motion - Enable");
            PrintSetting(ses, prof, kSmoothMotionApis, "Smooth Motion - Enabled APIs");
            PrintSetting(ses, prof, kSmoothMotionDebugBars, "Smooth Motion - Debug Bars");
        }
        NvDRSProfileHandle base = NULL;
        if (Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle*)>(0xDA8466A0)(ses, &base) == 0) {
            printf("global (base) profile, which applies where the game profile sets nothing:\n");
            PrintSetting(ses, base, kSmoothMotionEnable, "Smooth Motion - Enable");
            PrintSetting(ses, base, kSmoothMotionApis, "Smooth Motion - Enabled APIs");
        }
    } else if (!_wcsicmp(cmd, L"on")) {
        if (found != 0) {
            NVDRS_PROFILE p = {}; p.version = sizeof(p) | (1 << 16);
            wcscpy_s(p.profileName, kProfileName); p.gpuSupport = 1; // GeForce
            Check(Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NVDRS_PROFILE*, NvDRSProfileHandle*)>(0xCC176068)(ses, &p, &prof), "DRS_CreateProfile");
            NVDRS_APPLICATION_V4 a = {}; a.version = sizeof(a) | (4 << 16);
            wcscpy_s(a.appName, kApp); wcscpy_s(a.userFriendlyName, kProfileName);
            Check(Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_APPLICATION_V4*)>(0x4347A9DE)(ses, prof, &a), "DRS_CreateApplication");
            wprintf(L"created profile \"%ls\" for %ls\n", kProfileName, kApp);
        }
        SetDword(ses, prof, kSmoothMotionEnable, 1, "set Smooth Motion - Enable");
        // "Enabled APIs" is privileged (NVAPI_INVALID_USER_PRIVILEGE, -137, without admin); keep the driver
        // default then, and say so: elevated runs set it to DX11.
        NVDRS_SETTING apis = {}; apis.version = sizeof(apis) | (1 << 16);
        apis.settingId = kSmoothMotionApis; apis.settingType = 0; apis.currentValue.u32 = 2;
        NvAPI_Status ra = SetSettingAny(ses, prof, &apis);
        if (ra == -137) printf("Smooth Motion - Enabled APIs needs admin (status -137): left at the driver default\n");
        else Check(ra, "set Smooth Motion - Enabled APIs");
        NVDRS_SETTING db = {}; db.version = sizeof(db) | (1 << 16);
        db.settingId = kSmoothMotionDebugBars; db.settingType = 0; db.currentValue.u32 = bars ? 1 : 0;
        NvAPI_Status rb = SetSettingAny(ses, prof, &db);
        if (rb != 0) printf("Smooth Motion - Debug Bars not set (status %d)\n", rb);
        Check(SaveSettings(ses), "DRS_SaveSettings");
        PrintSetting(ses, prof, kSmoothMotionEnable, "Smooth Motion - Enable");
        PrintSetting(ses, prof, kSmoothMotionApis, "Smooth Motion - Enabled APIs");
        PrintSetting(ses, prof, kSmoothMotionDebugBars, "Smooth Motion - Debug Bars");
    } else if (!_wcsicmp(cmd, L"off")) {
        if (found != 0) { printf("nothing to undo\n"); }
        else if (!info.isPredefined && !wcscmp(info.profileName, kProfileName)) {
            Check(Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle, NvDRSProfileHandle)>(0x17093206)(ses, prof), "DRS_DeleteProfile");
            Check(SaveSettings(ses), "DRS_SaveSettings");
            wprintf(L"deleted profile \"%ls\"\n", kProfileName);
        } else {
            // Drop our user-level values so the predefined profile's own state applies again.
            // Ids as in nvidiaProfileInspector: RestoreProfileDefaultSetting 0x7DD5B261 (older 0x53F0381E),
            // DeleteProfileSetting 0xD20D29DF (older 0xE4A26362).
            typedef NvAPI_Status(__cdecl* PerSetting_t)(NvDRSSessionHandle, NvDRSProfileHandle, DWORD);
            PerSetting_t restore = Fn<PerSetting_t>(0x7DD5B261); if (!restore) restore = Fn<PerSetting_t>(0x53F0381E);
            PerSetting_t del = Fn<PerSetting_t>(0xD20D29DF); if (!del) del = Fn<PerSetting_t>(0xE4A26362);
            const DWORD ids[] = { kSmoothMotionEnable, kSmoothMotionApis, kSmoothMotionDebugBars };
            for (DWORD id : ids) {
                NVDRS_SETTING s = {}; s.version = sizeof(s) | (1 << 16);
                if (GetSettingAny(ses, prof, id, &s) != 0) continue;            // not set: nothing to undo
                NvAPI_Status r = restore ? restore(ses, prof, id) : -3;
                if (r != 0 && del) r = del(ses, prof, id);
                printf("  undo 0x%08lX: status %d\n", id, r);
            }
            Check(SaveSettings(ses), "DRS_SaveSettings");
            printf("removed our Smooth Motion values from the predefined profile\n");
        }
    } else {
        printf("usage: ts2-smoothmotion show | on [bars] | off\n");
    }
    Fn<NvAPI_Status(__cdecl*)(NvDRSSessionHandle)>(0xDAD9CFF8)(ses);
    return 0;
}
