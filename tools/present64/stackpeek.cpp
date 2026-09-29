// stackpeek.exe <pid> [tid] — where is a 32-bit (WOW64) thread stuck? Suspends it briefly, reads its stack, and
// lists values that point into loaded modules' code (likely return addresses), top of stack first.
// Without a tid, uses the thread with the most CPU time (the game's main loop).
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <vector>
#include <string>

struct Mod { DWORD base, end; std::string name; };

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) { printf("usage: stackpeek <pid> [tid]\n"); return 1; }
    DWORD pid = wcstoul(argv[1], NULL, 10), tid = argc > 2 ? wcstoul(argv[2], NULL, 10) : 0;
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }

    if (!tid) {   // busiest thread
        ULONGLONG best = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te = { sizeof(te) };
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            FILETIME c, e, k, u;
            if (t && GetThreadTimes(t, &c, &e, &k, &u)) {
                ULONGLONG cpu = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
                if (cpu > best) { best = cpu; tid = te.th32ThreadID; }
            }
            if (t) CloseHandle(t);
        }
        CloseHandle(snap);
    }

    std::vector<Mod> mods;
    HMODULE hm[512]; DWORD need = 0;
    EnumProcessModulesEx(proc, hm, sizeof(hm), &need, LIST_MODULES_32BIT);
    for (DWORD i = 0; i < need / sizeof(HMODULE); ++i) {
        MODULEINFO mi; char n[MAX_PATH];
        if (GetModuleInformation(proc, hm[i], &mi, sizeof(mi)) && GetModuleBaseNameA(proc, hm[i], n, MAX_PATH))
            mods.push_back({ (DWORD)(ULONG_PTR)mi.lpBaseOfDll, (DWORD)(ULONG_PTR)mi.lpBaseOfDll + mi.SizeOfImage, n });
    }

    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) { printf("OpenThread %lu failed %lu\n", tid, GetLastError()); return 1; }
    SuspendThread(th);
    WOW64_CONTEXT ctx = {}; ctx.ContextFlags = WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER;
    Wow64GetThreadContext(th, &ctx);
    std::vector<DWORD> stack(4096);
    SIZE_T got = 0;
    ReadProcessMemory(proc, (LPCVOID)(ULONG_PTR)ctx.Esp, stack.data(), stack.size() * 4, &got);
    ResumeThread(th);

    auto where = [&](DWORD a) -> std::string {
        for (auto& m : mods) if (a >= m.base && a < m.end) { char b[128]; sprintf_s(b, "%s+0x%lx", m.name.c_str(), a - m.base); return b; }
        return "";
    };
    printf("thread %lu: eip %08lx = %s  esp %08lx\n", tid, ctx.Eip, where(ctx.Eip).c_str(), ctx.Esp);
    int shown = 0;
    for (size_t i = 0; i < got / 4 && shown < 40; ++i) {
        std::string w = where(stack[i]);
        if (w.empty()) continue;
        printf("  [esp+%04zx] %08lx  %s\n", i * 4, stack[i], w.c_str());
        ++shown;
    }
    return 0;
}
