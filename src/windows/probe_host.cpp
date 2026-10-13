#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    std::wcout << L"Probe host PID: " << GetCurrentProcessId() << std::endl;
    if (argc == 3 && std::wstring(argv[1]) == L"--load") {
        const auto module = LoadLibraryW(argv[2]);
        if (!module) return 1;
        const auto probe = reinterpret_cast<DWORD(WINAPI*)(void*)>(GetProcAddress(module, "GimmdProbe"));
        if (!probe) { FreeLibrary(module); return 2; }
        const DWORD result = probe(nullptr);
        FreeLibrary(module);
        return static_cast<int>(result);
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--wait") {
        const auto event_name = L"Local\\GimmdProbeHostStop-" + std::to_wstring(GetCurrentProcessId());
        const auto stop = CreateEventW(nullptr, TRUE, FALSE, event_name.c_str());
        if (!stop) return 3;
        WaitForSingleObject(stop, 60000);
        CloseHandle(stop);
        return 0;
    }
    std::wcerr << L"Usage: gimmd-probe-host --load <dll> | --wait\n";
    return 2;
}

