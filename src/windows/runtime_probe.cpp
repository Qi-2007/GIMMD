// Runtime observer: FufuLauncher-derived masks are generated separately.
// Pose/time/Animator setters require explicit experiment options. FOV passes unchanged.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include "vmd.h"
#include "core/rotation_preview.h"
#include "generated/fk_profile.h"
#include "core/root_translation.h"
#include "core/leg_pose.h"
#include "windows/player_session.h"
#include "MinHook.h"
#include "generated/gi_profile.h"
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")

namespace gi = gimmd::gi;
static HMODULE self_module;
static std::atomic<bool> busy{false}, accepting{false};
static std::atomic<unsigned> active_calls{0}, callback_count{0}, window_callback_count{0}, sample_count{0};
static std::atomic<unsigned> message_callback_count{0};
static thread_local bool callback_reentrant = false;
static constexpr UINT PlaybackMessage = WM_APP + 0x4d4;
static DWORD window_thread;
static std::atomic<DWORD> last_callback_thread{0}, engine_error{0};
static ULONGLONG last_sample_time;
using FovFn = int(__fastcall*)(void*, float);
using GetMainFn = void*(__fastcall*)();
using GetTransformFn = void*(__fastcall*)(void*);
struct Vec3 { float x, y, z; };
using GetPositionFn = void(__fastcall*)(Vec3*, void*);
using FindStringFn = void*(__fastcall*)(const char*);
using FindObjectFn = void*(__fastcall*)(void*);
static FovFn original_fov;
static uintptr_t configured_target = 0;
static bool permanently_closed = false;
static GetMainFn get_main, get_all;
static GetTransformFn get_transform;
static GetPositionFn get_position;
static FindStringFn find_string;
static FindObjectFn find_object;
struct CameraReading { bool main = false, readable = false; Vec3 position{}; };
struct Reading {
    DWORD thread = 0;
    unsigned camera_count = 0;
    bool array_valid = false, entity_root = false, avatar_root = false;
    CameraReading cameras[16]{};
};
static Reading readings[4];

static bool read_bytes(uintptr_t address, void* out, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, size, &got) && got == size;
}
static bool executable(uintptr_t address, uintptr_t base) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory))) return false;
    const DWORD flags = memory.Protect & 0xff;
    return memory.State == MEM_COMMIT && !(memory.Protect & PAGE_GUARD) &&
        reinterpret_cast<uintptr_t>(memory.AllocationBase) == base &&
        (flags == PAGE_EXECUTE || flags == PAGE_EXECUTE_READ ||
         flags == PAGE_EXECUTE_READWRITE || flags == PAGE_EXECUTE_WRITECOPY);
}
static bool matches(const gi::Candidate& candidate, uintptr_t base) {
    if (!executable(base + candidate.rva, base)) return false;
    std::vector<uint8_t> bytes(candidate.size);
    if (!read_bytes(base + candidate.rva, bytes.data(), bytes.size())) return false;
    for (size_t i = 0; i < bytes.size(); ++i)
        if (candidate.mask[i] >= 0 && bytes[i] != candidate.mask[i]) return false;
    return true;
}
static uintptr_t unique(const char* name, uintptr_t base) {
    uintptr_t result = 0;
    unsigned count = 0;
    for (const auto& candidate : gi::Candidates) {
        if (!std::strcmp(name, candidate.name)) {
            ++count;
            if (!matches(candidate, base)) return 0;
            result = base + candidate.rva;
        }
    }
    return count == 1 ? result : 0;
}
static uintptr_t follow_jump(uintptr_t address, uintptr_t base) {
    std::array<uintptr_t, 8> visited{};
    for (size_t i = 0; i < visited.size(); ++i) {
        if (!executable(address, base)) return 0;
        for (size_t j = 0; j < i; ++j) if (visited[j] == address) return 0;
        visited[i] = address;
        uint8_t bytes[5]{};
        if (!read_bytes(address, bytes, sizeof(bytes))) return 0;
        if (bytes[0] != 0xe9) return address;
        int32_t relative = 0;
        std::memcpy(&relative, bytes + 1, sizeof(relative));
        address = static_cast<uintptr_t>(static_cast<intptr_t>(address + 5) + relative);
    }
    return 0;
}
static std::string image_hash(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0); return {};
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<char> chunk(1u << 20);
    bool ok = bool(file);
    while (ok && file) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (file.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk.data()),
            static_cast<ULONG>(file.gcount()), 0) < 0) ok = false;
    }
    ok = ok && file.eof();
    uint8_t digest[32]{};
    ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
static BOOL CALLBACK find_window(HWND window, LPARAM) {
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(window, &pid);
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (pid == GetCurrentProcessId() && !std::wcscmp(name, L"UnityWndClass")) {
        window_thread = tid; return FALSE;
    }
    return TRUE;
}

#include "hierarchy_observer.h"

