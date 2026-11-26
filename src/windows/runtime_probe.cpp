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
#include "core/face_bridge.h"
#include "core/face_profile.h"
#include "core/physics_bridge.h"
#include "core/secondary_physics.h"
#include "core/physics_controls.h"
#include "core/body_collision.h"
#include "core/cloth_links.h"
#include "core/cloth_surface.h"
#include "core/skinned_cloth.h"
#include "core/collei_shoulder.h"
#include "core/rotation_preview.h"
#include "generated/fk_profile.h"
#include "core/root_translation.h"
#include "core/root_controls.h"
#include "core/leg_pose.h"
#include "windows/player_session.h"
#include "MinHook.h"
#include "generated/gi_profile.h"
#include "generated/gi_actor_catalog.h"
#include "generated/gi_actor_resource_ids.h"
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
struct CameraReading {
    bool main=false,readable=false,callback_target=false,name_valid=false;
    Vec3 position{}; wchar_t name[96]{};
};
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
#include "resource_trace.h"
#include "camera_roundtrip.h"
#include "core/camera_pose.h"
#include "camera_playback.h"
#include "local_actors.h"
#include "bgm_pause.h"
#include "uid_text.h"

// Keep SEH in POD-only leaf functions; exception means stop engine sampling.
static void dynamic_dispatch(uintptr_t base,const std::filesystem::path& directory,uint64_t request) {
    const auto op=gimmd::command_op(request);
    namespace ps=gimmd::player_session;
    if (op==gimmd::PlayerOp::Exit) { ps::exit_requested.store(true); hierarchy::playback::stop_requested.store(true); }
    else if (op==gimmd::PlayerOp::CameraOn&&!ps::camera_requested.load()&&!camera_playback::dirty.load()) {
        if (camera_playback::queue(directory/L"camera-next.vmd")) ps::camera_requested.store(true);
    }
    else if (op==gimmd::PlayerOp::CameraOff) ps::camera_requested.store(false);
    else if (op==gimmd::PlayerOp::Restore||op==gimmd::PlayerOp::Stop) { if (ps::mmd_requested.load()||ps::rearm_pending.load()) { ps::mmd_phase.store(4); hierarchy::playback::stop_requested.store(true); } }
    else if (op==gimmd::PlayerOp::Play&&!ps::mmd_requested.load()&&!ps::rearm_pending.load()) {
        ps::mmd_phase.store(1); hierarchy::playback::consumed.store(false); hierarchy::playback::configure(base);
        hierarchy::playback::load(directory/L"multi-track.vmd");
        camera_playback::front_state.store(0);
        bool ready=hierarchy::playback::loaded&&ps::reload_audio();
        if(ready&&std::filesystem::exists(directory/L"camera-front.flag")) {
            ready=camera_playback::queue(directory/L"camera-next.vmd");
            if(ready){camera_playback::front_deadline=GetTickCount64()+5000;camera_playback::front_state.store(1);ps::camera_frame.store(0);ps::camera_requested.store(true);}
        }
        if(ready)ps::rearm_pending.store(true);else ps::mmd_phase.store(3);
    }
    if (op!=gimmd::PlayerOp::Pause&&op!=gimmd::PlayerOp::Seek) ps::acknowledged.store(gimmd::command_sequence(request));
    if (!gimmd::player_session::mmd_requested.load()&&(gimmd::command_op(request)==gimmd::PlayerOp::Pause||gimmd::command_op(request)==gimmd::PlayerOp::Seek)) {
        if (gimmd::command_op(request)==gimmd::PlayerOp::Seek) {
            auto f=gimmd::unpack_player_frame(gimmd::player_session::camera_frame.load());
            f.seconds=gimmd::command_seconds(request); ++f.epoch; f.state=gimmd::PlaybackState::Paused;
            gimmd::player_session::camera_frame.store(gimmd::pack_player_frame(f));
        }
        gimmd::player_session::acknowledged.store(gimmd::command_sequence(request));
    }
}
static void dynamic_tick() {
    namespace ps=gimmd::player_session;
    if (!ps::dynamic_session) return;
    if(ps::rearm_pending.load()&&(ps::exit_requested.load()||hierarchy::playback::stop_requested.load())) {
        ps::rearm_pending.store(false);ps::mmd_phase.store(0);
        if(camera_playback::front_state.load()==1){camera_playback::front_state.store(0);ps::camera_requested.store(false);}return;
    }
    if(ps::rearm_pending.load()&&camera_playback::front_state.load()==1) {
        if(GetTickCount64()<camera_playback::front_deadline)return;
        camera_playback::front_state.store(3);
    }
    if(ps::rearm_pending.load()&&camera_playback::front_state.load()==3) {
        ps::rearm_pending.store(false);ps::mmd_phase.store(3);ps::camera_requested.store(false);return;
    }
    if (ps::rearm_pending.load()) {
        ps::mmd_requested.store(true); ps::rearm_pending.store(false);
        hierarchy::attempted=false; hierarchy::published.store(false); hierarchy::count=0;
        hierarchy::root_name_valid=hierarchy::truncated=hierarchy::invalid_count=hierarchy::cycle=hierarchy::invalid_parent=hierarchy::invalid_name=hierarchy::timed_out=false;
        hierarchy::status="waiting-for-avatar-root";
        hierarchy::time_control::configure(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
        hierarchy::animator_observer::configure(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
        hierarchy::animator_lease::configure(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
        if(local_actors::Enabled&&local_actors::configured) {
            hierarchy::playback::multi::reference_pose::enabled=true;
            hierarchy::animator_lease::aux_begin=local_actors::driver_lease::begin;
            hierarchy::animator_lease::aux_release=local_actors::driver_lease::release;
        }
        ps::mmd_requested.store(true); ps::mmd_phase.store(1);
    }
    if (ps::mmd_requested.load()&&hierarchy::playback::done.load()) {
        ps::mmd_requested.store(false); ps::mmd_phase.store((hierarchy::playback::result.restored||hierarchy::playback::stop_requested.load()) ? 0:3);
    }
    if (ps::mmd_requested.load()&&hierarchy::playback::started.load()) ps::mmd_phase.store(hierarchy::playback::stop_requested.load() ? 4:2);
}
static bool capture(Reading* out,void* fov_target=nullptr) {
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
                uintptr_t native=0;
                if (read_bytes(reinterpret_cast<uintptr_t>(camera)+16,&native,sizeof(native)))
                    item.callback_target=camera==fov_target || (native&&native==reinterpret_cast<uintptr_t>(fov_target));
                void* transform = get_transform(camera);
                if (!transform) continue;
                if (hierarchy::configured) {
                    void* owner=hierarchy::component_game_object(transform); void* owner_transform=nullptr; uintptr_t owner_native=0;
                    wchar_t alias[hierarchy::MaxName]{};
                    if (owner&&read_bytes(reinterpret_cast<uintptr_t>(owner)+16,&owner_native,sizeof(owner_native))&&owner_native&&
                        hierarchy::object_transform(&owner_transform,reinterpret_cast<void*>(owner_native))==&owner_transform&&hierarchy::same_object(transform,owner_transform)&&
                        hierarchy::string_read(hierarchy::name_a(owner),item.name)&&hierarchy::string_read(hierarchy::name_b(owner),alias)&&!std::wcscmp(item.name,alias))
                        item.name_valid=true;
                }
                get_position(&item.position, transform);
                item.readable = std::isfinite(item.position.x) && std::isfinite(item.position.y) && std::isfinite(item.position.z) &&
                    std::abs(item.position.x) < 1e9f && std::abs(item.position.y) < 1e9f && std::abs(item.position.z) < 1e9f;
                if (item.readable&&item.callback_target&&item.name_valid&&!std::wcscmp(item.name,L"MainCamera(Clone)")) camera_experiment::run(transform);
            }
        }
        if (find_string && find_object) {
            void* text = find_string("/EntityRoot");
            if (text) out->entity_root = find_object(text) != nullptr;
            text = find_string("/EntityRoot/AvatarRoot");
            if (text) {
                void* root = find_object(text);
                out->avatar_root = root != nullptr;
                if (!gimmd::player_session::dynamic_session||gimmd::player_session::mmd_requested.load()) hierarchy::capture(root,(hierarchy::actor_survey::Enabled || local_actors::Enabled) ? 48 : (hierarchy::playback::FullBodyEnabled ? 24 : 12));
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
            dynamic_tick(); local_actors::tick(); hierarchy::actor_survey::tick();
            const unsigned index = sample_count.load();
            const auto now = GetTickCount64();
            if ((index < 4||(gimmd::player_session::dynamic_session&&gimmd::player_session::mmd_requested.load()&&!hierarchy::attempted)) && now - last_sample_time >= 250) {
                if (capture(&readings[std::min(index,3u)],object)&&index<4) sample_count.store(index + 1);
                last_sample_time = now;
            }
            hierarchy::playback::tick(now);
            camera_playback::tick(object); bgm_pause::tick(); uid_text::tick();
            callback_reentrant = false;
        }
        else if (!callback_reentrant && accepting.load() && (hierarchy::playback::dirty.load() || hierarchy::time_control::dirty.load() || hierarchy::animator_lease::dirty.load() || bgm_pause::dirty.load() || uid_text::dirty.load() || local_actors::dirty.load())) {
            callback_reentrant = true;
            local_actors::tick();
            hierarchy::playback::stop_requested.store(true);
            hierarchy::playback::tick(GetTickCount64());
                camera_playback::tick(); bgm_pause::tick(); uid_text::tick();
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
                dynamic_tick(); local_actors::tick(); hierarchy::actor_survey::tick();
                if (engine_error.load()) hierarchy::playback::stop_requested.store(true);
                hierarchy::playback::tick(GetTickCount64());
                camera_playback::tick(); bgm_pause::tick(); uid_text::tick();
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
        local_actors::configure(base, sha);
        if(local_actors::Enabled&&local_actors::configured){
            gimmd::face_bridge::prepare=local_actors::face_motion::prepare;gimmd::face_bridge::tick=local_actors::face_motion::tick;
            gimmd::face_bridge::restore=local_actors::face_motion::restore;gimmd::face_bridge::matched=local_actors::face_motion::matched;
            gimmd::physics_bridge::prepare=local_actors::secondary_physics::prepare;gimmd::physics_bridge::tick=local_actors::secondary_physics::tick;
            gimmd::physics_bridge::hold_settled=[] {return local_actors::secondary_physics::s.settle_remaining==0;};
            gimmd::physics_bridge::restore=local_actors::secondary_physics::restore;gimmd::physics_bridge::release_ready=local_actors::secondary_physics::release_ready;
        }
        if(local_actors::Enabled&&local_actors::configured) {
            hierarchy::playback::multi::reference_pose::enabled=true;
            hierarchy::animator_lease::aux_begin=local_actors::driver_lease::begin;
            hierarchy::animator_lease::aux_release=local_actors::driver_lease::release;
        }
        camera_experiment::configure(base);
        gimmd::player_session::initialize(std::filesystem::path(dll_path).parent_path());
        camera_playback::configure(base,std::filesystem::path(dll_path).parent_path());
        camera_playback::front_pose=local_actors::shooting::front;
        bgm_pause::configure(); uid_text::configure(base,std::filesystem::path(dll_path).parent_path());
        if (gimmd::player_session::dynamic_session) hierarchy::playback::done.store(true);
        if (hierarchy::configured&&!gimmd::player_session::dynamic_session) hierarchy::playback::load(std::filesystem::path(dll_path).parent_path() /
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
                                resource_trace::start(base, std::filesystem::path(dll_path).parent_path());
                                const auto deadline = GetTickCount64() + (gimmd::player_session::Enabled ? 600000 : (resource_trace::Enabled ? resource_trace::duration_ms : 5000));
                                uint64_t dispatched=0;
                                while ((gimmd::player_session::dynamic_session || gimmd::player_session::hold_at_end || GetTickCount64() < deadline) && (gimmd::player_session::dynamic_session || resource_trace::Enabled || hierarchy::actor_survey::work_pending.load() || sample_count.load() < 4 ||
                                    (hierarchy::playback::Enabled && !hierarchy::playback::done.load())) && !engine_error.load() &&
                                    !hierarchy::roundtrip::failed() && (gimmd::player_session::dynamic_session ? !gimmd::player_session::exit_requested.load():!hierarchy::playback::stop_requested.load())) {
                                    if (resource_trace::Enabled && GetFileAttributesW((std::filesystem::path(dll_path).parent_path() / L"resource-trace.stop").c_str()) != INVALID_FILE_ATTRIBUTES) break;
                                    gimmd::player_session::worker_tick();
                                    local_actors::worker(std::filesystem::path(dll_path).parent_path());
                                    const auto request=gimmd::player_session::command.load();
                                    if (gimmd::player_session::dynamic_session&&gimmd::command_sequence(request)>dispatched) {
                                        dispatched=gimmd::command_sequence(request);
                                        dynamic_dispatch(base,std::filesystem::path(dll_path).parent_path(),request);
                                    }
                                    else if (!gimmd::player_session::dynamic_session&&gimmd::player_session::Enabled && gimmd::command_op(request)==gimmd::PlayerOp::Stop) {
                                        hierarchy::playback::stop_requested.store(true);
                                        gimmd::player_session::acknowledged.store(gimmd::command_sequence(request));
                                    }
                                    bgm_pause::refresh();
                                    if (message_hook) PostThreadMessageW(window_thread, PlaybackMessage, reinterpret_cast<WPARAM>(self_module), 0);
                                    Sleep(20);
                                }
                                resource_trace::stop();
                                local_actors::stop.store(true);
                                hierarchy::playback::stop_requested.store(true);
                                gimmd::player_session::close_audio();
                                const auto cleanup_deadline = GetTickCount64() + 1000;
                                while (((hierarchy::playback::started.load() && !hierarchy::playback::done.load()) || camera_playback::dirty.load() || bgm_pause::dirty.load() || uid_text::dirty.load() || local_actors::dirty.load()) &&
                                    GetTickCount64() < cleanup_deadline) {
                                    if (message_hook) PostThreadMessageW(window_thread, PlaybackMessage, reinterpret_cast<WPARAM>(self_module), 0);
                                    Sleep(10);
                                }
                            }
                            local_actors::worker(std::filesystem::path(dll_path).parent_path());
                            const bool cleanup_pending = (hierarchy::playback::started.load() && !hierarchy::playback::done.load()) || camera_playback::dirty.load() || bgm_pause::dirty.load() || uid_text::dirty.load() || local_actors::dirty.load();
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
                            permanently_closed = !resource_trace::restored || resource_trace::active.load() || !restored || !message_unhooked || active_calls.load() != 0 || hierarchy::roundtrip::failed() || hierarchy::animator_observer::toggle::failed() || camera_experiment::failed() || camera_playback::dirty.load() || bgm_pause::dirty.load() || uid_text::dirty.load() || local_actors::dirty.load();
                            if (cleanup_pending) status = "playback-cleanup-pending-hook-retained";
                            else if (!restored) status = "hook-restoration-failed";
                            else if (hierarchy::roundtrip::failed()) status = "pose-restoration-failed";
                            else if (hierarchy::animator_observer::toggle::failed()) status = "animator-toggle-failed";
                            else if (camera_experiment::failed()) status = "camera-restoration-failed";
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
        gimmd::player_session::closed=true;
        gimmd::player_session::write_status();
        std::ofstream report(output, std::ios::trunc);
        if (!report) { busy.store(false); return ERROR_WRITE_FAULT; }
        report << "{\n  \"schemaVersion\": 5,\n  \"pid\": " << GetCurrentProcessId()
            << ",\n  \"mode\": \"" << (hierarchy::playback::ToeIkEnabled ? "canonical-full-body-toe-ik" : hierarchy::playback::LegIkEnabled ? "canonical-full-body-foot-ik" : hierarchy::playback::RootMotionEnabled ? "canonical-full-body-root-motion" : hierarchy::playback::FullBodyEnabled ? "canonical-full-body-fk" : gimmd::player_session::Enabled ? "controlled-multi-track-preview" : hierarchy::playback::MultiEnabled ? "bounded-multi-track-preview" : hierarchy::playback::Enabled ? "bounded-single-track-playback" :
                hierarchy::animator_observer::toggle::Enabled ? "bounded-animator-toggle" : "bounded-camera-observer")
            << "\",\n  \"executableSha256\": \"" << sha
            << "\",\n  \"buildMatch\": " << (build_match ? "true" : "false")
            << ",\n  \"status\": \"" << status << "\",\n  \"settersCalled\": "
            << (hierarchy::roundtrip::setter_calls.load() || hierarchy::playback::setter_calls.load() || hierarchy::time_control::setter_calls.load() || hierarchy::animator_observer::toggle::setter_calls.load() || hierarchy::animator_lease::setter_calls.load() || camera_experiment::result.calls || camera_playback::writes || bgm_pause::calls || uid_text::calls || local_actors::mutated.load() ? "true" : "false") << ','
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
                    << ", \"fovCallbackTarget\": " << (camera.callback_target ? "true":"false") << ", \"nameValidated\": " << (camera.name_valid ? "true":"false")
                    << ", \"positionReadable\": " << (camera.readable ? "true" : "false");
                if (camera.name_valid) { report << ", \"name\": "; hierarchy::json_string(report,camera.name); }
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
        camera_experiment::report(report);
        resource_trace::report(report);
        camera_playback::report(report); bgm_pause::report(report); uid_text::report(report);
        local_actors::driver_lease::report(report);
        local_actors::secondary_physics::report(report);
        local_actors::face_motion::report(report);
        report << "\n}\n"; report.flush();
        busy.store(false);
        return report && !hierarchy::roundtrip::failed() && !hierarchy::animator_observer::toggle::failed() && !camera_experiment::failed() && !camera_playback::dirty.load() && !bgm_pause::dirty.load() && !uid_text::dirty.load() && !hierarchy::playback::dirty.load() &&
            !hierarchy::time_control::dirty.load() && !hierarchy::animator_lease::dirty.load() && message_unhooked ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
    } catch (...) {
        resource_trace::stop();
        gimmd::player_session::close_audio();
        permanently_closed = true;
        hierarchy::playback::stop_requested.store(true);
        if (!hierarchy::playback::dirty.load() && !hierarchy::time_control::dirty.load() && !hierarchy::animator_lease::dirty.load() && !camera_playback::dirty.load() && !bgm_pause::dirty.load() && !uid_text::dirty.load()) accepting.store(false);
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
static bool __fastcall mock_resource_format(wchar_t* output, size_t capacity, void**, int32_t index, int32_t category, void**) {
    if (capacity < 21 || index != 42 || category != 2) return false;
    wcscpy_s(output, capacity, L"root/02/00000042.asb"); return true;
}
static void* __fastcall mock_resource_hash(void* bundle, uint32_t hash, void* type, int* output) {
    *output = 37;
    return hash == 0xdeadbeef && type == bundle ? bundle : nullptr;
}

static int pipeline_mock_token=0;
static bool pipeline_mock_void_ok=false;
static void* __fastcall mock_pipeline_file(const char* path,uint64_t offset,uint32_t crc,uint32_t options) {
    return path && !std::strcmp(path,"blocks/test.blk") && offset==0x100000001ull && crc==17 && options==29 ? &pipeline_mock_token : nullptr;
}
static void* __fastcall mock_pipeline_memory(void* data,uint64_t size,uint64_t offset,uint64_t length,uint32_t a,uint32_t b) {
    return data==&pipeline_mock_token && size==0x100000001ull && offset==0x200000002ull && length==0x300000003ull && a==0x12345678 && b==0xabcdef01 ? data : nullptr;
}
static void* __fastcall mock_pipeline_cache(void* bundle,uint32_t hash,void* type) {
    return bundle==type && hash==0xdeadbeef ? bundle : nullptr;
}
static void __fastcall mock_pipeline_template(void* value) { pipeline_mock_void_ok=value==&pipeline_mock_token; }
static void* __fastcall mock_pipeline_instance(void* value) { return value; }
static void __fastcall mock_pipeline_raw(void* data,void* vector) {
    pipeline_mock_void_ok=data==&pipeline_mock_token;
    *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(vector)+16)=7;
}

static void* __fastcall mock_pipeline_mhy(void* manager,const char* path,uint64_t offset,uint32_t size) {
    return manager==&pipeline_mock_token && path && !std::strcmp(path,"blocks/test.blk") && offset==0x100000001ull && size==43 ? manager : nullptr;
}
static uint32_t __fastcall mock_pipeline_async(void* cache) { return cache==&pipeline_mock_token ? 0xfedcba98u : 0; }
static uint32_t __fastcall mock_pipeline_provider(void* self,uint32_t hash,void* method) {
    return self==&pipeline_mock_token && hash==0xdeadbeef && method==&pipeline_mock_token ? 0xfedcba98u : 0;
}

static bool __fastcall mock_pipeline_ready(void* value) { return value==&pipeline_mock_token; }

static MockNode mock_nodes[hierarchy::MaxNodes + 512];
static MockString mock_text{};
static bool mock_name_mismatch = false;
static void* mock_string(const wchar_t* text) {
    mock_text.length = static_cast<int>(std::wcslen(text));
    std::memcpy(mock_text.text, text, (static_cast<size_t>(mock_text.length) + 1) * sizeof(wchar_t));
    return &mock_text;
}
static int mock_music_pause=0,mock_music_mode=0;
static char mock_music_name[64]="BGM";
static int __cdecl mock_music_ready() { return 1; }
static int __cdecl mock_music_count() { return mock_music_mode==2 ? 2:1; }
static int __cdecl mock_music_info(uint16_t,void* out) {
    uint32_t id=7; uintptr_t name=reinterpret_cast<uintptr_t>(mock_music_name);
    std::memcpy(static_cast<uint8_t*>(out)+4,&id,4); std::memcpy(static_cast<uint8_t*>(out)+8,&name,sizeof(name)); return 1;
}
static int __cdecl mock_music_paused(uint32_t) { return mock_music_pause; }
static void __cdecl mock_music_set(uint32_t,int value) { mock_music_pause=value; }
static MockString mock_uid{};
static void* __fastcall mock_uid_string(const char* value) {
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,mock_text.text,hierarchy::MaxName);
    mock_text.length=n-1; return n ? &mock_text:nullptr;
}
static void* __fastcall mock_uid_get(void*) { return &mock_uid; }
static void __fastcall mock_uid_set(void*,void* value) { mock_uid=*static_cast<MockString*>(value); }
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
#ifdef GIMMD_LOCAL_ACTORS
static unsigned mock_actor_destroyed = 0, mock_actor_source_writes = 0;
struct MockActorBones { uintptr_t klass=0, monitor=0, bounds=0; uint64_t length=0; void* bones[2]{}; };
static MockActorBones mock_actor_bones{};
static void* mock_actor_root_bone = nullptr;
static void* __fastcall mock_actor_bones_get(void*) { return &mock_actor_bones; }
static void* __fastcall mock_actor_root_get(void*) { return mock_actor_root_bone; }
static void* __fastcall mock_actor_find(void*) { return &mock_nodes[0]; }
static void __fastcall mock_actor_active(void* object, bool active) {
    if (object == &mock_nodes[3]) ++mock_actor_source_writes;
    static_cast<MockNode*>(object)->active = active;
}
static void __fastcall mock_actor_destroy(void* object, bool) {
    if (object == &mock_nodes[3]) ++mock_actor_source_writes;
    ++mock_actor_destroyed;
    auto& root = mock_nodes[0];
    for (int i = 0; i < root.count; ++i) if (root.children[i] == object) {
        for (int j = i + 1; j < root.count; ++j) root.children[j - 1] = root.children[j];
        --root.count; break;
    }
    static_cast<MockNode*>(object)->native = nullptr;
}
#endif
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
#if defined(GIMMD_CAMERA_ROUNDTRIP) || defined(GIMMD_CAMERA_PLAYBACK)
static unsigned camera_mock_fault=0;
#ifdef GIMMD_CAMERA_PLAYBACK
static float camera_mock_fov=45;
static bool camera_mock_fov_read(uintptr_t,float* out) { *out=camera_mock_fov; return true; }
static int __fastcall camera_mock_fov_set(void*,float value) { camera_mock_fov=value; return 0; }
#endif
static void __fastcall camera_mock_position(Vec3* out,void* object) {
    const auto& p=static_cast<MockNode*>(object)->position; *out={p.x,p.y,p.z};
}
static void __fastcall camera_mock_rotation(hierarchy::Quat* out,void* object) { *out=static_cast<MockNode*>(object)->local; }
static void __fastcall camera_mock_set_position(void* object,const Vec3* value) {
    static_cast<MockNode*>(object)->position={value->x,value->y,value->z};
}
static void __fastcall camera_mock_set_rotation(void* object,const hierarchy::Quat* value) {
    static_cast<MockNode*>(object)->local=*value;
    if (camera_mock_fault==1) { camera_mock_fault=0; RaiseException(0xe0060001,0,0,nullptr); }
    if (camera_mock_fault==2) { static_cast<MockNode*>(object)->native=nullptr; camera_mock_fault=0; }
}
static void camera_mock_fixture(unsigned fault=0) {
    mock_hierarchy_reset(); camera_mock_fault=fault;
    get_position=camera_mock_position; hierarchy::rotation_world=camera_mock_rotation;
    camera_experiment::result={}; camera_experiment::consumed=false; camera_experiment::verified=true;
    camera_experiment::set_position=camera_mock_set_position; camera_experiment::set_rotation=camera_mock_set_rotation;
    mock_nodes[0].position={10,20,30};
}
#endif
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
static unsigned mock_root_lookups=0;
static void* __fastcall mock_find_root(void*) { ++mock_root_lookups;return &mock_nodes[0]; }
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
int wmain(int argc,wchar_t** argv) {
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
#ifdef GIMMD_CAMERA_ROUNDTRIP
        camera_mock_fixture(); camera_experiment::run(&mock_nodes[0]);
        check(camera_experiment::result.position_verified&&camera_experiment::result.rotation_verified&&camera_experiment::result.restored&&
            camera_experiment::result.calls==4,"camera position/rotation roundtrip failed");
        check(mock_nodes[0].position.x==10&&mock_nodes[0].position.y==20&&mock_nodes[0].position.z==30&&mock_nodes[0].local.w==1,
            "camera pose not restored");
        camera_experiment::run(&mock_nodes[0]); check(camera_experiment::result.calls==4,"camera experiment consumed twice");
        camera_mock_fixture(1); camera_experiment::run(&mock_nodes[0]);
        check(camera_experiment::result.exception&&camera_experiment::result.restored,"camera partial mutation exception not restored");
        camera_mock_fixture(2); camera_experiment::run(&mock_nodes[0]);
        check(camera_experiment::failed()&&camera_experiment::result.attempts==8,"camera identity loss not bounded/reported");
        camera_experiment::result={}; camera_experiment::verified=false; get_position=mock_position;
#endif
#ifdef GIMMD_CAMERA_PLAYBACK
        auto camera_setup=[](unsigned fault=0) {
            camera_mock_fixture(fault); camera_mock_fov=45;
            camera_playback::fov_read=camera_mock_fov_read; original_fov=camera_mock_fov_set;
            camera_playback::manual_only=false; gimmd::player_session::camera_control={};
            gimmd::player_session::camera_acknowledged.store(0);
            camera_playback::loaded=true; camera_playback::acquired=false; camera_playback::aborted=false;
            camera_playback::restored=false; camera_playback::dirty.store(false); camera_playback::writes=0;
            camera_playback::set_position=camera_mock_set_position; camera_playback::set_rotation=camera_mock_set_rotation;
            camera_playback::keys.resize(2); camera_playback::keys[0]={}; camera_playback::keys[1]={};
            camera_playback::keys[1].frame=30; camera_playback::keys[1].interest={4,2,1};
            camera_playback::sample(0,&camera_playback::zero);
            hierarchy::playback::stop_requested.store(false); hierarchy::playback::done.store(false); engine_error.store(0);
            gimmd::player_session::frame.store(gimmd::pack_player_frame({0,0,gimmd::PlaybackState::Playing}));
        };
        camera_setup(); camera_playback::apply(&mock_nodes[0],123,false);
        check(!camera_playback::acquired,"camera acquired without FOV callback ownership");
        camera_playback::apply(&mock_nodes[0],123,true);
        check(camera_playback::acquired&&mock_nodes[0].position.x==10,"camera first frame changed initial view");
        gimmd::player_session::frame.store(gimmd::pack_player_frame({1,1,gimmd::PlaybackState::Paused}));
        camera_playback::apply(&mock_nodes[0],124,false);
        check(camera_playback::writes==1,"different camera received the original camera pose");
        camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::writes==2&&std::abs(mock_nodes[0].position.x-10)>0.2f,"paused seek did not move camera");
        camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::retained>0,"paused camera was not retained");
        const auto unadjusted_camera=mock_nodes[0].position; const float unadjusted_fov=camera_mock_fov;
        gimmd::player_session::camera_control={7,2,1,-1,0,0,0,0,0.5f};
        camera_playback::apply(&mock_nodes[0],123,false);
        check(std::abs(mock_nodes[0].position.x-unadjusted_camera.x-2)<0.0001f&&
            std::abs(mock_nodes[0].position.y-unadjusted_camera.y-1)<0.0001f&&
            std::abs(mock_nodes[0].position.z-unadjusted_camera.z+1)<0.0001f,
            "VMD camera offset did not add to the sampled pose");
        check(std::abs(camera_mock_fov-unadjusted_fov*0.5f)<0.0001f&&gimmd::player_session::camera_acknowledged.load()==7,
            "VMD camera multiplier was not applied while paused");
        gimmd::player_session::camera_control={8,0,0,0,0,0,0,100,4}; camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_mock_fov==179,"multiplied FOV exceeded the engine range");
        gimmd::player_session::camera_control={9}; camera_playback::apply(&mock_nodes[0],123,false);
        check(gimmd::leg_ik::length(gimmd::leg_ik::sub(unadjusted_camera,mock_nodes[0].position))<0.0001f&&camera_mock_fov==unadjusted_fov,
            "reset did not return to the VMD camera pose/FOV");
        hierarchy::playback::stop_requested.store(true); camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::restored&&!camera_playback::dirty.load() && !bgm_pause::dirty.load() && !uid_text::dirty.load()&&mock_nodes[0].position.x==10,"camera stop did not restore baseline");
        camera_setup(); camera_playback::manual_only=true; camera_playback::zero={};
        gimmd::player_session::camera_control={5,2,0,0,90,0,0,60};
        camera_playback::apply(&mock_nodes[0],123,true);
        check(mock_nodes[0].position.x==12&&std::abs(mock_nodes[0].local.y-0.70710678f)<0.0001f&&camera_mock_fov==60&&
            gimmd::player_session::camera_acknowledged.load()==5,"manual camera controls were not applied/acknowledged");
        gimmd::player_session::camera_control={6}; camera_playback::apply(&mock_nodes[0],123,false);
        check(mock_nodes[0].position.x==10&&mock_nodes[0].local.w==1&&camera_mock_fov==45,"manual camera reset failed");
        hierarchy::playback::stop_requested.store(true); camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::restored&&!camera_playback::dirty.load() && !bgm_pause::dirty.load() && !uid_text::dirty.load(),"manual camera stop did not restore");
        {
            BetterEndfieldNext::Vmd::Vec3 position{};BetterEndfieldNext::Vmd::Quaternion rotation{};
            check(gimmd::camera_front::pose({2,0,3},{0,0,0,1},1.5f,&position,&rotation)&&position.z>3,"front camera placed behind character");
            const auto look=gimmd::leg_ik::vector(gimmd::leg_ik::rotation(rotation),{0,0,1});
            const auto aim=gimmd::leg_ik::unit(gimmd::leg_ik::sub({2,.75f,3},position));
            check(gimmd::leg_ik::dot(look,aim)>.9999f,"front camera did not look at character center");
            check(gimmd::camera_front::pose({2,0,3},{0,.70710678f,0,.70710678f},1.5f,&position,&rotation)&&position.x>2,"front camera ignored character yaw");
        }
        camera_setup();camera_playback::manual_only=true;camera_playback::zero={};
        camera_playback::front_pose=+[](Vec3* p,hierarchy::Quat* q)->bool {*p={20,21,22};*q={0,1,0,0};return true;};
        camera_playback::front_state.store(1);camera_playback::apply(&mock_nodes[0],123,true);
        check(camera_playback::front_state.load()==2&&mock_nodes[0].position.x==20&&camera_playback::baseline.x==10,"front camera replaced restoration baseline or failed to acknowledge");
        hierarchy::playback::stop_requested.store(true);camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::restored&&!camera_playback::front_active&&mock_nodes[0].position.x==10,"front camera did not restore original view");
        camera_playback::front_state.store(0);camera_playback::front_pose=nullptr;
        camera_setup();camera_playback::manual_only=true;camera_playback::zero={};
        camera_playback::apply(&mock_nodes[0],123,true);
        static Vec3 target_position{4,2,1};
        camera_playback::actor_position=+[](unsigned slot,Vec3* out)->bool {if(slot!=1)return false;*out=target_position;return true;};
        camera_playback::actor_target=1;camera_playback::actor_origin={1,2,1};camera_playback::actor_follow=true;
        camera_playback::apply(&mock_nodes[0],123,false);
        check(mock_nodes[0].position.x==13,"actor camera did not follow target displacement");
        target_position.x=7;camera_playback::apply(&mock_nodes[0],123,false);
        check(mock_nodes[0].position.x==16,"actor camera accumulated displacement instead of using baseline");
        camera_playback::actor_follow=false;target_position.x=9;camera_playback::apply(&mock_nodes[0],123,false);
        check(mock_nodes[0].position.x==16,"one-shot actor camera continued following");
        camera_playback::release_actor();hierarchy::playback::stop_requested.store(true);camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::restored&&mock_nodes[0].position.x==10,"actor camera did not restore original view");
        camera_setup(); gimmd::player_session::frame.store(gimmd::pack_player_frame({1,0,gimmd::PlaybackState::Paused}));
        gimmd::player_session::camera_control={10,3,2,1,0,0,0,60,1,true};
        camera_playback::apply(&mock_nodes[0],123,true);
        check(mock_nodes[0].position.x==13&&mock_nodes[0].position.y==22&&mock_nodes[0].position.z==31&&camera_mock_fov==60,
            "authored live preview was added to the VMD trajectory instead of overriding it");
        gimmd::player_session::camera_control={11}; camera_playback::apply(&mock_nodes[0],123,false);
        check(std::abs(mock_nodes[0].position.x-13)>0.1f,"return from preview did not resume the session trajectory");
        hierarchy::playback::stop_requested.store(true); camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::restored,"live preview failed to restore the original camera");
        camera_setup(); gimmd::player_session::dynamic_session=true; gimmd::player_session::camera_requested.store(true);
        gimmd::player_session::camera_frame.store(0); hierarchy::playback::done.store(true); hierarchy::playback::stop_requested.store(true);
        camera_playback::apply(&mock_nodes[0],123,true);
        check(camera_playback::acquired&&camera_playback::dirty.load(),"standalone camera required MMD playback");
        gimmd::player_session::camera_requested.store(false); camera_playback::apply(&mock_nodes[0],123,false);
        check(!camera_playback::dirty.load()&&!camera_playback::acquired,"camera disable did not restore and release its baseline");
        gimmd::player_session::camera_requested.store(true); camera_playback::apply(&mock_nodes[0],123,true);
        check(camera_playback::acquired&&camera_playback::dirty.load(),"camera could not re-enable in the same session");
        gimmd::player_session::exit_requested.store(true); camera_playback::apply(&mock_nodes[0],123,false);
        check(!camera_playback::dirty.load(),"dynamic exit did not restore standalone camera");
        gimmd::player_session::dynamic_session=false; gimmd::player_session::camera_requested.store(false); gimmd::player_session::exit_requested.store(false);
        camera_setup(); gimmd::player_session::holding_end.store(true);
        gimmd::player_session::frame.store(gimmd::pack_player_frame({1,0,gimmd::PlaybackState::Ended}));
        camera_playback::apply(&mock_nodes[0],123,true);
        check(camera_playback::writes==1&&camera_playback::sampled_seconds==1,"final camera frame was not applied while holding");
        hierarchy::playback::stop_requested.store(true); camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::restored&&!camera_playback::dirty.load(),"held final camera did not restore");
        gimmd::player_session::holding_end.store(false);
        camera_setup(1); camera_playback::apply(&mock_nodes[0],123,true); camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::aborted&&camera_playback::restored&&!camera_playback::dirty.load() && !bgm_pause::dirty.load() && !uid_text::dirty.load(),"partial camera mutation exception did not restore");
        check(camera_mock_fov==45,"camera FOV baseline not restored after partial mutation");
        camera_setup(2); camera_playback::apply(&mock_nodes[0],123,true); camera_playback::apply(&mock_nodes[0],123,false);
        check(camera_playback::dirty.load()&&!camera_playback::restored,"lost camera identity falsely reported restored");
        camera_playback::dirty.store(false); camera_playback::loaded=false; get_position=mock_position;
        hierarchy::playback::stop_requested.store(false); hierarchy::playback::done.store(false);
