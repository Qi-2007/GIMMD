#pragma once
// Read-only candidate discovery. Method addresses here are evidence, never call targets.
namespace actor_survey {
#if defined(GIMMD_ACTOR_SURVEY) && !defined(GIMMD_OBSERVER_TEST)
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
struct Candidate { char owner[64]{}, name[64]{}; unsigned parameters = 0; uintptr_t rva = 0; };
static Candidate methods[2048]{};
static std::atomic<uintptr_t> provider_class_pending{0};
static uintptr_t provider_class_observed=0;
#include "resource_lookup_pins.h"
struct ResourceClass { const char* assembly; const char* space; const char* name; bool found = false; };
static ResourceClass resource_classes[] = {
    {"Assembly-CSharp.dll", "data", "AvatarExcelConfig"},
    {"Assembly-CSharp.dll", "data", "AvatarExcelConfigMgr"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "Material"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "Shader"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "Resources"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "GameObject"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "ObjectInstanceCache"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "TextAsset"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "AssetBundle"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "AssetBundleRequest"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "AssetBundleCreateRequest"},
    {"UnityEngine.CoreModule.dll", "UnityEngine", "AssetBundleInstanceRequest"},
    {"Assembly-CSharp.dll", "", "AssetBundleManager"},
    {"Assembly-CSharp.dll", "", "CustomAssetBundle"},
    {"Assembly-CSharp.dll", "", "UnityAssetBundle"},
    {"Assembly-CSharp.dll", "MoleMole", "AssetBundleExternalResourceProvider"},
    {"Assembly-CSharp.dll", "MoleMole", "AssetBundleManager"},
    {"Assembly-CSharp.dll", "MoleMole", "ResourceManager"},
    {"Assembly-CSharp.dll", "MoleMole", "ExcelConfigMgr"},
    {"Assembly-CSharp.dll", "MoleMole", "AvatarExcelConfig"},
    {"Assembly-CSharp.dll", "MoleMole.Config", "AvatarExcelConfig"},
    {"Assembly-CSharp.dll", "MoleMole.ExcelOutput", "AvatarExcelConfig"},
    {"Assembly-CSharp.dll", "MoleMole", "AssetBundleSettings"},
    {"Assembly-CSharp.dll", "MoleMole", "StreamPreloadConfig"}
};
static const char* resource_result = "not-attempted";
static const char* bundle_result = "not-attempted";
static unsigned bundle_count = 0, bundle_names = 0;
static char bundle_labels[128][96]{};
static void capture_bundles(void* klass, uintptr_t base) {
    using TypeFn = void*(__fastcall*)(void*);
    using FindFn = void*(__fastcall*)(void*);
    bundle_result = "type-unavailable";
    void* type = reinterpret_cast<TypeFn>(base + 0x52f450)(reinterpret_cast<uint8_t*>(klass) + 0x68);
    if (!type) return;
    void* array = reinterpret_cast<FindFn>(base + 0x19ccad20)(type);
    uint64_t length = 0;
    bundle_result = "array-rejected";
    if (!array || !read_bytes(reinterpret_cast<uintptr_t>(array) + 24, &length, 8) || length > 65536) return;
    bundle_count = static_cast<unsigned>(length);
    const auto deadline = GetTickCount64() + 12;
    for (unsigned i = 0; i < bundle_count && bundle_names < 128; ++i) {
        if (GetTickCount64() >= deadline) { bundle_result = "names-partial"; return; }
        void* object = nullptr; wchar_t label[96]{};
        if (!read_bytes(reinterpret_cast<uintptr_t>(array) + 32 + i * 8, &object, 8) || !object) continue;
        uintptr_t owner = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(object), &owner, 8) || owner != reinterpret_cast<uintptr_t>(klass)) continue;
        if (!string_read(name_a(object), label)) continue;
        auto& output = bundle_labels[bundle_names++];
        for (unsigned n = 0; n < 95 && label[n]; ++n) {
            const auto ch = label[n];
            output[n] = ch >= 32 && ch < 127 && ch != L'"' && ch != L'\\' ? static_cast<char>(ch) : '?';
        }
    }
    bundle_result = "bundle-names-observed";
}
static unsigned method_count = 0, queries = 0, renderers = 0;
static bool complete = false;
static std::atomic<bool> work_pending{false};
static const char* result = "not-attempted";
#include "resource_asset_links.h"
static const char* prefab_result = "not-attempted";
static unsigned object_count = 0, object_scanned = 0, prefab_count = 0;
static void* prefab_class = nullptr;
static uintptr_t prefab_base = 0;
static char prefab_labels[128][96]{};
static void capture_prefabs(void* klass, uintptr_t base) {
    using Fn = void*(__fastcall*)(void*);
    void* type = reinterpret_cast<Fn>(base + 0x52f450)(reinterpret_cast<uint8_t*>(klass) + 0x68);
    if (!type) { prefab_result = "type-unavailable"; return; }
    void* array = reinterpret_cast<Fn>(base + 0x19ccad20)(type);
    uint64_t length = 0;
    if (!array || !read_bytes(reinterpret_cast<uintptr_t>(array) + 24, &length, 8) || length > 262144) {
        prefab_result = "array-rejected"; return;
    }
    if (object_count && object_count != length) { object_scanned = prefab_count = 0; }
    object_count = static_cast<unsigned>(length);
    const auto deadline = GetTickCount64() + 24;
    prefab_result = "objects-observed";
    for (unsigned i = object_scanned; i < object_count && prefab_count < 128; ++i) {
        if (GetTickCount64() >= deadline) { prefab_result = "objects-partial"; return; }
        ++object_scanned;
        void* object = nullptr; wchar_t label[96]{};
        if (!read_bytes(reinterpret_cast<uintptr_t>(array) + 32 + i * 8, &object, 8) || !object) continue;
        uintptr_t native = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(object) + 16, &native, 8) || !native) continue;
        if (!string_read(name_a(object), label) || std::wcsncmp(label, L"Avatar_", 7)) continue;
        asset_links::observe_model(object, label);
        auto& output = prefab_labels[prefab_count++];
        for (unsigned n = 0; n < 95 && label[n]; ++n) {
            const auto ch = label[n];
            output[n] = ch >= 32 && ch < 127 && ch != L'"' && ch != L'\\' ? static_cast<char>(ch) : '?';
        }
    }
}
#include "resource_script_metadata.h"
static const char* text_result = "not-attempted";
static unsigned text_count = 0, text_scanned = 0, text_names = 0;
static char text_labels[256][96]{};
static void* text_class = nullptr;
static void capture_text_assets(uintptr_t base) {
    using Fn = void*(__fastcall*)(void*);
    void* type = reinterpret_cast<Fn>(base + 0x52f450)(reinterpret_cast<uint8_t*>(text_class) + 0x68);
    if (!type) { text_result = "type-unavailable"; return; }
    void* array = reinterpret_cast<Fn>(base + 0x19ccad20)(type);
    uint64_t length = 0;
    if (!array || !read_bytes(reinterpret_cast<uintptr_t>(array) + 24, &length, 8) || length > 262144) {
        text_result = "array-rejected"; return;
    }
    if (text_count && text_count != length) { text_scanned = text_names = 0; }
    text_count = static_cast<unsigned>(length);
    const auto deadline = GetTickCount64() + 12;
    text_result = "text-assets-observed";
    for (unsigned i = text_scanned; i < text_count; ++i) {
        if (GetTickCount64() >= deadline) { text_result = "text-assets-partial"; return; }
        ++text_scanned;
        void* object = nullptr; wchar_t label[96]{};
        if (!read_bytes(reinterpret_cast<uintptr_t>(array) + 32 + i * 8, &object, 8) || !object) continue;
        uintptr_t native = 0, klass = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(object), &klass, 8) || klass != reinterpret_cast<uintptr_t>(text_class)) continue;
        if (!read_bytes(reinterpret_cast<uintptr_t>(object) + 16, &native, 8) || !native) continue;
        if (!string_read(name_a(object), label) || text_names >= 256) continue;
        wchar_t lower[96]{};
        for (unsigned n = 0; n < 95 && label[n]; ++n) lower[n] = label[n] >= L'A' && label[n] <= L'Z' ? label[n] + (L'a' - L'A') : label[n];
        if (!std::wcsstr(lower, L"avatarexcel") && !std::wcsstr(lower, L"configavatar") &&
            !std::wcsstr(lower, L"assetbundle") && !std::wcsstr(lower, L"resourcemanager") &&
            !std::wcsstr(lower, L"preloadconfig") && !std::wcschr(lower, L'/') && !std::wcsstr(lower, L".bytes")) continue;
        script_metadata::capture(native, label, base);
        auto& output = text_labels[text_names++];
        std::memset(output, 0, sizeof(output));
        for (unsigned n = 0; n < 95 && label[n]; ++n) {
            const auto ch = label[n];
            output[n] = ch >= 32 && ch < 127 && ch != L'"' && ch != L'\\' ? static_cast<char>(ch) : '?';
        }
    }
}
static bool wanted(const char* name) {
    for (const char* value : {"Load", "LoadAsync", "LoadAsset", "LoadAssetAsync", "LoadAllAssets", "LoadFromFile",
        "LoadFromFileAsync", "GetAllLoadedAssetBundles", "GetAllAssetNames", "get_asset", "get_allAssets",
        "get_isDone", "get_mainAsset", "Unload", "Instantiate", "Internal_CloneSingle", "Internal_CloneSingleWithParent",
        "Destroy", "DestroyImmediate", "GetInstanceID", "op_Implicit", ".ctor", "SetActive",
        "AddComponent", "Internal_AddComponentWithType", "GetComponentsInternal", "SetParent",
        "get_sharedMesh", "set_sharedMesh", "get_bones", "set_bones", "get_rootBone", "set_rootBone",
        "get_sharedMaterials", "set_sharedMaterials", "get_localBounds", "set_localBounds"})
        if (!std::strcmp(name, value)) return true;
    return false;
}
static bool scan_class(uintptr_t klass, ULONGLONG deadline, bool all_methods = false) {
    using namespace animator_observer;
    for (unsigned depth = 0; klass && depth < 8; ++depth) {
        uintptr_t text = 0, table = 0; uint16_t length = 0; char owner[64]{};
        if (!read_bytes(klass + gimmd::hierarchy_profile::ClassName, &text, sizeof(text)) || !text ||
            !read_bytes(text, owner, sizeof(owner)) || !std::memchr(owner, 0, sizeof(owner)) ||
            !read_bytes(klass + gimmd::hierarchy_profile::ClassMethodTable, &table, sizeof(table)) ||
            !read_bytes(klass + gimmd::hierarchy_profile::ClassMethodCount, &length, sizeof(length)) || length > (all_methods ? 4096 : 1024) || (length && !table)) return false;
        for (const char* ch = owner; *ch; ++ch)
            if (!((*ch >= 'A' && *ch <= 'Z') || (*ch >= 'a' && *ch <= 'z') ||
                (*ch >= '0' && *ch <= '9') || *ch == '_' || *ch == '.' || *ch == '`')) return false;
        for (unsigned i = 0; i < length; ++i) {
            if (GetTickCount64() >= deadline) return false;
            uintptr_t item = 0, info[8]{}; char name[64]{};
            if (!read_bytes(table + i * 8, &item, sizeof(item)) || !item ||
                !read_bytes(item, info, sizeof(info)) || info[0] != klass) return false;
            const auto pointer = reinterpret_cast<uintptr_t>(metadata::method_name(item));
            if (!pointer || !read_bytes(pointer, name, sizeof(name)) || !std::memchr(name, 0, sizeof(name))) return false;
            if ((!all_methods && !wanted(name)) || !executable(info[1], metadata::image_base)) continue;
            const unsigned parameters = reinterpret_cast<const uint8_t*>(info)[0x2e];
            const auto rva = info[1] - metadata::image_base;
            bool duplicate = false;
            for (unsigned n = 0; n < method_count; ++n)
                if (methods[n].rva == rva && methods[n].parameters == parameters &&
                    !std::strcmp(methods[n].owner, owner) && !std::strcmp(methods[n].name, name)) duplicate = true;
            if (duplicate) continue;
            if (method_count == 2048) return false;
            auto& method = methods[method_count++];
            strcpy_s(method.owner, owner); strcpy_s(method.name, name);
            method.parameters = parameters; method.rva = rva;
        }
        const auto parent = metadata::parent_get(klass);
        if (parent == klass) return false;
        klass = parent;
    }
    return klass == 0;
}
struct DirectoryRow { uint32_t lookup_key=0,index=0,hash=0; uint64_t resource_id=0,block_offset=0; uint32_t file_key=0,file_type=0,block_index=0,block_category=0,block_bytes=0; wchar_t block_root[96]{}; bool block_found=false,persistent=false,found=false,matched=false; wchar_t bundle[96]{}; };
static DirectoryRow manager_directory[std::size(gimmd::actor_catalog::Entries)]{};
static const char* manager_directory_status="not-attempted";
static bool manager_directory_pending=false;
static uint64_t manager_directory_cursor=0;
static uintptr_t manager_dictionary_identity=0;
struct LoadedPackageRow { uint32_t key=0;wchar_t bundle[96]{}; };
static LoadedPackageRow loaded_packages[32768]{};
static unsigned loaded_package_count=0;static bool loaded_packages_truncated=false;
static void capture_manager_directory(uintptr_t base) {
    uintptr_t root=0,manager=0,services=0,bundles=0,dictionary=0,array=0;uint64_t size=0;
    manager_directory_status="manager-unavailable";
    loaded_package_count=0;loaded_packages_truncated=false;
    if (!read_bytes(base+0x5783de8,&root,8)||!root||!read_bytes(root+0x74a0,&manager,8)||!manager||
        !read_bytes(manager+0x10,&services,8)||!services||!read_bytes(services+0x40,&bundles,8)||!bundles||
        !read_bytes(bundles+0x18,&dictionary,8)||!dictionary||!read_bytes(dictionary+0x30,&array,8)||!array||
        !read_bytes(array+24,&size,8)||size>131072) return;
    for (unsigned i=0;i<std::size(manager_directory);++i) {
        auto& row=manager_directory[i];row={};const auto& item=gimmd::actor_catalog::Entries[i];
        row.hash=item.hash;
        for (unsigned n=0;item.bundle[n]>='0'&&item.bundle[n]<='9';++n) row.index=row.index*10+item.bundle[n]-'0';
    }
    manager_directory_status="scanning-actual-keys";manager_directory_pending=true;manager_directory_cursor=0;manager_dictionary_identity=array;
    const auto deadline=GetTickCount64()+12;
    for (const auto value:{manager,services,bundles}) {
        uintptr_t klass=0;
        if (GetTickCount64()>=deadline) break;
        if (read_bytes(value,&klass,8)&&klass) scan_class(klass,deadline,true);
    }
}
static void scan_manager_directory(uintptr_t base) {
    if (!manager_directory_pending) return;
    uintptr_t root=0,manager=0,services=0,bundles=0,dictionary=0,array=0;uint64_t size=0;
    if (!read_bytes(base+0x5783de8,&root,8)||!root||!read_bytes(root+0x74a0,&manager,8)||!manager||
        !read_bytes(manager+0x10,&services,8)||!services||!read_bytes(services+0x40,&bundles,8)||!bundles||
        !read_bytes(bundles+0x18,&dictionary,8)||!dictionary||!read_bytes(dictionary+0x30,&array,8)||!array||
        !read_bytes(array+24,&size,8)||size>131072) { manager_directory_pending=false;manager_directory_status="dictionary-unavailable";return; }
    if (manager_dictionary_identity!=array) { manager_dictionary_identity=array;manager_directory_cursor=0;loaded_package_count=0;loaded_packages_truncated=false; }
    const auto deadline=GetTickCount64()+8;
    struct DictionaryEntry { uint32_t hash,next,key,padding;uintptr_t value; };
    static_assert(sizeof(DictionaryEntry)==24);
    for (;manager_directory_cursor<size;++manager_directory_cursor) {
        if (GetTickCount64()>=deadline) return;
        DictionaryEntry item{};uintptr_t klass=0,text=0,asset=0,native=0;char type_name[96]{};
        if (!read_bytes(array+32+manager_directory_cursor*24,&item,sizeof(item))||!item.value||
            !read_bytes(item.value,&klass,8)||!klass||!read_bytes(klass + gimmd::hierarchy_profile::ClassName,&text,8)||!text||
            !read_bytes(text,type_name,95)||!std::memchr(type_name,0,95)||std::strcmp(type_name,"LoadedBundle")||
            !read_bytes(item.value+0x18,&asset,8)||!asset||!read_bytes(asset+16,&native,8)||!native) continue;
        wchar_t label[96]{};
        if (!string_read(name_a(reinterpret_cast<void*>(asset)),label)) continue;
        if (loaded_package_count<std::size(loaded_packages)) { auto& row=loaded_packages[loaded_package_count++];row.key=item.key;wcscpy_s(row.bundle,label); }
        else loaded_packages_truncated=true;
        for (unsigned i=0;i<std::size(manager_directory);++i) {
            wchar_t expected[96]{};const auto& source=gimmd::actor_catalog::Entries[i];
            for (unsigned n=0;n<95&&source.bundle[n];++n) expected[n]=static_cast<unsigned char>(source.bundle[n]);
            if (std::wcscmp(label,expected)) continue;
            auto& row=manager_directory[i];row.lookup_key=item.key;row.resource_id=(static_cast<uint64_t>(row.hash)<<32)|item.key;
            row.found=row.matched=true;wcscpy_s(row.bundle,label);
            if (!row.block_found) {
                uintptr_t catalog=0;
                if (read_bytes(services+0x70,&catalog,8) && catalog) {
                    using PackageQuery=bool(__fastcall*)(void*,uint32_t,void*);
                    using FileQuery=void*(__fastcall*)(void*,void*,uint32_t);
                    uint8_t package_info[32]{},file_info[32]{};
                    if (reinterpret_cast<PackageQuery>(base+0x710aa60)(reinterpret_cast<void*>(catalog),item.key,package_info)) {
                        std::memcpy(&row.file_key,package_info,4);
                        reinterpret_cast<FileQuery>(base+0x710ac70)(file_info,reinterpret_cast<void*>(catalog),row.file_key);
                        std::memcpy(&row.block_category,file_info,4);std::memcpy(&row.block_index,file_info+4,4);
                        uintptr_t path_string=0;int32_t offset=0;
                        std::memcpy(&path_string,file_info+8,8);std::memcpy(&row.file_type,file_info+16,4);
                        std::memcpy(&offset,package_info+8,4);std::memcpy(&row.block_bytes,package_info+12,4);
                        row.block_offset=offset>=0 ? static_cast<uint64_t>(offset):0;
                        row.persistent=file_info[20]!=0;
                        row.block_found=offset>=0 && row.block_index>0 && row.block_bytes>0 && row.block_category<100 && path_string && string_read(reinterpret_cast<void*>(path_string),row.block_root);
                    }
                }
            }
        }
    }
    manager_directory_pending=false;manager_directory_status="actual-keys-observed";
}
static void capture_resources() {
    const auto base = animator_observer::metadata::image_base;
    resource_result = "lookup-bytes-rejected";
    if (!resource_lookup_verified(base)) return;
    resource_result = "lookup-exception";
    __try {
        using Lookup = void*(__fastcall*)(void**, const char*, const char*, const char*);
        const auto lookup = reinterpret_cast<Lookup>(base + 0x14cfa10);
        capture_manager_directory(base);
        bool partial = false;
        const auto deadline = GetTickCount64() + 48;
        for (auto& item : resource_classes) {
            if (GetTickCount64() >= deadline) { partial = true; break; }
            void* klass = nullptr;
            if (lookup(&klass, item.assembly, item.space, item.name) != &klass) {
                resource_result = "lookup-return-rejected"; return;
            }
            item.found = klass != nullptr;
            if (klass && !scan_class(reinterpret_cast<uintptr_t>(klass), deadline, true)) {
                partial = true; break;
            }
        }
        void* bundle_class = nullptr;
        lookup(&bundle_class, "UnityEngine.CoreModule.dll", "UnityEngine", "AssetBundle");
        if (bundle_class) { asset_links::configure(base, bundle_class); capture_bundles(bundle_class, base); }
        void* object_class = nullptr;
        lookup(&object_class, "UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
        if (object_class) { prefab_class = object_class; prefab_base = base; capture_prefabs(object_class, base); }
        lookup(&text_class, "UnityEngine.CoreModule.dll", "UnityEngine", "TextAsset");
        if (text_class) capture_text_assets(base);
        resource_result = partial ? "metadata-partial" : "resource-methods-observed";
        work_pending.store(manager_directory_pending || !std::strcmp(prefab_result, "objects-partial") || !std::strcmp(text_result, "text-assets-partial") ||
            !std::strcmp(asset_links::status, "bundle-scan-pending"));
    } __except(EXCEPTION_EXECUTE_HANDLER) { resource_result = "lookup-exception"; }
}
static void tick() {
    if (!Enabled) return;
    __try {
        const auto provider_class=provider_class_pending.load();
        if (provider_class && provider_class!=provider_class_observed && animator_observer::metadata::accessors_verified) {
            if (scan_class(provider_class,GetTickCount64()+12,true)) provider_class_observed=provider_class;
        }
        if (prefab_class && !std::strcmp(prefab_result, "objects-partial")) capture_prefabs(prefab_class, prefab_base);
        if (text_class && !std::strcmp(text_result, "text-assets-partial")) capture_text_assets(prefab_base);
        asset_links::tick();
        scan_manager_directory(animator_observer::metadata::image_base);
        work_pending.store(manager_directory_pending || !std::strcmp(prefab_result, "objects-partial") || !std::strcmp(text_result, "text-assets-partial") ||
            !std::strcmp(asset_links::status, "bundle-scan-pending"));
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { prefab_result = "enumeration-exception"; asset_links::status = "asset-link-exception"; work_pending.store(false); }
}
static void capture() {
    if (!Enabled) return;
    method_count = queries = renderers = 0; complete = false;
    result = "incomplete-hierarchy-refused";
    if (std::strcmp(status, "hierarchy-read-observed")) return;
    result = "metadata-accessors-unavailable";
    if (!animator_observer::metadata::accessors_verified || !animator_observer::verified) return;
    capture_resources();
    const auto deadline = GetTickCount64() + 24;
    result = "method-survey-partial";
    uintptr_t klass = 0;
    if (!count || !read_bytes(reinterpret_cast<uintptr_t>(nodes[0].object), &klass, sizeof(klass)) ||
        !scan_class(klass, deadline)) return;
    void* owner = component_game_object(nodes[0].object);
    if (!owner || !read_bytes(reinterpret_cast<uintptr_t>(owner), &klass, sizeof(klass)) ||
        !scan_class(klass, deadline)) return;
    void* renderer_name = find_string("SkinnedMeshRenderer");
    if (!renderer_name) return;
    // Query only likely skin nodes; no activation, component creation, or model writes.
    for (unsigned i = 1; i < count; ++i) {
        if (std::wcscmp(nodes[i].name, L"Body") && std::wcscmp(nodes[i].name, L"Face") &&
            std::wcscmp(nodes[i].name, L"Hair")) continue;
        if (GetTickCount64() >= deadline || queries >= 128) return;
        ++queries;
        void* renderer = animator_observer::get_component(nodes[i].object, renderer_name);
        if (!renderer) continue;
        if (!same_object(component_game_object(renderer), component_game_object(nodes[i].object))) {
            result = "renderer-owner-refused"; return;
        }
        ++renderers;
        if (!read_bytes(reinterpret_cast<uintptr_t>(renderer), &klass, sizeof(klass)) || !scan_class(klass, deadline)) return;
    }
    complete = true; result = "read-only-candidates-observed";
}
static void report(std::ostream& out) {
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"status\":\"" << result
        << "\",\"complete\":" << (complete ? "true" : "false")
        << ",\"methodAbiVerified\":false,\"spawnEnabled\":false,\"rendererQueries\":" << queries
        << ",\"renderersObserved\":" << renderers << ",\"methods\":[";
    for (unsigned i = 0; i < method_count; ++i) {
        const auto& method = methods[i];
        out << (i ? "," : "") << "{\"class\":\"" << method.owner << "\",\"name\":\"" << method.name
            << "\",\"parameters\":" << method.parameters << ",\"codeRva\":" << method.rva << '}';
    }
    out << "],\"resourceLookupStatus\":\"" << resource_result << "\",\"resourceClasses\":[";
    for (unsigned i = 0; i < std::size(resource_classes); ++i) {
        const auto& item = resource_classes[i];
        out << (i ? "," : "") << "{\"assembly\":\"" << item.assembly << "\",\"name\":\""
            << item.name << "\",\"namespace\":\"" << item.space << "\",\"found\":" << (item.found ? "true" : "false") << '}';
    }
    out << "],\"bundleEnumeration\":{\"status\":\"" << bundle_result
        << "\",\"count\":" << bundle_count << ",\"names\":[";
    for (unsigned i = 0; i < bundle_names; ++i) out << (i ? "," : "") << '"' << bundle_labels[i] << '"';
    out << "]},\"prefabEnumeration\":{\"status\":\"" << prefab_result
        << "\",\"objects\":" << object_count << ",\"scanned\":" << object_scanned << ",\"names\":[";
    for (unsigned i = 0; i < prefab_count; ++i) out << (i ? "," : "") << '"' << prefab_labels[i] << '"';
    out << "]},\"textAssetEnumeration\":{\"status\":\"" << text_result
        << "\",\"objects\":" << text_count << ",\"scanned\":" << text_scanned << ",\"names\":[";
    for (unsigned i = 0; i < text_names; ++i) out << (i ? "," : "") << '"' << text_labels[i] << '"';
    out << "]}";
    out << ",\"managerDirectory\":{\"status\":\"" << manager_directory_status << "\",\"entries\":[";
    for (unsigned i=0;i<std::size(manager_directory);++i) {
        const auto& row=manager_directory[i];
        out << (i ? ",":"") << "{\"index\":" << row.index << ",\"lookupKey\":" << row.lookup_key << ",\"hash\":" << row.hash << ",\"resourceId\":" << row.resource_id
            << ",\"found\":" << (row.found ? "true":"false") << ",\"matched\":" << (row.matched ? "true":"false") << ",\"bundle\":";
        json_string(out,row.bundle);
        out << ",\"fileKey\":" << row.file_key << ",\"blockFound\":" << (row.block_found ? "true":"false") << ",\"blockIndex\":" << row.block_index
            << ",\"blockCategory\":" << row.block_category << ",\"blockOffset\":" << row.block_offset << ",\"blockBytes\":" << row.block_bytes << ",\"persistent\":" << (row.persistent ? "true":"false") << ",\"fileType\":" << row.file_type << ",\"blockRoot\":";
        json_string(out,row.block_root);out << '}';
    }
    out << "]}";
    out << ",\"loadedPackages\":{\"truncated\":" << (loaded_packages_truncated?"true":"false") << ",\"entries\":[";
    for (unsigned i=0;i<loaded_package_count;++i) { out << (i?",":"") << "{\"lookupKey\":" << loaded_packages[i].key << ",\"bundle\":";json_string(out,loaded_packages[i].bundle);out << '}'; }
    out << "]}";
    asset_links::report(out);
    script_metadata::report(out);
    out << "}";
}
}
