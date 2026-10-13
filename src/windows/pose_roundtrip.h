#pragma once
// Included within namespace hierarchy after checked getters are declared.
// Enabled only by an explicit build option. No pointer survives this callback.
namespace roundtrip {
#ifdef GIMMD_POSE_ROUNDTRIP
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using SetLocalFn = void(__fastcall*)(void*, const Quat*);
static SetLocalFn set_local;
static bool profile_verified = false;
static std::atomic<unsigned> setter_calls{0};
static std::atomic<bool> ready{false};
static std::atomic<bool> consumed{false}; // Deliberately not reset by another probe export call.
struct Result {
    const char* status = "disabled";
    int actor = -1, bone = -1;
    bool attempted = false, original_read = false, write_verified = false, restored = false, world_restored = false;
    unsigned restore_attempts = 0;
    DWORD read_exception = 0, write_exception = 0, restore_exception = 0;
    Quat before{}, world_before{}, requested{}, after{}, recovered{};
    float write_error = 0, restore_error = 0;
};
static Result result;
static bool failed() { return ready.load() && setter_calls.load() && !result.restored; }
static void reset() {
    ready.store(false); setter_calls.store(0); set_local = nullptr; profile_verified = false;
    result = {}; result.status = Enabled ? "not-attempted" : "disabled";
}
static void configure(uintptr_t base) {
    if (!Enabled) return;
    set_local = reinterpret_cast<SetLocalFn>(entry("SetLocalRotation", base));
    profile_verified = set_local && entry("SetLocalRotationNative", base) && entry("LocalRotationStore", base);
    result.status = profile_verified ? "waiting-for-complete-hierarchy" : "setter-profile-mismatch";
}
static Quat multiply(const Quat& a, const Quat& b) {
    return {a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}
static float error(const Quat& a, const Quat& b) {
    if (!unit_quat(a) || !unit_quat(b)) return 1e9f;
    float direct = 0, opposite = 0;
    const float first[] = {a.x, a.y, a.z, a.w}, second[] = {b.x, b.y, b.z, b.w};
    for (unsigned i = 0; i < 4; ++i) {
        const auto d = std::abs(first[i] - second[i]), o = std::abs(first[i] + second[i]);
        if (d > direct) direct = d;
        if (o > opposite) opposite = o;
    }
    return direct < opposite ? direct : opposite;
}
static bool read_original(void* bone) {
    __try {
        rotation_local(&result.before, bone); rotation_world(&result.world_before, bone);
        return unit_quat(result.before) && unit_quat(result.world_before);
    } __except (EXCEPTION_EXECUTE_HANDLER) { result.read_exception = GetExceptionCode(); return false; }
}
static void write_and_read(void* bone) {
    __try {
        setter_calls.fetch_add(1); // Count even a call that faults after partially changing the pose.
        set_local(bone, &result.requested);
        rotation_local(&result.after, bone);
        result.write_error = error(result.after, result.requested);
        result.write_verified = result.write_error < 0.00002f && error(result.after, result.before) > 0.001f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { result.write_exception = GetExceptionCode(); }
}
static bool restore_and_read(void* bone) {
    __try {
        ++result.restore_attempts; setter_calls.fetch_add(1);
        set_local(bone, &result.before);
        rotation_local(&result.recovered, bone);
        Quat world{}; rotation_world(&world, bone);
        result.restore_error = error(result.before, result.recovered);
        result.world_restored = error(result.world_before, world) < 0.00002f;
        return result.restore_error < 0.00002f && result.world_restored;
    } __except (EXCEPTION_EXECUTE_HANDLER) { result.restore_exception = GetExceptionCode(); return false; }
}
static bool identity_current(const Node& node) {
    uintptr_t current = 0;
    return node.object && node.identity && read_bytes(reinterpret_cast<uintptr_t>(node.object) + 16,
        &current, sizeof(current)) && current == node.identity;
}
static void run() {
    if (!Enabled || !profile_verified || ready.load()) return;
    if (consumed.load()) { result.status = "experiment-already-consumed"; ready.store(true); return; }
    result.status = "incomplete-hierarchy-refused";
    if (std::strcmp(status, "hierarchy-read-observed")) { ready.store(true); return; }
    unsigned actors = 0, bones = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto& node = nodes[i];
        if (node.parent == 0 && node.active && !std::wcsncmp(node.name, L"Avatar_", 7)) {
            ++actors; result.actor = static_cast<int>(i);
        }
    }
    result.status = "actor-ambiguity-refused";
    if (actors != 1) { ready.store(true); return; }
    for (unsigned i = 0; i < count; ++i) {
        const auto& node = nodes[i];
        if (std::wcscmp(node.name, L"Bip001 L UpperArm")) continue;
        int ancestor = static_cast<int>(i);
        for (unsigned depth = 0; depth <= MaxDepth && ancestor >= 0; ++depth) {
            if (ancestor == result.actor) { ++bones; result.bone = static_cast<int>(i); break; }
            ancestor = nodes[ancestor].parent;
        }
    }
    result.status = "bone-ambiguity-refused";
    if (bones != 1) { ready.store(true); return; }
    auto& actor = nodes[result.actor]; auto& bone = nodes[result.bone];
    result.status = "identity-recheck-refused";
    if (!bone.processed || !bone.active || !bone.aliases_equal || !bone.owner_valid || !bone.parent_valid ||
        !bone.rotation_relation_valid || !identity_current(actor) || !identity_current(bone)) {
        ready.store(true); return;
    }
    void* owner = component_game_object(bone.object);
    void* actor_owner = component_game_object(actor.object);
    if (!owner || !actor_owner || !get_active(owner) || !get_active(actor_owner)) { ready.store(true); return; }
    if (consumed.exchange(true)) { result.status = "experiment-already-consumed"; ready.store(true); return; }
    result.attempted = true;
    result.status = "original-read-failed";
    result.original_read = read_original(bone.object);
    if (!result.original_read) { ready.store(true); return; }
    // One degree about the bone's local Z; normalize the requested input.
    result.requested = multiply(result.before, {0,0,0.0087265355f,0.9999619231f});
    const auto& q = result.requested;
    const float length = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    result.requested = {q.x/length, q.y/length, q.z/length, q.w/length};
    if (!unit_quat(result.requested)) { ready.store(true); return; }
    write_and_read(bone.object);
    // Always restore after entering the write call, including write/read exceptions.
    for (unsigned attempt = 0; attempt < 2 && !result.restored; ++attempt)
        result.restored = restore_and_read(bone.object);
    result.status = !result.restored ? "pose-restoration-failed" : result.write_exception ? "write-exception-restored" :
        result.write_verified ? "single-bone-roundtrip-restored" : "write-mismatch-restored";
    if (!result.restored) accepting.store(false);
    ready.store(true);
}
static void quat_json(std::ostream& out, const Quat& q) {
    if (!unit_quat(q)) { out << "null"; return; }
    out << std::setprecision(9) << '[' << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ']';
}
static void report(std::ostream& out) {
    const bool report_ready = ready.load();
    out << "{\"enabled\":" << (Enabled ? "true" : "false")
        << ",\"profileVerified\":" << (profile_verified ? "true" : "false")
        << ",\"published\":" << (report_ready ? "true" : "false")
        << ",\"setterCallCount\":" << setter_calls.load() << ",\"angleDegrees\":1,\"sameCallbackRestore\":true";
    if (report_ready) {
        out << ",\"status\":\"" << result.status << "\",\"actorNodeId\":" << result.actor
            << ",\"boneNodeId\":" << result.bone << ",\"boneName\":\"Bip001 L UpperArm\""
            << ",\"attempted\":" << (result.attempted ? "true" : "false")
            << ",\"writeVerified\":" << (result.write_verified ? "true" : "false")
            << ",\"localRestored\":" << (result.restored ? "true" : "false")
            << ",\"worldRestored\":" << (result.world_restored ? "true" : "false")
            << ",\"restoreAttempts\":" << result.restore_attempts
            << ",\"readException\":" << result.read_exception << ",\"writeException\":" << result.write_exception
            << ",\"restoreException\":" << result.restore_exception
            << ",\"writeComponentError\":" << result.write_error << ",\"restoreComponentError\":" << result.restore_error;
        out << ",\"beforeLocal\":"; quat_json(out, result.before);
        out << ",\"requestedLocal\":"; quat_json(out, result.requested);
        out << ",\"observedLocal\":"; quat_json(out, result.after);
        out << ",\"recoveredLocal\":"; quat_json(out, result.recovered);
    }
    out << '}';
}
}
