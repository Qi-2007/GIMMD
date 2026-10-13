#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

static HMODULE self_module = nullptr;

// This function runs explicitly, outside DllMain, on a loader-created thread.
// It does not call engine exports, access managed objects, or install hooks.
extern "C" __declspec(dllexport) DWORD WINAPI GimmdProbe(void*) {
    try {
        wchar_t path[32768]{};
        const auto n = GetModuleFileNameW(self_module, path, 32768);
        if (!n || n >= 32768) return ERROR_INSUFFICIENT_BUFFER;
        const auto output = std::filesystem::path(path).parent_path() /
            (L"gimmd-" + std::to_wstring(GetCurrentProcessId()) + L".probe.json");
        std::ofstream report(output, std::ios::trunc);
        if (!report) return ERROR_WRITE_FAULT;
        report << "{\n  \"schemaVersion\": 1,\n  \"pid\": " << GetCurrentProcessId()
            << ",\n  \"pointerBits\": " << sizeof(void*) * 8
            << ",\n  \"probeOnly\": true,\n  \"engineCallsMade\": false,"
            << "\n  \"actorControl\": \"unverified\","
            << "\n  \"cameraControl\": \"unverified\",\n  \"modules\": [\n";
        const wchar_t* modules[] = {nullptr, L"UnityPlayer.dll", L"GameAssembly.dll", L"UserAssembly.dll"};
        const char* labels[] = {"main", "UnityPlayer.dll", "GameAssembly.dll", "UserAssembly.dll"};
        const char* exports[] = {"il2cpp_domain_get", "il2cpp_domain_get_assemblies",
            "il2cpp_class_from_name", "il2cpp_class_get_methods", "il2cpp_runtime_invoke",
            "il2cpp_thread_attach", "il2cpp_resolve_icall"};
        for (size_t i = 0; i < 4; ++i) {
            const auto module = GetModuleHandleW(modules[i]);
            report << "    {\"name\": \"" << labels[i] << "\", \"loaded\": "
                << (module ? "true" : "false") << ", \"exports\": {";
            for (size_t j = 0; j < 7; ++j) {
                if (j) report << ", ";
                report << '"' << exports[j] << "\": "
                    << (module && GetProcAddress(module, exports[j]) ? "true" : "false");
            }
            report << "}}" << (i == 3 ? "\n" : ",\n");
        }
        report << "  ]\n}\n";
        report.flush();
        return report ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
    } catch (...) { return ERROR_UNHANDLED_EXCEPTION; }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

