#pragma once
// Main-thread only engine access. Each tick resolves the camera again; only its
// native identity value and saved pose cross callbacks, never a callable pointer.
namespace camera_playback {
#ifdef GIMMD_CAMERA_PLAYBACK
inline constexpr bool Enabled=true;
#else
inline constexpr bool Enabled=false;
#endif
namespace cp=gimmd::camera_pose;
static std::vector<BetterEndfieldNext::Vmd::CameraKey> keys;
static cp::Pose zero;
static Vec3 baseline{},applied{};
static hierarchy::Quat baseline_q{},applied_q{};
static uintptr_t owner=0,transform_owner=0;
static camera_experiment::PositionSetter set_position;
static hierarchy::roundtrip::SetLocalFn set_rotation;
static bool loaded=false,acquired=false,restored=false,aborted=false;
static std::atomic<bool> dirty{false};
static unsigned writes=0,retained=0,overwritten=0,attempts=0;
static DWORD exception=0;
static float max_error=0,restore_error=0;
static double sampled_seconds=0;
struct Trace { double seconds=0; uint64_t epoch=0; unsigned state=0; Vec3 position{}; float error=0; };
static Trace traces[16]{}; static unsigned trace_count=0;
static const char* status="disabled";
static bool sample(double seconds,cp::Pose* out) {
    BetterEndfieldNext::Vmd::CameraSample value{};
    return BetterEndfieldNext::Vmd::SampleCamera(keys,seconds*30,value)&&!value.orthographic&&
        cp::evaluate(value,gimmd::fk_profile::RootMotionScale,{}, {},out);
}
static void configure(uintptr_t base,const std::filesystem::path& directory) {
    keys.clear(); loaded=acquired=restored=aborted=false; dirty.store(false); owner=0;
    writes=retained=overwritten=attempts=0; exception=0; max_error=restore_error=0; sampled_seconds=0;
    trace_count=0;
    status=Enabled ? "no-camera-track":"disabled";
    if (!Enabled||!hierarchy::configured) return;
    set_position=reinterpret_cast<camera_experiment::PositionSetter>(hierarchy::entry("WorldPositionSet",base));
    set_rotation=reinterpret_cast<hierarchy::roundtrip::SetLocalFn>(hierarchy::entry("WorldRotationSet",base));
    // Reuse the reviewed full native dependency contract from the roundtrip experiment.
    const char* dependencies[]={"WorldPositionGet","WorldPositionGetNative","WorldPositionSetNative","WorldPositionRead",
        "WorldPositionStore","WorldRotationSetNative","WorldRotationRead","WorldRotationStore"};
    if (!set_position||!set_rotation) { status="profile-unverified"; return; }
    for (auto name:dependencies) if (!hierarchy::entry(name,base)) { status="profile-unverified"; return; }
    const auto path=directory/L"camera.vmd";
    if (!std::filesystem::exists(path)) return;
    status="invalid-camera-track";
    std::ifstream input(path,std::ios::binary|std::ios::ate); const auto size=input ? input.tellg():std::streampos(-1);
    if (size<=0||size>16*1024*1024) return;
    std::vector<uint8_t> bytes(static_cast<size_t>(size)); input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()),size);
    BetterEndfieldNext::Vmd::Motion motion; std::string error;
    if (!input||!BetterEndfieldNext::Vmd::Parse(bytes,motion,error)||motion.cameras.empty()) return;
    // Projection switching needs another reviewed interface; refuse the entire track.
    for (const auto& key:motion.cameras) if (key.orthographic) { status="orthographic-camera-unsupported"; return; }
    keys=std::move(motion.cameras);
    if (!sample(0,&zero)) return;
    loaded=true; status="waiting-for-main-camera";
}
static bool read(void* transform,uintptr_t native,Vec3* p,hierarchy::Quat* q) {
    __try {
        if (!camera_experiment::identity(transform,native)) return false;
        get_position(p,transform); hierarchy::rotation_world(q,transform);
        return std::isfinite(p->x)&&std::isfinite(p->y)&&std::isfinite(p->z)&&hierarchy::unit_quat(*q);
    } __except(EXCEPTION_EXECUTE_HANDLER) { exception=GetExceptionCode(); return false; }
}
static bool write(void* transform,uintptr_t native,const Vec3* p,const hierarchy::Quat* q) {
    __try {
        if (!camera_experiment::identity(transform,native)) return false;
        set_position(transform,p);
        if (!camera_experiment::identity(transform,native)) return false;
        set_rotation(transform,q); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { exception=GetExceptionCode(); return false; }
}
// Accept a validated camera only. The FOV callback additionally proves ownership
// on acquisition; later message ticks require the very same native identity.
static void apply(void* transform,uintptr_t camera_native,bool callback_match) {
    const bool stopping=hierarchy::playback::stop_requested.load()||hierarchy::playback::done.load()||engine_error.load()||aborted;
    if (!Enabled||!loaded||(!dirty.load()&&stopping)) return;
    if (acquired&&camera_native!=owner) return;
    if (!acquired&&!callback_match) return;
    uintptr_t native=0; Vec3 observed{}; hierarchy::Quat observed_q{};
    if (!read_bytes(reinterpret_cast<uintptr_t>(transform)+16,&native,sizeof(native))||(acquired&&native!=transform_owner)||!read(transform,native,&observed,&observed_q)) {
        aborted=true; hierarchy::playback::stop_requested.store(true); status="camera-read-failed"; return;
    }
    if (stopping) {
        ++attempts;
        if (write(transform,native,&baseline,&baseline_q)&&read(transform,native,&observed,&observed_q)) {
            restore_error=std::max(camera_experiment::error(baseline,observed),hierarchy::roundtrip::error(baseline_q,observed_q));
            if (restore_error<0.0001f) { dirty.store(false); restored=true; status=aborted ? "camera-aborted-restored":"camera-stopped-restored"; }
        }
        return;
    }
    const auto frame=gimmd::unpack_player_frame(gimmd::player_session::frame.load());
    if (frame.state!=gimmd::PlaybackState::Playing&&frame.state!=gimmd::PlaybackState::Paused) return;
    cp::Pose pose{};
    if (!sample(frame.seconds,&pose)) { aborted=true; hierarchy::playback::stop_requested.store(true); status="camera-sample-failed"; return; }
    if (!acquired) { baseline=observed; baseline_q=observed_q; owner=camera_native; transform_owner=native; acquired=true; }
    else if (dirty.load()) {
        if (camera_experiment::error(applied,observed)<0.0001f&&hierarchy::roundtrip::error(applied_q,observed_q)<0.0001f) ++retained;
        else ++overwritten;
    }
    const auto alignment=gimmd::rotation_preview::multiply({baseline_q.x,baseline_q.y,baseline_q.z,baseline_q.w},
        {-zero.rotation.x,-zero.rotation.y,-zero.rotation.z,zero.rotation.w});
    const auto offset=gimmd::leg_ik::vector(gimmd::leg_ik::rotation(alignment),gimmd::leg_ik::sub(pose.position,zero.position));
    applied={baseline.x+offset.x,baseline.y+offset.y,baseline.z+offset.z};
    const auto q=gimmd::rotation_preview::multiply(alignment,pose.rotation); applied_q={q.x,q.y,q.z,q.w};
    dirty.store(true); ++writes; sampled_seconds=frame.seconds;
    if (!write(transform,native,&applied,&applied_q)||!read(transform,native,&observed,&observed_q)) {
        aborted=true; hierarchy::playback::stop_requested.store(true); status="camera-write-failed"; return;
    }
    const float error=std::max(camera_experiment::error(applied,observed),hierarchy::roundtrip::error(applied_q,observed_q));
    max_error=std::max(max_error,error);
    if (trace_count<16&&(!trace_count||traces[trace_count-1].epoch!=frame.epoch||traces[trace_count-1].state!=unsigned(frame.state)))
        traces[trace_count++]={frame.seconds,frame.epoch,unsigned(frame.state),observed,error};
    if (error>=0.0001f) { aborted=true; hierarchy::playback::stop_requested.store(true); status="camera-write-mismatch"; }
    else status="camera-playing-relative-to-initial-view";
}
static void tick(void* target=nullptr) {
    if (!Enabled||!loaded||(!dirty.load()&&(hierarchy::playback::done.load()||hierarchy::playback::stop_requested.load()))) return;
    __try {
        void* array=get_all(); uint64_t length=0;
        if (!array||!read_bytes(reinterpret_cast<uintptr_t>(array)+24,&length,sizeof(length))||length>16) return;
        for (unsigned i=0;i<length;++i) {
            void* camera=nullptr; uintptr_t native=0;
            if (!read_bytes(reinterpret_cast<uintptr_t>(array)+32+i*sizeof(void*),&camera,sizeof(camera))||!camera||
                !read_bytes(reinterpret_cast<uintptr_t>(camera)+16,&native,sizeof(native))||!native||(acquired&&native!=owner)) continue;
            void* transform=get_transform(camera);
            if (!transform) continue;
            void* go=hierarchy::component_game_object(transform); uintptr_t go_native=0; void* check=nullptr;
            wchar_t name[hierarchy::MaxName]{},alias[hierarchy::MaxName]{};
            if (!go||!read_bytes(reinterpret_cast<uintptr_t>(go)+16,&go_native,sizeof(go_native))||!go_native||
                hierarchy::object_transform(&check,reinterpret_cast<void*>(go_native))!=&check||!hierarchy::same_object(transform,check)||
                !hierarchy::string_read(hierarchy::name_a(go),name)||!hierarchy::string_read(hierarchy::name_b(go),alias)||
                std::wcscmp(name,alias)||std::wcscmp(name,L"MainCamera(Clone)")) continue;
            const bool match=camera==target||native==reinterpret_cast<uintptr_t>(target);
            if (target&&!match) continue;
            apply(transform,native,match); return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { exception=GetExceptionCode(); aborted=true; hierarchy::playback::stop_requested.store(true); }
}
static void report(std::ostream& out) {
    out<<",\n  \"cameraPlayback\":{\"enabled\":"<<(Enabled ? "true":"false")<<",\"loaded\":"<<(loaded ? "true":"false")
        <<",\"status\":\""<<status<<"\",\"acquired\":"<<(acquired ? "true":"false")<<",\"restored\":"<<(restored ? "true":"false")
        <<",\"cleanupPending\":"<<(dirty.load() ? "true":"false")<<",\"aborted\":"<<(aborted ? "true":"false")
        <<",\"poseWrites\":"<<writes<<",\"retainedSamples\":"<<retained<<",\"gameOverwrites\":"<<overwritten
        <<",\"restoreAttempts\":"<<attempts<<",\"maxWriteError\":"<<max_error<<",\"restoreError\":"<<restore_error
        <<",\"exception\":"<<exception<<",\"lastSampleSeconds\":"<<sampled_seconds
        <<",\"anchor\":\"initial-camera-pose\",\"fovWritten\":false,\"finalRenderVerified\":false,\"timelineSamples\":[";
    for (unsigned i=0;i<trace_count;++i) {
        const auto& t=traces[i];
        out<<(i ? ",":"")<<"{\"seconds\":"<<t.seconds<<",\"epoch\":"<<t.epoch<<",\"state\":"<<t.state
            <<",\"position\":["<<t.position.x<<','<<t.position.y<<','<<t.position.z<<"],\"error\":"<<t.error<<'}';
    }
    out<<"]}";
}
}