// Keep SEH in POD-only leaf functions; exception means stop engine sampling.
static bool capture(Reading* out) {
    __try {
        out->thread = GetCurrentThreadId();
        void* main_camera = get_main();
        void* array = get_all();
        // Verified against upstream's x64 Il2CppObject + bounds + max_length.
        // This is a layout hypothesis, reported explicitly and bounded to 16.
        uint64_t length = 0;
        if (array && read_bytes(reinterpret_cast<uintptr_t>(array) + 24, &length, sizeof(length)) && length <= 16) {
            out->array_valid = true;
            out->camera_count = static_cast<unsigned>(length);
            for (unsigned i = 0; i < out->camera_count; ++i) {
                void* camera = nullptr;
                if (!read_bytes(reinterpret_cast<uintptr_t>(array) + 32 + i * sizeof(void*), &camera, sizeof(camera)) || !camera)
                    continue;
                auto& item = out->cameras[i];
                item.main = camera == main_camera;
                void* transform = get_transform(camera);
                if (!transform) continue;
                get_position(&item.position, transform);
                item.readable = std::isfinite(item.position.x) && std::isfinite(item.position.y) && std::isfinite(item.position.z) &&
                    std::abs(item.position.x) < 1e9f && std::abs(item.position.y) < 1e9f && std::abs(item.position.z) < 1e9f;
            }
        }
        if (find_string && find_object) {
            void* text = find_string("/EntityRoot");
            if (text) out->entity_root = find_object(text) != nullptr;
            text = find_string("/EntityRoot/AvatarRoot");
            if (text) {
                void* root = find_object(text);
                out->avatar_root = root != nullptr;
                hierarchy::capture(root,hierarchy::playback::FullBodyEnabled ? 24 : 12);
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        engine_error.store(GetExceptionCode()); return false;
    }
}
static int __fastcall observe_fov(void* object, float fov) {
    active_calls.fetch_add(1);
    const int result = original_fov(object, fov);
    callback_count.fetch_add(1);
    const DWORD tid = GetCurrentThreadId();
    last_callback_thread.store(tid);
    if (tid == window_thread) {
        window_callback_count.fetch_add(1);
        if (!callback_reentrant && accepting.load() && !engine_error.load()) {
            callback_reentrant = true;
            const unsigned index = sample_count.load();
            const auto now = GetTickCount64();
            if (index < 4 && now - last_sample_time >= 250) {
                if (capture(&readings[index])) sample_count.store(index + 1);
                last_sample_time = now;
            }
            hierarchy::playback::tick(now);
            callback_reentrant = false;
        }
        else if (!callback_reentrant && accepting.load() && (hierarchy::playback::dirty.load() || hierarchy::time_control::dirty.load() || hierarchy::animator_lease::dirty.load())) {
            callback_reentrant = true;
            hierarchy::playback::stop_requested.store(true);
            hierarchy::playback::tick(GetTickCount64());
            callback_reentrant = false;
        }
    }
    active_calls.fetch_sub(1);
    return result;
}

// Independent of Unity's scaled update loop. Only our private posted messages
// are consumed; the original queue and other hooks retain their normal flow.
static LRESULT CALLBACK observe_message(int code, WPARAM removed, LPARAM data) {
    active_calls.fetch_add(1);
    if (code >= 0 && removed == PM_REMOVE && data && GetCurrentThreadId() == window_thread) {
        auto* message = reinterpret_cast<MSG*>(data);
        if (!message->hwnd && message->message == PlaybackMessage &&
            message->wParam == reinterpret_cast<WPARAM>(self_module)) {
            message->message = WM_NULL;
            if (!callback_reentrant && accepting.load()) {
                callback_reentrant = true;
                message_callback_count.fetch_add(1);
                if (engine_error.load()) hierarchy::playback::stop_requested.store(true);
                hierarchy::playback::tick(GetTickCount64());
                callback_reentrant = false;
            }
        }
    }
    const auto result = CallNextHookEx(nullptr, code, removed, data);
    active_calls.fetch_sub(1);
    return result;
}

extern "C" __declspec(dllexport) DWORD WINAPI GimmdProbe(void*) {
    if (busy.exchange(true)) return ERROR_BUSY;
    if (permanently_closed) { busy.store(false); return ERROR_INVALID_STATE; }
    try {
        wchar_t dll_path[32768]{}, exe_path[32768]{};
        if (!GetModuleFileNameW(self_module, dll_path, 32768) || !GetModuleFileNameW(nullptr, exe_path, 32768)) {
            busy.store(false); return ERROR_PATH_NOT_FOUND;
        }
        const auto output = std::filesystem::path(dll_path).parent_path() /
            (L"gimmd-" + std::to_wstring(GetCurrentProcessId()) + L".probe.json");
        const auto sha = image_hash(exe_path);
        const bool build_match = sha == gi::BuildSha256;
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        hierarchy::configure(base, sha);
        gimmd::player_session::initialize(std::filesystem::path(dll_path).parent_path());
        if (hierarchy::configured) hierarchy::playback::load(std::filesystem::path(dll_path).parent_path() /
            (hierarchy::playback::MultiEnabled ? L"multi-track.vmd" : L"single-track.vmd"));
        std::string status = "build-mismatch";
        bool installed = false, disabled = true, restored = true;
        int create_status = -1, enable_status = -1, disable_status = -1;
        uint8_t original_bytes[32]{};
        uintptr_t fov = 0;
        HHOOK message_hook = nullptr;
        bool message_unhooked = true;
        if (build_match) {
            fov = unique("ChangeFOV", base);
            get_main = reinterpret_cast<GetMainFn>(unique("FreeCamCameraGetMain", base));
            get_transform = reinterpret_cast<GetTransformFn>(unique("FreeCamComponentGetTransform", base));
            get_position = reinterpret_cast<GetPositionFn>(unique("FreeCamTransformGetPosition", base));
            find_string = reinterpret_cast<FindStringFn>(unique("FindString", base));
            find_object = reinterpret_cast<FindObjectFn>(unique("FindGameObject", base));
            const auto camera_entry = reinterpret_cast<uintptr_t>(get_main);
            const auto all_entry = camera_entry ? camera_entry + 16 : 0;
            uint8_t opcode = 0;
            if (all_entry && read_bytes(all_entry, &opcode, 1) && opcode == 0xe9 && follow_jump(all_entry, base))
                get_all = reinterpret_cast<GetMainFn>(all_entry);
            status = "runtime-contract-mismatch";
            if (fov && get_main && get_all && get_transform && get_position &&
                follow_jump(camera_entry, base) && read_bytes(fov, original_bytes, sizeof(original_bytes))) {
                window_thread = 0; EnumWindows(find_window, 0);
                message_callback_count.store(0);
                if (window_thread && (hierarchy::time_control::Enabled || hierarchy::animator_lease::Enabled)) {
                    message_hook = SetWindowsHookExW(WH_GETMESSAGE, observe_message, self_module, window_thread);
                    message_unhooked = message_hook == nullptr;
                }
                status = "unity-window-not-found";
                if (window_thread && (!(hierarchy::time_control::Enabled || hierarchy::animator_lease::Enabled) || message_hook)) {
                    callback_count.store(0); window_callback_count.store(0); sample_count.store(0); engine_error.store(0);
                    last_sample_time = 0;
                    for (auto& reading : readings) reading = {};
                    const auto initialized = MH_Initialize();
                    status = "hook-initialize-failed";
                    if (initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED) {
                        create_status = MH_CreateHook(reinterpret_cast<void*>(fov), reinterpret_cast<void*>(&observe_fov),
                            reinterpret_cast<void**>(&original_fov));
                        if (create_status == MH_OK) configured_target = fov;
                        if (create_status == MH_ERROR_ALREADY_CREATED && configured_target == fov && original_fov)
                            create_status = MH_OK;
                        status = "hook-create-failed";
                        if (create_status == MH_OK) {
                            accepting.store(true);
                            enable_status = MH_EnableHook(reinterpret_cast<void*>(fov));
                            installed = enable_status == MH_OK;
                            status = installed ? "waiting-for-camera-callback" : "hook-enable-failed";
                            if (installed) {
                                const auto deadline = GetTickCount64() + (gimmd::player_session::Enabled ? 90000 : 5000);
                                while (GetTickCount64() < deadline && (sample_count.load() < 4 ||
                                    (hierarchy::playback::Enabled && !hierarchy::playback::done.load())) && !engine_error.load() &&
                                    !hierarchy::roundtrip::failed()) {
                                    gimmd::player_session::worker_tick();
                                    if (message_hook) PostThreadMessageW(window_thread, PlaybackMessage, reinterpret_cast<WPARAM>(self_module), 0);
                                    Sleep(20);
                                }
                                hierarchy::playback::stop_requested.store(true);
                                gimmd::player_session::close_audio();
                                const auto cleanup_deadline = GetTickCount64() + 1000;
                                while (hierarchy::playback::started.load() && !hierarchy::playback::done.load() &&
                                    GetTickCount64() < cleanup_deadline) {
                                    if (message_hook) PostThreadMessageW(window_thread, PlaybackMessage, reinterpret_cast<WPARAM>(self_module), 0);
                                    Sleep(10);
                                }
                            }
                            const bool cleanup_pending = hierarchy::playback::started.load() && !hierarchy::playback::done.load();
                            if (!cleanup_pending) accepting.store(false);
                            disable_status = cleanup_pending ? -1 : MH_DisableHook(reinterpret_cast<void*>(fov));
                            disabled = disable_status == MH_OK || disable_status == MH_ERROR_DISABLED;
                            if (!cleanup_pending && message_hook) message_unhooked = UnhookWindowsHookEx(message_hook) != FALSE;
                            // Keep the disabled trampoline/DLL mapped: never free code
                            // that a delayed callback might still be returning through.
                            const auto deadline = GetTickCount64() + 1000;
                            while (active_calls.load() && GetTickCount64() < deadline) Sleep(1);
                            uint8_t after[32]{};
                            restored = disabled && read_bytes(fov, after, sizeof(after)) &&
                                !std::memcmp(original_bytes, after, sizeof(after));
                            permanently_closed = !restored || !message_unhooked || active_calls.load() != 0 || hierarchy::roundtrip::failed() || hierarchy::animator_observer::toggle::failed();
                            if (cleanup_pending) status = "playback-cleanup-pending-hook-retained";
                            else if (!restored) status = "hook-restoration-failed";
                            else if (hierarchy::roundtrip::failed()) status = "pose-restoration-failed";
                            else if (hierarchy::animator_observer::toggle::failed()) status = "animator-toggle-failed";
                            else if (hierarchy::animator_observer::ready.load() && hierarchy::animator_observer::toggle::restored) status = "animator-toggle-restored";
                            else if (engine_error.load()) status = "engine-read-exception";
                            else if (sample_count.load()) {
                                bool readable = false;
                                for (unsigned i = 0; i < sample_count.load(); ++i)
                                    for (unsigned j = 0; j < readings[i].camera_count; ++j)
                                        readable = readable || readings[i].cameras[j].readable;
                                status = readable ? "camera-read-observed" : "camera-callback-observed-no-readable-camera";
                            }
                            else if (callback_count.load()) status = "callback-thread-unconfirmed";
                            else if (installed) status = "no-camera-callback-observed";
                        }
                    }
                }
            }
        }
        if (!installed && message_hook) message_unhooked = UnhookWindowsHookEx(message_hook) != FALSE;
        gimmd::player_session::close_audio();
        gimmd::player_session::write_status();
        std::ofstream report(output, std::ios::trunc);
        if (!report) { busy.store(false); return ERROR_WRITE_FAULT; }
        report << "{\n  \"schemaVersion\": 5,\n  \"pid\": " << GetCurrentProcessId()
            << ",\n  \"mode\": \"" << (hierarchy::playback::ToeIkEnabled ? "canonical-full-body-toe-ik" : hierarchy::playback::LegIkEnabled ? "canonical-full-body-foot-ik" : hierarchy::playback::RootMotionEnabled ? "canonical-full-body-root-motion" : hierarchy::playback::FullBodyEnabled ? "canonical-full-body-fk" : gimmd::player_session::Enabled ? "controlled-multi-track-preview" : hierarchy::playback::MultiEnabled ? "bounded-multi-track-preview" : hierarchy::playback::Enabled ? "bounded-single-track-playback" :
                hierarchy::animator_observer::toggle::Enabled ? "bounded-animator-toggle" : "bounded-camera-observer")
            << "\",\n  \"executableSha256\": \"" << sha
            << "\",\n  \"buildMatch\": " << (build_match ? "true" : "false")
            << ",\n  \"status\": \"" << status << "\",\n  \"settersCalled\": "
            << (hierarchy::roundtrip::setter_calls.load() || hierarchy::playback::setter_calls.load() || hierarchy::time_control::setter_calls.load() || hierarchy::animator_observer::toggle::setter_calls.load() || hierarchy::animator_lease::setter_calls.load() ? "true" : "false") << ','
            << "\n  \"windowThread\": " << window_thread << ",\n  \"lastCallbackThread\": " << last_callback_thread.load()
            << ",\n  \"callbackCount\": " << callback_count.load() << ",\n  \"windowThreadCallbackCount\": " << window_callback_count.load()
            << ",\n  \"engineException\": " << engine_error.load() << ",\n  \"hookInstalled\": " << (installed ? "true" : "false")
            << ",\n  \"hookDisabled\": " << (disabled ? "true" : "false")
            << ",\n  \"entryBytesRestored\": " << (restored ? "true" : "false")
            << ",\n  \"activeCallbacksAtReport\": " << active_calls.load()
            << ",\n  \"messageCallbackCount\": " << message_callback_count.load()
            << ",\n  \"messageHookInstalled\": " << (message_hook ? "true" : "false")
            << ",\n  \"messageHookRemoved\": " << (message_unhooked ? "true" : "false")
            << ",\n  \"hookCreateStatus\": " << create_status << ",\n  \"hookEnableStatus\": " << enable_status
            << ",\n  \"hookDisableStatus\": " << disable_status
            << ",\n  \"gameplayCameraIdentity\": \"unverified\""
            << ",\n  \"arrayLayout\": \"upstream-x64-hypothesis-bounded-to-16\",\n  \"candidates\": [\n";
        bool first = true;
        for (const auto& candidate : gi::Candidates) {
            if (!first) report << ",\n";
            first = false;
            report << "    {\"name\": \"" << candidate.name << "\", \"rva\": " << candidate.rva
                << ", \"runtimeMaskMatches\": " << (build_match && matches(candidate, base) ? "true" : "false") << '}';
        }
        report << "\n  ],\n  \"samples\": [";
        const unsigned count = sample_count.load();
        for (unsigned i = 0; i < count; ++i) {
            const auto& reading = readings[i];
            report << (i ? ",\n" : "\n") << "    {\"thread\": " << reading.thread
                << ", \"arrayValid\": " << (reading.array_valid ? "true" : "false")
                << ", \"entityRootExists\": " << (reading.entity_root ? "true" : "false")
                << ", \"avatarRootExists\": " << (reading.avatar_root ? "true" : "false") << ", \"cameras\": [";
            for (unsigned j = 0; j < reading.camera_count; ++j) {
                const auto& camera = reading.cameras[j];
                report << (j ? ", " : "") << "{\"main\": " << (camera.main ? "true" : "false")
                    << ", \"positionReadable\": " << (camera.readable ? "true" : "false");
                if (camera.readable) report << ", \"position\": [" << std::setprecision(9)
                    << camera.position.x << ", " << camera.position.y << ", " << camera.position.z << ']';
                report << '}';
            }
            report << "]}";
        }
        report << "\n  ],\n  \"hierarchy\":";
        hierarchy::report(report);
        report << ",\n  \"poseRoundTrip\":";
        hierarchy::roundtrip::report(report);
        report << ",\n  \"singleTrackPlayback\":";
        hierarchy::playback::report(report);
        report << ",\n  \"animatorObserver\":";
        hierarchy::animator_observer::report(report);
        gimmd::player_session::report(report);
        report << "\n}\n"; report.flush();
        busy.store(false);
        return report && !hierarchy::roundtrip::failed() && !hierarchy::animator_observer::toggle::failed() && !hierarchy::playback::dirty.load() &&
            !hierarchy::time_control::dirty.load() && !hierarchy::animator_lease::dirty.load() && message_unhooked ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
    } catch (...) {
        gimmd::player_session::close_audio();
        permanently_closed = true;
        hierarchy::playback::stop_requested.store(true);
        if (!hierarchy::playback::dirty.load() && !hierarchy::time_control::dirty.load() && !hierarchy::animator_lease::dirty.load()) accepting.store(false);
        busy.store(false);
        return ERROR_UNHANDLED_EXCEPTION;
    }
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { self_module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

#ifdef GIMMD_OBSERVER_TEST
#include <iostream>
#include <sstream>
struct MockArray { void* klass; void* monitor; void* bounds; uint64_t length; void* items[16]; };
static_assert(offsetof(MockArray, length) == 24 && offsetof(MockArray, items) == 32);
static int mock_camera;
static MockArray mock_array{};
static bool inject_exception = false;
static void* __fastcall mock_main() { return &mock_camera; }
static void* __fastcall mock_all() { return &mock_array; }
static void* __fastcall mock_transform(void* camera) { return camera; }
static void __fastcall mock_position(Vec3* out, void*) {
    if (inject_exception) RaiseException(0xe0012345, 0, 0, nullptr);
    *out = {1.25f, 2.5f, -3.75f};
}
static __declspec(noinline) int __fastcall mock_fov(void* object, float value) {
    return object == &mock_camera ? static_cast<int>(value * 2) + 7 : -1;
}
struct MockNode {
    void* klass = nullptr;
    void* monitor = nullptr;
    MockNode* native = nullptr;
    const wchar_t* name = L"Bone";
    MockNode* parent = nullptr;
    int count = 0;
    MockNode* children[128]{};
    bool active = true;
    hierarchy::Quat local{0,0,0,1};
    gimmd::root_translation::Vec3 position{}, scale{1,1,1};
};
struct MockString { void* klass; void* monitor; int length; wchar_t text[hierarchy::MaxName]; };
static_assert(offsetof(MockNode, native) == 16 && offsetof(MockString, text) == 20);
static MockNode mock_nodes[1280];
static MockString mock_text{};
static bool mock_name_mismatch = false;
static void* mock_string(const wchar_t* text) {
    mock_text.length = static_cast<int>(std::wcslen(text));
    std::memcpy(mock_text.text, text, (static_cast<size_t>(mock_text.length) + 1) * sizeof(wchar_t));
    return &mock_text;
}
static void* __fastcall mock_find_string(const char*) { return mock_string(L"AvatarRoot"); }
static void* __fastcall mock_name_a(void* object) { return mock_string(static_cast<MockNode*>(object)->name); }
static void* __fastcall mock_name_b(void* object) {
    return mock_string(mock_name_mismatch ? L"WrongRoot" : static_cast<MockNode*>(object)->name);
}
static int __fastcall mock_child_count(void* object) { return static_cast<MockNode*>(object)->count; }
static void* __fastcall mock_get_child(void* object, int index) { return static_cast<MockNode*>(object)->children[index]; }
static void* __fastcall mock_get_parent(void* object) { return static_cast<MockNode*>(object)->parent; }
static void* __fastcall mock_object_transform(void** result, void* native) { *result = native; return result; }
static void* __fastcall mock_owner(void* object) { return object; }
static bool __fastcall mock_active(void* object) { return static_cast<MockNode*>(object)->active; }
static void __fastcall mock_rotation(hierarchy::Quat* out, void*) { *out = {0,0,0,1}; }
static void mock_hierarchy_reset() {
    hierarchy::configure(0, "");
    hierarchy::configured = true;
    hierarchy::name_a = mock_name_a; hierarchy::name_b = mock_name_b;
    hierarchy::child_count = mock_child_count; hierarchy::get_child = mock_get_child;
    hierarchy::get_parent = mock_get_parent; hierarchy::object_transform = mock_object_transform;
    hierarchy::component_game_object = mock_owner; hierarchy::get_active = mock_active;
    hierarchy::rotation_world = hierarchy::rotation_local = mock_rotation;
    find_string = mock_find_string; mock_name_mismatch = false;
    for (auto& node : mock_nodes) { node = {}; node.native = &node; }
    mock_nodes[0].name = L"AvatarRoot";
    mock_nodes[0].count = 2;
    mock_nodes[0].children[0] = &mock_nodes[1]; mock_nodes[0].children[1] = &mock_nodes[2];
    mock_nodes[1].parent = mock_nodes[2].parent = &mock_nodes[0];
}
#ifdef GIMMD_POSE_ROUNDTRIP
enum class PoseFault { none, write_after_mutation, read_after_write, original_read, ignore_write, restore_once, restore_always };
static PoseFault pose_fault;
static void __fastcall mock_pose_local(hierarchy::Quat* out, void* object) {
    const auto calls = hierarchy::roundtrip::setter_calls.load();
    if ((pose_fault == PoseFault::original_read && calls == 0) ||
        (pose_fault == PoseFault::read_after_write && calls == 1)) RaiseException(0xe0020001, 0, 0, nullptr);
    *out = static_cast<MockNode*>(object)->local;
}
static void __fastcall mock_pose_world(hierarchy::Quat* out, void* object) {
    auto* node = static_cast<MockNode*>(object);
    *out = node->local;
    while ((node = node->parent) != nullptr) *out = hierarchy::roundtrip::multiply(node->local, *out);
}
static void __fastcall mock_pose_set(void* object, const hierarchy::Quat* value) {
    const auto calls = hierarchy::roundtrip::setter_calls.load();
    if ((pose_fault == PoseFault::restore_once && calls == 2) ||
        (pose_fault == PoseFault::restore_always && calls >= 2)) RaiseException(0xe0020002, 0, 0, nullptr);
    if (pose_fault == PoseFault::ignore_write && calls == 1) return;
    static_cast<MockNode*>(object)->local = *value;
    if (pose_fault == PoseFault::write_after_mutation && calls == 1) RaiseException(0xe0020003, 0, 0, nullptr);
}
static void mock_pose_fixture(PoseFault fault = PoseFault::none) {
    mock_hierarchy_reset();
    hierarchy::roundtrip::consumed.store(false);
    mock_nodes[1].name = mock_nodes[2].name = L"Avatar_Test(Clone)";
    mock_nodes[2].active = false;
    mock_nodes[1].count = 1; mock_nodes[1].children[0] = &mock_nodes[3];
    mock_nodes[3].parent = &mock_nodes[1]; mock_nodes[3].name = L"Bip001 L UpperArm";
    hierarchy::roundtrip::profile_verified = true; hierarchy::roundtrip::set_local = mock_pose_set;
    // Observe the hierarchy with ordinary getters; inject faults only for the transaction.
    hierarchy::rotation_local = hierarchy::rotation_world = mock_rotation;
    hierarchy::roundtrip::profile_verified = false;
    hierarchy::capture(&mock_nodes[0], 1000);
    // Recreate only the callback-local pointers cleared after capture.
    for (unsigned i = 0; i < hierarchy::count; ++i) {
        auto& node = hierarchy::nodes[i];
        node.object = node.parent < 0 ? &mock_nodes[0] :
            !std::wcscmp(node.name, L"Bip001 L UpperArm") ? &mock_nodes[3] :
            node.active ? &mock_nodes[1] : &mock_nodes[2];
        node.identity = reinterpret_cast<uintptr_t>(node.object);
    }
    hierarchy::roundtrip::profile_verified = true;
    hierarchy::rotation_local = mock_pose_local; hierarchy::rotation_world = mock_pose_world;
    pose_fault = fault; accepting.store(true);
}
#endif
#ifdef GIMMD_SINGLE_TRACK_PLAYBACK
static unsigned playback_fault = 0;
#ifdef GIMMD_ANIMATOR_LEASE
static bool mock_lease_enabled = true;
static unsigned mock_lease_fault = 0;
static uintptr_t mock_lease_cache[11]{}, mock_lease_cache_pointer = 0;
static bool __fastcall mock_lease_get(void*) { return mock_lease_enabled; }
static void __fastcall mock_lease_set(void*, bool value) {
    if (value && mock_lease_fault == 2) RaiseException(0xe0070002, 0, 0, nullptr);
    mock_lease_enabled = value;
    if (!value && mock_lease_fault == 1) { mock_lease_fault = 0; RaiseException(0xe0070001, 0, 0, nullptr); }
}
static void* __fastcall mock_lease_component(void* object, void*) { return object == &mock_nodes[1] ? &mock_nodes[100] : nullptr; }
static void* __fastcall mock_lease_owner(void* object) { return object == &mock_nodes[100] ? &mock_nodes[1] : object; }
static void mock_lease_prepare() {
    namespace ao = hierarchy::animator_observer;
    namespace md = hierarchy::animator_observer::metadata;
    hierarchy::animator_lease::set = mock_lease_set; hierarchy::animator_lease::verified = true;
    mock_lease_enabled = true; mock_lease_fault = 0; message_callback_count.store(3);
    ao::ready.store(true); ao::result = "unique-animator-observed"; ao::owner_node = 1;
    ao::get_component = mock_lease_component;
    mock_nodes[100].klass = &mock_camera; mock_lease_cache[10] = reinterpret_cast<uintptr_t>(&mock_camera);
    mock_lease_cache_pointer = reinterpret_cast<uintptr_t>(mock_lease_cache);
    ao::cache_slot = reinterpret_cast<uintptr_t>(&mock_lease_cache_pointer);
    hierarchy::component_game_object = mock_lease_owner;
    md::state_observed = md::component_enabled = md::parent_matches = true; md::enabled_get = mock_lease_get;
    md::method_count = 2; md::image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (unsigned i = 0; i < 2; ++i) {
        md::methods[i] = {}; md::methods[i].parameter_count = i;
        md::methods[i].name = i ? "set_enabled" : "get_enabled"; md::methods[i].code_count = 1; md::methods[i].code_offsets[0] = 8;
        md::methods[i].code_rvas[0] = (i ? reinterpret_cast<uintptr_t>(&mock_lease_set) : reinterpret_cast<uintptr_t>(&mock_lease_get)) - md::image_base;
    }
}
#endif
#ifdef GIMMD_FREEZE_TIME
static float mock_time = 1.25f;
static unsigned time_fault = 0;
static float __fastcall mock_time_get() { return mock_time; }
static void __fastcall mock_time_set(float value) {
    if (time_fault == 2 && value != 0) RaiseException(0xe0040002, 0, 0, nullptr);
    mock_time = value;
    if (time_fault == 1) { time_fault = 0; RaiseException(0xe0040001, 0, 0, nullptr); }
}
#endif
static void* __fastcall mock_find_root(void*) { return &mock_nodes[0]; }
static void __fastcall mock_play_local(hierarchy::Quat* out, void* object) {
    *out = static_cast<MockNode*>(object)->local;
}
static void __fastcall mock_play_set(void* object, const hierarchy::Quat* value) {
    if (playback_fault == 2 && hierarchy::playback::stop_requested.load()) RaiseException(0xe0030002, 0, 0, nullptr);
    static_cast<MockNode*>(object)->local = *value;
    if (playback_fault == 3 && object == &mock_nodes[7]) { playback_fault = 0; RaiseException(0xe0030003, 0, 0, nullptr); }
    if (playback_fault == 1) { playback_fault = 0; RaiseException(0xe0030001, 0, 0, nullptr); }
}
static void mock_play_fixture() {
    mock_hierarchy_reset();
#ifdef GIMMD_FREEZE_TIME
    hierarchy::time_control::verified = true;
    hierarchy::time_control::get = mock_time_get; hierarchy::time_control::set = mock_time_set;
    mock_time = 1.25f; time_fault = 0; message_callback_count.store(3);
#endif
    namespace p = hierarchy::playback;
    p::multi_mode = false; p::full_mode = false; p::root_mode = false; p::leg_mode=false; p::toe_mode=false; // Exercise the legacy strategy in its existing tests.
    p::consumed.store(false); p::profile_verified = true; p::set_local = mock_play_set;
    p::done.store(false); p::loaded = true;
    p::keys.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        auto& k = p::keys[i]; k = {}; k.frame = i * 30;
        k.rotation = {0, 0, i == 1 ? 0.0436193874f : 0.0f, i == 1 ? 0.9990482216f : 1.0f};
        for (unsigned axis = 0; axis < 4; ++axis) {
            k.interpolation[axis] = k.interpolation[axis + 4] = 20;
            k.interpolation[axis + 8] = k.interpolation[axis + 12] = 107;
        }
    }
    mock_nodes[1].name = mock_nodes[2].name = L"Avatar_Test(Clone)"; mock_nodes[2].active = false;
    mock_nodes[1].count = 1; mock_nodes[1].children[0] = &mock_nodes[3];
    mock_nodes[3].parent = &mock_nodes[1]; mock_nodes[3].name = L"Bip001 L UpperArm";
    find_object = mock_find_root; playback_fault = 0;
#ifdef GIMMD_ANIMATOR_LEASE
    mock_lease_prepare();
#endif
    hierarchy::capture(&mock_nodes[0], 1000);
    hierarchy::rotation_local = mock_play_local;
}
static void mock_write_vmd(const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    char header[30] = "Vocaloid Motion Data 0002", model[20] = "gimmd synthetic", name[15] = "left_arm_test";
    const uint32_t three = 3, zero = 0; const float position[3]{};
    out.write(header, sizeof(header)); out.write(model, sizeof(model));
    out.write(reinterpret_cast<const char*>(&three), sizeof(three));
    for (const auto& k : hierarchy::playback::keys) {
        out.write(name, sizeof(name)); out.write(reinterpret_cast<const char*>(&k.frame), sizeof(k.frame));
        out.write(reinterpret_cast<const char*>(position), sizeof(position));
        const float rotation[] = {k.rotation.x,k.rotation.y,k.rotation.z,k.rotation.w};
        out.write(reinterpret_cast<const char*>(rotation), sizeof(rotation));
        out.write(reinterpret_cast<const char*>(k.interpolation.data()), k.interpolation.size());
    }
    for (unsigned i = 0; i < 5; ++i) out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    if (!out) throw std::runtime_error("cannot write playback test fixture");
}
#ifdef GIMMD_MULTI_TRACK_PLAYBACK
static unsigned root_fault = 0;
static gimmd::root_translation::Vec3* __fastcall mock_local_position(gimmd::root_translation::Vec3* out,void* object) {
    *out = static_cast<MockNode*>(object)->position; return out;
}
static gimmd::root_translation::Vec3* __fastcall mock_local_scale(gimmd::root_translation::Vec3* out,void* object) {
    *out = static_cast<MockNode*>(object)->scale; return out;
}
static void __fastcall mock_set_position(void* object,const gimmd::root_translation::Vec3* value) {
    static_cast<MockNode*>(object)->position = *value;
    if (root_fault == 1 && hierarchy::playback::multi::root_motion::calls == 1) RaiseException(0xe0060001,0,0,nullptr);
}
static void mock_multi_fixture(bool full = false,bool root = false,bool leg = false,bool toe = false) {
    mock_hierarchy_reset();
    namespace p = hierarchy::playback;
    p::multi_mode = true; p::consumed.store(false); p::profile_verified = true; p::set_local = mock_play_set;
    p::full_mode = full;
    p::root_mode = root; p::leg_mode=leg; p::toe_mode=toe; root_fault = 0;
    p::done.store(false); playback_fault = 0;
    mock_nodes[1].name = mock_nodes[2].name = L"Avatar_Test(Clone)"; mock_nodes[2].active = false;
    mock_nodes[1].count = full ? 52 : 10;
    if (full) {
        mock_nodes[1].name = gimmd::fk_profile::ModelName;
        namespace bp = hierarchy::animator_observer::bind_pose;
        bp::avatar_rest_status = "avatar-local-reference-observed"; bp::avatar_rest_count = 139;
        for (unsigned i = 0; i < 139; ++i) {
            const auto& r = gimmd::fk_profile::AvatarReference[i]; bp::avatar_parents[i] = r.parent;
            for (unsigned n = 0; n < 3; ++n) { bp::avatar_local[i][n] = r.position[n]; bp::avatar_local[i][8+n] = r.scale[n]; }
            for (unsigned n = 0; n < 4; ++n) bp::avatar_local[i][4+n] = r.rotation[n];
        }
    }
    BetterEndfieldNext::Vmd::Motion motion; motion.last_frame = 60;
    for (unsigned i = 0; i < (full ? 52u : 10u); ++i) {
        const auto& mapping = gimmd::motion_profile::Mappings[full ? i : gimmd::motion_profile::PreviewIndices[i]];
        mock_nodes[1].children[i] = &mock_nodes[3+i]; mock_nodes[3+i].parent = &mock_nodes[1]; mock_nodes[3+i].name = mapping.target;
        if (full && i == 3) continue; // Missing source track holds the target reference rotation.
        auto& keys = motion.bones[mapping.cp932]; keys.resize(3);
        for (unsigned n = 0; n < 3; ++n) {
            keys[n].frame = n * 30; keys[n].rotation = {0,0,n == 1 ? 0.0436193874f : 0.0f,n == 1 ? 0.9990482216f : 1.0f};
        }
    }
    if (root) {
        mock_nodes[1].count = 2; mock_nodes[1].children[0] = &mock_nodes[55]; mock_nodes[1].children[1] = &mock_nodes[56];
        mock_nodes[55].name = L"Bip001"; mock_nodes[55].parent = &mock_nodes[1]; mock_nodes[55].count = 52;
        mock_nodes[55].position = {0.2f,0.3f,0.4f}; mock_nodes[56].name = L"Body"; mock_nodes[56].parent = &mock_nodes[1];
        for (unsigned i = 0; i < 52; ++i) { mock_nodes[55].children[i] = &mock_nodes[3+i]; mock_nodes[3+i].parent = &mock_nodes[55]; }
        auto& control = motion.bones[gimmd::fk_profile::RootTrackNames[1]]; control.resize(3);
        for (unsigned i = 0; i < 3; ++i) { control[i].frame = i*30; control[i].position.y = i == 1 ? 10.0f : 0.0f; }
    }
    if (leg) for (const unsigned mapping:{0u,10u,11u,12u,33u,34u,35u}) {
        const auto& reference=gimmd::fk_profile::AvatarReference[gimmd::fk_profile::AvatarIndices[mapping]];
        mock_nodes[3+mapping].position={reference.position[0],reference.position[1],reference.position[2]};
        mock_nodes[3+mapping].scale={reference.scale[0],reference.scale[1],reference.scale[2]};
    }
    if (leg) for (unsigned side=0;side<2;++side) {
        auto& control=motion.bones[gimmd::fk_profile::FootTrackNames[side][0]]; control.resize(3);
        for (unsigned i=0;i<3;++i) { control[i].frame=i*30; control[i].position.z=i==1 ? 1.0f:0.0f; }
    }
    if (toe) for (unsigned side=0;side<2;++side) {
        const unsigned mapping=gimmd::leg_pose::Toe[side];
        const auto& reference=gimmd::fk_profile::AvatarReference[gimmd::fk_profile::AvatarIndices[mapping]];
        mock_nodes[3+mapping].position={reference.position[0],reference.position[1],reference.position[2]};
        mock_nodes[3+mapping].scale={reference.scale[0],reference.scale[1],reference.scale[2]};
        auto& control=motion.bones[gimmd::fk_profile::ToeTrackNames[side][0]]; control.resize(3);
        for (unsigned i=0;i<3;++i) { control[i].frame=i*30; control[i].position.y=i==1 ? 0.5f:0.0f; }
    }
    p::loaded = p::multi::prepare(motion); mock_lease_prepare(); find_object = mock_find_root;
    if (root) {
        namespace rm = hierarchy::playback::multi::root_motion;
        namespace pm = hierarchy::animator_observer::position_metadata;
        rm::get = mock_local_position; rm::set = mock_set_position; rm::get_scale = mock_local_scale; rm::verified = true;
        pm::method_count = 3;
        strcpy_s(pm::methods[0].name,"get_localPosition"); pm::methods[0].parameters = 0;
        pm::methods[0].code = reinterpret_cast<uintptr_t>(mock_local_position)-hierarchy::animator_observer::metadata::image_base;
        strcpy_s(pm::methods[1].name,"set_localPosition"); pm::methods[1].parameters = 1;
        pm::methods[1].code = reinterpret_cast<uintptr_t>(mock_set_position)-hierarchy::animator_observer::metadata::image_base;
        strcpy_s(pm::methods[2].name,"get_localScale"); pm::methods[2].parameters = 0;
        pm::methods[2].code = reinterpret_cast<uintptr_t>(mock_local_scale)-hierarchy::animator_observer::metadata::image_base;
    }
    hierarchy::capture(&mock_nodes[0], 1000); hierarchy::rotation_local = mock_play_local;
}
#endif
#endif
#ifdef GIMMD_ANIMATOR_PROBE
static uintptr_t mock_animator_cache[11]{};
static uintptr_t mock_animator_cache_pointer = 0;
static unsigned animator_fault = 0;
static uintptr_t mock_metadata_parent = 0;
static unsigned mock_metadata_reads = 0;
static int mock_metadata_mode = 0;
static bool __fastcall mock_metadata_enabled(void*) { ++mock_metadata_reads; return true; }
static int __fastcall mock_metadata_mode_get(void*) { return mock_metadata_mode; }
static uintptr_t __fastcall mock_metadata_parent_get(uintptr_t) { return mock_metadata_parent; }
static const char* __fastcall mock_metadata_method_name(uintptr_t method) {
    return reinterpret_cast<const char*>(reinterpret_cast<uintptr_t*>(method)[6]);
}
static void* __fastcall mock_animator_owner(void* object) {
    if (object == &mock_nodes[100]) return animator_fault == 3 ? &mock_nodes[2] : &mock_nodes[1];
    if (object == &mock_nodes[101]) return &mock_nodes[3];
    return object;
}
static void* __fastcall mock_get_animator(void* owner, void*) {
    if (animator_fault == 1) RaiseException(0xe0050001, 0, 0, nullptr);
    if (owner == &mock_nodes[1]) return &mock_nodes[100];
    if (animator_fault == 4 && owner == &mock_nodes[3]) return &mock_nodes[101];
    return nullptr;
}
static void mock_animator_fixture(unsigned fault = 0) {
    mock_hierarchy_reset(); animator_fault = fault;
    mock_nodes[1].name = mock_nodes[2].name = L"Avatar_Test(Clone)"; mock_nodes[2].active = false;
    mock_nodes[100].klass = fault == 2 ? nullptr : &mock_camera; mock_nodes[101].klass = &mock_camera;
    if (fault == 4) {
        mock_nodes[1].children[0] = &mock_nodes[3]; mock_nodes[1].count = 1; mock_nodes[3].parent = &mock_nodes[1];
    }
    mock_animator_cache[10] = reinterpret_cast<uintptr_t>(&mock_camera);
    mock_animator_cache_pointer = reinterpret_cast<uintptr_t>(mock_animator_cache);
    hierarchy::animator_observer::cache_slot = reinterpret_cast<uintptr_t>(&mock_animator_cache_pointer);
    hierarchy::animator_observer::verified = true; hierarchy::animator_observer::get_component = mock_get_animator;
    hierarchy::component_game_object = mock_animator_owner;
}
#ifdef GIMMD_ANIMATOR_TOGGLE
static bool mock_toggle_value = true;
static unsigned mock_toggle_fault = 0;
static bool __fastcall mock_toggle_get(void*) { return mock_toggle_value; }
static void __fastcall mock_toggle_set(void*, bool value) {
    if (value && mock_toggle_fault == 2) { mock_toggle_fault = 0; RaiseException(0xe0060002, 0, 0, nullptr); }
    if (value && mock_toggle_fault == 3) RaiseException(0xe0060003, 0, 0, nullptr);
    if (!value && mock_toggle_fault == 4) return;
    mock_toggle_value = value;
    if (!value && mock_toggle_fault == 1) RaiseException(0xe0060001, 0, 0, nullptr);
}
static void mock_toggle_fixture(unsigned fault = 0) {
    mock_animator_fixture();
    namespace md = hierarchy::animator_observer::metadata;
    namespace tg = hierarchy::animator_observer::toggle;
    tg::configure(0); tg::verified = true; tg::set = mock_toggle_set; tg::consumed.store(false);
    md::state_observed = md::parent_matches = md::component_enabled = true;
    md::enabled_get = mock_toggle_get; md::method_count = 1; md::image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    md::methods[0] = {}; md::methods[0].name = "set_enabled"; md::methods[0].parameter_count = 1;
    md::methods[0].code_count = 1; md::methods[0].code_offsets[0] = 8;
    md::methods[0].code_rvas[0] = reinterpret_cast<uintptr_t>(&mock_toggle_set) - md::image_base;
    mock_toggle_value = true; mock_toggle_fault = fault;
}
#endif
#endif
int main() {
    unsigned checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks; if (!ok) throw std::runtime_error(message);
    };
    try {
        window_thread = GetCurrentThreadId();
        get_main = mock_main; get_all = mock_all; get_transform = mock_transform; get_position = mock_position;
        mock_array.length = 1; mock_array.items[0] = &mock_camera;
        uint8_t before[32]{};
        const auto target = reinterpret_cast<void*>(&mock_fov);
        check(read_bytes(reinterpret_cast<uintptr_t>(target), before, sizeof(before)), "fixture read failed");
        check(MH_Initialize() == MH_OK, "hook initialize failed");
        check(MH_CreateHook(target, reinterpret_cast<void*>(&observe_fov), reinterpret_cast<void**>(&original_fov)) == MH_OK,
            "hook creation failed");
        accepting.store(true);
        check(MH_EnableHook(target) == MH_OK, "hook enable failed");
        FovFn volatile invoke = &mock_fov;
        check(invoke(&mock_camera, 30.5f) == 68, "FOV arguments/result changed");
        check(sample_count.load() == 1 && callback_count.load() == 1, "callback/sample count wrong");
        check(readings[0].thread == window_thread && readings[0].array_valid, "thread/array confirmation failed");
        check(readings[0].camera_count == 1 && readings[0].cameras[0].main && readings[0].cameras[0].readable &&
            readings[0].cameras[0].position.x == 1.25f, "camera getter ABI fixture failed");
        accepting.store(false);
        check(invoke(&mock_camera, 12) == 31 && sample_count.load() == 1, "closed observer kept sampling");
        accepting.store(true); last_sample_time = 0; mock_array.length = 17;
        invoke(&mock_camera, 12);
        check(sample_count.load() == 2 && !readings[1].array_valid, "array bounds not rejected");
        mock_array.length = 1; last_sample_time = 0; inject_exception = true;
        invoke(&mock_camera, 12);
        check(engine_error.load() == 0xe0012345 && sample_count.load() == 2, "SEH failure not contained");
        last_sample_time = 0;
        invoke(&mock_camera, 12);
        check(sample_count.load() == 2, "sampling continued after engine failure");
        accepting.store(false);
        check(MH_DisableHook(target) == MH_OK, "hook disable failed");
        uint8_t after[32]{};
        check(read_bytes(reinterpret_cast<uintptr_t>(target), after, sizeof(after)) &&
            !std::memcmp(before, after, sizeof(before)), "entry bytes not restored");
        check(active_calls.load() == 0, "callback still active");
        const auto count = callback_count.load();
        check(invoke(&mock_camera, 12) == 31 && callback_count.load() == count, "callback active after disable");
        mock_hierarchy_reset();
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::published.load() && hierarchy::root_name_valid && hierarchy::count == 3 &&
            !std::strcmp(hierarchy::status, "hierarchy-read-observed"), "hierarchy root/getter contract failed");
        check(hierarchy::nodes[2].parent == 0 && hierarchy::nodes[2].parent_valid &&
            hierarchy::nodes[2].aliases_equal && !hierarchy::nodes[2].object && !hierarchy::nodes[2].identity,
            "hierarchy alias/parent verification or pointer cleanup failed");
        check(hierarchy::nodes[2].rotation_readable && hierarchy::nodes[2].rotation_relation_valid,
            "rotation getter relation fixture failed");
        check(hierarchy::rotation_relation({0,0,0,1}, {0,0,0,-1}, {0,0,0,1}), "quaternion antipode rejected");
        check(!hierarchy::rotation_relation({0,0,0,1}, {0,0,0,1}, {1,0,0,0}), "wrong rotation relation accepted");
        check(!hierarchy::unit_quat({0,0,0,0}), "degenerate quaternion accepted");
        mock_text.length = 96; wchar_t string_buffer[hierarchy::MaxName]{};
        check(!hierarchy::string_read(&mock_text, string_buffer), "oversized managed name accepted");
        mock_text.length = -1;
        check(!hierarchy::string_read(&mock_text, string_buffer), "negative managed name length accepted");
        mock_hierarchy_reset(); mock_name_mismatch = true;
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::count == 0 && !hierarchy::root_name_valid, "root alias disagreement accepted");
        mock_hierarchy_reset(); mock_nodes[0].count = -1;
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::invalid_count && hierarchy::count == 1, "negative child count accepted");
        mock_hierarchy_reset(); mock_nodes[0].count = 129;
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::invalid_count && hierarchy::count == 1, "oversized child count accepted");
        mock_hierarchy_reset(); mock_nodes[0].children[0] = &mock_nodes[0];
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::cycle, "hierarchy cycle accepted");
        mock_hierarchy_reset(); mock_nodes[1].parent = &mock_nodes[2];
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::invalid_parent && !hierarchy::nodes[1].parent_valid, "wrong parent accepted");
        mock_hierarchy_reset();
        for (unsigned i = 0; i < 30; ++i) {
            mock_nodes[i].count = 1; mock_nodes[i].children[0] = &mock_nodes[i + 1];
            mock_nodes[i + 1].parent = &mock_nodes[i];
        }
        hierarchy::capture(&mock_nodes[0]);
        check(hierarchy::truncated && hierarchy::count == 25, "depth limit not enforced");
        mock_hierarchy_reset(); mock_nodes[0].count = 128;
        for (unsigned i = 1; i <= 128; ++i) {
            mock_nodes[0].children[i - 1] = &mock_nodes[i]; mock_nodes[i].parent = &mock_nodes[0];
            mock_nodes[i].count = 8;
            for (unsigned child = 0; child < 8; ++child) {
                const unsigned next = 129 + (i - 1) * 8 + child;
                mock_nodes[i].children[child] = &mock_nodes[next]; mock_nodes[next].parent = &mock_nodes[i];
            }
        }
        // Isolate the node bound from host scheduling/time-budget exhaustion.
        // Production capture uses the default 12 ms soft budget.
        hierarchy::capture(&mock_nodes[0], 1000);
        check(hierarchy::truncated && hierarchy::count == hierarchy::MaxNodes, "node limit not enforced");
        std::ostringstream encoded;
        hierarchy::json_string(encoded, L"\"\\\n");
        check(encoded.str() == "\"\\u0022\\u005c\\u000a\"", "managed name JSON escaping failed");
