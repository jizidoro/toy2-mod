// ts2mods.asi — mod loader for toy2.exe (retail, sha256 023eb6a9...), loaded from game\scripts\ by the ASI loader.
//
// Any file the game reads from its data folder or its CD folder can be replaced by putting a file with the same
// relative path in a mod folder:  game\mods\<mod>\data\...  or  game\mods\<mod>\cd\...
// Mod priority: game\mods\load_order.txt (one folder name per line, first = highest priority, '#' comments);
// without that file, every folder in game\mods in alphabetical order.
// game\mods\ts2mods.log lists every game file opened (the list of what can be replaced) and every redirect.
//
// Hooks kernelbase!CreateFileW / CreateFileA (CRT fopen and winmm mmioOpen end there) and LoadLibraryExW
// (the FMV containers cd\rtlibs\*.dll are loaded as libraries). Only read-only OPEN_EXISTING opens under the
// game's own paths are redirected; saves and config writes are never touched.
// Paths come from the game: FileUtils g_pathRegValue 0x00882F40 and g_cdPathRegValue 0x00883144 (toy2-decomp).
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include "minhook.h"

static const char* const kDataPath = (const char*)0x00882F40;
static const char* const kCdPath = (const char*)0x00883144;

static std::wstring g_modsDir;
static std::vector<std::wstring> g_loadOrder;
static std::unordered_map<std::wstring, std::wstring> g_cache;   // lower-case full path -> redirect ("" = none)
static CRITICAL_SECTION g_lock;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static thread_local bool t_inHook;

typedef HANDLE(WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HMODULE(WINAPI* LoadLibraryExW_t)(LPCWSTR, HANDLE, DWORD);
static CreateFileW_t oCreateFileW;
static CreateFileA_t oCreateFileA;
static LoadLibraryExW_t oLoadLibraryExW;

static std::wstring Widen(const char* s)
{
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
    return w;
}

static std::wstring Lower(std::wstring s)
{
    for (auto& c : s) { c = (wchar_t)towlower(c); if (c == L'/') c = L'\\'; }
    return s;
}

static void Log(const std::wstring& line)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    std::wstring l = line + L"\r\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, l.c_str(), (int)l.size(), NULL, 0, NULL, NULL);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, l.c_str(), (int)l.size(), &u[0], n, NULL, NULL);
    DWORD written;
    WriteFile(g_log, u.data(), (DWORD)u.size(), &written, NULL);
}

// Returns the mod file to open instead of `path`, or "" to open the original.
static std::wstring Resolve(LPCWSTR path)
{
    if (!path || !*kDataPath) return L"";           // the game has not read its install paths yet
    wchar_t full[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(path, MAX_PATH * 2, full, NULL);
    if (!n || n >= MAX_PATH * 2) return L"";
    std::wstring key = Lower(full);

    EnterCriticalSection(&g_lock);
    auto hit = g_cache.find(key);
    if (hit != g_cache.end()) { std::wstring r = hit->second; LeaveCriticalSection(&g_lock); return r; }

    std::wstring rel, result;
    std::wstring dataPrefix = Lower(Widen(kDataPath)), cdPrefix = Lower(Widen(kCdPath));
    if (!dataPrefix.empty() && dataPrefix.back() != L'\\') dataPrefix += L'\\';
    if (!cdPrefix.empty() && cdPrefix.back() != L'\\') cdPrefix += L'\\';
    if (!dataPrefix.empty() && key.compare(0, dataPrefix.size(), dataPrefix) == 0)
        rel = L"data\\" + std::wstring(full + dataPrefix.size());
    else if (!cdPrefix.empty() && key.compare(0, cdPrefix.size(), cdPrefix) == 0)
        rel = L"cd\\" + std::wstring(full + cdPrefix.size());

    if (!rel.empty()) {
        for (const auto& mod : g_loadOrder) {
            std::wstring candidate = g_modsDir + L"\\" + mod + L"\\" + rel;
            DWORD a = GetFileAttributesW(candidate.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                result = candidate;
                Log(L"REDIRECT " + rel + L" -> mods\\" + mod + L"\\" + rel);
                break;
            }
        }
        if (result.empty()) Log(L"open " + rel);
    }
    g_cache[key] = result;
    LeaveCriticalSection(&g_lock);
    return result;
}

static bool IsReadOnlyOpen(DWORD access, DWORD disposition)
{
    return disposition == OPEN_EXISTING &&
           !(access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE));
}

