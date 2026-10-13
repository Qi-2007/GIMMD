#pragma once
// Included within hierarchy. All calls run on the confirmed main callback thread.
namespace time_control {
#ifdef GIMMD_FREEZE_TIME
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using GetFn = float(__fastcall*)();
using SetFn = void(__fastcall*)(float);
static GetFn get;
static SetFn set;
static bool verified = false;
static std::atomic<bool> dirty{false};
static std::atomic<unsigned> setter_calls{0};
static float before = 0, observed = 0, recovered = 0;
static unsigned restore_attempts = 0;
static unsigned hold_checks = 0, override_count = 0;
static DWORD exception = 0;
static bool acquired = false, restored = false, refused = false;
static void configure(uintptr_t base) {
    dirty.store(false); setter_calls.store(0); acquired = restored = refused = false;
    before = observed = recovered = 0; restore_attempts = hold_checks = override_count = 0; exception = 0;
    get = Enabled ? reinterpret_cast<GetFn>(entry("TimeScaleGet", base)) : nullptr;
    set = Enabled ? reinterpret_cast<SetFn>(entry("TimeScaleSet", base)) : nullptr;
    verified = get && set && entry("TimeScaleSetNative", base) && entry("TimeManagerGet", base);
}
static bool begin() {
    if (!Enabled) return true;
    if (acquired) {
        __try {
            ++hold_checks;
            const float current = get();
            if (std::isfinite(current) && std::abs(current) < 1e-6f) return true;
            ++override_count; refused = true; return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); refused = true; return false; }
    }
    if (refused || !verified) { refused = true; return false; }
    // Require the independent message pump to have run before freezing.
    if (message_callback_count.load() < 3) return false;
    __try {
        before = get();
        if (!std::isfinite(before) || before <= 0 || before > 10) { refused = true; return false; }
        dirty.store(true); setter_calls.fetch_add(1); set(0);
        observed = get();
        acquired = std::isfinite(observed) && std::abs(observed) < 1e-6f;
        if (!acquired) refused = true;
        return acquired;
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); refused = true; return false; }
}
static bool release() {
    if (!dirty.load()) return true;
    if (restore_attempts >= 8) return false;
    __try {
        ++restore_attempts; setter_calls.fetch_add(1); set(before); recovered = get();
        restored = std::isfinite(recovered) && std::abs(recovered - before) < 1e-6f;
        if (restored) dirty.store(false);
        return restored;
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); return false; }
}
static void report(std::ostream& out, bool complete) {
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"setterCallCount\":" << setter_calls.load()
        << ",\"cleanupPending\":" << (dirty.load() ? "true" : "false");
    if (complete) {
        out << ",\"profileVerified\":" << (verified ? "true" : "false")
        << ",\"acquired\":" << (acquired ? "true" : "false") << ",\"restored\":" << (restored ? "true" : "false")
        << ",\"refused\":" << (refused ? "true" : "false")
        << ",\"holdChecks\":" << hold_checks << ",\"timeScaleOverrideCount\":" << override_count;
        out << ",\"before\":"; if (std::isfinite(before)) out << before; else out << "null";
        out << ",\"observed\":"; if (std::isfinite(observed)) out << observed; else out << "null";
        out << ",\"recovered\":"; if (std::isfinite(recovered)) out << recovered; else out << "null";
        out
        << ",\"restoreAttempts\":" << restore_attempts << ",\"exception\":" << exception;
    }
    out << '}';
}
}