#ifdef GIMMD_POSE_ROUNDTRIP
        namespace rt = hierarchy::roundtrip;
        mock_pose_fixture(); rt::run();
        check(rt::ready.load() && rt::result.write_verified && rt::result.restored && rt::result.world_restored &&
            rt::setter_calls.load() == 2 && rt::error(mock_nodes[3].local, {0,0,0,1}) == 0,
            "single-bone write/restore failed");
        check(rt::error(rt::result.requested, rt::result.before) > 0.001f, "transaction did not request a real change");
        const auto previous_calls = rt::setter_calls.load(); rt::run();
        check(rt::setter_calls.load() == previous_calls, "transaction repeated after publication");
        rt::reset(); rt::profile_verified = true; rt::set_local = mock_pose_set; rt::run();
        check(!rt::setter_calls.load() && !std::strcmp(rt::result.status, "experiment-already-consumed"),
            "another probe call repeated a consumed experiment");
        mock_pose_fixture(PoseFault::write_after_mutation); rt::run();
        check(rt::result.write_exception && rt::result.restored && rt::error(mock_nodes[3].local, {0,0,0,1}) == 0,
            "partial write exception was not restored");
        mock_pose_fixture(PoseFault::read_after_write); rt::run();
        check(rt::result.write_exception && rt::result.restored, "read-after-write exception was not restored");
        mock_pose_fixture(PoseFault::original_read); rt::run();
        check(rt::result.read_exception && !rt::setter_calls.load(), "write occurred after original read failure");
        mock_pose_fixture(PoseFault::ignore_write); rt::run();
        check(!rt::result.write_verified && rt::result.restored, "ignored setter reported as successful change");
        mock_pose_fixture(PoseFault::restore_once); rt::run();
        check(rt::result.restored && rt::result.restore_attempts == 2 && rt::setter_calls.load() == 3,
            "transient restoration failure was not retried");
        mock_pose_fixture(PoseFault::restore_always); rt::run();
        check(rt::failed() && !accepting.load() && rt::result.restore_attempts == 2,
            "permanent restoration failure not closed");
        mock_pose_fixture(); hierarchy::nodes[2].active = true; rt::run();
        check(!rt::setter_calls.load() && !std::strcmp(rt::result.status, "actor-ambiguity-refused"),
            "ambiguous actor accepted");
        mock_pose_fixture(); hierarchy::status = "hierarchy-partial"; rt::run();
        check(!rt::setter_calls.load(), "partial hierarchy accepted for write");
        mock_pose_fixture(); hierarchy::nodes[3].identity = 1; rt::run();
        check(!rt::setter_calls.load(), "stale native identity accepted for write");
        mock_pose_fixture(); rt::profile_verified = false; rt::run();
        check(!rt::setter_calls.load(), "unverified setter profile accepted");
