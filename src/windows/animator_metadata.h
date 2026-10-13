#pragma once
// Read-only discovery. No inferred metadata pointer is ever invoked.
namespace metadata {
struct TextField { unsigned type, offset; const char* text; };
struct Method { unsigned type, table_offset, index, name_offset; const char* name;
    unsigned parameter_count = 0, code_count = 0, code_offsets[8]{}; uintptr_t code_rvas[8]{}; };
static TextField texts[32]{};
static Method methods[32]{};
static unsigned text_count = 0, method_count = 0, parent_offset = 0;
static const char* status = "not-attempted";
static uintptr_t image_base = 0, behaviour_slot = 0;
static bool profile_verified = false;
using ParentGetFn = uintptr_t(__fastcall*)(uintptr_t);
using MethodNameFn = const char*(__fastcall*)(uintptr_t);
static ParentGetFn parent_get;
static MethodNameFn method_name;
static bool accessors_verified = false, parent_matches = false;
static unsigned method_queries = 0, owner_mismatches = 0, declared_counts[2]{};
using EnabledGetFn = bool(__fastcall*)(void*);
using UpdateModeGetFn = int(__fastcall*)(void*);
static EnabledGetFn enabled_get;
static UpdateModeGetFn mode_get;
static bool state_observed = false, component_enabled = false;
static int update_mode = -1;
static const char* known_text(uintptr_t address) {
    char value[48]{};
    if (!address || !read_bytes(address, value, sizeof(value)) || !std::memchr(value, 0, sizeof(value))) return nullptr;
    for (const char* name : {"Animator", "Behaviour", "UnityEngine", "get_enabled", "set_enabled",
        "get_isActiveAndEnabled", "get_updateMode", "set_updateMode", "get_speed", "set_speed", "get_avatar", "get_isHuman"})
        if (!std::strcmp(value, name)) return name;
    return nullptr;
}
static void reset(uintptr_t base) {
    image_base = base; behaviour_slot = base + 0x56cf1b8;
    profile_verified = entry("BehaviourClassRegistration", base) && entry("BehaviourClassCacheStore", base);
    text_count = method_count = parent_offset = 0; status = "not-attempted";
    parent_matches = false; method_queries = owner_mismatches = 0; declared_counts[0] = declared_counts[1] = 0;
    parent_get = reinterpret_cast<ParentGetFn>(entry("ClassParentGet", base));
    method_name = reinterpret_cast<MethodNameFn>(entry("MethodNameGet", base));
    accessors_verified = parent_get && method_name && entry("MethodNameInitialize", base) &&
        entry("ClassMethodSearch", base) && entry("ClassMethodSearchCompare", base) && entry("ClassMethodSearchParent", base);
    enabled_get = entry("BehaviourEnabledGetNative", base) ? reinterpret_cast<EnabledGetFn>(entry("BehaviourEnabledGet", base)) : nullptr;
    mode_get = entry("AnimatorUpdateModeGetNative", base) ? reinterpret_cast<UpdateModeGetFn>(entry("AnimatorUpdateModeGet", base)) : nullptr;
    state_observed = component_enabled = false; update_mode = -1;
}
static bool named_code(const char* name, unsigned parameter_count, uintptr_t code) {
    for (unsigned i = 0; i < method_count; ++i) {
        const auto& m = methods[i];
        if (std::strcmp(m.name, name) || m.parameter_count != parameter_count) continue;
        for (unsigned n = 0; n < m.code_count; ++n)
            if (m.code_offsets[n] == 8 && m.code_rvas[n] + image_base == code) return true;
    }
    return false;
}
static void observe_component(void* component) {
    if (!parent_matches || !enabled_get || !mode_get ||
        !named_code("get_enabled", 0, reinterpret_cast<uintptr_t>(enabled_get)) ||
        !named_code("get_updateMode", 0, reinterpret_cast<uintptr_t>(mode_get))) return;
    const bool first = enabled_get(component);
    const int mode = mode_get(component);
    if (enabled_get(component) != first || mode < 0 || mode > 2) return;
    component_enabled = first; update_mode = mode; state_observed = true;
}
static void capture_named_methods(const uintptr_t (&classes)[2]) {
    parent_matches = parent_get(classes[0]) == classes[1];
    if (!parent_matches) { status = "parent-accessor-mismatch-refused"; return; }
    const auto deadline = GetTickCount64() + 20;
    // The reviewed search loop uses class+0x58 and a uint16 count at +0xc0.
    // Behaviour first so the bounded Animator scan cannot starve its base class.
    for (unsigned order = 0; order < 2; ++order) {
        const unsigned type = 1 - order;
        uintptr_t table = 0; uint16_t length = 0;
        if (!read_bytes(classes[type] + 0xc0, &length, sizeof(length)) || length > 1024 ||
            !read_bytes(classes[type] + 0x58, &table, sizeof(table)) || (length && !table)) {
            status = "method-table-unavailable"; return;
        }
        declared_counts[type] = length;
        for (unsigned i = 0; i < length && i < 384; ++i) {
            if (GetTickCount64() >= deadline) { status = "named-method-scan-partial"; return; }
            uintptr_t item = 0, info[8]{};
            if (!read_bytes(table + i * 8, &item, sizeof(item)) || !item || !read_bytes(item, info, sizeof(info))) {
                status = "method-entry-unreadable"; return;
            }
            if (info[0] != classes[type]) { ++owner_mismatches; status = "method-owner-mismatch-refused"; return; }
            ++method_queries;
            const char* name = known_text(reinterpret_cast<uintptr_t>(method_name(item)));
            if (!name || (std::strncmp(name, "get_", 4) && std::strncmp(name, "set_", 4)) || method_count >= 32) continue;
            auto& m = methods[method_count++]; m = {};
            m.type = type; m.table_offset = 0x58; m.index = i; m.name_offset = 0x30; m.name = name;
            m.parameter_count = reinterpret_cast<const uint8_t*>(info)[0x2e];
            for (unsigned n = 0; n < 8; ++n) if (executable(info[n], image_base)) {
                m.code_offsets[m.code_count] = n * 8; m.code_rvas[m.code_count++] = info[n] - image_base;
            }
        }
    }
    status = declared_counts[0] > 384 || declared_counts[1] > 384 ? "named-method-scan-partial" : "named-methods-observed";
}
static void capture(uintptr_t animator_class) {
    if (!profile_verified) { status = "behaviour-profile-unverified"; return; }
    uintptr_t cache = 0, behaviour_class = 0;
    if (!read_bytes(behaviour_slot, &cache, sizeof(cache)) || !cache ||
        !read_bytes(cache + 0x108, &behaviour_class, sizeof(behaviour_class)) || !behaviour_class) {
        status = "behaviour-cache-unavailable"; return;
    }
    const auto deadline = GetTickCount64() + 12;
    const uintptr_t classes[] = {animator_class, behaviour_class};
    bool named[2]{};
    uintptr_t fields[2][64]{};
    for (unsigned type = 0; type < 2; ++type) {
        if (!read_bytes(classes[type], fields[type], sizeof(fields[type]))) { status = "class-unreadable"; return; }
        for (unsigned i = 0; i < 64; ++i) {
            if (type == 0 && fields[type][i] == behaviour_class) parent_offset = i * 8;
            const char* text = known_text(fields[type][i]);
            if (!text) continue;
            if (!std::strcmp(text, type ? "Behaviour" : "Animator")) named[type] = true;
            if (text_count < 32) texts[text_count++] = {type, i * 8, text};
        }
    }
    if (!named[0] || !named[1]) { status = "named-class-layout-unconfirmed"; return; }
    if (accessors_verified) { capture_named_methods(classes); return; }
    // Inspect at most eight entries of each candidate table; no layout assumed.
    for (unsigned type = 0; type < 2; ++type) for (unsigned i = 0; i < 64; ++i) {
        if (GetTickCount64() >= deadline) { status = "bounded-metadata-partial"; return; }
        uintptr_t candidates[8]{};
        if (!fields[type][i] || !read_bytes(fields[type][i], candidates, sizeof(candidates))) continue;
        for (unsigned j = 0; j < 8; ++j) {
            uintptr_t info[8]{};
            if (!candidates[j] || !read_bytes(candidates[j], info, sizeof(info))) continue;
            for (unsigned k = 0; k < 8; ++k) {
                const char* name = known_text(info[k]);
                if (!name || (std::strncmp(name, "get_", 4) && std::strncmp(name, "set_", 4)) || method_count >= 32) continue;
                auto& method = methods[method_count++]; method = {};
                method.type = type; method.table_offset = i * 8; method.index = j; method.name_offset = k * 8; method.name = name;
                for (unsigned n = 0; n < 8; ++n) if (executable(info[n], image_base)) {
                    method.code_offsets[method.code_count] = n * 8;
                    method.code_rvas[method.code_count++] = info[n] - image_base;
                }
            }
        }
    }
    status = parent_offset ? "bounded-metadata-observed" : "bounded-metadata-parent-unconfirmed";
}
static void report(std::ostream& out) {
    out << "{\"status\":\"" << status << "\",\"callable\":false,\"accessorsVerified\":" << (accessors_verified ? "true" : "false")
        << ",\"parentAccessorMatchesBehaviour\":" << (parent_matches ? "true" : "false")
        << ",\"methodQueryCount\":" << method_queries << ",\"methodOwnerMismatches\":" << owner_mismatches
        << ",\"componentStateObserved\":" << (state_observed ? "true" : "false")
        << ",\"componentEnabled\":" << (component_enabled ? "true" : "false") << ",\"animatorUpdateMode\":" << update_mode
        << ",\"declaredMethodCounts\":[" << declared_counts[0] << ',' << declared_counts[1]
        << "],\"parentFieldOffset\":" << parent_offset << ",\"namedFields\":[";
    for (unsigned i = 0; i < text_count; ++i) {
        if (i) out << ',';
        out << "{\"classIndex\":" << texts[i].type << ",\"offset\":" << texts[i].offset << ",\"text\":\"" << texts[i].text << "\"}";
    }
    out << "],\"methodCandidates\":[";
    for (unsigned i = 0; i < method_count; ++i) {
        if (i) out << ',';
        const auto& m = methods[i];
        out << "{\"classIndex\":" << m.type << ",\"tableOffset\":" << m.table_offset << ",\"index\":" << m.index
            << ",\"nameOffset\":" << m.name_offset << ",\"name\":\"" << m.name
            << "\",\"parameterCount\":" << m.parameter_count << ",\"codeCandidates\":[";
        for (unsigned j = 0; j < m.code_count; ++j) {
            if (j) out << ',';
            out << "{\"fieldOffset\":" << m.code_offsets[j] << ",\"rva\":" << m.code_rvas[j] << '}';
        }
        out << "]}";
    }
    out << "]}";
}
}
