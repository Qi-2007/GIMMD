#pragma once
#include "generated/gi_actor_profile.h"
#include "generated/gi_actor_catalog.h"
// Local visual copies only. Never activate or destroy a source game entity.
namespace local_actors {
#ifdef GIMMD_LOCAL_ACTORS
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
#ifdef GIMMD_ACTOR_TEMPLATES
inline constexpr bool TemplatesEnabled = true;
#else
inline constexpr bool TemplatesEnabled = false;
#endif
using CloneFn = void*(__fastcall*)(void*, void*, bool);
using DestroyFn = void(__fastcall*)(void*, bool);
using ActiveFn = void(__fastcall*)(void*, bool);
using ParentFn = void(__fastcall*)(void*, void*, bool);
using NameFn = void(__fastcall*)(void*, void*);
using WrapFn = void*(__fastcall*)(void**, void*);
using PositionFn = void(__fastcall*)(void*, const Vec3*);
using RotationFn = void(__fastcall*)(void*, const hierarchy::Quat*);
static CloneFn clone;
static DestroyFn destroy;
static ActiveFn activate;
static ParentFn parent;
static NameFn rename;
static WrapFn wrap;
static GetPositionFn position_get;
static PositionFn position_set;
static RotationFn rotation_set;
static GetTransformFn root_bone;
static GetTransformFn bones_get, mesh_get;
static hierarchy::animator_observer::metadata::EnabledGetFn animator_enabled;
static void(__fastcall* animator_enable)(void*, bool);
static bool configured = false;
static uintptr_t resource_base = 0;
static std::atomic<bool> dirty{false};
static std::atomic<bool> stop{false};
static std::atomic<bool> mutated{false};
struct Slot { uintptr_t identity = 0; char path[256]{}, source[96]{}; unsigned clone_material_mismatches = 0, stripped_material_mismatches = 0, material_slots = 0, material_renderers = 0, invalid_materials = 0, nodes = 0, skins = 0, removed = 0, checked_skins = 0, missing_meshes = 0; bool health_checked = false, healthy = false, template_source = false, sdk_source = false; };
struct State {
    unsigned hidden_renderers=0;
    unsigned sdk_handles=0,sdk_released=0;char sdk_path[256]{};
    Slot slots[3]{}, attempt{}; uint64_t acknowledged = 0;
    char status[96] = "disabled", detail[96] = "none";
    wchar_t failed_node[96]{}, failed_component[96]{};
    unsigned node_index = 0, component_index = 0, bone_count = 0;
    char removed_classes[64][96]{}; unsigned removed_class_count = 0;
    uintptr_t exception_rva = 0; uint64_t template_objects = 0;
    uint64_t resource_id = 0; bool package_was_resident = false, package_load_attempted = false, package_load_succeeded = false;
    unsigned dependency_count = 0, dependency_packages_loaded = 0;
    unsigned package_leases = 0, package_leases_released = 0;
    char shader_names[32][96]{};unsigned shader_name_count=0,shader_slots_checked=0,shader_invalid=0,shader_error_names=0;
    char motion_status[40]="idle";unsigned motion_slot=0,motion_tracks=0,motion_writes=0;double motion_seconds=0,motion_duration=0;
    unsigned motion_helpers=0,motion_helper_writes=0,motion_root_writes=0;double motion_root_offset=0;bool motion_root_available=false;
    DWORD exception = 0; bool orphan = false;
};
struct Request { uint64_t sequence = 0; unsigned op = 0; char source[64]{}; };
static State state{}, published{};
static Request request{}, pending_search{};
static uint64_t search_cursor = 0, search_total = 0;
static ULONGLONG search_deadline = 0;
static unsigned search_phase = 0;
static SRWLOCK lock = SRWLOCK_INIT;
static uint64_t worker_sequence = 0;
struct Node { void* transform = nullptr; uintptr_t identity = 0; wchar_t name[96]{}; int parent = -1; };
static Node tree[768]{};
static unsigned tree_count = 0;
static uintptr_t entry(const char* name, uintptr_t base) {
    for (const auto& item : gimmd::actor_profile::Entries) {
        if (std::strcmp(name, item.name)) continue;
        uint8_t bytes[32]{};
        return executable(base + item.rva, base) && read_bytes(base + item.rva, bytes, 32) &&
            !std::memcmp(bytes, item.bytes, 32) ? base + item.rva : 0;
    }
    return 0;
}
static uintptr_t identity(void* object) {
    uintptr_t value = 0;
    return object && read_bytes(reinterpret_cast<uintptr_t>(object) + 16, &value, sizeof(value)) ? value : 0;
}
static void publish(const char* status) {
    strcpy_s(state.status, status);
    bool any = state.hidden_renderers != 0 || state.orphan || state.package_leases != 0 || state.sdk_handles != 0; for (const auto& slot : state.slots) any = any || slot.identity != 0;
    dirty.store(any);
    AcquireSRWLockExclusive(&lock); published = state; ReleaseSRWLockExclusive(&lock);
}
static void configure(uintptr_t base, const std::string& sha) {
    state = {}; published = {}; request = {}; worker_sequence = 0; dirty.store(false); stop.store(false); mutated.store(false);
    configured = false; resource_base = 0; pending_search = {}; search_cursor = search_total = 0; search_deadline = 0; search_phase = 0;
    if (!Enabled || sha != gimmd::actor_profile::BuildSha256) { publish("disabled"); return; }
    for (const auto& item : gimmd::actor_profile::Entries) if (!entry(item.name, base)) { publish("profile-refused"); return; }
    clone = reinterpret_cast<CloneFn>(entry("CloneWithParent", base));
    destroy = reinterpret_cast<DestroyFn>(entry("DestroyImmediate", base));
    activate = reinterpret_cast<ActiveFn>(entry("SetActive", base));
    parent = reinterpret_cast<ParentFn>(entry("SetParent", base));
    rename = reinterpret_cast<NameFn>(entry("SetName", base));
    wrap = reinterpret_cast<WrapFn>(entry("NativeToManaged", base));
    root_bone = reinterpret_cast<GetTransformFn>(entry("RendererRootBoneGet", base));
    mesh_get = reinterpret_cast<GetTransformFn>(hierarchy::entry("RendererMeshGet", base));
    bones_get = reinterpret_cast<GetTransformFn>(hierarchy::entry("RendererBonesGet", base));
    position_get = reinterpret_cast<GetPositionFn>(hierarchy::entry("WorldPositionGet", base));
    position_set = reinterpret_cast<PositionFn>(hierarchy::entry("WorldPositionSet", base));
    rotation_set = reinterpret_cast<RotationFn>(hierarchy::entry("WorldRotationSet", base));
    animator_enabled = reinterpret_cast<hierarchy::animator_observer::metadata::EnabledGetFn>(hierarchy::entry("BehaviourEnabledGet", base));
    animator_enable = reinterpret_cast<decltype(animator_enable)>(hierarchy::entry("BehaviourEnabledSet", base));
    configured = mesh_get && hierarchy::entry("RendererMeshGetNative", base) && position_get && position_set && rotation_set && animator_enabled && animator_enable &&
        hierarchy::entry("BehaviourEnabledSetNative", base) && hierarchy::entry("BehaviourEnabledGetNative", base) &&
        hierarchy::entry("RendererBonesGet", base) && hierarchy::entry("RendererBonesGetNative", base);
    if (configured) resource_base = base; // Native material interfaces share the actor profile.
    publish(configured ? "ready" : "interfaces-refused");
}
static void* transform(void* go) {
    void* result = nullptr; const auto native = identity(go);
    return native && hierarchy::object_transform(&result, reinterpret_cast<void*>(native)) == &result ? result : nullptr;
}
static bool collect(void* root) {
    tree_count = 0;
    if (!root) return false;
    tree[0] = {}; tree[0].transform = root; tree[0].identity = identity(root); tree_count = 1;
    const auto deadline = GetTickCount64() + 48;
    for (unsigned i = 0; i < tree_count; ++i) {
        if (GetTickCount64() >= deadline || !tree[i].identity ||
            !hierarchy::string_read(hierarchy::name_a(tree[i].transform), tree[i].name)) return false;
        const int count = hierarchy::child_count(tree[i].transform);
        if (count < 0 || count > 128) return false;
        for (int child = 0; child < count; ++child) {
            if (tree_count == 768) return false;
            void* value = hierarchy::get_child(tree[i].transform, child);
            const auto native = identity(value);
            if (!native || !hierarchy::same_object(hierarchy::get_parent(value), tree[i].transform)) return false;
            for (unsigned n = 0; n < tree_count; ++n) if (tree[n].identity == native) return false;
            auto& next = tree[tree_count++]; next = {}; next.transform = value; next.identity = native; next.parent = static_cast<int>(i);
        }
    }
    return true;
}
static bool class_name(void* component, char (&name)[96]) {
    uintptr_t klass = 0, text = 0;
    return component && read_bytes(reinterpret_cast<uintptr_t>(component), &klass, 8) && klass &&
        read_bytes(klass + gimmd::hierarchy_profile::ClassName, &text, 8) && text && read_bytes(text, name, sizeof(name)) && std::memchr(name, 0, sizeof(name));
}
// The reviewed GetComponent native loop reads GO+0x40, count+0x48, 16-byte entries.
// Snapshot one node before removing components, then recheck every wrapper owner.
static bool components(void* go, void* (&out)[64], unsigned& count) {
    count = 0; const auto native = identity(go); uintptr_t vector = 0; unsigned length = 0;
    if (!native || !read_bytes(native + 0x40, &vector, 8) || !read_bytes(native + 0x48, &length, 4) ||
        !length || length > 64 || !vector) { strcpy_s(state.detail,"component-vector-header-refused"); return false; }
    for (unsigned i = 0; i < length; ++i) {
        uintptr_t component = 0;
        state.component_index = i;
        if (!read_bytes(vector + i * 16 + 8, &component, 8) || !component) { strcpy_s(state.detail,"component-native-entry-refused"); return false; }
        void* managed = nullptr;
        if (wrap(&managed, reinterpret_cast<void*>(component)) != &managed || !managed || identity(managed) != component ||
            !hierarchy::same_object(hierarchy::component_game_object(managed), go)) { strcpy_s(state.detail,"component-wrapper-owner-refused"); return false; }
        out[count++] = managed;
    }
    uintptr_t again = 0; unsigned repeated = 0;
    const bool stable = read_bytes(native + 0x40, &again, 8) && again == vector &&
        read_bytes(native + 0x48, &repeated, 4) && repeated == length;
    if (!stable) strcpy_s(state.detail,"component-vector-changed");
    return stable;
}
static bool owned_bone(void* bone) {
    const auto native = identity(bone);
    for (unsigned i = 0; native && i < tree_count; ++i) if (tree[i].identity == native) return true;
    return false;
}
// -1 means unreadable; 0 means removed; 1 means still registered on this GO.
// Do not dereference a destroyed component wrapper to infer its lifetime.
static int component_membership(void* go, uintptr_t wanted) {
    const auto native = identity(go); uintptr_t vector = 0; unsigned count = 0;
    if (!native || !wanted || !read_bytes(native+0x40,&vector,8) || !read_bytes(native+0x48,&count,4) ||
        count>64 || (count && !vector)) return -1;
    bool found = false;
    for (unsigned i=0;i<count;++i) {
        uintptr_t component = 0;
        if (!read_bytes(vector+i*16+8,&component,8)) return -1;
        found = found || component==wanted;
    }
    uintptr_t again=0; unsigned repeated=0;
    if (!read_bytes(native+0x40,&again,8) || again!=vector || !read_bytes(native+0x48,&repeated,4) || repeated!=count) return -1;
    return found ? 1:0;
}
static bool verify_skin(void* renderer) {
    void* bones = bones_get(renderer);
    void* root = root_bone(renderer);
    if (root && !owned_bone(root)) { strcpy_s(state.detail,"skin-root-bone-outside-clone"); return false; }
    uint64_t count = 0;
    if (!bones || !read_bytes(reinterpret_cast<uintptr_t>(bones) + 24, &count, 8) || count > 512) {
        strcpy_s(state.detail,"skin-bone-array-refused"); return false;
    }
    // Auxiliary skins may have no weighted bones. An empty valid array has no
    // external Transform references and must not reject the entire model.
    state.bone_count = static_cast<unsigned>(count);
    for (unsigned i = 0; i < count; ++i) {
        void* bone = nullptr;
        if (!read_bytes(reinterpret_cast<uintptr_t>(bones) + 32 + i * 8, &bone, 8) || !owned_bone(bone)) {
            strcpy_s(state.detail,"skin-weighted-bone-outside-clone"); return false;
        }
    }
    return true;
}
static bool strip(Slot& slot) {
    slot.nodes = tree_count;
    for (unsigned i = 0; i < tree_count; ++i) {
        state.node_index = i; wcscpy_s(state.failed_node,tree[i].name); state.failed_component[0] = 0;
        state.attempt = slot;
        void* go = hierarchy::component_game_object(tree[i].transform); void* list[64]{}; unsigned count = 0;
        if (!components(go, list, count)) return false;
        uintptr_t natives[64]{}; char names[64][96]{};
        // Snapshot names and identities before any destruction can invalidate a sibling wrapper.
        for (unsigned n = 0; n < count; ++n) {
            natives[n] = identity(list[n]);
            if (!natives[n] || !class_name(list[n],names[n])) { strcpy_s(state.detail,"component-class-name-refused"); return false; }
        }
        // ParticleSystem owns/depends on its renderer. Process the system first;
        // if it removes the renderer automatically, the second pass skips it.
        for (unsigned pass=0;pass<2;++pass) for (unsigned n = 0; n < count; ++n) {
            const char* name=names[n];
            const bool particle_renderer=!std::strcmp(name,"ParticleSystemRenderer");
            if ((pass==1)!=particle_renderer) continue;
            state.component_index = n;
            MultiByteToWideChar(CP_UTF8,0,name,-1,state.failed_component,96);
            const int member=component_membership(go,natives[n]);
            if (member<0) { strcpy_s(state.detail,"component-membership-unreadable"); return false; }
            if (!member) continue;
            if (!std::strcmp(name, "Transform") || !std::strcmp(name, "MeshRenderer") || !std::strcmp(name, "MeshFilter")) continue;
            if (!std::strcmp(name, "SkinnedMeshRenderer")) {
                if (!verify_skin(list[n])) return false;
                ++slot.skins; continue;
            }
            if (!std::strcmp(name, "Animator")) {
                animator_enable(list[n], false); if (animator_enabled(list[n])) { strcpy_s(state.detail,"animator-disable-readback-failed"); return false; }
                continue;
            }
            // Remove only components on our inactive clone. No source pointer enters this loop.
            bool recorded=false;
            for (unsigned k=0;k<state.removed_class_count;++k) recorded=recorded || !std::strcmp(state.removed_classes[k],name);
            if (!recorded && state.removed_class_count<64) strcpy_s(state.removed_classes[state.removed_class_count++],name);
            destroy(list[n], false);
            const int remaining=component_membership(go,natives[n]);
            if (remaining!=0) { strcpy_s(state.detail,remaining<0 ? "component-membership-unreadable":"component-still-registered-after-destroy"); return false; }
            ++slot.removed;
        }
    }
    state.attempt = slot;
    if (slot.skins < 2) { strcpy_s(state.detail,"too-few-skins"); return false; }
    strcpy_s(state.detail,"components-validated"); return true;
}
#include "local_actor_materials.h"
#include "face_motion.h"
static bool ascii_name(const wchar_t* input, char (&output)[96]) {
    unsigned i = 0;
    for (; input[i] && i < 95; ++i) {
        const auto ch = input[i];
        if (!((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_')) return false;
        output[i] = static_cast<char>(ch);
    }
    output[i] = 0; return input[i] == 0;
}
static bool source_name(const wchar_t* input, char (&output)[96]) {
    wchar_t name[96]{};
    if (wcsnlen_s(input, 96) >= 96) return false;
    wcscpy_s(name, input);
    if (auto suffix = std::wcsstr(name, L"(Clone)")) {
        if (suffix[7]) return false;
        *suffix = 0;
    }
    return ascii_name(name, output);
}
static bool model_root(void* model) {
    if (!collect(model)) return false;
    bool body = false, skeleton = false;
    for (unsigned i = 0; i < tree_count; ++i) {
        body = body || !std::wcscmp(tree[i].name, L"Body");
        skeleton = skeleton || !std::wcscmp(tree[i].name, L"Bip001");
    }
    return body && skeleton;
}
#include "sdk_prefab_loader.h"
#include "local_actor_templates.h"
static void* find_loaded_model(const char* selector) {
    if (!gimmd::actor_profile::ResourceLookupSupported) {
        strcpy_s(state.detail,"scene-model-sources-only"); return nullptr;
    }
    if (!resource_base || !selector[0]) return nullptr;
    using Lookup = void*(__fastcall*)(void**, const char*, const char*, const char*);
    using Fn = void*(__fastcall*)(void*);
    void* klass = nullptr;
    const auto lookup = reinterpret_cast<Lookup>(entry("ClassLookup",resource_base));
    if (lookup(&klass, "UnityEngine.CoreModule.dll", "UnityEngine", "GameObject") != &klass || !klass) return nullptr;
    void* type = reinterpret_cast<Fn>(entry("ReflectionType",resource_base))(reinterpret_cast<uint8_t*>(klass) + gimmd::actor_profile::ClassType);
    if (!type) return nullptr;
    void* array = reinterpret_cast<Fn>(entry("FindObjectsAll",resource_base))(type);
    uint64_t length = 0;
    if (!array || !read_bytes(reinterpret_cast<uintptr_t>(array) + 24, &length, 8) || length > 262144) return nullptr;
    const auto now = GetTickCount64();
    if (!search_deadline) search_deadline = now + 2000;
    if (now >= search_deadline) { strcpy_s(state.detail, "loaded-model-search-timeout"); return nullptr; }
    if (search_total != length) { search_cursor = 0; search_total = length; }
    const auto deadline = now + 12;
    for (uint64_t i = search_cursor; i < length; ++i) {
        if (GetTickCount64() >= deadline) { strcpy_s(state.detail, "loaded-model-search-pending"); return nullptr; }
        search_cursor = i + 1;
        void* object = nullptr; uintptr_t owner = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(array) + 32 + i * 8, &object, 8) || !object ||
            !identity(object) || !read_bytes(reinterpret_cast<uintptr_t>(object), &owner, 8) || owner != reinterpret_cast<uintptr_t>(klass)) continue;
        wchar_t label[96]{}; char name[96]{};
        if (!hierarchy::string_read(hierarchy::name_a(object), label) || std::wcsncmp(label, L"Avatar_", 7) ||
            !source_name(label, name) || !std::strstr(name, selector) || std::strstr(name, "Ragdoll")) continue;
        wcscpy_s(state.failed_node, label);
        void* model = transform(object);
        if (model && model_root(model)) { state.failed_node[0] = 0; return model; }
        strcpy_s(state.detail, "loaded-model-shape-refused");
    }
    strcpy_s(state.detail, "preview-character-before-spawn"); return nullptr;
}
static void* find_model(void* actor) {
    const int count = hierarchy::child_count(actor);
    for (int i = 0; i < count && i < 128; ++i) {
        void* offset = hierarchy::get_child(actor, i); wchar_t name[96]{};
        if (!hierarchy::string_read(hierarchy::name_a(offset), name) || std::wcscmp(name, L"OffsetDummy")) continue;
        const int children = hierarchy::child_count(offset);
        for (int n = 0; n < children && n < 128; ++n) {
            void* model = hierarchy::get_child(offset, n);
            if (!hierarchy::string_read(hierarchy::name_a(model), name) || std::wcsncmp(name, L"Avatar_", 7) || std::wcsstr(name, L"Clone")) continue;
            bool body = false, skeleton = false;
            const int size = hierarchy::child_count(model);
            for (int j = 0; j < size && j < 128; ++j) {
                wchar_t child[96]{};
                if (!hierarchy::string_read(hierarchy::name_a(hierarchy::get_child(model, j)), child)) return nullptr;
                body = body || !std::wcscmp(child, L"Body"); skeleton = skeleton || !std::wcscmp(child, L"Bip001");
            }
            if (body && skeleton) return model;
        }
    }
    return nullptr;
}
#include "actor_shooting.h"
#include "actor_driver_lease.h"
static void spawn(const Request& command) {
    if (state.orphan) { publish("rollback-failed-restart-game"); return; }
    state.removed_class_count=0;
    state.attempt = {}; state.exception = 0; state.exception_rva=0; state.template_objects=0; state.failed_node[0] = state.failed_component[0] = 0;
    state.node_index = state.component_index = state.bone_count = 0; strcpy_s(state.detail,"none");
    void* created = nullptr;
    const char* result = "source-not-found";
    unsigned slot_index = 3;
    for (unsigned i = 0; i < 3; ++i) if (!state.slots[i].identity) { slot_index = i; break; }
    if (slot_index == 3) { publish("actor-limit-3"); return; }
    __try {
        do {
            void* root_go = find_object(find_string("/EntityRoot/AvatarRoot")); void* root = transform(root_go);
            if (!root) break;
            void* source = nullptr; void* staging = nullptr; void* anchor = nullptr; unsigned active = 0;
            const int count = hierarchy::child_count(root);
            if (count < 0 || count > 128) break;
            for (int i = 0; i < count; ++i) {
                void* actor = hierarchy::get_child(root, i); wchar_t actor_name[96]{};
                if (!hierarchy::string_read(hierarchy::name_a(actor), actor_name) || std::wcsncmp(actor_name, L"Avatar_", 7)) continue;
                void* go = hierarchy::component_game_object(actor); const bool enabled = hierarchy::get_active(go);
                void* model = find_model(actor);
                if (enabled) { ++active; anchor = model; }
                else if (!staging) staging = actor;
                if (!model) continue;
                wchar_t model_name[96]{}; char ascii[96]{};
                if (!hierarchy::string_read(hierarchy::name_a(model), model_name) || !ascii_name(model_name, ascii)) continue;
                if ((!command.source[0] && enabled) || (command.source[0] && std::strstr(ascii, command.source))) source = model;
            }
            const bool force_template=command.op==4 || command.op==5;
            bool sdk_source=false;
            if (force_template) source=nullptr;
            if(SdkPrefabsEnabled && command.source[0]) {
                wchar_t selected_name[96]{};
                const bool preview_source=source && hierarchy::string_read(hierarchy::name_a(source),selected_name) && std::wcsstr(selected_name,L"_Manekin");
                if(force_template||preview_source) {source=sdk_load_source(command.source);sdk_source=true;}
            }
            void* cached_template = nullptr;
            const gimmd::actor_catalog::Entry* catalog_entry = nullptr;
            if (!force_template && !sdk_source && !source && command.source[0] && search_phase == 0) {
                source = find_loaded_model(command.source);
                if (!source && std::strcmp(state.detail,"loaded-model-search-pending")) {
                    search_phase=1; search_cursor=search_total=0; search_deadline=0;
                }
            }
            if(SdkPrefabsEnabled && !sdk_source && source && command.source[0]) {
                wchar_t selected_name[96]{};
                if(hierarchy::string_read(hierarchy::name_a(source),selected_name)&&std::wcsstr(selected_name,L"_Manekin")) {
                    source=sdk_load_source(command.source);sdk_source=true;
                }
            }
            if (TemplatesEnabled && !source && command.source[0] && (force_template || search_phase == 1)) cached_template = find_cached_template(command.source,catalog_entry);
            if (command.op==5 && cached_template) { result="actor-template-ready";strcpy_s(state.detail,"template-loaded-name-validated");break; }
            if(command.op==5&&sdk_source&&source) {result="actor-template-ready";break;}
            if(sdk_source&&!source) {result="sdk-resource-load-refused";break;}
            if (active != 1 || !anchor || !staging) { result = "need-one-active-and-inactive-staging"; break; }
            if (!source && !cached_template) {
                if (!std::strcmp(state.detail, "loaded-model-search-pending")) { pending_search = command; result = "searching-loaded-model"; }
                else result = "loaded-model-not-found";
                break;
            }
            if (hierarchy::get_active(hierarchy::component_game_object(staging))) break;
            Vec3 position{}; hierarchy::Quat rotation{};
            position_get(&position, anchor); hierarchy::rotation_world(&rotation, anchor);
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !hierarchy::unit_quat(rotation)) break;
            Slot candidate{}; candidate.sdk_source=sdk_source; wchar_t source_name[96]{};
            if (cached_template && catalog_entry) {
                strcpy_s(candidate.source,catalog_entry->name); candidate.template_source=true;
            } else if (!hierarchy::string_read(hierarchy::name_a(source), source_name) || !local_actors::source_name(source_name, candidate.source)) break;
            result = "clone-failed";
            if (!cached_template) {
                result="source-materials-refused";
                if (!capture_materials(source)) break;
            }
            mutated.store(true);
            if (cached_template) {
                result="template-clone-failed";
                if (!instantiate_template(cached_template,staging,created)) break;
                wchar_t actual_source[96]{}; char canonical[96]{};
                if (!hierarchy::string_read(hierarchy::name_a(created),actual_source)||!local_actors::source_name(actual_source,canonical)||std::strcmp(canonical,candidate.source)) {
                    result="template-name-readback-failed";break;
                }
            } else created = clone(hierarchy::component_game_object(source), staging, false);
            if (!created || !identity(created)) break;
            if (hierarchy::get_active(created)) { result = "inactive-clone-contract-failed"; break; }
            activate(created, false);
            char name[96]{}; sprintf_s(name, "GIMMDActor_%lu_%u", GetCurrentProcessId(), slot_index + 1);
            rename(created, find_string(name));
            wchar_t observed[96]{}; char observed_ascii[96]{};
            if (!hierarchy::string_read(hierarchy::name_a(created), observed) || !ascii_name(observed, observed_ascii) || std::strcmp(observed_ascii, name)) {
                result = "clone-name-readback-failed"; break;
            }
            void* model = transform(created);
            result = "clone-tree-refused"; if (!collect(model)) break;
            if (!cached_template) {
                result="clone-material-targets-refused";
                if (!match_material_targets(candidate)) break;
            }
            result = "clone-component-strip-refused";
            const bool stripped = strip(candidate); state.attempt = candidate;
            if (!stripped) break;
            if (!cached_template) {
                result="clone-material-restore-refused";
                if (!restore_materials(candidate)) break;
                state.attempt=candidate;
            }
            parent(model, root, false);
            if (!hierarchy::same_object(hierarchy::get_parent(model), root)) { result = "clone-parent-refused"; break; }
            const float distance = 2.0f * static_cast<float>(slot_index + 1);
            position.x += distance * (1 - 2 * (rotation.y * rotation.y + rotation.z * rotation.z));
            position.y += distance * 2 * (rotation.x * rotation.y + rotation.w * rotation.z);
            position.z += distance * 2 * (rotation.x * rotation.z - rotation.w * rotation.y);
            position_set(model, &position); rotation_set(model, &rotation);
            Vec3 observed_position{}; position_get(&observed_position, model);
            if (std::abs(observed_position.x-position.x)>0.01f || std::abs(observed_position.y-position.y)>0.01f ||
                std::abs(observed_position.z-position.z)>0.01f) { result = "clone-position-refused"; break; }
            activate(created, true);
            if (!hierarchy::get_active(created)) { result = "clone-activation-refused"; break; }
            candidate.identity = identity(created); sprintf_s(candidate.path, "/EntityRoot/AvatarRoot/%s", name);
            state.slots[slot_index] = candidate; created = nullptr; state.failed_node[0] = state.failed_component[0] = 0; result = "actor-created";
        } while (false);
    } __except (EXCEPTION_EXECUTE_HANDLER) { state.exception = GetExceptionCode(); result = "clone-engine-exception"; }
    // Same callback rollback, including exceptions before the clone receives a stable path.
    if (created) {
        __try { activate(created, false); destroy(created, false); if (identity(created)) { state.orphan = true; result = "rollback-failed-restart-game"; } }
        __except (EXCEPTION_EXECUTE_HANDLER) { state.exception = GetExceptionCode(); state.orphan = true; result = "rollback-failed-restart-game"; }
    }
    for (auto& node : tree) node = {};
    tree_count = 0; forget_materials(); publish(result);
}
#include "local_actor_motion.h"
static void clear() {
    if(camera_playback::actor_target) {camera_playback::release_actor();gimmd::player_session::camera_requested.store(false);}
    if (!actor_motion::restore()) return;
    const char* result = state.orphan ? "rollback-failed-restart-game" : "actors-cleared";
    __try {
        void* root = transform(find_object(find_string("/EntityRoot/AvatarRoot")));
        if (!root) { publish("waiting-for-avatar-root-cleanup"); return; }
        const int children = hierarchy::child_count(root);
        if (children < 0 || children > 128) { publish("actor-root-count-refused"); return; }
        for (auto& slot : state.slots) {
            if (!slot.identity) continue;
            void* go = nullptr;
            for (int i = 0; i < children; ++i) {
                void* child = hierarchy::get_child(root, i);
                void* owner = hierarchy::component_game_object(child);
                if (identity(owner) == slot.identity) { go = owner; break; }
            }
            if (!go || !identity(go)) { slot = {}; continue; } // Scene already destroyed it.
            if (identity(go) != slot.identity) { result = "actor-identity-changed"; slot = {}; continue; }
            wchar_t actual[96]{}; char ascii[96]{};
            const char* expected = std::strrchr(slot.path, '/');
            if (!expected || !hierarchy::string_read(hierarchy::name_a(go), actual) || !ascii_name(actual, ascii) ||
                std::strcmp(ascii, expected + 1)) { result = "actor-identity-changed"; slot = {}; continue; }
            activate(go, false); destroy(go, false);
            if (identity(go)) { result = "actor-destroy-pending"; continue; }
            slot = {};
            // Destroy mutates the root child list; restart enumeration for the next slot.
            return clear();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { state.exception = GetExceptionCode(); result = "actor-cleanup-exception"; }
    publish(result);
}
static void check_models() {
    const char* result = "actors-none";
    state.exception = 0;
    state.shader_name_count=state.shader_slots_checked=state.shader_invalid=state.shader_error_names=0;
    __try {
        void* root = transform(find_object(find_string("/EntityRoot/AvatarRoot")));
        if (!root) { publish("waiting-for-avatar-root"); return; }
        const int children = hierarchy::child_count(root);
        if (children < 0 || children > 128) { publish("actor-root-count-refused"); return; }
        bool any = false, valid = true;
        for (auto& slot : state.slots) {
            if (!slot.identity) continue;
            any = true; slot.health_checked = true; slot.healthy = false; slot.checked_skins = slot.missing_meshes = slot.invalid_materials = 0;
            const auto shader_invalid_before=state.shader_invalid,shader_errors_before=state.shader_error_names;
            void* model = nullptr;
            for (int i = 0; i < children; ++i) {
                void* child = hierarchy::get_child(root,i);
                if (identity(hierarchy::component_game_object(child)) == slot.identity) { model = child; break; }
            }
            if (!model || !hierarchy::get_active(hierarchy::component_game_object(model)) || !collect(model)) { valid = false; continue; }
            bool skin_valid = true;
            for (unsigned i = 0; i < tree_count; ++i) {
                void* list[64]{}; unsigned length = 0;
                if (!components(hierarchy::component_game_object(tree[i].transform),list,length)) { skin_valid = false; break; }
                for (unsigned j = 0; j < length; ++j) {
                    char name[96]{};
                    if (!class_name(list[j],name)) { skin_valid = false; break; }
                    if (!std::strcmp(name,"SkinnedMeshRenderer") || !std::strcmp(name,"MeshRenderer")) {
                        uintptr_t values[64]{}; unsigned material_length=0;
                        const auto getter=reinterpret_cast<MaterialsGetFn>(entry("SharedMaterialsGet",resource_base));
                        void* material_objects=getter?getter(list[j]):nullptr;
                        if (!material_array(material_objects,values,material_length)) ++slot.invalid_materials;
                        else {
                            using ShaderGet=void*(__fastcall*)(void*);
                            const auto shader_get=reinterpret_cast<ShaderGet>(entry("MaterialShaderGet",resource_base));
                            for (unsigned k=0;k<material_length;++k) {
                                void* material=nullptr;char material_type[96]{},shader_type[96]{};
                                if (!shader_get||!read_bytes(reinterpret_cast<uintptr_t>(material_objects)+32+k*8,&material,8)||
                                    !class_name(material,material_type)||std::strcmp(material_type,"Material")) { ++state.shader_invalid;continue; }
                                void* shader=shader_get(material);++state.shader_slots_checked;
                                if (!shader||!identity(shader)||!class_name(shader,shader_type)||std::strcmp(shader_type,"Shader")) { ++state.shader_invalid;continue; }
                                wchar_t wide_name[96]{};char shader_name[96]{};
                                if (!hierarchy::string_read(hierarchy::name_a(shader),wide_name)) { ++state.shader_invalid;continue; }
                                bool ascii_valid=true;for (unsigned n=0;n<95&&wide_name[n];++n) {
                                    if (wide_name[n]<32||wide_name[n]>126) { ascii_valid=false;break; } shader_name[n]=static_cast<char>(wide_name[n]);
                                }
                                if (!ascii_valid) { ++state.shader_invalid;continue; }
                                if (std::strstr(shader_name,"InternalError")||std::strstr(shader_name,"ErrorShader")) ++state.shader_error_names;
                                bool seen=false;for (unsigned n=0;n<state.shader_name_count;++n) seen=seen||!std::strcmp(state.shader_names[n],shader_name);
                                if (!seen&&state.shader_name_count<32) strcpy_s(state.shader_names[state.shader_name_count++],shader_name);
                            }
                        }
                    }
                    if (std::strcmp(name,"SkinnedMeshRenderer")) continue;
                    ++slot.checked_skins;
                    if (!identity(mesh_get(list[j]))) ++slot.missing_meshes;
                    if (!verify_skin(list[j])) skin_valid = false;
                }
            }
            slot.healthy = skin_valid && slot.checked_skins >= 2 && slot.missing_meshes == 0 && slot.invalid_materials == 0 &&
                state.shader_invalid==shader_invalid_before && state.shader_error_names==shader_errors_before;
            valid = valid && slot.healthy;
        }
        result = !any ? "actors-none" : (valid ? "actor-models-valid" : "actor-models-incomplete");
    } __except(EXCEPTION_EXECUTE_HANDLER) { state.exception = GetExceptionCode(); result = "actor-check-exception"; }
    for (auto& node : tree) node = {};
    tree_count = 0; publish(result);
}
static void tick() {
    if (!Enabled || !configured) return;
    if (stop.load() || gimmd::player_session::exit_requested.load()) {
        if(state.hidden_renderers) shooting::visibility(false);
        if (dirty.load()) clear();
        bool actors_remaining=state.orphan;for (const auto& slot:state.slots) actors_remaining=actors_remaining||slot.identity!=0;
        if (!actors_remaining) release_packages();
        if (!actors_remaining) sdk_release_handles();
        return;
    }
    Request command{};
    AcquireSRWLockExclusive(&lock); command = request; request = {}; ReleaseSRWLockExclusive(&lock);
    if (command.op) { pending_search = {}; search_cursor = search_total = 0; search_deadline = 0; search_phase = 0; }
    else { command = pending_search; pending_search = {}; }
    actor_motion::tick();
    if (!command.op) return;
    if(command.op>=11) {
        state.acknowledged=command.sequence;
        if(command.op==11||command.op==12) shooting::visibility(command.op==11);
        else if(command.op==15) {camera_playback::release_actor();gimmd::player_session::camera_requested.store(false);publish("actor-camera-released");}
        else shooting::camera(command.source[0]>='1'&&command.source[0]<='3'?command.source[0]-'0':0,command.op==14);
        return;
    }
    if (command.op>=6&&command.op<=10) {
        state.acknowledged=command.sequence;
        if (command.op==6) actor_motion::begin(command.source[0]>='1'&&command.source[0]<='3'?static_cast<unsigned>(command.source[0]-'1'):3);
        else if (command.op==7) actor_motion::pause(true);
        else if (command.op==8) actor_motion::pause(false);
        else if (command.op==9) actor_motion::restore();
        else publish("actor-motion-restore-before-play");
        return;
    }
    if (command.op == 2) { state.acknowledged = command.sequence; clear(); }
    else if (command.op == 3) { state.acknowledged = command.sequence; check_models(); }
    else if (hierarchy::playback::started.load() && !hierarchy::playback::done.load()) {
        state.acknowledged = command.sequence; publish("restore-animation-before-spawn");
    } else {
        spawn(command);
        if (!pending_search.op) {
            search_cursor = search_total = 0; search_deadline = 0; search_phase = 0;
            state.acknowledged = command.sequence;
            AcquireSRWLockExclusive(&lock); published = state; ReleaseSRWLockExclusive(&lock);
        }
    }
}
static void worker(const std::filesystem::path& directory) {
    if (!Enabled) return;
    std::ifstream input(directory / "actors.command", std::ios::binary | std::ios::ate);
    if (input && input.tellg() > 0 && input.tellg() < 128) {
        input.seekg(0); Request next{}; std::string op, source, extra;
        if (input >> next.sequence >> op) {
            if (op == "spawn") { input >> source; next.op = 1; }
            else if (op == "spawn-template" && (TemplatesEnabled||SdkPrefabsEnabled)) { input >> source; next.op=4; }
            else if (op == "load-template" && (TemplatesEnabled||SdkPrefabsEnabled)) { input >> source; next.op=5; }
            else if (op == "clear") next.op = 2;
            else if (op == "check") next.op = 3;
            else if(op=="hide-player")next.op=11;
            else if(op=="show-player")next.op=12;
            else if(op=="camera-move"){input>>source;next.op=13;}
            else if(op=="camera-follow"){input>>source;next.op=14;}
            else if(op=="camera-release")next.op=15;
            else if (op == "motion-play") { input >> source;next.op=6; }
            else if (op == "motion-pause") next.op=7;
            else if (op == "motion-resume") next.op=8;
            else if (op == "motion-restore") next.op=9;
            bool valid = source.size() < 64;
            for (const char ch : source) valid = valid && ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') || ch == '_');
            if (next.op && valid && !(input >> extra) && next.sequence > worker_sequence && next.sequence <= 0xffffffff) {
                input.close(); // Do not hold the command file open while parsing VMD.
                if (next.op==6&&!actor_motion::prepare(directory)) next.op=10;
                strcpy_s(next.source, source.c_str());
                AcquireSRWLockExclusive(&lock);
                if (!request.op) { request = next; worker_sequence = next.sequence; }
                ReleaseSRWLockExclusive(&lock);
            }
        }
    }
    State snapshot{}; AcquireSRWLockShared(&lock); snapshot = published; ReleaseSRWLockShared(&lock);
    const auto temporary = directory / "actors.status.tmp";
    std::ofstream out(temporary, std::ios::trunc);
    out << "{\"schemaVersion\":1,\"status\":\"" << snapshot.status << "\",\"acknowledgedSequence\":" << snapshot.acknowledged
        << ",\"exception\":" << snapshot.exception << ",\"orphanedObject\":" << (snapshot.orphan ? "true" : "false")
        << ",\"detail\":\"" << snapshot.detail << "\",\"failedNode\":";
    hierarchy::json_string(out,snapshot.failed_node);
    out<<",\"hiddenRenderers\":"<<snapshot.hidden_renderers;
    out<<",\"sdkResourceHandles\":"<<snapshot.sdk_handles<<",\"sdkHandlesReleased\":"<<snapshot.sdk_released<<",\"sdkPath\":\""<<snapshot.sdk_path<<"\"";
    out << ",\"removedClasses\":[";
    for (unsigned i=0;i<snapshot.removed_class_count;++i) out << (i ? ",":"") << '"' << snapshot.removed_classes[i] << '"';
    out << ']';
    out << ",\"actorMotion\":{\"status\":\"" << snapshot.motion_status << "\",\"slot\":" << snapshot.motion_slot
        << ",\"seconds\":" << snapshot.motion_seconds << ",\"duration\":" << snapshot.motion_duration << ",\"tracks\":" << snapshot.motion_tracks << ",\"writes\":" << snapshot.motion_writes
        << ",\"pelvisHelpers\":" << snapshot.motion_helpers << ",\"pelvisHelperWrites\":" << snapshot.motion_helper_writes
        << ",\"rootMotion\":" << (snapshot.motion_root_available?"true":"false") << ",\"rootWrites\":" << snapshot.motion_root_writes << ",\"maximumRootOffset\":" << snapshot.motion_root_offset << '}';
    out << ",\"exceptionRva\":" << snapshot.exception_rva << ",\"templateObjects\":" << snapshot.template_objects;
    out << ",\"resourceId\":" << snapshot.resource_id << ",\"packageWasResident\":" << (snapshot.package_was_resident ? "true" : "false")
        << ",\"packageLoadAttempted\":" << (snapshot.package_load_attempted ? "true" : "false") << ",\"packageLoadSucceeded\":" << (snapshot.package_load_succeeded ? "true" : "false");
    out << ",\"dependencyCount\":" << snapshot.dependency_count << ",\"dependencyPackagesLoaded\":" << snapshot.dependency_packages_loaded;
    out << ",\"packageLeases\":" << snapshot.package_leases << ",\"packageLeasesReleased\":" << snapshot.package_leases_released;
    out << ",\"shaderSlotsChecked\":" << snapshot.shader_slots_checked << ",\"invalidShaderReferences\":" << snapshot.shader_invalid
        << ",\"errorShaderNames\":" << snapshot.shader_error_names << ",\"shaderNames\":[";
    for (unsigned i=0;i<snapshot.shader_name_count;++i) { if (i) out << ','; wchar_t label[96]{};
        for (unsigned n=0;n<95&&snapshot.shader_names[i][n];++n) label[n]=static_cast<unsigned char>(snapshot.shader_names[i][n]);
        hierarchy::json_string(out,label);
    } out << ']';
    out << ",\"failedComponent\":"; hierarchy::json_string(out,snapshot.failed_component);
    out << ",\"nodeIndex\":" << snapshot.node_index << ",\"componentIndex\":" << snapshot.component_index
        << ",\"boneCount\":" << snapshot.bone_count << ",\"attemptNodes\":" << snapshot.attempt.nodes
        << ",\"attemptSkins\":" << snapshot.attempt.skins << ",\"attemptRemoved\":" << snapshot.attempt.removed << ",\"slots\":[";
    for (unsigned i = 0; i < 3; ++i) {
        const auto& slot = snapshot.slots[i];
        out << (i ? "," : "") << "{\"slot\":" << i+1 << ",\"created\":" << (slot.identity ? "true" : "false")
            << ",\"source\":\"" << slot.source << "\",\"sourceKind\":\"" << (slot.sdk_source ? "sdk-prefab":slot.template_source ? "template":"loaded-model") << "\",\"nodes\":" << slot.nodes << ",\"skins\":" << slot.skins
            << ",\"healthChecked\":" << (slot.health_checked ? "true":"false") << ",\"healthy\":" << (slot.healthy ? "true":"false")
            << ",\"checkedSkins\":" << slot.checked_skins << ",\"missingMeshes\":" << slot.missing_meshes
            << ",\"cloneMaterialMismatches\":" << slot.clone_material_mismatches << ",\"strippedMaterialMismatches\":" << slot.stripped_material_mismatches
            << ",\"materialRenderers\":" << slot.material_renderers << ",\"materialSlots\":" << slot.material_slots << ",\"invalidMaterials\":" << slot.invalid_materials
            << ",\"removedComponents\":" << slot.removed << '}';
    }
    out << "]}"; out.close();
    MoveFileExW(temporary.c_str(), (directory / "actors.status.json").c_str(), MOVEFILE_REPLACE_EXISTING);
}
}
