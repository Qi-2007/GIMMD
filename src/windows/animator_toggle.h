#pragma once
// Single-callback API verification, never a cross-frame animation lease.
namespace toggle {
#ifdef GIMMD_ANIMATOR_TOGGLE
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using Setter = void(__fastcall*)(void*, bool);
static Setter set;
static bool verified = false, before = false, disabled_observed = false, restored = false;
static unsigned restore_attempts = 0;
static DWORD exception = 0;
static std::atomic<unsigned> setter_calls{0};
static std::atomic<bool> dirty{false}, consumed{false}, failure{false};
static const char* status = "disabled";
static void configure(uintptr_t base) {
    set = Enabled ? reinterpret_cast<Setter>(entry("BehaviourEnabledSet", base)) : nullptr;
    verified = set && entry("BehaviourEnabledSetNative", base);
    before = disabled_observed = restored = false; restore_attempts = 0; exception = 0;
    setter_calls.store(0); dirty.store(false); failure.store(false); status = Enabled ? "not-attempted" : "disabled";
}
static bool identity(void* component, uintptr_t native, uintptr_t klass, uintptr_t owner) {
    uintptr_t actual_native = 0, actual_class = 0, actual_owner = 0;
    if (!read_bytes(reinterpret_cast<uintptr_t>(component), &actual_class, sizeof(actual_class)) || actual_class != klass ||
        !read_bytes(reinterpret_cast<uintptr_t>(component) + 16, &actual_native, sizeof(actual_native)) || actual_native != native) return false;
    void* game_object = component_game_object(component);
    return game_object && read_bytes(reinterpret_cast<uintptr_t>(game_object) + 16, &actual_owner, sizeof(actual_owner)) && actual_owner == owner;
}
static void run(void* component) {
    if (!Enabled) return;
    status = "preflight-refused";
    if (!verified || !metadata::state_observed || !metadata::parent_matches || !metadata::component_enabled ||
        !metadata::named_code("set_enabled", 1, reinterpret_cast<uintptr_t>(set))) return;
    if (consumed.exchange(true)) { status = "experiment-already-consumed"; return; }
    uintptr_t native = 0, klass = 0, owner = 0;
    __try {
        void* game_object = component_game_object(component);
        if (!game_object || !read_bytes(reinterpret_cast<uintptr_t>(component), &klass, sizeof(klass)) || !klass ||
            !read_bytes(reinterpret_cast<uintptr_t>(component) + 16, &native, sizeof(native)) || !native ||
            !read_bytes(reinterpret_cast<uintptr_t>(game_object) + 16, &owner, sizeof(owner)) || !owner) return;
        before = metadata::enabled_get(component);
        if (!before || !identity(component, native, klass, owner)) return;
        dirty.store(true); // Covers partial mutation before a setter exception.
        setter_calls.fetch_add(1); set(component, false);
        disabled_observed = identity(component, native, klass, owner) && !metadata::enabled_get(component);
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); }
    if (!dirty.load()) return;
    // Restore the saved value, including after mutation/readback faults.
    for (unsigned i = 0; i < 2 && dirty.load(); ++i) {
        __try {
            ++restore_attempts;
            if (!identity(component, native, klass, owner)) { status = "identity-lost-restart-required"; break; }
            setter_calls.fetch_add(1); set(component, before);
            if (identity(component, native, klass, owner) && metadata::enabled_get(component) == before) {
                restored = true; dirty.store(false);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); }
    }
    if (dirty.load()) status = "restoration-failed-restart-required";
    else if (exception) status = "exception-restored";
    else status = disabled_observed ? "animator-toggle-restored" : "disable-not-observed-restored";
    failure.store(dirty.load() || exception || !disabled_observed);
}
static bool failed() { return Enabled && failure.load(); }
static void report(std::ostream& out) {
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"setterCallCount\":" << setter_calls.load()
        << ",\"cleanupPending\":" << (dirty.load() ? "true" : "false") << ",\"status\":\"" << status << "\""
        << ",\"before\":" << (before ? "true" : "false") << ",\"disabledObserved\":" << (disabled_observed ? "true" : "false")
        << ",\"restored\":" << (restored ? "true" : "false") << ",\"restoreAttempts\":" << restore_attempts
        << ",\"exception\":" << exception << '}';
}
}