#else
        hierarchy::roundtrip::run();
        check(!hierarchy::roundtrip::Enabled && !hierarchy::roundtrip::setter_calls.load(),
            "default observer unexpectedly enabled pose writes");
#endif
#ifdef GIMMD_SINGLE_TRACK_PLAYBACK
        namespace p = hierarchy::playback;
        mock_play_fixture();
        const std::filesystem::path fixture_path = "playback-parser-test.vmd";
        mock_write_vmd(fixture_path); p::consumed.store(false); p::load(fixture_path);
        check(p::loaded && p::keys.size() == 3, "generated VMD rejected by playback preflight");
        { std::ofstream invalid(fixture_path, std::ios::binary | std::ios::trunc); invalid << "invalid-vmd"; }
        p::load(fixture_path);
        check(!p::loaded && p::done.load(), "invalid VMD accepted by playback preflight");
        std::filesystem::remove(fixture_path);
        mock_play_fixture();
        check(p::started.load() && p::path_size == 3 && !hierarchy::nodes[3].object, "playback binding/pointer cleanup failed");
        p::tick(1000); p::tick(1010);
        check(p::result.samples == 1, "duplicate callback was not throttled");
        p::tick(2000);
        check(p::result.samples == 2 && std::abs(mock_nodes[3].local.z - 0.0436193874f) < 1e-6f, "VMD peak sample not applied");
        p::tick(2040);
        check(p::result.persisted == 2 && p::dirty.load(), "cross-callback pose retention not observed");
        mock_nodes[3].local = {0.02f,0,0,0.99979998f}; p::tick(2100);
        check(p::result.overwritten == 1 && p::result.baseline.x == 0.02f, "animation overwrite not adopted as restoration baseline");
        p::tick(3000);
        check(p::done.load() && p::result.restored && !p::dirty.load() && mock_nodes[3].local.x == 0.02f,
            "duration end did not restore latest engine pose");
        const auto calls = p::setter_calls.load(); p::tick(4000);
        check(p::setter_calls.load() == calls, "writes continued after stop");
        mock_play_fixture(); p::tick(1000); p::tick(2000); p::stop_requested.store(true); p::tick(2001);
        check(p::done.load() && p::result.restored && mock_nodes[3].local.z == 0, "explicit stop failed");
        mock_play_fixture(); playback_fault = 1; p::tick(1000);
        check(p::done.load() && p::result.exception && p::result.restored && !p::dirty.load(), "partial setter mutation not restored");
        mock_play_fixture(); p::tick(1000); p::tick(2000); playback_fault = 2; p::stop_requested.store(true); p::tick(2001);
        check(!p::done.load() && p::dirty.load() && p::result.restore_attempts == 2, "restore failure incorrectly reported complete");
        playback_fault = 0; p::tick(2002);
        check(p::done.load() && !p::dirty.load() && p::result.restored, "pending restoration did not recover");
        mock_play_fixture(); p::tick(1000); p::tick(2000); mock_nodes[3].native = &mock_nodes[4]; p::tick(2040);
        check(!p::done.load() && p::dirty.load() && p::result.resolve_failures == 1, "changed identity accepted for cleanup/write");
        mock_nodes[3].native = &mock_nodes[3]; p::tick(2041);
        check(p::done.load() && p::result.restored, "identity recovery failed to clean up");
        mock_play_fixture(); mock_nodes[1].active = false; p::tick(1000);
        check(p::done.load() && !p::setter_calls.load(), "inactive actor received a pose write");
        mock_play_fixture(); mock_nodes[2].active = true; p::tick(1000);
        check(p::done.load() && !p::setter_calls.load(), "new actor ambiguity accepted for write");
        mock_play_fixture(); p::tick(1000); playback_fault = 2; p::stop_requested.store(true);
        for (unsigned i = 0; i < 10; ++i) p::tick(1100 + i);
        check(p::dirty.load() && !p::done.load() && p::result.restore_attempts == 8, "restoration retry bound exceeded");
        // Test cleanup only: permit one final mock restoration after asserting the cap.
        playback_fault = 0; p::result.restore_attempts = 0; p::tick(1200);
        mock_play_fixture(); mock_nodes[0].children[0] = &mock_nodes[2]; p::tick(1000);
        check(p::done.load() && !p::setter_calls.load(), "reordered hierarchy accepted for write");
        p::load(L"fixture-that-does-not-exist.vmd");
        check(p::done.load() && !p::loaded, "consumed/missing fixture not rejected");
