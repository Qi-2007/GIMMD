#pragma once
// Included inside local_actors. Engine-owned assets are retained by independent
// handles; only our visual clones are destroyed. Release handles after all clones.
#ifdef GIMMD_SDK_PREFABS
inline constexpr bool SdkPrefabsEnabled = true;
#else
inline constexpr bool SdkPrefabsEnabled = false;
#endif
static uint32_t sdk_handles[16]{};
static unsigned sdk_handle_count=0;
static void* sdk_load_source(const char* selector) {
    if (!SdkPrefabsEnabled || sdk_handle_count>=16) { strcpy_s(state.detail,"sdk-handle-limit-or-disabled"); return nullptr; }
    const char* model=nullptr;char suffix[96]{};sprintf_s(suffix,"_%s_Manekin",selector);
    for (const auto& item:gimmd::actor_catalog::Entries) {
        const auto n=std::strlen(item.name),s=std::strlen(suffix);
        if (n>=s && !std::strcmp(item.name+n-s,suffix)) { if(model) {strcpy_s(state.detail,"sdk-model-ambiguous");return nullptr;}model=item.name; }
    }
    if (!model) { strcpy_s(state.detail,"sdk-model-not-catalogued");return nullptr; }
    char folder[96]{};strcpy_s(folder,model);char* end=std::strstr(folder,"_Manekin");if(end)*end=0;
    using Load=void*(__fastcall*)(void*,uint32_t*);
    const auto load=reinterpret_cast<Load>(entry("SdkLoadIndependentObject",resource_base));
    if(!load) {strcpy_s(state.detail,"sdk-load-profile-refused");return nullptr;}
    const char* formats[]={"ART/Char/Avatar/%s/%s","ART/Char/Avatar/%s/Prefab/%s"};
    for(const auto format:formats) {
        sprintf_s(state.sdk_path,format,folder,model);
        void* path=find_string(state.sdk_path);if(!path)return nullptr;
        // Publish the output slot before entering the engine: a failure after it
        // writes the handle must still leave a cleanup obligation.
        auto& handle=sdk_handles[sdk_handle_count++];handle=0;state.sdk_handles=sdk_handle_count;
        mutated.store(true);void* asset=load(path,&handle);
        if(!handle) {--sdk_handle_count;state.sdk_handles=sdk_handle_count;}
        if(!asset) {if(handle)break;continue;}
        char type[96]{},actual[96]{};wchar_t wide[96]{};
        if(!handle||!class_name(asset,type)||std::strcmp(type,"GameObject")||
            !hierarchy::string_read(hierarchy::name_a(asset),wide)||!source_name(wide,actual)||std::strcmp(actual,model)) {
            strcpy_s(state.detail,"sdk-loaded-source-identity-refused");return nullptr;
        }
        strcpy_s(state.detail,"sdk-source-loaded-with-handle");return transform(asset);
    }
    strcpy_s(state.detail,"sdk-path-not-found");return nullptr;
}
static void sdk_release_handles() {
    if(!sdk_handle_count)return;
    using Dismiss=void(__fastcall*)(uint32_t,bool);
    using Lookup=bool(__fastcall*)(uint32_t,uint64_t*);
    auto dismiss=reinterpret_cast<Dismiss>(entry("SdkDismissExternal",resource_base));
    auto lookup=reinterpret_cast<Lookup>(entry("SdkTryGetAssetByHandle",resource_base));
    if(!dismiss||!lookup)return;
    __try {
        while(sdk_handle_count) {
            const auto handle=sdk_handles[sdk_handle_count-1];uint64_t asset=0;
            if(lookup(handle,&asset))dismiss(handle,false);
            asset=0;if(lookup(handle,&asset)) {strcpy_s(state.detail,"sdk-handle-release-pending");break;}
            sdk_handles[--sdk_handle_count]=0;++state.sdk_released;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {state.exception=GetExceptionCode();strcpy_s(state.detail,"sdk-release-exception");}
    state.sdk_handles=sdk_handle_count;publish(sdk_handle_count?"sdk-release-pending":"sdk-handles-released");
}
