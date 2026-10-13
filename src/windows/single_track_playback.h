#pragma once
// Included within hierarchy. Engine pointers are reacquired on every tick.
namespace playback {
#ifdef GIMMD_MULTI_TRACK_PLAYBACK
inline constexpr bool MultiEnabled = true;
#else
inline constexpr bool MultiEnabled = false;
#endif
static bool multi_mode = MultiEnabled;
#ifdef GIMMD_FULL_BODY_FK
inline constexpr bool FullBodyEnabled = true;
#else
inline constexpr bool FullBodyEnabled = false;
#endif
static bool full_mode = FullBodyEnabled;
#ifdef GIMMD_ROOT_MOTION
inline constexpr bool RootMotionEnabled = true;
#else
inline constexpr bool RootMotionEnabled = false;
#endif
static bool root_mode = RootMotionEnabled;
#ifdef GIMMD_LEG_IK
inline constexpr bool LegIkEnabled = true;
#else
inline constexpr bool LegIkEnabled = false;
#endif
static bool leg_mode = LegIkEnabled;
#ifdef GIMMD_TOE_IK
inline constexpr bool ToeIkEnabled = true;
#else
inline constexpr bool ToeIkEnabled = false;
#endif
static bool toe_mode=ToeIkEnabled;
#ifdef GIMMD_SINGLE_TRACK_PLAYBACK
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
namespace vmd = BetterEndfieldNext::Vmd;
static roundtrip::SetLocalFn set_local;
static std::vector<vmd::BoneKey> keys;
static std::atomic<bool> done{true}, started{false}, dirty{false}, stop_requested{false}, consumed{false};
static std::atomic<unsigned> setter_calls{0};
static bool profile_verified = false, loaded = false;
struct Step { int child = -1; uintptr_t identity = 0; wchar_t name[MaxName]{}; };
static Step path[MaxDepth + 1];
static unsigned path_size = 0;
static unsigned model_path_size = 0;
static void* resolve_model(bool* active);
struct Result {
    const char* status = "disabled";
    unsigned samples = 0, persisted = 0, overwritten = 0, resolve_failures = 0, restore_attempts = 0;
    ULONGLONG began = 0, last = 0, elapsed = 0;
    DWORD exception = 0;
    bool restored = false;
    Quat baseline{0,0,0,1}, applied{0,0,0,1};
    float restore_error = 0;
};
static Result result;
namespace multi {
static void configure_root(uintptr_t base);
static void reset();
static bool prepare(const vmd::Motion& motion);
static bool bind(int actor);
static void tick(ULONGLONG now);
static void report(std::ostream& out, bool complete);
}
static void configure(uintptr_t base) {
    done.store(!Enabled); started.store(false); dirty.store(false); stop_requested.store(false);
    setter_calls.store(0); result = {}; path_size = model_path_size = 0; loaded = false; keys.clear();
    multi_mode = MultiEnabled; full_mode = FullBodyEnabled; root_mode = RootMotionEnabled; leg_mode = LegIkEnabled; toe_mode=ToeIkEnabled; multi::reset();
    multi::configure_root(base);
    set_local = Enabled ? reinterpret_cast<roundtrip::SetLocalFn>(entry("SetLocalRotation", base)) : nullptr;
    profile_verified = set_local && entry("SetLocalRotationNative", base) && entry("LocalRotationStore", base);
    result.status = Enabled ? "waiting-for-fixture" : "disabled";
}
// Worker-side parsing only. This experiment accepts exactly the generated fixture.
static void load(const std::filesystem::path& file) {
    if (!Enabled) return;
    loaded = false;
    result.status = "fixture-rejected";
    if (!profile_verified || consumed.load()) { done.store(true); return; }
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if (size <= 0 || size > (multi_mode ? (16 << 20) : 4096)) { done.store(true); return; }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    vmd::Motion motion; std::string error;
    if (!input || !vmd::Parse(bytes, motion, error)) { done.store(true); return; }
    if (multi_mode) {
        loaded = multi::prepare(motion); done.store(!loaded);
        result.status = loaded ? "waiting-for-complete-hierarchy" : "multi-track-source-rejected"; return;
    }
    if (motion.bones.size() != 1 ||
        motion.bones.begin()->first != "left_arm_test" || !motion.morphs.empty() || !motion.cameras.empty()) {
        done.store(true); return;
    }
    keys = motion.bones.begin()->second;
    if (keys.size() != 3) { done.store(true); return; }
    for (unsigned i = 0; i < 3; ++i) {
        const auto& k = keys[i];
        const float z = i == 1 ? 0.0436193874f : 0.0f, w = i == 1 ? 0.9990482216f : 1.0f;
        if (k.frame != i * 30 || k.position.x || k.position.y || k.position.z ||
            std::abs(k.rotation.x) > 1e-6f || std::abs(k.rotation.y) > 1e-6f ||
            std::abs(k.rotation.z - z) > 1e-6f || std::abs(k.rotation.w - w) > 1e-6f) {
            done.store(true); return;
        }
    }
    loaded = true; result.status = "waiting-for-complete-hierarchy";
}
static void finish(const char* status_text) {
    result.status = status_text;
    const bool time_ok = time_control::release(), animator_ok = animator_lease::release();
    if (time_ok && animator_ok) done.store(true);
    else { result.status = time_ok ? "animator-restoration-pending" : "time-restoration-pending"; stop_requested.store(true); }
}
static void bind() {
    if (!Enabled || !loaded || done.load() || started.load()) return;
    if (stop_requested.load()) { finish("cancelled-before-binding"); return; }
    if (std::strcmp(status, "hierarchy-read-observed")) { finish("incomplete-hierarchy-refused"); return; }
    int actor = -1, bone = -1; unsigned actors = 0, bones = 0;
    for (unsigned i = 0; i < count; ++i)
        if (nodes[i].parent == 0 && nodes[i].active && !std::wcsncmp(nodes[i].name, L"Avatar_", 7)) {
            actor = static_cast<int>(i); ++actors;
        }
    if (actors != 1) { finish("actor-ambiguity-refused"); return; }
    for (unsigned i = 0; i < count; ++i) {
        if (std::wcscmp(nodes[i].name, L"Bip001 L UpperArm")) continue;
        for (int n = static_cast<int>(i); n >= 0; n = nodes[n].parent)
            if (n == actor) { bone = static_cast<int>(i); ++bones; break; }
    }
    if (bones != 1) { finish("bone-ambiguity-refused"); return; }
    Step reverse[MaxDepth + 1]{}; unsigned size = 0;
    for (int n = bone; n >= 0; n = nodes[n].parent) {
        if (size >= MaxDepth + 1 || !nodes[n].processed || !nodes[n].aliases_equal || !nodes[n].parent_valid ||
            !nodes[n].owner_valid || !nodes[n].active || (nodes[n].parent >= 0 && !nodes[n].rotation_relation_valid) || !roundtrip::identity_current(nodes[n])) {
            finish("binding-validation-refused"); return;
        }
        auto& step = reverse[size++]; step.identity = nodes[n].identity;
        wcscpy_s(step.name, nodes[n].name);
        if (nodes[n].parent >= 0) {
            step.child = 0;
            for (int previous = 0; previous < n; ++previous)
                if (nodes[previous].parent == nodes[n].parent) ++step.child;
        }
    }
    path_size = size;
    for (unsigned i = 0; i < size; ++i) path[i] = reverse[size - i - 1];
    if (animator_lease::Enabled) {
        const int owner = animator_observer::owner_node;
        if (owner >= 0 && static_cast<unsigned>(owner) < count)
            for (unsigned i = 0; i < size; ++i) if (path[i].identity == nodes[owner].identity) model_path_size = i + 1;
        if (!model_path_size || !animator_lease::bind(resolve_model)) { finish("animator-binding-refused"); return; }
    }
    if (multi_mode && !multi::bind(actor)) { finish("multi-track-binding-refused"); return; }
    if (consumed.exchange(true)) { finish("experiment-already-consumed"); return; }
    result.status = "bound"; started.store(true);
}
// POD-only SEH boundary: no engine object pointer is stored between callbacks.
static void* resolve_steps(bool* active, const Step* steps, unsigned length) {
    __try {
        void* root = find_object(find_string("/EntityRoot/AvatarRoot"));
        uintptr_t native = 0; void* current = nullptr;
        if (!root || !read_bytes(reinterpret_cast<uintptr_t>(root) + 16, &native, sizeof(native)) || !native ||
            object_transform(&current, reinterpret_cast<void*>(native)) != &current) return nullptr;
        const int root_children = child_count(current);
        if (root_children < 0 || root_children > static_cast<int>(MaxChildren)) return nullptr;
        unsigned actors = 0;
        for (int i = 0; i < root_children; ++i) {
            void* child = get_child(current, i); wchar_t name[MaxName]{};
            if (!child || !string_read(name_a(child), name)) return nullptr;
            if (!std::wcsncmp(name, L"Avatar_", 7)) {
                void* owner = component_game_object(child);
                if (owner && get_active(owner)) ++actors;
            }
        }
        if (actors != 1) *active = false;
        for (unsigned i = 0; i < length; ++i) {
            void* parent = current;
            if (i) {
                const int children = child_count(parent);
                if (children < 0 || children > static_cast<int>(MaxChildren) || steps[i].child >= children) return nullptr;
                current = get_child(parent, steps[i].child);
            }
            wchar_t name[MaxName]{}; uintptr_t identity = 0;
            if (!current || !read_bytes(reinterpret_cast<uintptr_t>(current) + 16, &identity, sizeof(identity)) ||
                identity != steps[i].identity || !string_read(name_a(current), name) || std::wcscmp(name, steps[i].name) ||
                (i && !same_object(get_parent(current), parent))) return nullptr;
            void* owner = component_game_object(current);
            if (!owner || !get_active(owner)) *active = false;
        }
        return current;
    } __except (EXCEPTION_EXECUTE_HANDLER) { result.exception = GetExceptionCode(); return nullptr; }
}
static void* resolve_path(bool* active, unsigned length) { return resolve_steps(active, path, length); }
static void* resolve(bool* active) { return resolve_path(active, path_size); }
static void* resolve_model(bool* active) { return model_path_size ? resolve_path(active, model_path_size) : nullptr; }
#include "batch_resolver.h"
static bool read_local(void* bone, Quat* q) {
    __try { rotation_local(q, bone); return unit_quat(*q); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result.exception = GetExceptionCode(); return false; }
}
static bool write_local(void* bone, const Quat* q, Quat* observed) {
    __try {
        setter_calls.fetch_add(1); set_local(bone, q); rotation_local(observed, bone);
        return roundtrip::error(*q, *observed) < 0.00002f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { result.exception = GetExceptionCode(); return false; }
}
#include "multi_track_playback.h"
static void restore(void* bone) {
    if (!dirty.load()) { result.restored = true; finish("stopped-before-write"); return; }
    if (result.restore_attempts >= 8) { animator_lease::release(); result.status = "restoration-exhausted-restart-required"; return; }
    Quat observed{};
    for (unsigned i = 0; i < 2; ++i) {
        ++result.restore_attempts;
        if (write_local(bone, &result.baseline, &observed)) {
            result.restore_error = roundtrip::error(result.baseline, observed);
            result.restored = true; dirty.store(false);
            finish(result.exception ? "exception-restored" : "single-track-stopped-restored"); return;
        }
    }
    // Keep the hook and callback available for a later restoration retry.
    result.status = "restoration-pending"; stop_requested.store(true);
}
static void tick(ULONGLONG now) {
    if (!Enabled || !started.load() || done.load()) return;
    if (multi_mode) { multi::tick(now); return; }
    if (!stop_requested.load() && !animator_lease::begin()) {
        if (!animator_lease::refused) return;
        stop_requested.store(true);
        if (!dirty.load()) { finish("animator-acquisition-refused"); return; }
    }
    if (!stop_requested.load() && !time_control::begin()) {
        if (!time_control::refused) return;
        stop_requested.store(true);
        if (!dirty.load()) { finish("time-freeze-refused"); return; }
    }
    if (!result.began) result.began = now;
    result.elapsed = now >= result.began ? now - result.began : 2000;
    if (result.elapsed >= 2000) stop_requested.store(true);
    if (stop_requested.load()) time_control::release();
    if (!stop_requested.load() && result.samples && now - result.last < 33) return;
    bool active = true;
    void* bone = resolve(&active);
    if (!bone) {
        ++result.resolve_failures; stop_requested.store(true);
        animator_lease::release(); // Model path does not depend on the lost bone.
        if (!dirty.load()) finish("identity-changed-before-write");
        return;
    }
    Quat current{};
    if (!read_local(bone, &current)) { stop_requested.store(true); restore(bone); return; }
    if (!dirty.load()) result.baseline = current;
    else if (roundtrip::error(current, result.applied) < 0.00002f) ++result.persisted;
    else { ++result.overwritten; result.baseline = current; }
    if (stop_requested.load()) { restore(bone); return; }
    // Refuse new writes to an inactive actor; cleanup is still allowed there.
    if (!active) stop_requested.store(true);
    if (stop_requested.load()) { restore(bone); return; }
    vmd::BoneSample sample{};
    if (!vmd::SampleBone(keys, static_cast<double>(result.elapsed) * 0.03, sample)) {
        stop_requested.store(true); restore(bone); return;
    }
    const Quat delta{sample.rotation.x, sample.rotation.y, sample.rotation.z, sample.rotation.w};
    result.applied = roundtrip::multiply(result.baseline, delta);
    dirty.store(true); // Includes partial mutation before a setter exception.
    Quat observed{};
    if (!write_local(bone, &result.applied, &observed)) { stop_requested.store(true); restore(bone); return; }
    ++result.samples; result.last = now;
}
static void report(std::ostream& out) {
    const bool complete = done.load();
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"published\":" << (complete ? "true" : "false")
        << ",\"setterCallCount\":" << setter_calls.load() << ",\"cleanupPending\":" << (dirty.load() ? "true" : "false")
        << ",\"durationMs\":" << (gimmd::player_session::Enabled && multi_mode ? double(multi::last_frame)*1000/30 : 2000)
        << ",\"wallTimeLimitMs\":" << (gimmd::player_session::Enabled ? 90000 : 5000)
        << ",\"maxAngleDegrees\":" << (full_mode ? "null" : "5") << ",\"multiTrack\":" << (multi_mode ? "true" : "false");
    if (complete) out << ",\"status\":\"" << result.status << "\",\"sampleCount\":" << result.samples
        << ",\"poseRetainedCount\":" << result.persisted << ",\"animationOverwriteCount\":" << result.overwritten
        << ",\"resolveFailureCount\":" << result.resolve_failures << ",\"elapsedMs\":" << result.elapsed
        << ",\"localRestored\":" << (result.restored ? "true" : "false") << ",\"restoreAttempts\":" << result.restore_attempts
        << ",\"restoreComponentError\":" << result.restore_error << ",\"exception\":" << result.exception;
    out << ",\"timeControl\":"; time_control::report(out, complete);
    out << ",\"animatorLease\":"; animator_lease::report(out, complete);
    out << ",\"multiTrackPreview\":"; multi::report(out, complete);
    out << '}';
}
}