#ifdef GIMMD_ANIMATOR_LEASE
        namespace al = hierarchy::animator_lease;
        mock_play_fixture(); message_callback_count.store(0); p::tick(1000);
        check(!al::setter_calls.load() && !p::setter_calls.load(), "Animator disabled before independent callback confirmation");
        message_callback_count.store(3); p::tick(1100); p::tick(1140);
        check(al::acquired && !mock_lease_enabled && al::checks == 1, "Animator lease did not remain disabled across callbacks");
        p::tick(3100);
        check(p::done.load() && p::result.restored && al::restored && mock_lease_enabled && al::setter_calls.load() == 2,
            "Animator lease duration end did not restore bone and component");
        mock_play_fixture(); mock_lease_enabled = false; p::tick(1000);
        check(p::done.load() && !al::setter_calls.load() && !p::setter_calls.load() && !mock_lease_enabled,
            "already disabled Animator was modified");
        mock_play_fixture(); mock_lease_fault = 1; p::tick(1000);
        check(p::done.load() && al::exception && al::restored && mock_lease_enabled && !p::setter_calls.load(),
            "partial Animator lease mutation exception not restored");
        mock_play_fixture(); p::tick(1000); mock_lease_fault = 2; p::stop_requested.store(true); p::tick(1100);
        check(!p::done.load() && al::dirty.load() && !p::dirty.load(), "Animator restore failure incorrectly completed playback");
        mock_lease_fault = 0; p::tick(1200);
        check(p::done.load() && al::restored && mock_lease_enabled, "Animator restoration retry did not recover");
        mock_play_fixture(); p::tick(1000); p::tick(2000); mock_lease_enabled = true; p::tick(2040);
        check(p::done.load() && al::overrides == 1 && !p::dirty.load() && al::restored,
            "external Animator override failed to stop and restore playback");
        mock_play_fixture(); p::tick(1000); p::tick(2000); mock_nodes[3].native = &mock_nodes[4]; p::tick(2040);
        check(!p::done.load() && p::dirty.load() && !al::dirty.load() && mock_lease_enabled,
            "lost bone identity left Animator disabled");
        mock_nodes[3].native = &mock_nodes[3]; p::tick(2041);
        check(p::done.load() && p::result.restored, "bone identity recovery failed after Animator release");
        mock_play_fixture(); p::tick(1000); mock_nodes[100].native = &mock_nodes[101]; p::tick(1100);
        check(!p::done.load() && al::dirty.load() && al::setter_calls.load() == 1, "changed Animator identity accepted for restoration");
        mock_nodes[100].native = &mock_nodes[100]; p::tick(1200);
        check(p::done.load() && al::restored, "Animator identity recovery failed to restore lease");
        mock_play_fixture(); p::tick(1000); mock_nodes[1].active = false; p::tick(1100);
        check(p::done.load() && al::restored && mock_lease_enabled, "inactive actor prevented original Animator state restoration");
        mock_play_fixture(); p::tick(1000); mock_lease_fault = 2; p::stop_requested.store(true);
        for (unsigned i = 0; i < 20; ++i) p::tick(1100 + i);
        check(!p::done.load() && !p::dirty.load() && al::dirty.load() && al::restore_attempts == 8 && al::setter_calls.load() == 9,
            "Animator restoration attempt cap exceeded or falsely completed");
        mock_lease_fault = 0; al::restore_attempts = 0; p::tick(1200); // Synthetic cleanup after checking the cap.
        check(p::done.load() && al::restored, "synthetic Animator cleanup failed");
        mock_play_fixture(); ++hierarchy::animator_observer::metadata::methods[1].code_rvas[0];
        check(!al::bind(p::resolve_model) && !al::setter_calls.load(), "unnamed Animator lease setter accepted");
        mock_play_fixture(); p::tick(1000); playback_fault = 2; p::stop_requested.store(true);
        for (unsigned i = 0; i < 10; ++i) p::tick(1100 + i);
        check(p::dirty.load() && !al::dirty.load() && mock_lease_enabled, "exhausted bone restoration left Animator disabled");
        playback_fault = 0; p::result.restore_attempts = 0; p::tick(1200); // Synthetic cleanup only.
        mock_play_fixture();
        MSG lease_message{}; lease_message.message = PlaybackMessage; lease_message.wParam = reinterpret_cast<WPARAM>(self_module);
        accepting.store(true); window_thread = GetCurrentThreadId(); engine_error.store(0);
        observe_message(0, PM_REMOVE, reinterpret_cast<LPARAM>(&lease_message));
        check(lease_message.message == WM_NULL && al::acquired && p::result.samples == 1, "message callback failed to drive Animator lease");
        p::stop_requested.store(true); lease_message.message = PlaybackMessage;
        observe_message(0, PM_REMOVE, reinterpret_cast<LPARAM>(&lease_message));
        check(p::done.load() && al::restored, "message callback failed to restore Animator lease");
