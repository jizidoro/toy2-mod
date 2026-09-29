// audiopeak.exe <pid> <seconds> — is a process making sound? Reads only the peak LEVEL (0..1) of that process's
// audio session on the default output device (IAudioMeterInformation), 20 times a second; records nothing.
// Prints the loudest peak per second.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <stdio.h>
#include <vector>

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { printf("usage: audiopeak <pid> <seconds>\n"); return 1; }
    DWORD pid = wcstoul(argv[1], NULL, 10); int seconds = _wtoi(argv[2]);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en; IMMDevice* dev; IAudioSessionManager2* mgr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en);
    en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&mgr);

    std::vector<float> perSecond(seconds, 0.0f);
    bool found = false;
    DWORD start = GetTickCount();
    while ((int)((GetTickCount() - start) / 1000) < seconds) {
        IAudioSessionEnumerator* se; int n = 0;
        mgr->GetSessionEnumerator(&se); se->GetCount(&n);
        for (int i = 0; i < n; ++i) {
            IAudioSessionControl* c; se->GetSession(i, &c);
            IAudioSessionControl2* c2; c->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2);
            DWORD p = 0; c2->GetProcessId(&p);
            if (p == pid) {
                found = true;
                IAudioMeterInformation* m; c->QueryInterface(__uuidof(IAudioMeterInformation), (void**)&m);
                float peak = 0; m->GetPeakValue(&peak);
                int s = (GetTickCount() - start) / 1000;
                if (s < seconds && peak > perSecond[s]) perSecond[s] = peak;
                m->Release();
            }
            c2->Release(); c->Release();
        }
        se->Release();
        Sleep(50);
    }
    printf("audio session for pid %lu %s; loudest peak per second:", pid, found ? "found" : "NOT found");
    for (float v : perSecond) printf(" %.2f", v);
    printf("\n");
    return 0;
}
