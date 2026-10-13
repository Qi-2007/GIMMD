#pragma once
// No engine pointer is retained beyond the current FOV callback.
namespace camera_experiment {
#ifdef GIMMD_CAMERA_ROUNDTRIP
inline constexpr bool Enabled=true;
#else
inline constexpr bool Enabled=false;
#endif
using PositionSetter=void(__fastcall*)(void*,const Vec3*);
static PositionSetter set_position;
static hierarchy::roundtrip::SetLocalFn set_rotation;
static bool verified=false,consumed=false;
struct Result {
    bool attempted=false,position_verified=false,rotation_verified=false,restored=false,position_dirty=false,rotation_dirty=false;
    unsigned calls=0,attempts=0; DWORD exception=0;
    float position_error=0,rotation_error=0,restore_position_error=0,restore_rotation_error=0;
    const char* status="disabled";
};
static Result result;
static void configure(uintptr_t base) {
    result={}; result.status=Enabled ? "profile-unverified":"disabled";
    set_position=Enabled ? reinterpret_cast<PositionSetter>(hierarchy::entry("WorldPositionSet",base)):nullptr;
    set_rotation=Enabled ? reinterpret_cast<hierarchy::roundtrip::SetLocalFn>(hierarchy::entry("WorldRotationSet",base)):nullptr;
    verified=set_position&&set_rotation&&hierarchy::entry("WorldPositionGet",base)&&hierarchy::entry("WorldPositionGetNative",base)&&
        hierarchy::entry("WorldPositionSetNative",base)&&hierarchy::entry("WorldPositionRead",base)&&hierarchy::entry("WorldPositionStore",base)&&
        hierarchy::entry("WorldRotationSetNative",base)&&hierarchy::entry("WorldRotationRead",base)&&hierarchy::entry("WorldRotationStore",base);
    if (verified) result.status="waiting-for-main-camera-callback";
}
static bool identity(void* transform,uintptr_t native) {
    uintptr_t current=0;
    return transform&&native&&read_bytes(reinterpret_cast<uintptr_t>(transform)+16,&current,sizeof(current))&&current==native;
}
static float error(Vec3 a,Vec3 b) { return std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)}); }
static bool read(void* object,uintptr_t native,Vec3* p,hierarchy::Quat* q) {
    __try {
        if (!identity(object,native)) return false;
        get_position(p,object); hierarchy::rotation_world(q,object);
        return std::isfinite(p->x)&&std::isfinite(p->y)&&std::isfinite(p->z)&&hierarchy::unit_quat(*q);
    } __except(EXCEPTION_EXECUTE_HANDLER) { result.exception=GetExceptionCode(); return false; }
}
static bool position(void* object,uintptr_t native,const Vec3* p) {
    __try { if (!identity(object,native)) return false; ++result.calls; set_position(object,p); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { result.exception=GetExceptionCode(); return false; }
}
static bool rotation(void* object,uintptr_t native,const hierarchy::Quat* q) {
    __try { if (!identity(object,native)) return false; ++result.calls; set_rotation(object,q); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { result.exception=GetExceptionCode(); return false; }
}
static void run(void* transform) {
    if (!Enabled||!verified||consumed||!get_position||!hierarchy::rotation_world) return;
    uintptr_t native=0; Vec3 before{},observed{}; hierarchy::Quat q_before{},q_observed{};
    if (!read_bytes(reinterpret_cast<uintptr_t>(transform)+16,&native,sizeof(native))||!read(transform,native,&before,&q_before)) return;
    consumed=true; result.attempted=true;
    const Vec3 requested{before.x+0.02f,before.y,before.z};
    const auto turned=gimmd::rotation_preview::multiply({0,0.0043633093f,0,0.999990481f},{q_before.x,q_before.y,q_before.z,q_before.w});
    const hierarchy::Quat requested_q{turned.x,turned.y,turned.z,turned.w};
    result.position_dirty=true;
    if (position(transform,native,&requested)&&read(transform,native,&observed,&q_observed)) {
        result.position_error=error(requested,observed); result.position_verified=result.position_error<0.00002f;
        if (result.position_verified) {
            result.rotation_dirty=true;
            if (rotation(transform,native,&requested_q)&&read(transform,native,&observed,&q_observed)) {
                result.rotation_error=hierarchy::roundtrip::error(requested_q,q_observed); result.rotation_verified=result.rotation_error<0.00002f;
            }
        }
    }
    // Restore independently, including mutation that occurred before a setter exception.
    while (result.attempts<8&&(result.position_dirty||result.rotation_dirty)) {
        ++result.attempts;
        if (result.rotation_dirty&&rotation(transform,native,&q_before)&&read(transform,native,&observed,&q_observed)) {
            result.restore_rotation_error=hierarchy::roundtrip::error(q_before,q_observed);
            if (result.restore_rotation_error<0.00002f) result.rotation_dirty=false;
        }
        if (result.position_dirty&&position(transform,native,&before)&&read(transform,native,&observed,&q_observed)) {
            result.restore_position_error=error(before,observed);
            if (result.restore_position_error<0.00002f) result.position_dirty=false;
        }
    }
    result.restored=!result.position_dirty&&!result.rotation_dirty;
    result.status=!result.restored ? "camera-restoration-failed":result.position_verified&&result.rotation_verified ?
        "camera-position-rotation-roundtrip-restored":"camera-write-refused-restored";
}
static bool failed() { return result.attempted&&!result.restored; }
static void report(std::ostream& out) {
    out << ",\n  \"cameraRoundTrip\":{\"enabled\":" << (Enabled ? "true":"false") << ",\"status\":\"" << result.status
        << "\",\"attempted\":" << (result.attempted ? "true":"false") << ",\"positionWriteVerified\":" << (result.position_verified ? "true":"false")
        << ",\"rotationWriteVerified\":" << (result.rotation_verified ? "true":"false") << ",\"restored\":" << (result.restored ? "true":"false")
        << ",\"cleanupPending\":" << (failed() ? "true":"false") << ",\"setterCalls\":" << result.calls << ",\"restoreAttempts\":" << result.attempts
        << ",\"positionWriteError\":" << result.position_error << ",\"rotationWriteError\":" << result.rotation_error
        << ",\"positionRestoreError\":" << result.restore_position_error << ",\"rotationRestoreError\":" << result.restore_rotation_error
        << ",\"exception\":" << result.exception << ",\"sameCallbackRestore\":true,\"fovWritten\":false}";
}
}