#else
        check(!hierarchy::animator_lease::Enabled && !hierarchy::animator_lease::setter_calls.load(), "ordinary playback acquired Animator");
#endif
#ifdef GIMMD_FREEZE_TIME
        namespace tc = hierarchy::time_control;
        mock_play_fixture(); message_callback_count.store(0); p::tick(1000);
        check(!tc::setter_calls.load() && !p::setter_calls.load(), "freeze began without independent callback confirmation");
        message_callback_count.store(3); p::tick(1100);
        check(tc::acquired && mock_time == 0 && tc::dirty.load(), "timeScale did not freeze");
        p::tick(3100);
        check(p::done.load() && tc::restored && !tc::dirty.load() && mock_time == 1.25f, "original non-default timeScale not restored");
        mock_play_fixture(); mock_time = 0; p::tick(1000);
        check(p::done.load() && !tc::setter_calls.load(), "already paused timeScale was changed");
        mock_play_fixture(); time_fault = 1; p::tick(1000);
        check(p::done.load() && tc::exception && tc::restored && mock_time == 1.25f && !p::setter_calls.load(),
            "partial timeScale write exception not restored");
        mock_play_fixture(); p::tick(1000); time_fault = 2; p::stop_requested.store(true); p::tick(1100);
        check(!p::done.load() && tc::dirty.load() && !p::dirty.load(), "time restore failure reported success");
        time_fault = 0; p::tick(1200);
        check(p::done.load() && tc::restored && mock_time == 1.25f, "time restoration retry failed");
        mock_play_fixture(); p::tick(1000); p::tick(2000); mock_time = 0.75f; p::tick(2040);
        check(p::done.load() && !p::dirty.load() && tc::restored && tc::override_count == 1 && mock_time == 1.25f,
            "external timeScale override left the bone or time unrestored");
        mock_play_fixture(); p::tick(1000); p::tick(2000); mock_nodes[3].native = &mock_nodes[4]; p::tick(2040); p::tick(2041);
        check(!p::done.load() && p::dirty.load() && !tc::dirty.load() && mock_time == 1.25f,
            "lost bone identity left the global time frozen");
        mock_nodes[3].native = &mock_nodes[3]; p::tick(2042);
        mock_play_fixture();
        MSG private_message{}; private_message.message = PlaybackMessage;
        private_message.wParam = reinterpret_cast<WPARAM>(self_module);
        accepting.store(true); window_thread = GetCurrentThreadId(); engine_error.store(0);
        observe_message(0, PM_REMOVE, reinterpret_cast<LPARAM>(&private_message));
        check(private_message.message == WM_NULL && tc::acquired && p::result.samples == 1,
            "independent message callback did not drive playback");
        p::stop_requested.store(true); private_message.message = PlaybackMessage;
        observe_message(0, PM_REMOVE, reinterpret_cast<LPARAM>(&private_message));
        check(p::done.load() && tc::restored, "message pump did not stop/restore playback");
        MSG unrelated{}; unrelated.message = WM_KEYDOWN;
        observe_message(0, PM_REMOVE, reinterpret_cast<LPARAM>(&unrelated));
        check(unrelated.message == WM_KEYDOWN, "unrelated message was modified");
