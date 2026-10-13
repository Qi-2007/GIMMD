#pragma once
namespace animator_observer {
#ifdef GIMMD_ANIMATOR_PROBE
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using GetComponentFn = void*(__fastcall*)(void*, void*);
static GetComponentFn get_component;
static uintptr_t cache_slot;
static bool verified = false;
static std::atomic<bool> ready{false};
static const char* result = "disabled";
static unsigned queries = 0, matches = 0, mismatches = 0;
static int owner_node = -1;
static DWORD exception = 0;
#include "animator_metadata.h"
#include "bind_pose_observer.h"
#include "animator_toggle.h"
static void configure(uintptr_t base) {
    metadata::reset(base);
    bind_pose::configure(base);
    toggle::configure(base);
    ready.store(false); queries = matches = mismatches = 0; owner_node = -1; exception = 0;
    result = Enabled ? "not-attempted" : "disabled";
    get_component = Enabled ? reinterpret_cast<GetComponentFn>(entry("ComponentGetByName", base)) : nullptr;
    verified = get_component && entry("ComponentGetByNameNative", base) &&
        entry("AnimatorClassRegistration", base) && entry("AnimatorClassCacheStore", base);
    cache_slot = base + 0x56ce298; // Reviewed named class registration; read-only.
}
static void capture() {
    if (!Enabled || !verified || ready.load()) return;
    __try {
        result = "incomplete-hierarchy-refused";
        if (std::strcmp(status, "hierarchy-read-observed")) { ready.store(true); return; }
        int actor = -1; unsigned actors = 0;
        for (unsigned i = 0; i < count; ++i)
            if (nodes[i].parent == 0 && nodes[i].active && !std::wcsncmp(nodes[i].name, L"Avatar_", 7)) {
                ++actors; actor = static_cast<int>(i);
            }
        if (actors != 1) { result = "actor-ambiguity-refused"; ready.store(true); return; }
        uintptr_t cache = 0, expected_class = 0;
        if (!read_bytes(cache_slot, &cache, sizeof(cache)) || !cache ||
            !read_bytes(cache + 0x50, &expected_class, sizeof(expected_class)) || !expected_class) {
            result = "animator-class-cache-unavailable"; ready.store(true); return;
        }
        void* name = find_string("Animator");
        void* unique_component = nullptr;
        if (!name) { result = "name-conversion-failed"; ready.store(true); return; }
        const auto deadline = GetTickCount64() + 8;
        for (unsigned i = 0; i < count; ++i) {
            if (nodes[i].depth > 3 || !nodes[i].active) continue;
            bool under_actor = false;
            for (int n = static_cast<int>(i); n >= 0; n = nodes[n].parent)
                if (n == actor) { under_actor = true; break; }
            if (!under_actor) continue;
            if (queries >= 32 || GetTickCount64() >= deadline) { result = "component-query-partial"; ready.store(true); return; }
            if (!roundtrip::identity_current(nodes[i])) { result = "owner-identity-changed"; ready.store(true); return; }
            ++queries;
            void* component = get_component(nodes[i].object, name);
            if (!component) continue;
            uintptr_t actual_class = 0;
            if (!read_bytes(reinterpret_cast<uintptr_t>(component), &actual_class, sizeof(actual_class)) ||
                actual_class != expected_class || !same_object(component_game_object(component), component_game_object(nodes[i].object))) {
                ++mismatches; continue;
            }
            ++matches; owner_node = static_cast<int>(i); unique_component = component;
        }
        result = mismatches ? "component-class-or-owner-mismatch" : matches == 1 ? "unique-animator-observed" :
            matches ? "animator-ambiguity-refused" : "animator-not-found-in-bounded-scope";
        if (!mismatches && matches == 1) {
            metadata::capture(expected_class); metadata::observe_component(unique_component); toggle::run(unique_component);
            bind_pose::capture(unique_component);
            bind_pose::capture_skins(owner_node);
        }
        ready.store(true);
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); result = "component-read-exception"; ready.store(true); }
}
static void report(std::ostream& out) {
    const bool complete = ready.load();
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"published\":" << (complete ? "true" : "false")
        << ",\"profileVerified\":" << (verified ? "true" : "false") << ",\"settersCalled\":" << (toggle::setter_calls.load() ? "true" : "false");
    if (complete) out << ",\"status\":\"" << result << "\",\"queryCount\":" << queries << ",\"matchingAnimatorCount\":" << matches
        << ",\"classOrOwnerMismatchCount\":" << mismatches << ",\"ownerNodeId\":" << owner_node << ",\"exception\":" << exception;
    if (complete) { out << ",\"metadata\":"; metadata::report(out); out << ",\"toggle\":"; toggle::report(out);
        out << ",\"bindPose\":"; bind_pose::report(out); }
    out << '}';
}
}
