#pragma once
#include "generated/gi_hierarchy_profile.h"

// Included after the observer's checked read helpers. All engine calls run
// inside capture()'s SEH boundary, on its confirmed callback thread.
namespace hierarchy {
using NameFn = void*(__fastcall*)(void*);
using CountFn = int(__fastcall*)(void*);
using ChildFn = void*(__fastcall*)(void*, int);
using ParentFn = void*(__fastcall*)(void*);
using ObjectTransformFn = void*(__fastcall*)(void**, void*);
using ActiveFn = bool(__fastcall*)(void*);
struct Quat { float x, y, z, w; };
using RotationFn = void(__fastcall*)(Quat*, void*);
static NameFn name_a, name_b;
static CountFn child_count;
static ChildFn get_child;
static ParentFn get_parent;
static ObjectTransformFn object_transform;
static ParentFn component_game_object;
static ActiveFn get_active;
static RotationFn rotation_world, rotation_local;
// AvatarRoot also retains inactive party models during single-character playback.
// Capacity must not shrink when the optional actor/resource extensions are disabled.
constexpr unsigned MaxNodes = 4096;
constexpr unsigned MaxDepth = 24, MaxChildren = 128, MaxName = 96;
struct Node {
    void* object = nullptr;
    uintptr_t identity = 0;
    int parent = -1, children = 0;
    unsigned depth = 0;
    wchar_t name[MaxName]{};
    bool processed = false, name_valid = false, aliases_equal = false, parent_valid = false;
    bool owner_valid = false, active = false;
    bool rotation_readable = false, rotation_relation_valid = false;
    Quat world{}, local{};
};
static Node nodes[MaxNodes];
static unsigned count = 0;
static bool configured = false, attempted = false, root_name_valid = false, truncated = false;
static bool invalid_count = false, cycle = false, invalid_parent = false, invalid_name = false, timed_out = false;
static std::atomic<bool> published{false};
static const char* status = "not-attempted";

static bool string_read(void* value, wchar_t (&out)[MaxName]) {
    if (!value) return false;
    int length = 0;
    const auto address = reinterpret_cast<uintptr_t>(value);
    if (!read_bytes(address + 16, &length, sizeof(length)) || length < 0 || length >= static_cast<int>(MaxName)) return false;
    if (length && !read_bytes(address + 20, out, static_cast<size_t>(length) * sizeof(wchar_t))) return false;
    out[length] = 0;
    for (int i = 0; i < length; ++i) if (!out[i]) return false;
    return true;
}
static bool same_object(void* left, void* right) {
    if (left == right) return true;
    uintptr_t first = 0, second = 0;
    return left && right && read_bytes(reinterpret_cast<uintptr_t>(left) + 16, &first, sizeof(first)) &&
        read_bytes(reinterpret_cast<uintptr_t>(right) + 16, &second, sizeof(second)) && first && first == second;
}
static uintptr_t entry(const char* name, uintptr_t base) {
    for (const auto& item : gimmd::hierarchy_profile::Entries) {
        if (std::strcmp(item.name, name)) continue;
        uint8_t bytes[32]{};
        return executable(base + item.rva, base) && read_bytes(base + item.rva, bytes, sizeof(bytes)) &&
            !std::memcmp(bytes, item.bytes, sizeof(bytes)) ? base + item.rva : 0;
    }
    return 0;
}
static bool unit_quat(const Quat& q);
#include "pose_roundtrip.h"
#include "time_control.h"
#include "animator_observer.h"
#include "animator_lease.h"
#include "single_track_playback.h"
static void json_string(std::ostream& report, const wchar_t* text);
#include "actor_survey.h"
static void configure(uintptr_t base, const std::string& sha) {
    roundtrip::reset();
    time_control::configure(base);
    playback::configure(base);
    animator_observer::configure(base);
    animator_lease::configure(base);
    published.store(false); attempted = false; count = 0; root_name_valid = false; truncated = false;
    invalid_count = cycle = invalid_parent = invalid_name = timed_out = false;
    for (auto& node : nodes) node = {};
    configured = false; status = "profile-mismatch";
    if (sha != gimmd::hierarchy_profile::BuildSha256) return;
    name_a = reinterpret_cast<NameFn>(entry("NameA", base));
    name_b = reinterpret_cast<NameFn>(entry("NameB", base));
    child_count = reinterpret_cast<CountFn>(entry("ChildCount", base));
    get_child = reinterpret_cast<ChildFn>(entry("GetChild", base));
    get_parent = reinterpret_cast<ParentFn>(entry("GetParent", base));
    object_transform = reinterpret_cast<ObjectTransformFn>(entry("GameObjectTransform", base));
    component_game_object = reinterpret_cast<ParentFn>(entry("ComponentGameObject", base));
    get_active = reinterpret_cast<ActiveFn>(entry("GetActive", base));
    rotation_world = reinterpret_cast<RotationFn>(entry("RotationWorldCandidate", base));
    rotation_local = reinterpret_cast<RotationFn>(entry("RotationLocalCandidate", base));
    configured = name_a && name_b && child_count && get_child && get_parent && object_transform && component_game_object &&
        get_active && rotation_world && rotation_local;
    if (configured) roundtrip::configure(base);
    if (configured) status = "waiting-for-avatar-root";
}
static bool unit_quat(const Quat& q) {
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(norm) && std::abs(norm - 1) < 0.002f;
}
static bool rotation_relation(const Quat& parent, const Quat& local, const Quat& world) {
    if (!unit_quat(parent) || !unit_quat(local) || !unit_quat(world)) return false;
    const Quat product{
        parent.w*local.x + parent.x*local.w + parent.y*local.z - parent.z*local.y,
        parent.w*local.y - parent.x*local.z + parent.y*local.w + parent.z*local.x,
        parent.w*local.z + parent.x*local.y - parent.y*local.x + parent.z*local.w,
        parent.w*local.w - parent.x*local.x - parent.y*local.y - parent.z*local.z};
    const float dot = product.x*world.x + product.y*world.y + product.z*world.z + product.w*world.w;
    return std::isfinite(dot) && std::abs(std::abs(dot) - 1) < 0.0002f;
}
static unsigned capture_budget_ms = 12;
static void capture(void* root_object, unsigned time_budget_ms = 12) {
    capture_budget_ms = time_budget_ms;
    if (!configured || attempted || !root_object) return;
    attempted = true;
    status = "root-name-contract-failed";
    wchar_t first[MaxName]{}, second[MaxName]{}, known[MaxName]{};
    if (!string_read(find_string("AvatarRoot"), known) || std::wcscmp(known, L"AvatarRoot") ||
        !string_read(name_a(root_object), first) || !string_read(name_b(root_object), second) ||
        std::wcscmp(first, L"AvatarRoot") || std::wcscmp(first, second)) {
        published.store(true); return;
    }
    root_name_valid = true;
    status = "root-transform-contract-failed";
    void* native = nullptr;
    void* transform = nullptr;
    if (!read_bytes(reinterpret_cast<uintptr_t>(root_object) + 16, &native, sizeof(native)) || !native ||
        object_transform(&transform, native) != &transform || !transform ||
        !string_read(name_a(transform), first) || std::wcscmp(first, L"AvatarRoot")) {
        published.store(true); return;
    }
    nodes[0].object = transform;
    if (!read_bytes(reinterpret_cast<uintptr_t>(transform) + 16, &nodes[0].identity, sizeof(uintptr_t)) || !nodes[0].identity) {
        nodes[0].object = nullptr; nodes[0].identity = 0;
        published.store(true); return;
    }
    count = 1;
    const auto deadline = GetTickCount64() + time_budget_ms;
    // Breadth-first traversal retains pointers only within this callback.
    // The report exposes node indices, never object addresses.
    for (unsigned index = 0; index < count; ++index) {
        if (GetTickCount64() >= deadline) { timed_out = truncated = true; break; }
        auto& node = nodes[index];
        node.processed = true;
        wchar_t alternate[MaxName]{};
        node.name_valid = string_read(name_a(node.object), node.name);
        node.aliases_equal = node.name_valid && string_read(name_b(node.object), alternate) && !std::wcscmp(node.name, alternate);
        if (!node.name_valid || !node.aliases_equal) { invalid_name = true; continue; }
        void* owner = component_game_object(node.object);
        wchar_t owner_name[MaxName]{};
        node.owner_valid = owner && string_read(name_a(owner), owner_name) && !std::wcscmp(owner_name, node.name);
        if (!node.owner_valid) { invalid_name = true; continue; }
        node.active = get_active(owner);
        node.parent_valid = node.parent < 0 || same_object(get_parent(node.object), nodes[node.parent].object);
        if (!node.parent_valid) { invalid_parent = true; continue; }
        rotation_world(&node.world, node.object); rotation_local(&node.local, node.object);
        node.rotation_readable = unit_quat(node.world) && unit_quat(node.local);
        if (node.parent >= 0 && nodes[node.parent].rotation_readable)
            node.rotation_relation_valid = rotation_relation(nodes[node.parent].world, node.local, node.world);
        node.children = child_count(node.object);
        if (node.children < 0 || node.children > static_cast<int>(MaxChildren)) {
            invalid_count = truncated = true; continue;
        }
#if !defined(GIMMD_ACTOR_SURVEY) && !defined(GIMMD_LOCAL_ACTORS)
        // Single-character binding needs all active avatar bones, but no bones
        // below inactive party roots. Keep the roots for ambiguity checks.
        if (node.parent == 0 && !node.active && !std::wcsncmp(node.name, L"Avatar_", 7)) continue;
#endif
        if (node.depth >= MaxDepth) { truncated = truncated || node.children != 0; continue; }
        for (int child = 0; child < node.children; ++child) {
            if (GetTickCount64() >= deadline) { timed_out = truncated = true; break; }
            if (count == MaxNodes) { truncated = true; break; }
            void* object = get_child(node.object, child);
            if (!object) { invalid_parent = true; continue; }
            uintptr_t identity = 0;
            if (!read_bytes(reinterpret_cast<uintptr_t>(object) + 16, &identity, sizeof(identity)) || !identity) {
                invalid_parent = true; continue;
            }
            bool duplicate = false;
            for (unsigned previous = 0; previous < count; ++previous)
                if (nodes[previous].identity == identity) { duplicate = true; break; }
            if (duplicate) { cycle = true; continue; }
            auto& next = nodes[count++]; next.object = object;
            next.identity = identity;
            next.parent = static_cast<int>(index); next.depth = node.depth + 1;
        }
    }
    status = invalid_parent || invalid_count || invalid_name || cycle ? "hierarchy-contract-failed" :
        (truncated ? "hierarchy-partial" : "hierarchy-read-observed");
    roundtrip::run();
    animator_observer::capture();
    playback::bind();
    actor_survey::capture();
    for (unsigned i = 0; i < count; ++i) { nodes[i].object = nullptr; nodes[i].identity = 0; }
    published.store(true);
}
static void json_string(std::ostream& report, const wchar_t* text) {
    constexpr char digits[] = "0123456789abcdef";
    report << '"';
    for (; *text; ++text) {
        const auto value = static_cast<unsigned short>(*text);
        report << "\\u" << digits[value >> 12] << digits[(value >> 8) & 15]
            << digits[(value >> 4) & 15] << digits[value & 15];
    }
    report << '"';
}
static void report(std::ostream& out) {
    const bool ready = published.load();
    out << "{\"profileVerified\":" << (configured ? "true" : "false")
        << ",\"published\":" << (ready ? "true" : "false") << ",\"status\":\""
        << (ready ? status : (configured ? "not-published" : "profile-mismatch"))
        << "\",\"nodeLimit\":" << MaxNodes << ",\"depthLimit\":24,\"childLimit\":128,\"callbackBudgetMs\":" << capture_budget_ms
        << ",\"activeSemantics\":\"Fufu-GameObject.get_active; rendering-identity-unverified\"";
    if (ready) {
        out << ",\"rootNameValidated\":" << (root_name_valid ? "true" : "false")
            << ",\"truncated\":" << (truncated ? "true" : "false")
            << ",\"timedOut\":" << (timed_out ? "true" : "false") << ",\"nodes\":[";
        for (unsigned i = 0; i < count; ++i) {
            const auto& node = nodes[i];
            out << (i ? "," : "") << "{\"id\":" << i << ",\"parent\":" << node.parent
                << ",\"depth\":" << node.depth << ",\"childCount\":" << node.children
                << ",\"processed\":" << (node.processed ? "true" : "false")
                << ",\"nameValid\":" << (node.name_valid ? "true" : "false")
                << ",\"nameAliasesAgree\":" << (node.aliases_equal ? "true" : "false")
                << ",\"parentValidated\":" << (node.parent_valid ? "true" : "false") << ",\"name\":";
            json_string(out, node.name);
            out << ",\"ownerValidated\":" << (node.owner_valid ? "true" : "false")
                << ",\"upstreamActive\":" << (node.active ? "true" : "false")
                << ",\"rotationReadable\":" << (node.rotation_readable ? "true" : "false")
                << ",\"parentWorldTimesLocalMatchesWorld\":" << (node.rotation_relation_valid ? "true" : "false");
            if (node.rotation_readable) out << std::setprecision(9)
                << ",\"rotationWorld\":[" << node.world.x << ',' << node.world.y << ',' << node.world.z << ',' << node.world.w << ']'
                << ",\"rotationLocal\":[" << node.local.x << ',' << node.local.y << ',' << node.local.z << ',' << node.local.w << ']';
            out << '}';
        }
        out << ']';
    }
    out << ",\"actorSurvey\":";
    actor_survey::report(out);
    out << '}';
}
}