#else
        check(!hierarchy::time_control::Enabled && !hierarchy::time_control::setter_calls.load(), "ordinary playback froze global time");
#endif
#else
        check(!hierarchy::playback::Enabled && !hierarchy::playback::setter_calls.load(), "default observer enabled playback writes");
#endif
#ifdef GIMMD_ANIMATOR_PROBE
        namespace ao = hierarchy::animator_observer;
#ifdef GIMMD_MULTI_TRACK_PLAYBACK
        namespace mp = hierarchy::playback;
        mock_multi_fixture();
        check(mp::started.load() && mp::multi::tracks[9].length == 3, "multi-track binding failed");
        mp::tick(1000); mp::tick(2000); mp::tick(2040);
        check(mp::result.samples == 3 && mp::setter_calls.load() == 30 && mp::result.persisted == 20,
            "multi-track batching or retention failed");
        mp::tick(3000);
        check(mp::done.load() && mp::result.restored && mp::setter_calls.load() == 40 && hierarchy::animator_lease::restored,
            "multi-track stop did not restore all targets and Animator");
        for (unsigned i = 0; i < mp::multi::TrackCount; ++i) {
            const auto& track = mp::multi::tracks[i];
            check(track.restored && !track.dirty && track.attempts == 1, "multi-track target restoration incomplete");
        }
        mock_multi_fixture(); playback_fault = 3; mp::tick(1000);
        check(mp::done.load() && mp::result.exception && !mp::dirty.load() && hierarchy::animator_lease::restored,
            "partial batch exception left target states dirty");
        for (unsigned i = 3; i < 13; ++i) check(mock_nodes[i].local.w == 1, "partial batch baseline not restored");
        mock_multi_fixture(); mock_nodes[12].native = &mock_nodes[13]; mp::tick(1000);
        check(mp::done.load() && !mp::setter_calls.load() && hierarchy::animator_lease::restored,
            "batch wrote before all target identities passed preflight");
        mock_multi_fixture(); mp::tick(1000); mp::tick(2000); mock_nodes[12].native = &mock_nodes[13]; mp::tick(2040);
        check(!mp::done.load() && mp::dirty.load() && !hierarchy::animator_lease::dirty.load(),
            "lost multi-track target left Animator disabled or reported success");
        mock_nodes[12].native = &mock_nodes[12]; mp::tick(2041);
        check(mp::done.load() && mp::multi::tracks[9].restored, "multi-track identity recovery failed");
        mock_multi_fixture(); mp::tick(1000); playback_fault = 2; mp::stop_requested.store(true);
        for (unsigned i = 0; i < 10; ++i) mp::tick(1100+i);
        check(mp::dirty.load() && !mp::done.load() && mp::result.restore_attempts == 80 && !hierarchy::animator_lease::dirty.load(),
            "multi-track restoration retry cap failed");
        playback_fault = 0; for (auto& track : mp::multi::tracks) track.attempts = 0; mp::tick(1200); // Synthetic cleanup only.
        check(mp::done.load() && mp::result.restored, "multi-track synthetic cleanup failed");
        mock_multi_fixture(); mp::tick(1000); mock_nodes[5].local = {0.02f,0,0,0.99979998f}; mp::tick(1040);
        check(mp::done.load() && mp::result.overwritten == 1 && mp::result.restored, "external pose write did not stop multi-track preview");
        mock_hierarchy_reset(); BetterEndfieldNext::Vmd::Motion missing_motion; missing_motion.last_frame = 60;
        check(!mp::multi::prepare(missing_motion), "missing required source tracks accepted");
        mock_multi_fixture(); gimmd::player_session::active = true;
        gimmd::player_session::command.store(0); mp::tick(1000); mp::tick(1500);
        gimmd::player_session::command.store(gimmd::parse_player_command("1 pause")); mp::tick(1540); mp::tick(6000);
        auto session_frame = gimmd::unpack_player_frame(gimmd::player_session::frame.load());
        check(!mp::done.load() && session_frame.state == gimmd::PlaybackState::Paused && std::abs(session_frame.seconds-0.54)<0.002,
            "session pause advanced or retained old two-second timeout");
        gimmd::player_session::command.store(gimmd::parse_player_command("2 seek 1.5")); mp::tick(6040);
        session_frame = gimmd::unpack_player_frame(gimmd::player_session::frame.load());
        check(session_frame.seconds == 1.5 && session_frame.epoch == 1, "session seek not published coherently");
        gimmd::player_session::command.store(gimmd::parse_player_command("3 loop-on")); mp::tick(6080);
        gimmd::player_session::command.store(gimmd::parse_player_command("4 play")); mp::tick(6120); mp::tick(6800);
        session_frame = gimmd::unpack_player_frame(gimmd::player_session::frame.load());
        check(session_frame.epoch == 2 && session_frame.seconds < 0.2 && !mp::done.load(), "session loop did not wrap");
        gimmd::player_session::command.store(gimmd::parse_player_command("5 stop")); mp::tick(6840);
        check(mp::done.load() && mp::result.restored && hierarchy::animator_lease::restored && mp::multi::commands_applied == 5,
            "session stop did not restore poses and Animator");
        gimmd::player_session::active = false;
#ifdef GIMMD_FULL_BODY_FK
        mock_multi_fixture(true);
        check(mp::started.load() && mp::multi::TrackCount == 52 && mp::multi::source_used == 51 && mp::multi::tracks[3].keys.empty(),
            "full-body admission or missing-track reference fallback failed");
        mp::tick(1000); mp::tick(2000);
        const auto fk_peak = gimmd::fk_retarget::evaluate(gimmd::fk_profile::Bindings[7],{0,0,0.0436193874f,0.9990482216f});
        check(mp::result.samples == 2 && mp::setter_calls.load() == 104 &&
            hierarchy::roundtrip::error(mock_nodes[10].local,{fk_peak.x,fk_peak.y,fk_peak.z,fk_peak.w}) < 0.00001f,
            "full-body FK target rotation not applied");
        mp::tick(3000);
        check(mp::done.load() && mp::result.restored && mp::setter_calls.load() == 156 && hierarchy::animator_lease::restored,
            "full-body stop left pose or Animator dirty");
        mock_multi_fixture(true); hierarchy::animator_observer::bind_pose::avatar_local[0][0] += 0.01f;
        check(!mp::multi::bind(1) && !mp::setter_calls.load(), "changed reference skeleton admitted");
        mp::stop_requested.store(true); mp::tick(1000);
        mock_multi_fixture(true); playback_fault = 3; mp::tick(1000);
        check(mp::done.load() && mp::result.exception && mp::result.restored && hierarchy::animator_lease::restored,
            "full-body partial batch rollback failed");
        mock_multi_fixture(true); gimmd::player_session::active = true; gimmd::player_session::command.store(0);
        mp::tick(1000); mp::tick(1100);
        gimmd::player_session::command.store(gimmd::parse_player_command("1 pause")); mp::tick(1140);
        const auto paused_blend = mp::multi::entry_blend_seconds; mp::tick(2000);
        check(mp::multi::entry_blend_seconds == paused_blend, "entry blend continued while paused");
        gimmd::player_session::command.store(gimmd::parse_player_command("2 play")); mp::tick(2040); mp::tick(2440);
        check(mp::multi::entry_blend_seconds == 0.5, "entry blend did not resume");
        mp::stop_requested.store(true); mp::tick(2500); gimmd::player_session::active = false;
#ifdef GIMMD_ROOT_MOTION
        namespace rm = hierarchy::playback::multi::root_motion;
        mock_multi_fixture(true,true);
        check(mp::started.load() && rm::bound, "visual root binding failed");
        mp::tick(1000); mp::tick(2000);
        check(rm::writes == 2 && std::abs(mock_nodes[55].position.y-(0.3f+10*gimmd::fk_profile::RootMotionScale)) < 0.00001f,
            "scaled Center translation not applied");
        mp::tick(3000);
        check(mp::done.load() && rm::restored && !rm::dirty && rm::error(mock_nodes[55].position,{0.2f,0.3f,0.4f}) == 0 &&
            hierarchy::animator_lease::restored, "visual root position did not restore");
        mock_multi_fixture(true,true); mp::tick(1000); mock_nodes[55].position.y += 0.01f; mp::tick(2000);
        check(mp::done.load() && rm::overwrites == 1 && rm::restored && mp::result.restored,
            "external visual root write did not stop and restore playback");
        mock_multi_fixture(true,true); root_fault = 1; mp::tick(1000);
        check(mp::done.load() && mp::result.exception && rm::restored && mp::result.restored && hierarchy::animator_lease::restored,
            "partial position write exception left state dirty");
        rm::reset(); BetterEndfieldNext::Vmd::Motion rotated_control;
        auto& control = rotated_control.bones[gimmd::fk_profile::RootTrackNames[1]]; control.resize(1); control[0].rotation = {0.1f,0,0,0.9949874f};
        check(!rm::prepare(rotated_control), "unsupported rotated Center control silently accepted");
#ifdef GIMMD_LEG_IK
        namespace lk=hierarchy::playback::multi::leg_playback;
        mock_multi_fixture(true,true,true);
        check(mp::loaded && !std::strcmp(lk::status,"foot-ik-prepared"),"foot IK tracks not admitted");
        mp::tick(1000); mp::tick(2000);
        check(lk::solved==4 && lk::residual<0.002f && mp::result.samples==2,"two-bone IK not applied before engine writes");
        mp::tick(3000);
        check(mp::done.load() && mp::result.restored && rm::restored && hierarchy::animator_lease::restored,
            "IK playback did not restore poses/root/Animator");
        mock_multi_fixture(true,true,true); lk::switches[0]={{0,false},{30,true}};
        mp::tick(1000);
        check(lk::solved==1 && lk::disabled==1,"IK off switch did not preserve FK");
        mp::tick(2000); check(lk::solved==3,"IK on switch did not resume solving");
        mp::stop_requested.store(true); mp::tick(2034);
        mock_multi_fixture(true,true,true); mp::tick(1000); playback_fault=3; mp::tick(2000);
        check(mp::done.load() && mp::result.restored && hierarchy::animator_lease::restored,"IK partial write exception failed cleanup");
        mock_multi_fixture(true,true,true); mp::tick(1000); mock_nodes[55].local.x+=0.01f; mp::tick(2000);
        check(mp::done.load() && rm::restored && mp::result.samples==1,"external skeleton root rotation did not stop IK");
        mock_multi_fixture(true,true,true); mock_nodes[3+11].position.x+=0.01f; mp::tick(1000);
        check(mp::done.load()&&!mp::setter_calls.load()&&lk::geometry_error>0.009f&&hierarchy::animator_lease::restored,
            "unverified live leg lengths accepted for IK");
