#pragma once
// Included inside resource_trace. Fixed buffers, no file IO inside engine hooks.
static uintptr_t pipeline_base = 0;
struct PipelineEvent {
    std::atomic<bool> ready{false}; const char* kind = "";
    DWORD thread = 0; ULONGLONG started = 0, finished = 0;
    uintptr_t object = 0, result = 0, template_data = 0;
    uint64_t arg1 = 0, arg2 = 0, arg3 = 0; uint32_t option1 = 0, option2 = 0;
    char path[384]{}, object_name[96]{}, result_name[96]{};
    uintptr_t frames[16]{}; unsigned frame_count = 0;
};
static PipelineEvent pipeline_events[4096];
static std::atomic<unsigned> pipeline_calls{0};
static bool ascii_copy(uintptr_t source, char* output, unsigned capacity) {
    if (!source || capacity<2) return false;
    for (unsigned i=0;i<capacity-1;) {
        const unsigned chunk=std::min(16u,capacity-1-i);
        if (!read_bytes(source+i,output+i,chunk)) return false;
        for (unsigned j=0;j<chunk;++j) if (!output[i+j]) return true;
        i+=chunk;
    }
    output[capacity-1]=0; return false;
}
static void native_name(uintptr_t object,char (&output)[96]) {
    if (!object || GetCurrentThreadId()!=window_thread) return;
    __try {
        uintptr_t table=0,getter=0;
        if (!read_bytes(object,&table,8) || !table || !read_bytes(table+0x58,&getter,8) || !executable(getter,pipeline_base)) return;
        using Getter=const char*(__fastcall*)(void*);
        const auto text=reinterpret_cast<Getter>(getter)(reinterpret_cast<void*>(object));
        ascii_copy(reinterpret_cast<uintptr_t>(text),output,96);
    } __except(EXCEPTION_EXECUTE_HANDLER) { output[0]=0; }
}
static PipelineEvent* pipeline_begin(const char* kind,uintptr_t object=0) {
    if (!recording.load()) return nullptr;
    const auto index=pipeline_calls.fetch_add(1);
    if (index>=std::size(pipeline_events)) return nullptr;
    auto& row=pipeline_events[index]; row.kind=kind; row.object=object;
    row.thread=GetCurrentThreadId(); row.started=GetTickCount64();
    void* frames[16]{};
    const auto length=CaptureStackBackTrace(1,16,frames,nullptr);
    for (unsigned i=0;i<length;++i) {
        const auto address=reinterpret_cast<uintptr_t>(frames[i]);
        if (address>=pipeline_base && address-pipeline_base<0x21000000)
            row.frames[row.frame_count++]=address-pipeline_base;
    }
    return &row;
}
static void pipeline_end(PipelineEvent* row,uintptr_t result=0,bool names=true) {
    if (!row) return;
    row->result=result; row->finished=GetTickCount64();
    if (names) { native_name(row->object,row->object_name); native_name(result,row->result_name); }
    row->ready.store(true);
}
using FileLoadFn=void*(__fastcall*)(const char*,uint64_t,uint32_t,uint32_t);
using MemoryLoadFn=void*(__fastcall*)(void*,uint64_t,uint64_t,uint64_t,uint32_t,uint32_t);
using CacheLoadFn=void*(__fastcall*)(void*,uint32_t,void*);
using TemplateLoadFn=void(__fastcall*)(void*);
using InstanceFn=void*(__fastcall*)(void*);
using RawCloneFn=void(__fastcall*)(void*,void*);
static FileLoadFn file_original=nullptr;
static MemoryLoadFn memory_original=nullptr;
static CacheLoadFn cache_original=nullptr;
static TemplateLoadFn template_original=nullptr;
static InstanceFn instance_original=nullptr;
static RawCloneFn raw_original=nullptr;
static void* __fastcall observe_file(const char* path,uint64_t offset,uint32_t crc,uint32_t options) {
    active.fetch_add(1); auto* row=pipeline_begin("bundle-file");
    if (row) { row->arg1=offset;row->option1=crc;row->option2=options;ascii_copy(reinterpret_cast<uintptr_t>(path),row->path,384); }
    void* result=file_original(path,offset,crc,options);
    pipeline_end(row,reinterpret_cast<uintptr_t>(result));active.fetch_sub(1);return result;
}
static void* __fastcall observe_memory(void* data,uint64_t size,uint64_t offset,uint64_t length,uint32_t option1,uint32_t option2) {
    active.fetch_add(1);auto* row=pipeline_begin("bundle-memory");
    if (row) { row->arg1=size;row->arg2=offset;row->arg3=length;row->option1=option1;row->option2=option2; }
    void* result=memory_original(data,size,offset,length,option1,option2);
    pipeline_end(row,reinterpret_cast<uintptr_t>(result));active.fetch_sub(1);return result;
}
static void* __fastcall observe_cache(void* bundle,uint32_t hash,void* type) {
    active.fetch_add(1);auto* row=pipeline_begin("instance-cache",reinterpret_cast<uintptr_t>(bundle));
    if (row) row->arg1=hash;
    void* result=cache_original(bundle,hash,type);
    pipeline_end(row,reinterpret_cast<uintptr_t>(result));active.fetch_sub(1);return result;
}
static void __fastcall observe_template(void* cache) {
    active.fetch_add(1);auto* row=pipeline_begin("load-template",reinterpret_cast<uintptr_t>(cache));
    template_original(cache);
    if (row && cache) { uintptr_t spawner=0; if (read_bytes(reinterpret_cast<uintptr_t>(cache)+0x88,&spawner,8) && spawner) read_bytes(spawner+0x58,&row->template_data,8); }
    pipeline_end(row);active.fetch_sub(1);
}
static void* __fastcall observe_instance(void* cache) {
    active.fetch_add(1);auto* row=pipeline_begin("instance",reinterpret_cast<uintptr_t>(cache));
    void* result=instance_original(cache);
    pipeline_end(row,reinterpret_cast<uintptr_t>(result));active.fetch_sub(1);return result;
}
static void __fastcall observe_raw(void* data,void* vector) {
    active.fetch_add(1);auto* row=pipeline_begin("template-clone");
    if (row) row->template_data=reinterpret_cast<uintptr_t>(data);
    raw_original(data,vector);
    if (row && vector) read_bytes(reinterpret_cast<uintptr_t>(vector)+16,&row->arg1,8);
    // Raw output is not finalized: never call native or managed object APIs here.
    pipeline_end(row,0,false);active.fetch_sub(1);
}
using MhyFileFn=void*(__fastcall*)(void*,const char*,uint64_t,uint32_t);
using AsyncTemplateFn=uint32_t(__fastcall*)(void*);
using ProviderFn=uint32_t(__fastcall*)(void*,uint32_t,void*);
static MhyFileFn mhy_file_original=nullptr;
static AsyncTemplateFn async_template_original=nullptr;
static RawCloneFn finalize_original=nullptr;
static ProviderFn provider_original=nullptr;
static void* __fastcall observe_mhy_file(void* manager,const char* path,uint64_t offset,uint32_t option) {
    active.fetch_add(1);auto* row=pipeline_begin("mhy-bundle-file");
    if (row) { row->arg1=offset;row->option2=option;ascii_copy(reinterpret_cast<uintptr_t>(path),row->path,384); }
    void* result=mhy_file_original(manager,path,offset,option);
    pipeline_end(row,reinterpret_cast<uintptr_t>(result));active.fetch_sub(1);return result;
}
static uint32_t __fastcall observe_async_template(void* cache) {
    active.fetch_add(1);auto* row=pipeline_begin("load-template-async",reinterpret_cast<uintptr_t>(cache));
    const auto result=async_template_original(cache);
    if (row && cache) {
        uintptr_t spawner=0;
        if (read_bytes(reinterpret_cast<uintptr_t>(cache)+0x88,&spawner,8) && spawner) {
            row->arg2=spawner;read_bytes(spawner+0x58,&row->template_data,8);
        }
    }
    if (row) row->arg1=result;pipeline_end(row,0);
    active.fetch_sub(1);return result;
}
static void __fastcall observe_finalize(void* data,void* vector) {
    active.fetch_add(1);auto* row=pipeline_begin("template-finalize");
    if (row) row->template_data=reinterpret_cast<uintptr_t>(data);
    finalize_original(data,vector);
    uintptr_t root=0,items=0;
    if (row && vector && read_bytes(reinterpret_cast<uintptr_t>(vector)+16,&row->arg1,8) && row->arg1 &&
        read_bytes(reinterpret_cast<uintptr_t>(vector),&items,8) && items) read_bytes(items,&root,8);
    pipeline_end(row,root);active.fetch_sub(1);
}
static void managed_class_label(uintptr_t object,char (&output)[96]) {
    uintptr_t klass=0,text=0;
    if (object && read_bytes(object,&klass,8) && klass && read_bytes(klass+0x28,&text,8)) ascii_copy(text,output,96);
}
static uint32_t __fastcall observe_provider(void* self,uint32_t hash,void* method) {
    active.fetch_add(1);auto* row=pipeline_begin("role-provider",reinterpret_cast<uintptr_t>(self));
    if (row) {
        row->arg1=hash;managed_class_label(row->object,row->object_name);
        if (!std::strcmp(row->object_name,"LoadedBundle")) {
            uintptr_t klass=0;
            if (read_bytes(row->object,&klass,8)) hierarchy::actor_survey::provider_class_pending.store(klass);
        }
        uintptr_t bundle=0;
        if (self && read_bytes(row->object+0x18,&bundle,8) && bundle) read_bytes(bundle+16,&row->arg2,8);
        if (self) read_bytes(row->object+0x10,&row->arg3,8);
    }
    const auto result=provider_original(self,hash,method);
    if (row) { row->option1=result;native_name(static_cast<uintptr_t>(row->arg2),row->result_name); }
    pipeline_end(row,0,false);active.fetch_sub(1);return result;
}
using ReadyFn=bool(__fastcall*)(void*);
static ReadyFn ready_original=nullptr;
static std::atomic<uintptr_t> ready_spawners[256]{};
static bool __fastcall observe_ready(void* spawner) {
    active.fetch_add(1);const bool result=ready_original(spawner);
    if (result && recording.load()) {
        const auto identity=reinterpret_cast<uintptr_t>(spawner);
        for (auto& observed:ready_spawners) {
            uintptr_t value=observed.load();
            if (value==identity) break;
            if (!value && observed.compare_exchange_strong(value,identity)) {
                auto* row=pipeline_begin("template-ready",identity);
                if (row) read_bytes(identity+0x58,&row->template_data,8);
                pipeline_end(row,0,false);break;
            }
            if (value==identity) break;
        }
    }
    active.fetch_sub(1);return result;
}
struct PipelineHook { uintptr_t rva; const uint8_t* expected; void* detour; void** trampoline; uintptr_t target=0; bool enabled=false; const char* status="disabled"; };
static PipelineHook pipeline_hooks[] = {
    {0x19d8ba0,FileLoadBytes,reinterpret_cast<void*>(&observe_file),reinterpret_cast<void**>(&file_original)},
    {0x19d9030,MemoryLoadBytes,reinterpret_cast<void*>(&observe_memory),reinterpret_cast<void**>(&memory_original)},
    {0x19d95d0,CacheLoadBytes,reinterpret_cast<void*>(&observe_cache),reinterpret_cast<void**>(&cache_original)},
    {0x19d9660,TemplateLoadBytes,reinterpret_cast<void*>(&observe_template),reinterpret_cast<void**>(&template_original)},
    {0x19d97c0,InstanceBytes,reinterpret_cast<void*>(&observe_instance),reinterpret_cast<void**>(&instance_original)},
    {0x19e2510,RawCloneBytes,reinterpret_cast<void*>(&observe_raw),reinterpret_cast<void**>(&raw_original)},
    {0x19f3dd0,MhyFileBytes,reinterpret_cast<void*>(&observe_mhy_file),reinterpret_cast<void**>(&mhy_file_original)},
    {0x19d9880,AsyncTemplateBytes,reinterpret_cast<void*>(&observe_async_template),reinterpret_cast<void**>(&async_template_original)},
    {0x19e2d10,FinalizeBytes,reinterpret_cast<void*>(&observe_finalize),reinterpret_cast<void**>(&finalize_original)},
    {0x6c1e400,ProviderBytes,reinterpret_cast<void*>(&observe_provider),reinterpret_cast<void**>(&provider_original)},
    {0x19e0b90,ReadyBytes,reinterpret_cast<void*>(&observe_ready),reinterpret_cast<void**>(&ready_original)}
};
static void pipeline_start(uintptr_t base) {
    pipeline_base=base;
    for (auto& hook:pipeline_hooks) {
        uint8_t actual[32]{};hook.target=base+hook.rva;hook.status="entry-rejected";
        if (!executable(hook.target,base) || !read_bytes(hook.target,actual,32) || std::memcmp(actual,hook.expected,32)) continue;
        hook.status="create-failed";
        if (MH_CreateHook(reinterpret_cast<void*>(hook.target),hook.detour,hook.trampoline)!=MH_OK) continue;
        hook.enabled=MH_EnableHook(reinterpret_cast<void*>(hook.target))==MH_OK;
        hook.status=hook.enabled ? "recording":"enable-failed";
    }
}
static bool pipeline_disable() {
    bool success=true;
    for (auto& hook:pipeline_hooks) if (hook.enabled) {
        const auto result=MH_DisableHook(reinterpret_cast<void*>(hook.target));
        success=success && (result==MH_OK || result==MH_ERROR_DISABLED);
    }
    return success;
}
static bool pipeline_verify() {
    bool success=true;
    for (auto& hook:pipeline_hooks) if (hook.enabled) {
        uint8_t actual[32]{};
        const bool match=read_bytes(hook.target,actual,32) && !std::memcmp(actual,hook.expected,32);
        hook.status=match ? "restored":"restore-failed";success=success && match;
    }
    return success;
}
static void pipeline_json(std::ostream& out,const char* value) {
    wchar_t converted[384]{};
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,converted,384)) {
        for (unsigned i=0;i<383 && value[i];++i) converted[i]=static_cast<unsigned char>(value[i]);
    }
    hierarchy::json_string(out,converted);
}
static void pipeline_report(std::ostream& out) {
    out << ",\"pipelineCalls\":" << pipeline_calls.load() << ",\"pipelineCapacity\":" << std::size(pipeline_events) << ",\"pipelineHooks\":[";
    bool first=true;
    for (const auto& hook:pipeline_hooks) { out << (first ? "":",") << "{\"rva\":" << hook.rva << ",\"status\":\"" << hook.status << "\"}";first=false; }
    out << "],\"pipelineRecords\":[";first=true;
    for (const auto& row:pipeline_events) {
        if (!row.ready.load()) continue;
        out << (first ? "":",") << "{\"kind\":\"" << row.kind << "\",\"thread\":" << row.thread << ",\"startTick\":" << row.started << ",\"endTick\":" << row.finished
            << ",\"object\":" << row.object << ",\"result\":" << row.result << ",\"templateData\":" << row.template_data
            << ",\"arg1\":" << row.arg1 << ",\"arg2\":" << row.arg2 << ",\"arg3\":" << row.arg3 << ",\"option1\":" << row.option1 << ",\"option2\":" << row.option2 << ",\"path\":";
        pipeline_json(out,row.path);
        out << ",\"objectName\":";pipeline_json(out,row.object_name);
        out << ",\"resultName\":";pipeline_json(out,row.result_name);
        out << ",\"callerRvas\":[";
        for (unsigned i=0;i<row.frame_count;++i) out << (i ? ",":"") << row.frames[i];
        out << "]}";first=false;
    }
    out << ']';
}
