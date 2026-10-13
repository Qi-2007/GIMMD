#pragma once
// Experimental category-specific pause. Never mutes the process/device, so the
// independent WinMM dance audio is unaffected. Missing categories are refused.
namespace bgm_pause {
using Ready=int(__cdecl*)(); using Count=int(__cdecl*)();
using Info=int(__cdecl*)(uint16_t,void*); using Paused=int(__cdecl*)(uint32_t);
using Pause=void(__cdecl*)(uint32_t,int);
static Ready ready; static Count count; static Info info; static Paused paused; static Pause pause;
static std::atomic<bool> configured{false};
static bool attempted=false,acquired=false,restored=false;
static bool late_module_checked=false; static ULONGLONG next_module_check=0;
static std::atomic<bool> dirty{false}; static uint32_t category=0; static int before=0;
static unsigned calls=0,restore_attempts=0; static DWORD exception=0;
static const char* status="disabled"; static char category_name[64]{};
static void configure() {
    configured.store(false); attempted=acquired=restored=false; dirty.store(false); calls=restore_attempts=0; exception=0; category_name[0]=0;
    status=gimmd::player_session::pause_bgm ? "audio-engine-unavailable":"disabled";
    if (!gimmd::player_session::pause_bgm) return;
    auto module=GetModuleHandleW(L"cri_ware_unity.dll"); wchar_t path[32768]{};
    if (!module||!GetModuleFileNameW(module,path,32768)||image_hash(path)!="0A86D930601F04D106EA8C627A225F0C67E1B7A530650C89D4768242B375C645") {
        gimmd::player_session::bgm_state.store(3); return;
    }
    ready=reinterpret_cast<Ready>(GetProcAddress(module,"criAtomEx_IsInitialized"));
    count=reinterpret_cast<Count>(GetProcAddress(module,"criAtomExAcf_GetNumCategories"));
    info=reinterpret_cast<Info>(GetProcAddress(module,"criAtomExAcf_GetCategoryInfo"));
    paused=reinterpret_cast<Paused>(GetProcAddress(module,"criAtomExCategory_IsPausedById"));
    pause=reinterpret_cast<Pause>(GetProcAddress(module,"criAtomExCategory_PauseById"));
    bool valid=ready&&count&&info&&paused&&pause;
    for (auto address:{reinterpret_cast<uintptr_t>(ready),reinterpret_cast<uintptr_t>(count),reinterpret_cast<uintptr_t>(info),
        reinterpret_cast<uintptr_t>(paused),reinterpret_cast<uintptr_t>(pause)}) valid=valid&&executable(address,reinterpret_cast<uintptr_t>(module));
    configured.store(valid);
    if (!configured) gimmd::player_session::bgm_state.store(3);
    else { status="waiting-for-music-category"; gimmd::player_session::bgm_state.store(1); }
}
// Worker-only: injection from login can precede loading the audio plug-in.
// No engine calls here. Publish validated exports only after the full profile check.
static void refresh() {
    if (!gimmd::player_session::pause_bgm||configured.load()||late_module_checked) return;
    const auto now=GetTickCount64(); if (now<next_module_check) return; next_module_check=now+2000;
    if (!GetModuleHandleW(L"cri_ware_unity.dll")) return;
    late_module_checked=true; configure();
}
static void tick() {
    if (!gimmd::player_session::pause_bgm||!configured) return;
    __try {
        const bool stopping=hierarchy::playback::stop_requested.load()||hierarchy::playback::done.load();
        if (stopping) {
            if (!dirty.load()||restore_attempts>=8) return;
            ++restore_attempts; ++calls; pause(category,before);
            if (paused(category)==before) { dirty.store(false); restored=true; status="bgm-restored"; gimmd::player_session::bgm_state.store(4); }
            return;
        }
        if (!hierarchy::playback::started.load()) return;
        if (!attempted) {
            attempted=true;
            if (!ready()) { status="audio-engine-not-initialized"; gimmd::player_session::bgm_state.store(3); return; }
            const int categories=count(); int best=0; unsigned matches=0;
            if (categories<0||categories>256) { status="category-count-refused"; gimmd::player_session::bgm_state.store(3); return; }
            for (int i=0;i<categories;++i) {
                alignas(16) uint8_t buffer[128]{}; char name[64]{}; uintptr_t name_ptr=0; uint32_t id=0;
                if (!info(uint16_t(i),buffer)) continue;
                std::memcpy(&id,buffer+4,4); std::memcpy(&name_ptr,buffer+8,sizeof(name_ptr));
                if (!name_ptr||!read_bytes(name_ptr,name,sizeof(name))||!std::memchr(name,0,sizeof(name))) continue;
                const int priority=!_stricmp(name,"BGM") ? 2:(!_stricmp(name,"Music")||!_stricmp(name,"BackgroundMusic")) ? 1:0;
                if (priority>best) { best=priority; matches=0; }
                if (priority&&priority==best) { ++matches; category=id; strcpy_s(category_name,name); }
            }
            if (!best||matches!=1) { status=best ? "music-category-ambiguous":"music-category-not-found"; gimmd::player_session::bgm_state.store(3); return; }
            before=paused(category);
            if (before!=0&&before!=1) { status="pause-state-refused"; gimmd::player_session::bgm_state.store(3); return; }
            acquired=true;
        }
        if (!acquired) return;
        if (!paused(category)) { dirty.store(true); ++calls; pause(category,1); }
        if (paused(category)==1) { status="bgm-category-paused"; gimmd::player_session::bgm_state.store(2); }
        else { status="bgm-pause-not-confirmed"; gimmd::player_session::bgm_state.store(5); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { exception=GetExceptionCode(); status="bgm-interface-exception"; gimmd::player_session::bgm_state.store(5); }
}
static void report(std::ostream& out) {
    out<<",\n  \"bgmPause\":{\"requested\":"<<(gimmd::player_session::pause_bgm ? "true":"false")<<",\"status\":\""<<status
        <<"\",\"acquired\":"<<(acquired ? "true":"false")<<",\"restored\":"<<(restored ? "true":"false")
        <<",\"cleanupPending\":"<<(dirty.load() ? "true":"false")<<",\"calls\":"<<calls<<",\"exception\":"<<exception
        <<",\"category\":\""<<category_name<<"\",\"backgroundMusicCoverageVerified\":false}";
}
}
