#pragma once
// Display-only watermark Text lease. Original text is never written to logs.
namespace uid_text {
using Getter=void*(__fastcall*)(void*); using Setter=void(__fastcall*)(void*,void*);
static Getter get; static Setter set;
static bool requested=false,verified=false,captured=false,aborted=false,restored=false;
static std::atomic<bool> dirty{false};
static uintptr_t identity=0; static char replacement[257]{},baseline[257]{};
static unsigned calls=0,attempts=0; static DWORD exception=0; static ULONGLONG last=0;
static const char* status="disabled";
static void configure(uintptr_t base,const std::filesystem::path& directory) {
    requested=verified=captured=aborted=restored=false; dirty.store(false); identity=0; calls=attempts=0; exception=0; last=0; status="disabled";
    const auto path=directory/L"uid-text.txt";
    requested=std::filesystem::exists(path); gimmd::player_session::uid_state.store(1); status="uid-text-profile-unverified";
    if (!hierarchy::configured) return;
    get=reinterpret_cast<Getter>(hierarchy::entry("UITextGet",base)); set=reinterpret_cast<Setter>(hierarchy::entry("UITextSet",base));
    verified=get&&set&&hierarchy::entry("UITextStore",base)&&hierarchy::animator_observer::metadata::accessors_verified;
    if (!requested) { status="disabled"; gimmd::player_session::uid_state.store(0); return; }
    std::ifstream input(path,std::ios::binary|std::ios::ate); const auto size=input ? input.tellg():std::streampos(-1);
    if (size<0||size>256) { verified=false; return; }
    std::memset(replacement,0,sizeof(replacement)); input.seekg(0); input.read(replacement,size);
    wchar_t checked[129]{};
    if (!input||(size>0&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,replacement,int(size),checked,128))) verified=false;
    if (!verified) { gimmd::player_session::uid_state.store(3); return; }
    status="waiting-for-uid-text";
}
static bool text(void* component,char (&out)[257]) {
    wchar_t value[hierarchy::MaxName]{};
    return hierarchy::string_read(get(component),value)&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,out,257,nullptr,nullptr)>0;
}
static bool named_interface(void* component) {
    namespace md=hierarchy::animator_observer::metadata;
    uintptr_t klass=0;
    if (!read_bytes(reinterpret_cast<uintptr_t>(component),&klass,sizeof(klass))) return false;
    bool getter=false,setter=false;
    for (unsigned depth=0;klass&&depth<8;++depth) {
        uintptr_t table=0; uint16_t count=0;
        if (!read_bytes(klass + gimmd::hierarchy_profile::ClassMethodTable,&table,sizeof(table))||!table||!read_bytes(klass + gimmd::hierarchy_profile::ClassMethodCount,&count,sizeof(count))||count>384) return false;
        for (unsigned i=0;i<count;++i) {
            uintptr_t method=0,info[8]{}; char name[64]{};
            if (!read_bytes(table+i*8,&method,sizeof(method))||!method||!read_bytes(method,info,sizeof(info))||info[0]!=klass) return false;
            const auto name_ptr=reinterpret_cast<uintptr_t>(md::method_name(method));
            if (!name_ptr||!read_bytes(name_ptr,name,sizeof(name))||!std::memchr(name,0,sizeof(name))) continue;
            const auto parameters=reinterpret_cast<const uint8_t*>(info)[0x2e];
            if (!std::strcmp(name,"get_text")&&!parameters&&info[1]==reinterpret_cast<uintptr_t>(get)) getter=true;
            if (!std::strcmp(name,"set_text")&&parameters==1&&info[1]==reinterpret_cast<uintptr_t>(set)) setter=true;
        }
        if (getter&&setter) return true;
        klass=md::parent_get(klass);
    }
    return false;
}
static bool write(void* component,const char* value) {
    void* managed=find_string(value); if (!managed) return false;
    ++calls; set(component,managed); char observed[257]{};
    return text(component,observed)&&!std::strcmp(value,observed);
}
static void tick() {
    bool enabled=false; char updated[257]{};
    const bool live=gimmd::player_session::uid_snapshot(updated,&enabled);
    if (live&&enabled) { requested=true; strcpy_s(replacement,updated); }
    if ((!requested&&!dirty.load())||!verified) return;
    const bool stopping=(gimmd::player_session::dynamic_session ? gimmd::player_session::exit_requested.load():(hierarchy::playback::stop_requested.load()||hierarchy::playback::done.load()))||aborted||(live&&!enabled);
    if ((!dirty.load()&&stopping)||(!stopping&&(!gimmd::player_session::dynamic_session&&!hierarchy::playback::started.load()))) return;
    const auto now=GetTickCount64(); if (!stopping&&now-last<200) return; last=now;
    __try {
        // FufuLauncher's exact watermark path; profile/card UI is not targeted.
        void* go=find_object(find_string("/BetaWatermarkCanvas(Clone)/Panel/TxtUID"));
        uintptr_t native=0; void* transform=nullptr;
        if (!go||!read_bytes(reinterpret_cast<uintptr_t>(go)+16,&native,sizeof(native))||!native||
            hierarchy::object_transform(&transform,reinterpret_cast<void*>(native))!=&transform||!transform) {
            status="uid-object-not-found"; gimmd::player_session::uid_state.store(3); return;
        }
        void* component=hierarchy::animator_observer::get_component(transform,find_string("Text")); uintptr_t current=0;
        if (!component||!hierarchy::same_object(hierarchy::component_game_object(component),go)||
            !read_bytes(reinterpret_cast<uintptr_t>(component)+16,&current,sizeof(current))||!current||(captured&&identity!=current)) {
            status="uid-text-component-refused"; gimmd::player_session::uid_state.store(3); return;
        }
        if (stopping) {
            if (!dirty.load()||attempts>=8) return;
            ++attempts;
            if (write(component,baseline)) { dirty.store(false); restored=true; status="uid-text-restored"; gimmd::player_session::uid_state.store(4); }
            return;
        }
        if (!captured) {
            if (!named_interface(component)||!text(component,baseline)) { verified=false; status="uid-text-methods-unverified"; gimmd::player_session::uid_state.store(3); return; }
            captured=true; identity=current;
        }
        char observed[257]{};
        if (text(component,observed)&&!std::strcmp(observed,replacement)) return;
        restored=false; dirty.store(true);
        if (write(component,replacement)) { status="uid-text-applied"; gimmd::player_session::uid_state.store(2); }
        else { aborted=true; status="uid-text-write-not-confirmed"; gimmd::player_session::uid_state.store(5); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { exception=GetExceptionCode(); aborted=true; status="uid-text-interface-exception"; gimmd::player_session::uid_state.store(5); }
}
static void report(std::ostream& out) {
    out<<",\n  \"uidText\":{\"requested\":"<<(requested ? "true":"false")<<",\"status\":\""<<status<<"\",\"captured\":"<<(captured ? "true":"false")
        <<",\"restored\":"<<(restored ? "true":"false")<<",\"cleanupPending\":"<<(dirty.load() ? "true":"false")
        <<",\"setterCalls\":"<<calls<<",\"exception\":"<<exception<<",\"accountDataChanged\":false}";
}
}
