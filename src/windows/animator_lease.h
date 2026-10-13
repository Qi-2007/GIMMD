#pragma once
// Bounded per-character animation lease. Only identity integers survive callbacks.
namespace animator_lease {
#ifdef GIMMD_ANIMATOR_LEASE
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using ModelFn = void*(*)(bool*);
using Setter = animator_observer::toggle::Setter;
static ModelFn model;
static Setter set;
static bool verified = false, bound = false, before = false, acquired = false, restored = false, refused = false;
static uintptr_t component_identity = 0;
static unsigned checks = 0, overrides = 0, resolve_failures = 0, restore_attempts = 0;
static DWORD exception = 0;
static std::atomic<bool> dirty{false};
static std::atomic<unsigned> setter_calls{0};
static void configure(uintptr_t base) {
    model = nullptr; component_identity = 0; bound = before = acquired = restored = refused = false;
    checks = overrides = resolve_failures = restore_attempts = 0; exception = 0;
    dirty.store(false); setter_calls.store(0);
    set = Enabled ? reinterpret_cast<Setter>(entry("BehaviourEnabledSet", base)) : nullptr;
    verified = set && entry("BehaviourEnabledSetNative", base);
}
static void* resolve(bool* active) {
    if (!model) return nullptr;
    void* owner_transform = model(active);
    if (!owner_transform) return nullptr;
    namespace ao = animator_observer;
    void* component = ao::get_component(owner_transform, find_string("Animator"));
    uintptr_t cache = 0, expected_class = 0, actual_class = 0, native = 0;
    if (!component || !read_bytes(ao::cache_slot, &cache, sizeof(cache)) || !cache ||
        !read_bytes(cache + 0x50, &expected_class, sizeof(expected_class)) || !expected_class ||
        !read_bytes(reinterpret_cast<uintptr_t>(component), &actual_class, sizeof(actual_class)) || actual_class != expected_class ||
        !read_bytes(reinterpret_cast<uintptr_t>(component) + 16, &native, sizeof(native)) || !native ||
        (component_identity && native != component_identity) ||
        !same_object(component_game_object(component), component_game_object(owner_transform))) return nullptr;
    return component;
}
static bool bind(ModelFn resolve_model) {
    if (!Enabled) return true;
    namespace ao = animator_observer;
    namespace md = animator_observer::metadata;
    if (!verified || !ao::ready.load() || std::strcmp(ao::result, "unique-animator-observed") ||
        !md::state_observed || !md::parent_matches || !md::component_enabled ||
        !md::named_code("get_enabled", 0, reinterpret_cast<uintptr_t>(md::enabled_get)) ||
        !md::named_code("set_enabled", 1, reinterpret_cast<uintptr_t>(set))) return false;
    model = resolve_model;
    __try {
        bool active = true; void* component = resolve(&active);
        bound = active && component && read_bytes(reinterpret_cast<uintptr_t>(component) + 16, &component_identity, sizeof(component_identity));
        return bound;
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); return false; }
}
static bool begin() {
    if (!Enabled) return true;
    if (!bound || refused) { refused = true; return false; }
    if (message_callback_count.load() < 3) return false;
    __try {
        bool active = true; void* component = resolve(&active);
        if (!component || !active) { ++resolve_failures; refused = true; return false; }
        const bool enabled = animator_observer::metadata::enabled_get(component);
        if (acquired) {
            ++checks;
            if (!enabled) return true;
            ++overrides; refused = true; return false;
        }
        before = enabled;
        if (!before) { refused = true; return false; }
        dirty.store(true); setter_calls.fetch_add(1); set(component, false);
        component = resolve(&active);
        acquired = component && active && !animator_observer::metadata::enabled_get(component);
        refused = !acquired;
        return acquired;
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); refused = true; return false; }
}
static bool release() {
    if (!dirty.load()) return true;
    if (restore_attempts >= 8) return false;
    __try {
        bool active = true; void* component = resolve(&active);
        if (!component) { ++resolve_failures; return false; }
        ++restore_attempts; setter_calls.fetch_add(1); set(component, before);
        component = resolve(&active);
        restored = component && animator_observer::metadata::enabled_get(component) == before;
        if (restored) dirty.store(false);
        return restored;
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); return false; }
}
static void report(std::ostream& out, bool complete) {
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"setterCallCount\":" << setter_calls.load()
        << ",\"cleanupPending\":" << (dirty.load() ? "true" : "false");
    if (complete) out << ",\"profileVerified\":" << (verified ? "true" : "false")
        << ",\"bound\":" << (bound ? "true" : "false") << ",\"before\":" << (before ? "true" : "false")
        << ",\"acquired\":" << (acquired ? "true" : "false") << ",\"restored\":" << (restored ? "true" : "false")
        << ",\"refused\":" << (refused ? "true" : "false") << ",\"disabledChecks\":" << checks
        << ",\"enabledOverrideCount\":" << overrides << ",\"resolveFailureCount\":" << resolve_failures
        << ",\"restoreAttempts\":" << restore_attempts << ",\"exception\":" << exception;
    out << '}';
}
}