#ifdef GIMMD_TOE_IK
        mock_multi_fixture(true,true,true,true); mp::tick(1000); mp::tick(2000);
        check(lk::toe_solved==4 && lk::toe_corrected==2 && lk::toe_direction_error<0.002f,"toe targets not applied after leg solve");
        mp::tick(3000);
        check(mp::done.load()&&mp::result.restored&&rm::restored&&hierarchy::animator_lease::restored,"toe playback cleanup failed");
        mock_multi_fixture(true,true,true,true); lk::toe_switches[0]={{0,false},{30,true}}; mp::tick(1000);
        check(lk::toe_solved==1&&lk::toe_disabled==1,"toe IK switch ignored");
        mp::tick(2000); check(lk::toe_solved==3,"toe IK on switch did not resume");
        mp::stop_requested.store(true); mp::tick(2034);
        mock_multi_fixture(true,true,true,true); mock_nodes[3+13].position.x+=0.01f; mp::tick(1000);
        check(mp::done.load()&&!mp::setter_calls.load()&&hierarchy::animator_lease::restored,"unverified toe geometry accepted");
        mock_multi_fixture(true,true,true,true); mp::tick(1000); playback_fault=3; mp::tick(2000);
        check(mp::done.load()&&mp::result.restored&&rm::restored&&hierarchy::animator_lease::restored,"toe partial write exception failed cleanup");
        mock_multi_fixture(true,true,true,true); mock_nodes[3].position.x+=0.005f; mp::tick(1000);
        check(mp::result.samples==1&&lk::geometry_captured&&lk::adjusted_joints==1,"small live geometry deviation not measured");
        mock_nodes[3].position.x+=0.0001f; mp::tick(2000);
        check(mp::done.load()&&mp::result.restored&&rm::restored&&!std::strcmp(lk::status,"live-leg-captured-geometry-changed"),
            "captured live geometry mutation did not stop and restore");
#endif
#endif
#endif
#endif
#endif
        mock_animator_fixture(); hierarchy::capture(&mock_nodes[0], 1000);
        check(ao::ready.load() && ao::matches == 1 && ao::owner_node == 1 && !std::strcmp(ao::result, "unique-animator-observed"),
            "named component class/owner verification failed");
        mock_animator_fixture(1); hierarchy::capture(&mock_nodes[0], 1000);
        check(ao::exception && ao::ready.load(), "component query exception not contained");
        mock_animator_fixture(2); hierarchy::capture(&mock_nodes[0], 1000);
        check(!ao::matches && ao::mismatches == 1, "wrong Animator class accepted");
        mock_animator_fixture(3); hierarchy::capture(&mock_nodes[0], 1000);
        check(!ao::matches && ao::mismatches == 1, "wrong Animator owner accepted");
        mock_animator_fixture(4); hierarchy::capture(&mock_nodes[0], 1000);
        check(ao::matches == 2 && !std::strcmp(ao::result, "animator-ambiguity-refused"), "ambiguous Animator accepted");
        mock_animator_fixture(); mock_animator_cache_pointer = 0; hierarchy::capture(&mock_nodes[0], 1000);
        check(!ao::queries && !std::strcmp(ao::result, "animator-class-cache-unavailable"), "missing class cache queried components");
        namespace md = hierarchy::animator_observer::metadata;
        uintptr_t class_a[64]{}, class_b[64]{}, cache_b[34]{}, info[8]{}, table[8]{};
        static const char animator_name[48] = "Animator", behaviour_name[48] = "Behaviour", enabled_name[48] = "get_enabled";
        class_a[2] = reinterpret_cast<uintptr_t>(animator_name); class_a[11] = reinterpret_cast<uintptr_t>(class_b);
        class_b[2] = reinterpret_cast<uintptr_t>(behaviour_name); class_b[19] = reinterpret_cast<uintptr_t>(table);
        table[0] = reinterpret_cast<uintptr_t>(info); info[2] = reinterpret_cast<uintptr_t>(enabled_name);
        info[0] = reinterpret_cast<uintptr_t>(&mock_fov);
        cache_b[33] = reinterpret_cast<uintptr_t>(class_b); uintptr_t cache_pointer = reinterpret_cast<uintptr_t>(cache_b);
        md::reset(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))); md::profile_verified = true;
        md::behaviour_slot = reinterpret_cast<uintptr_t>(&cache_pointer); md::capture(reinterpret_cast<uintptr_t>(class_a));
        check(md::parent_offset == 88 && md::text_count == 2 && md::method_count == 1, "bounded metadata discovery failed");
        check(md::methods[0].table_offset == 152 && md::methods[0].name_offset == 16 && md::methods[0].code_count == 1,
            "method name/code candidate association failed");
        md::reset(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))); md::profile_verified = true;
        md::behaviour_slot = reinterpret_cast<uintptr_t>(&cache_pointer); class_a[2] = 0;
        md::capture(reinterpret_cast<uintptr_t>(class_a));
        check(!md::method_count && !std::strcmp(md::status, "named-class-layout-unconfirmed"), "unconfirmed class name accepted");
        md::reset(0); md::profile_verified = false; md::capture(1);
        check(!md::method_count && !std::strcmp(md::status, "behaviour-profile-unverified"), "metadata profile bypassed");
        class_a[2] = reinterpret_cast<uintptr_t>(animator_name); class_b[11] = reinterpret_cast<uintptr_t>(table);
        class_b[24] = 1; info[0] = reinterpret_cast<uintptr_t>(class_b);
        info[1] = reinterpret_cast<uintptr_t>(&mock_fov); info[6] = reinterpret_cast<uintptr_t>(enabled_name);
        auto reset_accessors = [&]() {
            md::reset(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))); md::profile_verified = md::accessors_verified = true;
            md::behaviour_slot = reinterpret_cast<uintptr_t>(&cache_pointer);
            md::parent_get = mock_metadata_parent_get; md::method_name = mock_metadata_method_name;
            mock_metadata_parent = reinterpret_cast<uintptr_t>(class_b);
        };
        reset_accessors(); md::capture(reinterpret_cast<uintptr_t>(class_a));
        check(md::parent_matches && md::method_queries == 1 && md::method_count == 1 &&
            !std::strcmp(md::status, "named-methods-observed"), "named accessor discovery failed");
        check(md::methods[0].code_offsets[0] == 8 && md::declared_counts[1] == 1, "named accessor code association failed");
        reset_accessors(); mock_metadata_parent = 0; md::capture(reinterpret_cast<uintptr_t>(class_a));
        check(!md::method_queries && !std::strcmp(md::status, "parent-accessor-mismatch-refused"), "wrong parent accessor accepted");
        reset_accessors(); info[0] = 0; md::capture(reinterpret_cast<uintptr_t>(class_a));
        check(md::owner_mismatches == 1 && !md::method_queries, "wrong method owner accepted");
        reset_accessors(); class_b[24] = 1025; md::capture(reinterpret_cast<uintptr_t>(class_a));
        check(!md::method_queries && !std::strcmp(md::status, "method-table-unavailable"), "oversized method count accepted");
        auto reset_state_probe = [&]() {
            md::reset(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))); md::parent_matches = true;
            md::enabled_get = mock_metadata_enabled; md::mode_get = mock_metadata_mode_get;
            md::method_count = 2; mock_metadata_reads = 0; mock_metadata_mode = 0;
            md::methods[0] = {}; md::methods[0].name = "get_enabled";
            md::methods[1] = {}; md::methods[1].name = "get_updateMode";
            for (unsigned i = 0; i < 2; ++i) { md::methods[i].code_count = 1; md::methods[i].code_offsets[0] = 8; }
            md::methods[0].code_rvas[0] = reinterpret_cast<uintptr_t>(md::enabled_get) - md::image_base;
            md::methods[1].code_rvas[0] = reinterpret_cast<uintptr_t>(md::mode_get) - md::image_base;
        };
        reset_state_probe(); md::observe_component(nullptr);
        check(md::state_observed && md::component_enabled && md::update_mode == 0 && mock_metadata_reads == 2,
            "component read-only state observation failed");
        reset_state_probe(); ++md::methods[0].code_rvas[0]; md::observe_component(nullptr);
        check(!md::state_observed && !mock_metadata_reads, "unmatched enabled method called");
        reset_state_probe(); mock_metadata_mode = 3; md::observe_component(nullptr);
        check(!md::state_observed, "invalid Animator update mode accepted");
#ifdef GIMMD_ANIMATOR_TOGGLE
        namespace tg = hierarchy::animator_observer::toggle;
        mock_toggle_fixture(); tg::run(&mock_nodes[100]);
        check(tg::disabled_observed && tg::restored && mock_toggle_value && tg::setter_calls.load() == 2 && !tg::failed(),
            "Animator toggle did not disable/restore");
        tg::run(&mock_nodes[100]);
        check(tg::setter_calls.load() == 2, "Animator toggle repeated consumed experiment");
        mock_toggle_fixture(1); tg::run(&mock_nodes[100]);
        check(tg::exception && tg::restored && mock_toggle_value && !tg::dirty.load(), "partial Animator mutation not restored");
        mock_toggle_fixture(2); tg::run(&mock_nodes[100]);
        check(tg::restored && tg::restore_attempts == 2 && mock_toggle_value, "Animator restore retry failed");
        mock_toggle_fixture(3); tg::run(&mock_nodes[100]);
        check(tg::failed() && tg::dirty.load() && tg::restore_attempts == 2, "Animator permanent restoration failure accepted");
        mock_toggle_fault = 0; mock_toggle_set(nullptr, true); // Synthetic cleanup only.
        mock_toggle_fixture(4); tg::run(&mock_nodes[100]);
        check(!tg::disabled_observed && tg::restored && tg::failed(), "ignored Animator disable accepted");
        mock_toggle_fixture(); ++md::methods[0].code_rvas[0]; tg::run(&mock_nodes[100]);
        check(!tg::setter_calls.load(), "unnamed Animator setter invoked");
        mock_toggle_fixture(); mock_toggle_value = false; tg::run(&mock_nodes[100]);
        check(!tg::setter_calls.load() && !mock_toggle_value, "originally disabled Animator enabled");
#else
        check(!hierarchy::animator_observer::toggle::Enabled && !hierarchy::animator_observer::toggle::setter_calls.load(),
            "read-only Animator observer changed enabled state");
#endif
#else
        check(!hierarchy::animator_observer::Enabled, "unexpected Animator probing");
#endif
        std::cout << checks << " runtime observer checks passed in synthetic host\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime test failed: " << error.what() << '\n'; return 1;
    }
}
#endif