static HANDLE WINAPI hCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (!t_inHook && IsReadOnlyOpen(access, disp)) {
        t_inHook = true;
        std::wstring r = Resolve(name);
        t_inHook = false;
        if (!r.empty()) return oCreateFileW(r.c_str(), access, share, sa, disp, flags, tmpl);
    }
    return oCreateFileW(name, access, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI hCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (!t_inHook && name && IsReadOnlyOpen(access, disp)) {
        t_inHook = true;
        std::wstring r = Resolve(Widen(name).c_str());
        t_inHook = false;
        if (!r.empty()) return oCreateFileW(r.c_str(), access, share, sa, disp, flags, tmpl);
    }
    return oCreateFileA(name, access, share, sa, disp, flags, tmpl);
}

static HMODULE WINAPI hLoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    if (!t_inHook && name) {
        t_inHook = true;
        std::wstring r = Resolve(name);
        t_inHook = false;
        if (!r.empty()) return oLoadLibraryExW(r.c_str(), file, flags);
    }
    return oLoadLibraryExW(name, file, flags);
}

// Portable install: FileUtils::ValidateInstall 0x004A6390 (called from WinMain 0x004316E8 and lazily by the file
// getters while g_registryKeysRead 0x00882F3C is 0) reads `path` and `cdpath` (512 bytes each into the two globals
// above) from HKLM\Software\TravellersTalesToyStory2 — on this PC the per-user VirtualStore copy — and fails without
// them; the game never writes the registry. When the exe's folder holds data\ and cd\validate.tta, those two folders
// are the paths (with the trailing backslash the game appends file names to) and the registry is not opened.
// Otherwise the original runs, so any other layout still installs through the registry.
typedef void(__cdecl* ValidateInstall_t)();
static ValidateInstall_t oValidateInstall;
static void __cdecl hValidateInstall()
{
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* slash = n && n < MAX_PATH ? strrchr(dir, '\\') : NULL;
    if (slash) {
        slash[1] = 0;
        std::string data = std::string(dir) + "data\\", cd = std::string(dir) + "cd\\";
        DWORD a = GetFileAttributesA(data.c_str()), v = GetFileAttributesA((cd + "validate.tta").c_str());
        if (data.size() < 500 && cd.size() < 500 && a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
            v != INVALID_FILE_ATTRIBUTES && !(v & FILE_ATTRIBUTE_DIRECTORY)) {
            strcpy_s((char*)kDataPath, 512, data.c_str());
            strcpy_s((char*)kCdPath, 512, cd.c_str());
            *(volatile int*)0x00882F3C = 1;                       // g_registryKeysRead
            Log(L"install paths from the exe folder (registry not read): " + Widen(data.c_str()) + L" | " + Widen(cd.c_str()));
            return;
        }
    }
    Log(L"install paths: no data\\ and cd\\validate.tta beside the exe; reading the registry");
    oValidateInstall();
}

static void ReadLoadOrder()
{
    std::wstring list = g_modsDir + L"\\load_order.txt";
    FILE* f = NULL;
    if (_wfopen_s(&f, list.c_str(), L"rt") == 0 && f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
            s.erase(0, s.find_first_not_of(" \t"));
            if (s.empty() || s[0] == '#') continue;
            g_loadOrder.push_back(Widen(s.c_str()));
        }
        fclose(f);
        return;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_modsDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') g_loadOrder.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(g_loadOrder.begin(), g_loadOrder.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    InitializeCriticalSection(&g_lock);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);                  // ...\game\toy2.exe
    *wcsrchr(exe, L'\\') = 0;
    g_modsDir = std::wstring(exe) + L"\\mods";
    CreateDirectoryW(g_modsDir.c_str(), NULL);
    ReadLoadOrder();

    g_log = CreateFileW((g_modsDir + L"\\ts2mods.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    std::wstring order;
    for (const auto& m : g_loadOrder) order += (order.empty() ? L"" : L", ") + m;
    Log(L"ts2mods loaded; mods (highest priority first): " + (order.empty() ? std::wstring(L"none") : order));

    MH_STATUS s = MH_Initialize();
    if (s == MH_OK) s = MH_CreateHookApi(L"kernelbase", "CreateFileW", (void*)hCreateFileW, (void**)&oCreateFileW);
    if (s == MH_OK) s = MH_CreateHookApi(L"kernelbase", "CreateFileA", (void*)hCreateFileA, (void**)&oCreateFileA);
    if (s == MH_OK) s = MH_CreateHookApi(L"kernelbase", "LoadLibraryExW", (void*)hLoadLibraryExW, (void**)&oLoadLibraryExW);
    if (s == MH_OK) s = MH_CreateHook((void*)0x004A6390, (void*)hValidateInstall, (void**)&oValidateInstall);
    if (s == MH_OK) s = MH_EnableHook(MH_ALL_HOOKS);
    Log(s == MH_OK ? L"hooks: ok" : L"hooks: FAILED " + std::to_wstring((int)s));
    return TRUE;
}
