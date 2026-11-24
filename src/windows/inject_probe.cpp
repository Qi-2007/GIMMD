#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <stdexcept>

namespace fs = std::filesystem;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
static void fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
static uintptr_t module_base(DWORD pid, const std::wstring& name, bool full_path = false) {
    Handle snapshot;
    for (int i = 0; i < 10; ++i) {
        snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot.value != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_BAD_LENGTH) fail("module snapshot");
        Sleep(20);
    }
    if (snapshot.value == INVALID_HANDLE_VALUE) fail("module snapshot");
    MODULEENTRY32W entry{sizeof(entry)};
    if (!Module32FirstW(snapshot.value, &entry)) fail("Module32FirstW");
    do {
        const wchar_t* candidate = full_path ? entry.szExePath : entry.szModule;
        if (_wcsicmp(candidate, name.c_str()) == 0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
    } while (Module32NextW(snapshot.value, &entry));
    return 0;
}
static DWORD run_thread(HANDLE process, uintptr_t address, void* parameter, bool& finished, DWORD timeout = 15000) {
    finished = false;
    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(address), parameter, 0, nullptr));
    if (!thread.value) { finished = true; fail("CreateRemoteThread"); }
    const DWORD waited = WaitForSingleObject(thread.value, timeout);
    if (waited != WAIT_OBJECT_0)
        throw std::runtime_error("remote thread not confirmed finished; memory retained until process exit");
    finished = true;
    DWORD result = 0;
    if (!GetExitCodeThread(thread.value, &result)) fail("GetExitCodeThread");
    return result;
}
static void inject(DWORD pid, const fs::path& dll) {
    // Validate the local probe export without executing the DLL.
    const auto mapped = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!mapped) fail("map local DLL");
    const auto local_probe = GetProcAddress(mapped, "GimmdProbe");
    const uintptr_t probe_rva = reinterpret_cast<uintptr_t>(local_probe) - reinterpret_cast<uintptr_t>(mapped);
    FreeLibrary(mapped);
    if (!local_probe) throw std::runtime_error("DLL has no GimmdProbe export");
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid));
    if (!process.value) fail("OpenProcess");
    USHORT target_machine = 0, native_machine = 0;
    if (!IsWow64Process2(process.value, &target_machine, &native_machine)) fail("IsWow64Process2");
    if (target_machine != IMAGE_FILE_MACHINE_UNKNOWN || native_machine != IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("target must be native Windows x64");
    wchar_t image[32768]{};
    DWORD image_size = 32768;
    if (!QueryFullProcessImageNameW(process.value, 0, image, &image_size)) fail("query target path");
    const auto name = fs::path(image).filename().wstring();
    if (_wcsicmp(name.c_str(), L"YuanShen.exe") && _wcsicmp(name.c_str(), L"GenshinImpact.exe") &&
        _wcsicmp(name.c_str(), L"gimmd-probe-host.exe"))
        throw std::runtime_error("target must be Genshin or the gimmd probe host");
    std::wcout << L"Target: " << image << L" PID " << pid << L'\n';
    if (!module_base(pid, dll.wstring(), true)) {
        const auto kernel = GetModuleHandleW(L"kernel32.dll");
        const auto load = GetProcAddress(kernel, "LoadLibraryW");
        if (!load) fail("resolve LoadLibraryW");
        // Exports may forward to KernelBase. Resolve the actual owning module,
        // then relocate its RVA rather than assuming equal local/remote bases.
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(load), &owner))
            fail("resolve LoadLibrary owner");
        wchar_t owner_path[32768]{};
        if (!GetModuleFileNameW(owner, owner_path, 32768)) fail("owner path");
        const auto remote_owner = module_base(pid, fs::path(owner_path).filename().wstring());
        if (!remote_owner) throw std::runtime_error("LoadLibrary owner absent in target");
        const auto remote_load = remote_owner + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner);
        const SIZE_T bytes = (dll.wstring().size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(process.value, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!remote) fail("VirtualAllocEx");
        SIZE_T written = 0;
        if (!WriteProcessMemory(process.value, remote, dll.c_str(), bytes, &written) || written != bytes) {
            const auto error = GetLastError();
            VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
            SetLastError(error); fail("write DLL path");
        }
        bool finished = false;
        try {
            // Thread exit code truncates 64-bit HMODULE. Verify full path instead.
            run_thread(process.value, remote_load, remote, finished);
        } catch (...) {
            if (finished) VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
            throw;
        }
        VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
    }
    const auto base = module_base(pid, dll.wstring(), true);
    if (!base) throw std::runtime_error("LoadLibrary completed but probe DLL was not found");
    bool finished = false;
    const auto result = run_thread(process.value, base + probe_rva, nullptr, finished, (fs::exists(dll.parent_path()/L"hold-end.flag")||fs::exists(dll.parent_path()/L"dynamic-session.flag")) ? INFINITE:605000);
    if (result) throw std::runtime_error("probe report failed (Win32 " + std::to_string(result) + ")");
    std::wcout << L"Probe report: " << (dll.parent_path() / (L"gimmd-" + std::to_wstring(pid) + L".probe.json")) << L'\n';
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: gimmd-inject-probe --pid <pid> <probe.dll>\n"
                << "       gimmd-inject-probe --launch <game.exe> <probe.dll>\n";
            return 2;
        }
        const auto dll = fs::canonical(argv[3]);
        if (!fs::is_regular_file(dll)) throw std::runtime_error("probe DLL missing");
        const std::wstring mode(argv[1]);
        if (mode == L"--pid") {
            const std::wstring value(argv[2]);
            size_t used = 0;
            const auto pid = std::stoull(value, &used);
            if (used != value.size() || pid == 0 || pid > MAXDWORD) throw std::runtime_error("invalid PID");
            inject(static_cast<DWORD>(pid), dll);
        } else if (mode == L"--launch") {
            const auto exe = fs::canonical(argv[2]);
            const auto name = exe.filename().wstring();
            if (_wcsicmp(name.c_str(), L"YuanShen.exe") && _wcsicmp(name.c_str(), L"GenshinImpact.exe"))
                throw std::runtime_error("launch target must be YuanShen.exe or GenshinImpact.exe");
            auto command = L"\"" + exe.wstring() + L"\"";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION info{};
            if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                nullptr, exe.parent_path().c_str(), &startup, &info)) fail("CreateProcessW");
            Handle process(info.hProcess), thread(info.hThread);
            std::wcout << L"Started PID " << info.dwProcessId << L"; waiting 10 seconds for initialization.\n";
            Sleep(10000);
            // On any failure leave the game intact; do not terminate/restart it.
            inject(info.dwProcessId, dll);
        } else throw std::runtime_error("unknown mode");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Probe load failed: " << e.what() << '\n';
        return 1;
    }
}