#endif
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
            constexpr unsigned branches = hierarchy::MaxNodes / 128 + 1;
            mock_nodes[i].count = branches;
            for (unsigned child = 0; child < branches; ++child) {
                const unsigned next = 129 + (i - 1) * branches + child;
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
        mock_play_fixture();
        static bool aux_restored=false;aux_restored=false;
        al::aux_begin=+[](void*,bool&)->bool {return false;};
        al::aux_release=+[]()->bool {aux_restored=true;return true;};
        p::tick(1000);
        check(aux_restored&&al::restored&&mock_lease_enabled&&!p::setter_calls.load(),"auxiliary pause failure did not restore Animator before pose writes");
        mock_play_fixture();
        al::aux_begin=+[](void*,bool&)->bool {return true;};
        al::aux_release=+[]()->bool {return false;};
        p::tick(1000);p::stop_requested.store(true);p::tick(1100);
        check(al::dirty.load()&&!mock_lease_enabled&&!p::done.load(),"auxiliary restore failure released the Animator prematurely");
        al::aux_release=+[]()->bool {return true;};p::tick(1200);
        check(al::restored&&mock_lease_enabled&&p::done.load(),"auxiliary restore retry failed");
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
        mp::tick(1000);
        mock_root_lookups=0;
        mp::tick(2000); mp::tick(2040);
        check(mock_root_lookups==2,"steady pose resolved the scene root more than once per update");
        check(mp::result.samples == 3 && mp::setter_calls.load() == 30 && mp::result.persisted == 20,
            "multi-track batching or retention failed");
        mp::tick(3000);
        check(mp::done.load() && mp::result.restored && mp::setter_calls.load() == 40 && hierarchy::animator_lease::restored,
            "multi-track stop did not restore all targets and Animator");
        for (unsigned i = 0; i < mp::multi::TrackCount; ++i) {
            const auto& track = mp::multi::tracks[i];
            check(track.restored && !track.dirty && track.attempts == 1, "multi-track target restoration incomplete");
        }
        mock_multi_fixture();mp::tick(1000);
        hierarchy::animator_lease::aux_begin=+[](void*,bool& changed){changed=true;return true;};
        mock_root_lookups=0;mp::tick(1040);
        check(mock_root_lookups==2&&mp::result.samples==2,"pose reused the model path after a driver change");
        hierarchy::animator_lease::aux_begin=nullptr;
        mp::stop_requested.store(true);mp::tick(1080);
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
        mock_multi_fixture(); gimmd::player_session::active=true; gimmd::player_session::hold_at_end=true;
        gimmd::player_session::command.store(0); mp::tick(1000); mp::tick(3040);
        const auto held_pose=mock_nodes[5].local;
        check(!mp::done.load()&&gimmd::player_session::holding_end.load()&&
            gimmd::unpack_player_frame(gimmd::player_session::frame.load()).state==gimmd::PlaybackState::Ended,
            "end hold restored instead of keeping the final pose");
        mp::tick(700000);
        check(!mp::done.load()&&hierarchy::roundtrip::error(held_pose,mock_nodes[5].local)<0.00001f,
            "end hold expired or changed the final pose");
        gimmd::player_session::command.store(gimmd::parse_player_command("1 stop")); mp::tick(700040);
        check(mp::done.load()&&mp::result.restored&&!gimmd::player_session::holding_end.load(),"manual restore from final pose failed");
        gimmd::player_session::hold_at_end=false;
        mock_multi_fixture(); gimmd::player_session::command.store(0); mp::tick(1000); mp::tick(3040);
        check(mp::done.load()&&mp::result.restored,"default natural completion stopped restoring");
        gimmd::player_session::active=false;
        mock_multi_fixture(); gimmd::player_session::active=true; gimmd::player_session::dynamic_session=true;
        gimmd::player_session::mmd_requested.store(false); gimmd::player_session::command.store(0);
        const auto idle_writes=mp::setter_calls.load(); mp::tick(1000);
        check(mp::setter_calls.load()==idle_writes,"dynamic standby wrote character pose");
        gimmd::player_session::mmd_requested.store(true); gimmd::player_session::hold_at_end=true;
        mp::tick(1000); mp::tick(3040);
        check(gimmd::unpack_player_frame(gimmd::player_session::frame.load()).state==gimmd::PlaybackState::Paused&&!mp::done.load(),
            "dynamic completion did not pause at the final frame");
        const auto retained_camera_frame=gimmd::player_session::camera_frame.load();
        mp::stop_requested.store(true); mp::tick(3080);
        check(mp::done.load()&&mp::result.restored&&gimmd::player_session::camera_frame.load()==retained_camera_frame,
            "restoring character reset the independent camera timeline");
        gimmd::player_session::dynamic_session=false; gimmd::player_session::mmd_requested.store(false);
        gimmd::player_session::hold_at_end=false; gimmd::player_session::active=false;
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
        mock_multi_fixture(true);
        hierarchy::animator_observer::bind_pose::avatar_rest_status="avatar-local-reference-observed";
        hierarchy::animator_observer::bind_pose::avatar_rest_count=139;
        wcscpy_s(hierarchy::nodes[1].name,L"Avatar_TestOther");mock_nodes[1].name=L"Avatar_TestOther";
        for(unsigned n=0;n<hierarchy::count;++n)for(unsigned i=0;i<52;++i)if(!std::wcscmp(hierarchy::nodes[n].name,gimmd::motion_profile::Mappings[i].target))hierarchy::animator_observer::bind_pose::avatar_links[n]=gimmd::fk_profile::AvatarIndices[i];
        const auto other_index=gimmd::fk_profile::AvatarIndices[7];
        auto* other_rest=hierarchy::animator_observer::bind_pose::avatar_local[other_index]+4;
        other_rest[0]=0;other_rest[1]=0;other_rest[2]=0;other_rest[3]=1;
        // capture clears borrowed pointers and identities at callback exit.
        // Resolve the synthetic fixture anew before invoking bind.
        for(unsigned n=0;n<hierarchy::count;++n)for(auto& mock:mock_nodes)if(!std::wcscmp(hierarchy::nodes[n].name,mock.name)) {
            hierarchy::nodes[n].object=&mock;hierarchy::nodes[n].identity=reinterpret_cast<uintptr_t>(&mock);break;}
        const bool adaptive_bound=mp::multi::bind(1);
        check(adaptive_bound&&mp::multi::adaptive_reference&&mp::multi::bindings[7].rest.w==1,
            "other character did not use its own Avatar rest rotation");
        for(unsigned n=0;n<hierarchy::count;++n)if(!std::wcscmp(hierarchy::nodes[n].name,gimmd::motion_profile::Mappings[7].target))hierarchy::animator_observer::bind_pose::avatar_links[n]=-1;
        check(!mp::multi::bind(1)&&!mp::setter_calls.load(),"other character admitted a bone without Avatar correspondence");
        mp::stop_requested.store(true);mp::tick(1000);
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
        namespace rp=hierarchy::playback::multi::reference_pose;
        rp::enabled=true;
        namespace reference_bp=hierarchy::animator_observer::bind_pose;
        reference_bp::avatar_rest_status="avatar-local-reference-observed";reference_bp::avatar_rest_count=139;
        for(auto& link:reference_bp::avatar_links)link=-1;
        for(unsigned n=0;n<hierarchy::count;++n)for(auto& mock:mock_nodes)if(!std::wcscmp(hierarchy::nodes[n].name,mock.name)) {
            hierarchy::nodes[n].object=&mock;hierarchy::nodes[n].identity=reinterpret_cast<uintptr_t>(&mock);
            if(&mock==&mock_nodes[10])reference_bp::avatar_links[n]=gimmd::fk_profile::AvatarIndices[7];break;}
        auto* reference_trs=reference_bp::avatar_local[gimmd::fk_profile::AvatarIndices[7]];
        reference_trs[0]=.1f;reference_trs[1]=.2f;reference_trs[2]=.3f;
        reference_trs[4]=reference_trs[5]=reference_trs[6]=0;reference_trs[7]=1;
        check(rp::bind(1)&&rp::size==1,"reference pose could not build a verified skeleton path");
        mock_nodes[10].position={2,3,4};mock_nodes[10].local={0,0,.70710678f,.70710678f};
        check(rp::initialize()&&mock_nodes[10].local.w==1&&rm::error(mock_nodes[10].position,{.1f,.2f,.3f})<.00001f,"reference initialization retained idle pose");
        hierarchy::playback::ResolutionBatch reference_batch{};
        check(rp::restore(&reference_batch)&&rm::error(mock_nodes[10].position,{2,3,4})<.00001f&&std::abs(mock_nodes[10].local.z-.70710678f)<.00001f,"reference pose overwrote rollback snapshot");
        rp::initialized=false;mock_nodes[10].position={4,5,6};mock_nodes[10].local={0,.70710678f,0,.70710678f};
        check(rp::initialize()&&mock_nodes[10].local.w==1&&rm::error(mock_nodes[10].position,{.1f,.2f,.3f})<.00001f,"different idle poses produced different initial skeletons");
        reference_batch={};rp::restore(&reference_batch);rp::enabled=false;rp::reset();
        mp::stop_requested.store(true);mp::tick(1000);
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
        mock_multi_fixture(true,true);
        namespace ph=hierarchy::playback::multi::pelvis_helper;
        mock_nodes[55].children[52]=&mock_nodes[57]; mock_nodes[55].count=53;
        mock_nodes[57].name=L"+PelvisTwist CF A01"; mock_nodes[57].parent=&mock_nodes[55];
        const auto& helper_rest=gimmd::fk_profile::AvatarReference[123].rotation;
        mock_nodes[57].local={helper_rest[0],helper_rest[1],helper_rest[2],helper_rest[3]};
        ph::track={}; ph::bound=true; ph::track.length=mp::multi::tracks[0].length;
        for (unsigned i=0;i<ph::track.length;++i) ph::track.steps[i]=mp::multi::tracks[0].steps[i];
        auto& helper_leaf=ph::track.steps[ph::track.length-1]; helper_leaf.identity=reinterpret_cast<uintptr_t>(&mock_nodes[57]);
        helper_leaf.child=52; wcscpy_s(helper_leaf.name,L"+PelvisTwist CF A01");
        const auto helper_turn=gimmd::rotation_preview::multiply({0,0,0.70710678f,0.70710678f},gimmd::fk_profile::Bindings[0].rest);
        hierarchy::playback::ResolutionBatch helper_batch{};
        check(ph::preflight(&helper_batch)&&ph::apply(&helper_batch,{helper_turn.x,helper_turn.y,helper_turn.z,helper_turn.w}),
            "pelvis helper did not accept/apply sibling rotation");
        const auto helper_expected=gimmd::rotation_preview::multiply({0,0,0.70710678f,0.70710678f},
            {helper_rest[0],helper_rest[1],helper_rest[2],helper_rest[3]});
        check(hierarchy::roundtrip::error(mock_nodes[57].local,{helper_expected.x,helper_expected.y,helper_expected.z,helper_expected.w})<0.00002f,
            "pelvis helper delta was applied in the wrong basis");
        check(ph::restore(&helper_batch)&&ph::track.restored&&!ph::track.dirty&&
            hierarchy::roundtrip::error(mock_nodes[57].local,{helper_rest[0],helper_rest[1],helper_rest[2],helper_rest[3]})<0.00002f,
            "pelvis helper baseline did not restore");
        namespace cs=hierarchy::playback::multi::cloth_stabilizer;
        cs::reset(); cs::size=1; cs::bones[0].track=ph::track; cs::bones[0].track.captured=false;
        cs::bones[0].track.dirty=false; cs::bones[0].reference=123;
        mock_nodes[57].position={.1f,.2f,.3f};
        hierarchy::playback::ResolutionBatch cloth_batch{};
        check(cs::preflight(&cloth_batch)&&cs::apply(&cloth_batch,1),"static cloth pose did not apply");
        mock_nodes[57].local=hierarchy::roundtrip::multiply(mock_nodes[57].local,{0,0.005f,0,0.9999875f});
        check(cs::preflight(&cloth_batch)&&cs::overwrites==1,"native cloth overwrite was not counted");
        check(cs::restore(&cloth_batch)&&!cs::bones[0].position_dirty&&!cs::bones[0].track.dirty&&
            rm::error(mock_nodes[57].position,{.1f,.2f,.3f})<0.00002f,"static cloth position/rotation baseline not restored");
        cs::reset();
        // Exercise the actual cloth path builder, sharing an already populated
        // skeleton cache. Root is a sentinel, not child zero of a parent.
        mock_nodes[57].name=L"+AmiceB L B11";
        MockNode* cloth_tree[hierarchy::MaxNodes]{}; cloth_tree[0]=&mock_nodes[0];
        hierarchy::count=1; hierarchy::nodes[0]={}; hierarchy::nodes[0].parent=-1;
        int cloth_model=-1;
        for (unsigned i=0;i<hierarchy::count;++i) {
            auto* object=cloth_tree[i]; auto& node=hierarchy::nodes[i];
            node.object=object; node.identity=reinterpret_cast<uintptr_t>(object); wcscpy_s(node.name,object->name);
            node.processed=node.aliases_equal=node.parent_valid=node.owner_valid=node.rotation_relation_valid=true;
            node.active=object->active;
            if (object==&mock_nodes[1]) cloth_model=int(i);
            if (object==&mock_nodes[57]) hierarchy::animator_observer::bind_pose::avatar_links[i]=123;
            for (int child=0;child<object->count;++child) {
                const unsigned index=hierarchy::count++; cloth_tree[index]=object->children[child];
                hierarchy::nodes[index]={}; hierarchy::nodes[index].parent=int(i);
            }
        }
        gimmd::player_session::stabilize_cloth=true;
        check(cs::bind(cloth_model)&&cs::size==1&&cs::bones[0].track.steps[0].child==-1,"cloth root sentinel generated incorrectly");
        hierarchy::playback::ResolutionBatch shared_cloth_batch{}; bool shared_active=true;
        check(hierarchy::playback::resolve_batch_steps(&shared_cloth_batch,&shared_active,mp::multi::tracks[0].steps,mp::multi::tracks[0].length)&&
            cs::preflight(&shared_cloth_batch),"cloth path conflicted with populated skeleton cache");
        cs::reset(); gimmd::player_session::stabilize_cloth=false; gimmd::player_session::stabilize_braid=true;
        for (unsigned i=0;i<hierarchy::count;++i) if (hierarchy::nodes[i].object==&mock_nodes[57]) wcscpy_s(hierarchy::nodes[i].name,L"+HairB CB A01");
        mock_nodes[57].name=L"+HairB CB A01";
        check(cs::bind(cloth_model)&&cs::size==1,"braid-only binding did not select the back braid");
        hierarchy::playback::ResolutionBatch braid_batch{};
        check(cs::preflight(&braid_batch)&&cs::apply(&braid_batch,1)&&cs::restore(&braid_batch),"braid static pose did not restore");
        cs::reset(); gimmd::player_session::stabilize_braid=false;
        rm::reset(); BetterEndfieldNext::Vmd::Motion rotated_control;
        auto& control = rotated_control.bones[gimmd::fk_profile::RootTrackNames[0]]; control.resize(1); control[0].rotation = {0.1f,0,0,0.9949874f};
        check(!rm::prepare(rotated_control), "unsupported rotated master control silently accepted");
#ifdef GIMMD_LEG_IK
        namespace lk=hierarchy::playback::multi::leg_playback;
        mock_multi_fixture(true,true,true);
        rm::controls[1][1].rotation={0,0.17177446f,0,0.98513633f};
        const auto yaw_expected=gimmd::fk_retarget::evaluate(gimmd::fk_profile::Bindings[0],
            gimmd::root_controls::pelvis(rm::controls[1][1].rotation,mp::multi::tracks[0].keys[1].rotation));
        mp::tick(1000); mp::tick(2000);
        check(mp::result.samples==2&&rm::yaw_samples==1&&rm::maximum_yaw_degrees>19&&
            hierarchy::roundtrip::error(mock_nodes[3].local,{yaw_expected.x,yaw_expected.y,yaw_expected.z,yaw_expected.w})<0.00002f,
            "Center yaw did not reach target pelvis before leg IK");
        mp::stop_requested.store(true); mp::tick(2034);
        check(mp::done.load()&&rm::restored&&mp::result.restored&&hierarchy::animator_lease::restored,"Center yaw playback did not restore");
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
        mock_multi_fixture(true,true,true); mock_nodes[3+11].position.x+=0.04f; mp::tick(1000);
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
        mock_multi_fixture(true,true,true,true); mock_nodes[3].position.x+=0.005f; mock_nodes[3+11].position.x+=0.005f; mp::tick(1000);
        check(mp::result.samples==1&&lk::geometry_captured&&lk::adjusted_joints==2,"small live geometry deviation not measured");
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
        class_a[2] = reinterpret_cast<uintptr_t>(animator_name);
        class_b[gimmd::hierarchy_profile::ClassMethodTable / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(table);
        auto& method_count = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(class_b) + gimmd::hierarchy_profile::ClassMethodCount);
        method_count = 1; info[0] = reinterpret_cast<uintptr_t>(class_b);
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
        reset_accessors(); method_count = 1025; md::capture(reinterpret_cast<uintptr_t>(class_a));
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
        if (argc>1) {
            std::ifstream source_input(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);
            const auto source_size=source_input ? source_input.tellg():std::streampos(-1);
            if (source_size<=0||source_size>static_cast<std::streamoff>(BetterEndfieldNext::Vmd::Limits{}.file_bytes))
                throw std::runtime_error("compatibility source size refused");
            std::vector<uint8_t> source_bytes(static_cast<size_t>(source_size)); source_input.seekg(0);
            source_input.read(reinterpret_cast<char*>(source_bytes.data()),source_size);
            BetterEndfieldNext::Vmd::Motion source_motion; std::string source_error;
            if (!source_input||!BetterEndfieldNext::Vmd::Parse(source_bytes,source_motion,source_error)) throw std::runtime_error(source_error);
            namespace compatibility=hierarchy::playback;
            compatibility::full_mode=true; compatibility::root_mode=true; compatibility::leg_mode=true; compatibility::toe_mode=true;
            if (!compatibility::multi::prepare(source_motion)) throw std::runtime_error(compatibility::multi::state);
            std::cout<<"Source admitted by runtime preparation: "<<source_motion.bones.size()<<" tracks, "<<source_motion.last_frame<<" frames\n";
        }
#ifdef GIMMD_SINGLE_TRACK_PLAYBACK
        hierarchy::playback::started.store(false); hierarchy::playback::done.store(false);
        hierarchy::playback::dirty.store(false); hierarchy::playback::stop_requested.store(true);
        hierarchy::time_control::dirty.store(false); hierarchy::animator_lease::dirty.store(false);
        const auto cancel_setters=hierarchy::playback::setter_calls.load();
        hierarchy::playback::tick(GetTickCount64());
        check(hierarchy::playback::done.load()&&!std::strcmp(hierarchy::playback::result.status,"cancelled-before-binding"),
            "stop before entering a character scene was ignored");
        check(hierarchy::playback::setter_calls.load()==cancel_setters,"cancel before binding wrote an actor pose");
#endif
        // Independent dynamic controls must not stop the whole session.
        namespace dps=gimmd::player_session;
        dps::dynamic_session=true; dps::exit_requested.store(false); dps::mmd_requested.store(true); dps::camera_requested.store(true);
        hierarchy::playback::stop_requested.store(false);
        dynamic_dispatch(0,{},gimmd::parse_player_command("1 camera-off"));
        check(!dps::camera_requested.load()&&!hierarchy::playback::stop_requested.load(),"camera off stopped MMD");
        dps::camera_requested.store(true); dynamic_dispatch(0,{},gimmd::parse_player_command("2 restore"));
        check(hierarchy::playback::stop_requested.load()&&dps::camera_requested.load()&&!dps::exit_requested.load(),"restore animation exited other dynamic features");
        dynamic_dispatch(0,{},gimmd::parse_player_command("3 exit"));
        check(dps::exit_requested.load(),"explicit exit was ignored");
        dps::dynamic_session=false; dps::mmd_requested.store(false); dps::exit_requested.store(false); dps::camera_requested.store(false);
        // Optional shooting leases: category-specific pause and exact restoration.
        const auto music_setup=[](int initial,int mode) {
            bgm_pause::configured=true; bgm_pause::attempted=bgm_pause::acquired=bgm_pause::restored=false;
            bgm_pause::dirty.store(false); bgm_pause::calls=bgm_pause::restore_attempts=0;
            bgm_pause::ready=mock_music_ready; bgm_pause::count=mock_music_count; bgm_pause::info=mock_music_info;
            bgm_pause::paused=mock_music_paused; bgm_pause::pause=mock_music_set;
            mock_music_pause=initial; mock_music_mode=mode; strcpy_s(mock_music_name,mode==1 ? "SFX":"BGM");
            gimmd::player_session::pause_bgm=true; hierarchy::playback::started.store(true);
            hierarchy::playback::done.store(false); hierarchy::playback::stop_requested.store(false);
        };
        music_setup(0,0); bgm_pause::tick();
        check(mock_music_pause==1&&bgm_pause::dirty.load()&&bgm_pause::acquired,"BGM category pause not acquired");
        hierarchy::playback::stop_requested.store(true); bgm_pause::tick();
        check(mock_music_pause==0&&!bgm_pause::dirty.load()&&bgm_pause::restored,"BGM previous state not restored");
        music_setup(1,0); bgm_pause::tick(); hierarchy::playback::stop_requested.store(true); bgm_pause::tick();
        check(mock_music_pause==1&&!bgm_pause::dirty.load(),"already paused music was resumed");
        music_setup(0,1); bgm_pause::tick();
        check(!bgm_pause::acquired&&mock_music_pause==0,"SFX category paused as music");
        music_setup(0,2); bgm_pause::tick();
        check(!bgm_pause::acquired&&mock_music_pause==0,"ambiguous BGM categories accepted");
        bgm_pause::configured=false; gimmd::player_session::pause_bgm=false;
        auto saved_find_string=find_string; find_string=mock_uid_string; uid_text::get=mock_uid_get; uid_text::set=mock_uid_set;
        check(uid_text::write(nullptr,"MMD test"),"UID text write/readback failed");
        char uid_observed[257]{}; check(uid_text::text(nullptr,uid_observed)&&!std::strcmp(uid_observed,"MMD test"),"UID text mismatch");
        check(uid_text::write(nullptr,"")&&mock_uid.length==0,"empty UID replacement failed");
        check(uid_text::write(nullptr,"UID: baseline"),"UID baseline restore failed");
        find_string=saved_find_string;
#ifdef GIMMD_LOCAL_ACTORS
        // An inactive self-created clone must still be found and removed. Source nodes survive.
        auto saved_actor_find = find_object;
        mock_hierarchy_reset(); find_object = mock_actor_find;
        local_actors::state = {}; local_actors::configured = true;
        local_actors::activate = mock_actor_active; local_actors::destroy = mock_actor_destroy;
        mock_actor_destroyed = mock_actor_source_writes = 0;
        mock_nodes[0].count = 3; mock_nodes[0].children[2] = &mock_nodes[3];
        mock_nodes[1].name = L"GIMMDActorTestA"; mock_nodes[1].active = false;
        mock_nodes[2].name = L"GIMMDActorTestB"; mock_nodes[3].name = L"Avatar_Source";
        for (unsigned i = 0; i < 2; ++i) {
            auto& slot = local_actors::state.slots[i]; slot.identity = reinterpret_cast<uintptr_t>(&mock_nodes[i+1]);
            strcpy_s(slot.path, i ? "/EntityRoot/AvatarRoot/GIMMDActorTestB" : "/EntityRoot/AvatarRoot/GIMMDActorTestA");
        }
        local_actors::publish("test"); local_actors::stop.store(true); local_actors::tick();
        check(mock_actor_destroyed == 2 && !local_actors::dirty.load(), "actor exit did not remove active and inactive copies");
        check(mock_nodes[0].count == 1 && mock_nodes[0].children[0] == &mock_nodes[3] &&
            !mock_actor_source_writes && mock_nodes[3].native == &mock_nodes[3], "actor cleanup modified the game source");
        local_actors::state.slots[0].identity = reinterpret_cast<uintptr_t>(&mock_nodes[3]);
        strcpy_s(local_actors::state.slots[0].path, "/EntityRoot/AvatarRoot/GIMMDActorTestA");
        local_actors::publish("test"); local_actors::tick();
        check(mock_actor_destroyed == 2 && !mock_actor_source_writes && !local_actors::dirty.load(), "reused identity destroyed an unrelated model");
        local_actors::configured = false; local_actors::stop.store(false); find_object = saved_actor_find;
        local_actors::bones_get=mock_actor_bones_get; local_actors::root_bone=mock_actor_root_get;
        local_actors::tree_count=1; local_actors::tree[0].identity=reinterpret_cast<uintptr_t>(&mock_nodes[3]);
        mock_actor_bones={}; mock_actor_root_bone=nullptr;
        check(local_actors::verify_skin(nullptr),"empty auxiliary skin bone array rejected");
        mock_actor_root_bone=&mock_nodes[3]; mock_actor_bones.length=1; mock_actor_bones.bones[0]=&mock_nodes[3];
        check(local_actors::verify_skin(nullptr),"clone-owned weighted skin rejected");
        mock_actor_root_bone=&mock_nodes[4];
        check(!local_actors::verify_skin(nullptr)&&!std::strcmp(local_actors::state.detail,"skin-root-bone-outside-clone"),"external root bone admitted");
        mock_actor_root_bone=nullptr; mock_actor_bones.bones[0]=&mock_nodes[4];
        check(!local_actors::verify_skin(nullptr)&&!std::strcmp(local_actors::state.detail,"skin-weighted-bone-outside-clone"),"external weighted bone admitted");
        local_actors::tree_count=0; local_actors::tree[0]={};
        struct ComponentEntry { uintptr_t type=0, native=0; };
        struct ComponentOwner { uint8_t padding[0x40]{}; ComponentEntry* vector=nullptr; unsigned count=0; };
        static_assert(offsetof(ComponentOwner,vector)==0x40 && offsetof(ComponentOwner,count)==0x48);
        ComponentEntry entries[2]{{1,0x1000},{2,0x2000}};
        ComponentOwner owner{}; owner.vector=entries; owner.count=2;
        MockNode wrapper{}; wrapper.native=reinterpret_cast<MockNode*>(&owner);
        check(local_actors::component_membership(&wrapper,0x1000)==1,"registered component not detected");
        entries[0]=entries[1]; owner.count=1; // Renderer removed indirectly; its old wrapper may still cache native=0x1000.
        check(local_actors::component_membership(&wrapper,0x1000)==0 &&
            local_actors::component_membership(&wrapper,0x2000)==1,"stale component pointer confused removal with registration");
        char canonical_source[96]{};
        check(local_actors::source_name(L"Avatar_Girl_Catalyst_Vodyanitsa_Manekin(Clone)", canonical_source) &&
            !std::strcmp(canonical_source, "Avatar_Girl_Catalyst_Vodyanitsa_Manekin"), "preview source suffix rejected");
        check(!local_actors::source_name(L"Avatar_Unsafe(Clone)extra", canonical_source) &&
            !local_actors::source_name(L"Avatar_Unsafe/name", canonical_source), "unsafe preview source label admitted");
        mock_hierarchy_reset();
        mock_nodes[0].count=1; mock_nodes[0].children[0]=&mock_nodes[1];
        mock_nodes[1].count=2; mock_nodes[1].children[0]=&mock_nodes[2]; mock_nodes[1].children[1]=&mock_nodes[3];
        mock_nodes[2].parent=mock_nodes[3].parent=&mock_nodes[1];
        mock_nodes[2].name=L"Body"; mock_nodes[3].name=L"Bip001";
        local_actors::tree[1].parent=0;
        local_actors::tree[2].parent=local_actors::tree[3].parent=1;
        local_actors::driver_lease::Item hint{};
        local_actors::driver_lease::remember_owner(hint,3);
        check(hint.depth==2&&hint.child_from_leaf[0]==1&&hint.child_from_leaf[1]==0,"driver owner hint lost a sibling index");
        local_actors::driver_lease::remember_owner(hint,0);
        check(hint.depth==0,"driver owner hint retained its old path");
        check(local_actors::model_root(&mock_nodes[0]), "nested preview skeleton rejected");
        mock_nodes[3].parent=&mock_nodes[0];
        check(!local_actors::model_root(&mock_nodes[0]), "inconsistent preview hierarchy admitted");
        owner.count=65;
        check(local_actors::component_membership(&wrapper,0x1000)==-1,"invalid component membership vector admitted");
#endif
        // Hook observers must preserve the original results and output parameters even when capture inputs are invalid.
        const DWORD saved_trace_thread = window_thread; window_thread = 0;
        resource_trace::original = mock_resource_format; resource_trace::hash_original = mock_resource_hash;
        resource_trace::recording.store(true); resource_trace::calls.store(0); resource_trace::hash_calls.store(0);
        MockString trace_root{}, trace_suffix{};
        trace_root.length = 4; wcscpy_s(trace_root.text, L"root");
        trace_suffix.length = 4; wcscpy_s(trace_suffix.text, L".asb");
        void* root_ref = &trace_root; void* suffix_ref = &trace_suffix; wchar_t trace_path[64]{};
        check(resource_trace::observe(trace_path, 64, &root_ref, 42, 2, &suffix_ref) &&
            !std::wcscmp(trace_path, L"root/02/00000042.asb") && resource_trace::records[0].ready.load() &&
            !std::wcscmp(resource_trace::records[0].path, trace_path), "path trace altered formatter output");
        check(resource_trace::observe(trace_path, 64, nullptr, 42, 2, nullptr) &&
            !resource_trace::records[1].strings_valid, "invalid capture arguments changed original result");
        check(!resource_trace::observe(trace_path, 1, &root_ref, 42, 2, &suffix_ref), "formatter failure changed by trace");
        int trace_output = 0;
        check(resource_trace::observe_hash(&trace_root, 0xdeadbeef, &trace_root, &trace_output) == &trace_root &&
            trace_output == 37 && resource_trace::hash_records[0].returned_object, "hash trace changed returned object or out parameter");
        resource_trace::calls.store(256); resource_trace::hash_calls.store(256);
        check(resource_trace::observe(trace_path, 64, &root_ref, 42, 2, &suffix_ref) &&
            resource_trace::observe_hash(&trace_root, 0xdeadbeef, &trace_root, &trace_output) == &trace_root &&
            resource_trace::active.load() == 0, "full trace buffer affected engine call");
        resource_trace::file_original=mock_pipeline_file;resource_trace::memory_original=mock_pipeline_memory;
        resource_trace::cache_original=mock_pipeline_cache;resource_trace::template_original=mock_pipeline_template;
        resource_trace::instance_original=mock_pipeline_instance;resource_trace::raw_original=mock_pipeline_raw;
        resource_trace::pipeline_calls.store(0);
        check(resource_trace::observe_file("blocks/test.blk",0x100000001ull,17,29)==&pipeline_mock_token,"file trace changed path or 64-bit offset");
        check(resource_trace::observe_memory(&pipeline_mock_token,0x100000001ull,0x200000002ull,0x300000003ull,0x12345678,0xabcdef01)==&pipeline_mock_token,"memory trace changed register or stack arguments");
        check(resource_trace::observe_cache(&pipeline_mock_token,0xdeadbeef,&pipeline_mock_token)==&pipeline_mock_token,"cache trace changed type/hash");
        resource_trace::observe_template(&pipeline_mock_token);
        check(pipeline_mock_void_ok,"template trace changed argument");
        check(resource_trace::observe_instance(&pipeline_mock_token)==&pipeline_mock_token,"instance trace changed return value");
        uint64_t trace_vector[4]{};resource_trace::observe_raw(&pipeline_mock_token,trace_vector);
        check(pipeline_mock_void_ok && trace_vector[2]==7,"raw clone trace changed output vector");
        resource_trace::mhy_file_original=mock_pipeline_mhy;resource_trace::async_template_original=mock_pipeline_async;
        resource_trace::finalize_original=mock_pipeline_raw;resource_trace::provider_original=mock_pipeline_provider;
        check(resource_trace::observe_mhy_file(&pipeline_mock_token,"blocks/test.blk",0x100000001ull,43)==&pipeline_mock_token,"MHY file trace changed offset or argument order");
        check(resource_trace::observe_async_template(&pipeline_mock_token)==0xfedcba98u,"async template trace changed handle bits");
        resource_trace::observe_finalize(&pipeline_mock_token,trace_vector);
        check(pipeline_mock_void_ok && trace_vector[2]==7,"finalize trace changed output vector");
        check(resource_trace::observe_provider(&pipeline_mock_token,0xdeadbeef,&pipeline_mock_token)==0xfedcba98u,"provider trace changed hash, method metadata or handle");
        resource_trace::ready_original=mock_pipeline_ready;
        check(resource_trace::observe_ready(&pipeline_mock_token) && !resource_trace::observe_ready(nullptr),"ready trace changed boolean result");
        resource_trace::pipeline_calls.store(4096);
        check(resource_trace::observe_memory(&pipeline_mock_token,0x100000001ull,0x200000002ull,0x300000003ull,0x12345678,0xabcdef01)==&pipeline_mock_token && resource_trace::active.load()==0,"full pipeline buffer affected call");
        resource_trace::recording.store(false); window_thread = saved_trace_thread;
        {
            namespace fm=local_actors::face_motion;
            static fm::Context face_test;
            face_test.channel_count=2;wcscpy_s(face_test.channels[0].name,L"Eye_WinkA_L");wcscpy_s(face_test.channels[1].name,L"Eye_Default");
            face_test.sources[0]={{0,0},{30,1}};float values[256]{};fm::values_for(face_test,.5,values);
            check(std::abs(values[0]-50)<.001f&&std::abs(values[1]-50)<.001f,"MMD morph interpolation or neutral complement failed");
            for(auto& value:values)value=0;fm::values_for(face_test,0,values);
            check(values[0]==0&&values[1]==100,"facial seek did not return neutral expression");
            check(gimmd::face_profile::bounded(-1)==0&&gimmd::face_profile::bounded(2)==1,"morph values escaped engine weight limits");
        }
#ifdef GIMMD_MULTI_TRACK_PLAYBACK
        {
            namespace physics=local_actors::secondary_physics;
            mock_multi_fixture(true,true);physics::reset_context();
            auto& context=physics::s;
            check(physics::tick(nullptr,0,0,true,nullptr,0),"disabled physics touched the model");
            context.dirty=true;
            check(!physics::prepare()&&!physics::release_ready(),"pending physics restoration was discarded");
            context.bound=true;context.model=local_actors::identity(&mock_nodes[1]);context.path_count=2;
            auto* secondary=mock_nodes[1].children[0];
            context.paths[0].identity=context.model;wcscpy_s(context.paths[0].name,mock_nodes[1].name);
            context.paths[1].identity=local_actors::identity(secondary);context.paths[1].parent_node=0;context.paths[1].child=0;
            wcscpy_s(context.paths[1].name,secondary->name);
            context.bone_count=1;auto& bone=context.bones[0];bone.path=1;bone.rotation={0,0,0,1};bone.local={.1f,.2f,.3f};bone.dirty=true;
            secondary->position={1,2,3};
            check(!physics::restore(&mock_nodes[2])&&context.dirty&&secondary->position.x==1,"physics restored a different character");
            check(physics::restore(&mock_nodes[1])&&!context.dirty&&context.restores==1&&
                physics::position::error(secondary->position,bone.local)<.00002f,"physics local pose restoration failed");
            check(physics::restore(nullptr)&&context.restores==1&&physics::release_ready(),"physics restoration was not idempotent");
            physics::reset_context();
            // Open skirt with a single real bone and a virtual tip, beside
            // an unrelated hair strand. The hem has no other movable point.
            context.particle_count=4;
            context.particles[0].real=context.particles[0].skirt=true;
            context.particles[1].parent_global=0;
            context.particles[2].real=true;
            context.particles[3].parent_global=2;
            check(physics::skirt_tip_contact(1)&&!physics::skirt_tip_contact(3),"open skirt tip or hair incorrectly classified");
            context.particles[3].parent_global=0;
            check(!physics::skirt_tip_contact(1),"branched open skirt classified as a single-bone flap");
            context.upper_ring=true;
            check(physics::skirt_tip_contact(1),"closed skirt lost hem collision");
            // Nahida, 14.731 s. E22's center line misses the raised left thigh;
            // the inner portion of the same flap overlaps it in the hip plane.
            namespace bc=gimmd::body_collision;
            context.hip_up={0,1,0};context.hip_right={1,0,0};
            bc::Capsule thighs[physics::BodyCapsules]{};
            thighs[6]={{-.05824f,.00120f,0},{-.00046f,.07111f,.30284f},.06323f};
            thighs[8]={{.05824f,-.00123f,0},{.03095f,-.25172f,.19091f},.06323f};
            const bc::Vec anchor{-.12390f,.03880f,.15916f},tip{-.20273f,.03857f,.26101f};
            const auto inner=[](bc::Vec v){v.x*=.2f;return v;};
            const auto base=bc::sub(tip,anchor);
            const float depth=bc::inner_flap_lift(tip,inner(tip),context.hip_up,thighs[6]);
            check(bc::distance(thighs[6],tip)>.18f&&depth>.07f,"recorded inner hem contact missed");
            const auto lifted=physics::lift_front_flap(anchor,base,bc::length(base),thighs);
            const auto hem=bc::add(anchor,lifted);
            check(bc::inner_flap_lift(hem,inner(hem),context.hip_up,thighs[6])<.012f&&
                  std::abs(bc::length(lifted)-bc::length(base))<.00001f,"inner hem clearance or length regressed");
            check(bc::inner_flap_lift(tip,inner(tip),context.hip_up,thighs[8])==0,"lowered leg lifted the opposite flap");
            // After the hem fix, Nahida's left E22 still overlapped the raised
            // thigh at 14.734 s. Move the attachment by the residual, measured
            // from the restored local position every step.
            thighs[6]={{-.05824f,.00122f,0},{.00003f,.07589f,.30160f},.06323f};
            const bc::Vec elbow{-.12515f,.04112f,.16065f};
            const float needed=physics::inner_clearance(elbow,thighs);
            physics::UpperSkirt attachment{};
            attachment.response=.21f;attachment.step_limit=.065f;
            constexpr float test_length=.14f;
            for(unsigned i=0;i<90;++i)attachment.root_lift=physics::next_root_lift(attachment,needed,test_length);
            const auto displaced=bc::add(elbow,bc::scale(context.hip_up,attachment.root_lift));
            check(needed>.05f&&attachment.root_lift<=.060001f&&physics::inner_clearance(displaced,thighs)<.01f,
                  "bounded root lift did not clear the front attachment");
            for(unsigned i=0;i<90;++i)attachment.root_lift=physics::next_root_lift(attachment,0,test_length);
            check(attachment.root_lift<.00001f,"attachment stayed raised after thigh clearance");
            {
                // 14.342 s, opposite knee raised. Isolate the root feedback
                // using midpoint samples from the recorded front chains.
                namespace sc=gimmd::skinned_cloth;
                const bc::Vec joints[]={{-.090541f,.012508f,.139026f},{.129338f,.081474f,.180699f}};
                const bc::Vec ends[]={{-.138340f,-.082936f,.211114f},{.203233f,.099562f,.284601f}};
                const bc::Capsule down{{-.058247f,-.000789f,0},{-.018107f,-.299867f,.094206f},.063226f};
                const bc::Capsule knee{{.058249f,.000819f,0},{.036570f,.039799f,.312962f},.063226f};
                sc::FrontPose base_pose{};
                for(unsigned row=0;row<3;++row){
                    const float t=.5f*row;
                    for(unsigned b=0;b<4;++b){
                        const auto side=b/2;
                        base_pose.offset[row][b]=bc::add(bc::scale(joints[side],1-t),bc::scale(ends[side],t));
                    }
                }
                const float no_twist[4]{};
                const auto gap=[&](const sc::FrontPose& pose,float remove){
                    return sc::front_overlap(pose,no_twist,context.hip_up,down,knee,remove);
                };
                const float before_gap=gap(base_pose,0);
                auto corrected_pose=base_pose;
                attachment.root_lift=0;
                for(unsigned i=0;i<60;++i){
                    const float requested_lift=gap(corrected_pose,attachment.root_lift);
                    attachment.root_lift=physics::next_root_lift(attachment,requested_lift,test_length);
                    for(auto& origin:corrected_pose.origin)origin=bc::scale(context.hip_up,attachment.root_lift);
                }
                check(before_gap>.085f&&gap(corrected_pose,0)<.028f&&attachment.root_lift<=.06f,
                      "central contact failed to engage the available root travel");
                check(std::abs(gap(corrected_pose,attachment.root_lift)-before_gap)<.00001f,
                      "seam feedback counted applied translation twice");
                check(sc::front_overlap(corrected_pose,no_twist,context.hip_up,down,down,attachment.root_lift)==0,
                      "lowered knees kept requesting shared root lift");
            }
            physics::reset_context();
        }
#endif
        std::cout << checks << " runtime observer checks passed in synthetic host\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime test failed: " << error.what() << '\n'; return 1;
    }
}
#endif
