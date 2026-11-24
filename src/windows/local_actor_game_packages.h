#pragma once
// Included inside local_actors; this path is reachable only in the template experiment.
struct PackageLease { uint32_t key=0; uintptr_t bundle_identity=0; };
static PackageLease package_leases[3072]{};
static unsigned package_lease_count=0;
static uintptr_t loaded_bundle_identity(void* loaded) {
    void* asset=nullptr;
    return loaded&&read_bytes(reinterpret_cast<uintptr_t>(loaded)+0x18,&asset,8)?identity(asset):0;
}
static bool retain_package(void* manager,uint32_t key) {
    for (unsigned i=0;i<package_lease_count;++i) if (package_leases[i].key==key) return true;
    if (package_lease_count>=3072) return false;
    using Lookup=void*(__fastcall*)(void*,uint64_t);
    using Retain=void(__fastcall*)(void*,uint32_t,int);
    auto lookup=reinterpret_cast<Lookup>(entry("GameBundleLookup",resource_base));
    auto retain=reinterpret_cast<Retain>(entry("GamePackageRetain",resource_base));
    void* loaded=lookup(manager,key);int before=0,after=0;
    const auto native=loaded_bundle_identity(loaded);
    if (!retain||!native||!read_bytes(reinterpret_cast<uintptr_t>(loaded)+0x20,&before,4)||before<0||before>=INT_MAX) return false;
    retain(manager,key,1);
    // Record ownership immediately after the call so failures remain visible.
    package_leases[package_lease_count++]={key,native};state.package_leases=package_lease_count;
    return read_bytes(reinterpret_cast<uintptr_t>(loaded)+0x20,&after,4)&&after==before+1;
}
static void release_packages() {
    if (!package_lease_count) return;
    uintptr_t root=0,proxy=0,services=0,manager=0;
    if (!read_bytes(resource_base+0x5783de8,&root,8)||!root||!read_bytes(root+0x74a0,&proxy,8)||!proxy||
        !read_bytes(proxy+0x10,&services,8)||!services||!read_bytes(services+0x40,&manager,8)||!manager) return;
    using Lookup=void*(__fastcall*)(void*,uint64_t);
    using Release=void(__fastcall*)(void*,uint32_t);
    auto lookup=reinterpret_cast<Lookup>(entry("GameBundleLookup",resource_base));
    auto release=reinterpret_cast<Release>(entry("GamePackageRelease",resource_base));
    if (!lookup||!release) return;
    __try {
        while (package_lease_count) {
            const auto lease=package_leases[package_lease_count-1];
            void* loaded=lookup(reinterpret_cast<void*>(manager),lease.key);int refs=0;
            if (loaded_bundle_identity(loaded)!=lease.bundle_identity||!read_bytes(reinterpret_cast<uintptr_t>(loaded)+0x20,&refs,4)||refs<=0) {
                strcpy_s(state.detail,"game-package-release-identity-refused");break;
            }
            release(reinterpret_cast<void*>(manager),lease.key);
            package_leases[--package_lease_count]={};++state.package_leases_released;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode();strcpy_s(state.detail,"game-package-release-exception"); }
    state.package_leases=package_lease_count;publish(package_lease_count?"package-release-incomplete":"packages-released");
}
static const gimmd::actor_resource_ids::Entry* game_resource_id(const gimmd::actor_catalog::Entry& source) {
    for (const auto& item:gimmd::actor_resource_ids::Entries)
        if (item.hash==source.hash && !std::strcmp(item.bundle,source.bundle) && !std::strcmp(item.model,source.name)) return &item;
    return nullptr;
}
static void* game_package(const gimmd::actor_resource_ids::Entry& source) {
    state.resource_id=(static_cast<uint64_t>(source.hash)<<32)|source.package;
    state.package_was_resident=false;state.package_load_attempted=false;state.package_load_succeeded=false;
    state.dependency_count=state.dependency_packages_loaded=0;
    uintptr_t root=0,proxy=0,services=0,bundles=0;
    if (!read_bytes(resource_base+0x5783de8,&root,8)||!root||!read_bytes(root+0x74a0,&proxy,8)||!proxy||
        !read_bytes(proxy+0x10,&services,8)||!services||!read_bytes(services+0x40,&bundles,8)||!bundles) {
        strcpy_s(state.detail,"game-resource-manager-unavailable");return nullptr;
    }
    using Lookup=void*(__fastcall*)(void*,uint64_t);
    using Ensure=bool(__fastcall*)(void*,uint32_t);
    const auto lookup=reinterpret_cast<Lookup>(entry("GameBundleLookup",resource_base));
    const auto ensure=reinterpret_cast<Ensure>(entry("GameEnsurePackage",resource_base));
    if (!lookup||!ensure) { strcpy_s(state.detail,"game-package-interfaces-refused");return nullptr; }
    const uint64_t id=(static_cast<uint64_t>(source.hash)<<32)|source.package;
    void* loaded=lookup(reinterpret_cast<void*>(bundles),id);
    state.package_was_resident=loaded!=nullptr;
    // Match the game's dependency expansion and reverse load order. Copy keys before
    // returning its pooled list; never retain managed pool objects between callbacks.
    uintptr_t catalog=0;
    using Dependencies=void(__fastcall*)(void**,uint64_t,void*,bool);
    using ReturnList=void(__fastcall*)(void**);
    const auto dependencies=reinterpret_cast<Dependencies>(entry("GameDependencyList",resource_base));
    const auto return_list=reinterpret_cast<ReturnList>(entry("GameDependencyListReturn",resource_base));
    if (!dependencies||!return_list||!read_bytes(services+0x70,&catalog,8)||!catalog) {
        strcpy_s(state.detail,"game-dependencies-unavailable");return nullptr;
    }
    void* list=nullptr;dependencies(&list,id,reinterpret_cast<void*>(catalog),true);
    int dependency_count=0;uintptr_t dependency_array=0;uint64_t array_capacity=0;uint32_t dependency_keys[1024]{};
    const bool list_valid=list && read_bytes(reinterpret_cast<uintptr_t>(list)+0x18,&dependency_count,4) &&
        dependency_count>0 && dependency_count<=1024 && read_bytes(reinterpret_cast<uintptr_t>(list)+0x10,&dependency_array,8) &&
        dependency_array && read_bytes(dependency_array+24,&array_capacity,8) && array_capacity>=static_cast<uint64_t>(dependency_count) &&
        read_bytes(dependency_array+32,dependency_keys,static_cast<size_t>(dependency_count)*4);
    if (list) return_list(&list);
    if (!list_valid) { strcpy_s(state.detail,"game-dependency-list-refused");return nullptr; }
    state.dependency_count=static_cast<unsigned>(dependency_count);
    for (int i=dependency_count-1;i>=0;--i) {
        if (dependency_keys[i]==source.package) continue;
        if (!lookup(reinterpret_cast<void*>(bundles),dependency_keys[i])) {
            mutated.store(true);
            if (!ensure(reinterpret_cast<void*>(services),dependency_keys[i])) { strcpy_s(state.detail,"game-dependency-load-refused");return nullptr; }
            ++state.dependency_packages_loaded;
        }
        if (!retain_package(reinterpret_cast<void*>(bundles),dependency_keys[i])) { strcpy_s(state.detail,"game-dependency-retain-refused");return nullptr; }
    }
    if (!loaded) {
        state.package_load_attempted=true;
        strcpy_s(state.detail,"game-package-load");mutated.store(true);
        if (!ensure(reinterpret_cast<void*>(services),source.package)) { strcpy_s(state.detail,"game-package-load-refused");return nullptr; }
        state.package_load_succeeded=true;
        loaded=lookup(reinterpret_cast<void*>(bundles),id);
    }
    char type[96]{};void* asset=nullptr;
    if (!loaded||!class_name(loaded,type)||std::strcmp(type,"LoadedBundle")||
        !read_bytes(reinterpret_cast<uintptr_t>(loaded)+0x18,&asset,8)||!asset||
        !class_name(asset,type)||std::strcmp(type,"AssetBundle")||!identity(asset)) {
        strcpy_s(state.detail,"game-loaded-bundle-refused");return nullptr;
    }
    if (!retain_package(reinterpret_cast<void*>(bundles),source.package)) { strcpy_s(state.detail,"game-package-retain-refused");return nullptr; }
    wchar_t observed[96]{},expected[96]{};
    for (unsigned i=0;i<95&&source.bundle[i];++i) expected[i]=static_cast<unsigned char>(source.bundle[i]);
    if (!hierarchy::string_read(hierarchy::name_a(asset),observed)||std::wcscmp(observed,expected)) {
        strcpy_s(state.detail,"game-package-name-mismatch");return nullptr;
    }
    return asset;
}
