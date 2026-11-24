#pragma once
// Included inside local_actors. Every engine pointer is used only in the current Unity callback.
struct NativeCloneVector { void* data = nullptr; uint32_t allocator = 0x48, padding = 0; uint64_t length = 0, capacity = 0; };
static_assert(offsetof(NativeCloneVector,length)==16 && offsetof(NativeCloneVector,capacity)==24);
using RawTemplateFn = void(__fastcall*)(void*, NativeCloneVector*);
using NativeFreeFn = void(__fastcall*)(void*, uint32_t);
static bool template_name(void* value, char (&output)[96]) {
    const auto address=reinterpret_cast<uintptr_t>(value);
    uintptr_t table=0, getter=0, blocks=0; uint64_t block_count=0; uint8_t ready=0;
    if (!value || !read_bytes(address+0x19,&ready,1) || !ready ||
        !read_bytes(address+0x20,&table,8) || !table || !read_bytes(table+0x28,&getter,8) || getter!=resource_base+0x12a8fb0 ||
        !read_bytes(table+0x40,&getter,8) || getter!=resource_base+0x976970 ||
        !read_bytes(address+0x38,&blocks,8) || !blocks || !read_bytes(address+0x48,&block_count,8) || !block_count || block_count>65536) return false;
    char bytes[1024]{}; unsigned copied=0;
    for (uint64_t i=0;i<std::min<uint64_t>(4,block_count);++i) {
        uintptr_t block=0;
        if (!read_bytes(blocks+i*8,&block,8)||!block||!read_bytes(block,bytes+copied,256)) break;
        copied+=256;
    }
    for (unsigned i=0;i+16<copied;++i) {
        const uint8_t wide_prefix[]={65,0,118,0,97,0,116,0,97,0,114,0,95,0};
        const bool narrow=!std::memcmp(bytes+i,"Avatar_",7), wide=!std::memcmp(bytes+i,wide_prefix,sizeof(wide_prefix));
        if (!narrow&&!wide) continue;
        char name[96]{}; unsigned size=0;
        for (;size<95&&i+size*(wide?2:1)<copied;++size) {
            const char ch=bytes[i+size*(wide?2:1)];
            if (wide&&(i+size*2+1>=copied||bytes[i+size*2+1])) break;
            if (!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_')) break;
            name[size]=ch;
        }
        if (std::strncmp(name,"Avatar_",7)) continue;
        strcpy_s(output,name); return true;
    }
    return false;
}
#include "local_actor_game_packages.h"
static void* find_cached_template(const char* selector, const gimmd::actor_catalog::Entry*& selected) {
    if (!resource_base) { strcpy_s(state.detail,"template-interfaces-unavailable"); return nullptr; }
    selected=nullptr;
    char suffix[96]{};sprintf_s(suffix,"_%s_Manekin",selector);
    for (const auto& item:gimmd::actor_catalog::Entries) {
        const size_t name_length=std::strlen(item.name),suffix_length=std::strlen(suffix);
        if (name_length>=suffix_length&&!std::strcmp(item.name+name_length-suffix_length,suffix)) { selected=&item;break; }
    }
    if (!selected) for (const auto& item:gimmd::actor_catalog::Entries) if (std::strstr(item.name,selector)) {
        if (selected) { strcpy_s(state.detail,"template-source-ambiguous"); return nullptr; }
        selected=&item;
    }
    if (!selected) { strcpy_s(state.detail,"template-source-not-catalogued"); return nullptr; }
    using Lookup=void*(__fastcall*)(void**,const char*,const char*,const char*);
    using Fn=void*(__fastcall*)(void*);
    void* selected_bundle=nullptr;
    if (const auto* resource=game_resource_id(*selected)) {
        selected_bundle=game_package(*resource);
        if (!selected_bundle) return nullptr;
    } else {
    void* klass=nullptr;
    if (reinterpret_cast<Lookup>(resource_base+0x14cfa10)(&klass,"UnityEngine.CoreModule.dll","UnityEngine","AssetBundle")!=&klass||!klass) return nullptr;
    void* type=reinterpret_cast<Fn>(resource_base+0x52f450)(reinterpret_cast<uint8_t*>(klass)+0x68);
    if (!type) return nullptr;
    void* array=reinterpret_cast<Fn>(resource_base+0x19ccad20)(type);
    uint64_t length=0;
    if (!array||!read_bytes(reinterpret_cast<uintptr_t>(array)+24,&length,8)||length>65536) return nullptr;
    const auto now=GetTickCount64();
    if (!search_deadline) search_deadline=now+4000;
    if (now>=search_deadline) { strcpy_s(state.detail,"template-search-timeout"); return nullptr; }
    if (search_total!=length) { search_cursor=0;search_total=length; }
    const auto deadline=now+12;
    for (uint64_t i=search_cursor;i<length;++i) {
        if (GetTickCount64()>=deadline) { strcpy_s(state.detail,"loaded-model-search-pending"); return nullptr; }
        search_cursor=i+1;
        void* object=nullptr; uintptr_t owner=0,native=0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(array)+32+i*8,&object,8)||!object||
            !read_bytes(reinterpret_cast<uintptr_t>(object),&owner,8)||owner!=reinterpret_cast<uintptr_t>(klass)||
            !(native=identity(object))) continue;
        wchar_t label[96]{};
        if (!hierarchy::string_read(hierarchy::name_a(object),label)) continue;
        wchar_t expected[96]{};
        for (unsigned n=0;n<95&&selected->bundle[n];++n) expected[n]=static_cast<unsigned char>(selected->bundle[n]);
        if (std::wcscmp(label,expected)) continue;
        selected_bundle=object;break;
    }
    }
    if (!selected_bundle) { strcpy_s(state.detail,"actor-resource-id-unknown");return nullptr; }
    {
        // Ask the engine to obtain the known role's cache, then load its template.
        // Cache/assets belong to the engine and are never stripped or destroyed.
        void* go_class=nullptr;
        if (reinterpret_cast<Lookup>(resource_base+0x14cfa10)(&go_class,"UnityEngine.CoreModule.dll","UnityEngine","GameObject")!=&go_class || !go_class) {
            strcpy_s(state.detail,"template-gameobject-type-refused");return nullptr;
        }
        void* go_type=reinterpret_cast<Fn>(resource_base+0x52f450)(reinterpret_cast<uint8_t*>(go_class)+0x68);
        using LoadCache=void*(__fastcall*)(void*,uint32_t,void*);
        const auto load_cache=reinterpret_cast<LoadCache>(entry("LoadInstanceCache",resource_base));
        const auto load_template=reinterpret_cast<void(__fastcall*)(void*)>(entry("LoadTemplate",resource_base));
        if (!go_type || !load_cache || !load_template) { strcpy_s(state.detail,"template-load-interfaces-refused");return nullptr; }
        strcpy_s(state.detail,"template-cache-load");
        mutated.store(true);
        void* managed_cache=load_cache(selected_bundle,selected->hash,go_type);
        char cache_class[96]{};
        if (!managed_cache || !class_name(managed_cache,cache_class) || std::strcmp(cache_class,"ObjectInstanceCache")) {
            strcpy_s(state.detail,"template-cache-load-refused");return nullptr;
        }
        const auto cache=identity(managed_cache);
        if (!cache) { strcpy_s(state.detail,"template-cache-native-refused");return nullptr; }
        strcpy_s(state.detail,"template-load");
        load_template(managed_cache);
        uintptr_t spawner=0,data=0;char model_name[96]{};
        if (!read_bytes(cache+0x88,&spawner,8)||!spawner||!read_bytes(spawner+0x58,&data,8)||!data||
            !template_name(reinterpret_cast<void*>(data),model_name)||std::strcmp(model_name,selected->name)) {
            strcpy_s(state.detail,"template-loaded-name-refused");return nullptr;
        }
        return reinterpret_cast<void*>(data);
    }
}
static bool native_game_object(uintptr_t native) {
    uint32_t tag=0;uintptr_t info=0,text=0;char name[64]{};
    return native&&read_bytes(native+0xc,&tag,4)&&
        read_bytes(resource_base+0x56c1c18+static_cast<uintptr_t>(tag>>21)*8,&info,8)&&info&&
        read_bytes(info+0x10,&text,8)&&text&&read_bytes(text,name,63)&&!std::strcmp(name,"GameObject");
}
static int template_exception(EXCEPTION_POINTERS* info) {
    state.exception=info->ExceptionRecord->ExceptionCode;
    const auto address=reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    state.exception_rva=address>=resource_base && address-resource_base<0x10000000 ? address-resource_base : 0;
    state.orphan=true;
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool instantiate_template(void* data,void* staging,void*& created) {
    NativeCloneVector vector{};bool valid=false;
    __try {
        uintptr_t descriptors=0,info=0,text=0;uint64_t size=0;char type_name[64]{};
        const auto address=reinterpret_cast<uintptr_t>(data);
        if (!read_bytes(address+0x60,&descriptors,8)||!descriptors||!read_bytes(address+0x70,&size,8)||!size||size>8192||
            !read_bytes(descriptors+8,&info,8)||!info||!read_bytes(info+0x10,&text,8)||!text||!read_bytes(text,type_name,63)||std::strcmp(type_name,"GameObject")) {
            strcpy_s(state.detail,"template-root-type-refused");return false;
        }
        strcpy_s(state.detail,"template-raw-clone");
        reinterpret_cast<RawTemplateFn>(entry("RawTemplateClone",resource_base))(data,&vector);
        state.template_objects=vector.length;
        uintptr_t root=0;
        if (!vector.data||!vector.length||vector.length>8192||!read_bytes(reinterpret_cast<uintptr_t>(vector.data),&root,8)||!native_game_object(root)) {
            if (vector.length) state.orphan=true;
            strcpy_s(state.detail,"template-output-refused");
        } else {
            // Same owned-root active byte cleared by Unity's reviewed CloneWithParent default-deactivate path.
            uint8_t active_self=0;SIZE_T written=0;const uint8_t inactive=0;
            const bool prepared=read_bytes(root+0x1f6,&active_self,1)&&active_self<=1&&
                WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(root+0x1f6),&inactive,1,&written)&&written==1;
            // Unity's public Instance bridge wraps only AFTER native finalization.
            // Never expose partially constructed objects to managed Transform APIs.
            if (prepared) {
                strcpy_s(state.detail,"template-finalize");
                reinterpret_cast<RawTemplateFn>(entry("FinishTemplateClone",resource_base))(data,&vector);
                strcpy_s(state.detail,"template-wrap");
                void* go=nullptr;
                if (wrap(&go,reinterpret_cast<void*>(root))==&go&&identity(go)==root) created=go;
            }
            if (!created) state.orphan=true;
            if (prepared&&created) {
                strcpy_s(state.detail,"template-parent");
                void* model=transform(created);
                if (model) parent(model,staging,false);
                valid=model&&hierarchy::same_object(hierarchy::get_parent(model),staging)&&!hierarchy::get_active(created);
            }
            if (!valid) strcpy_s(state.detail,"template-inactive-parent-refused");
        }
    } __except(template_exception(GetExceptionInformation())) {}
    if (vector.data&&!(vector.capacity&(uint64_t{1}<<63))) {
        __try { reinterpret_cast<NativeFreeFn>(entry("NativeVectorFree",resource_base))(vector.data,vector.allocator); }
        __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode();valid=false; }
    }
    return valid;
}
