#pragma once
// Included within multi. Only the visual skeleton root is translated.
namespace root_motion {
using Vec = gimmd::root_translation::Vec3;
using Getter = Vec*(__fastcall*)(Vec*,void*);
using Setter = void(__fastcall*)(void*,const Vec*);
static Getter get, get_scale;
static Setter set;
static bool verified = false, bound = false, captured = false, dirty = false, restored = false;
static unsigned writes = 0, calls = 0, retained = 0, overwrites = 0, attempts = 0;
static double maximum_offset = 0;
static float restore_error = 0;
static const char* status = "disabled";
static Step root_steps[MaxDepth+1]{}, body_steps[MaxDepth+1]{};
static unsigned root_length = 0, body_length = 0;
static Vec baseline{}, applied{}, pending{}, source_zero{}, body_scale{1,1,1};
static Vec body_position{};
static Quat body_rotation{0,0,0,1};
static Quat root_rotation{0,0,0,1};
static std::vector<vmd::BoneKey> controls[3];
static void reset() {
    bound = captured = dirty = restored = false; writes = calls = retained = overwrites = attempts = 0;
    maximum_offset = 0; restore_error = 0; root_length = body_length = 0;
    baseline = applied = pending = source_zero = body_position = {}; body_scale = {1,1,1}; body_rotation = {0,0,0,1};
    for (auto& control_track : controls) control_track.clear();
    status = root_mode ? "not-prepared" : "disabled";
}
static void configure(uintptr_t base) {
    get = root_mode ? reinterpret_cast<Getter>(entry("LocalPositionGet",base)) : nullptr;
    set = root_mode ? reinterpret_cast<Setter>(entry("LocalPositionSet",base)) : nullptr;
    get_scale = root_mode ? reinterpret_cast<Getter>(entry("LocalScaleGet",base)) : nullptr;
    verified = get && set && get_scale && entry("LocalPositionGetNative",base) && entry("LocalPositionRead",base) &&
        entry("LocalPositionSetNative",base) && entry("LocalPositionStore",base) && entry("LocalScaleGetNative",base) && entry("LocalScaleRead",base);
}
static bool finite(Vec p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
static float error(Vec a,Vec b) { return std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)}); }
static bool read(Getter fn, void* object, Vec* out) {
    __try { return fn && fn(out,object) == out && finite(*out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result.exception = GetExceptionCode(); return false; }
}
static bool write(void* object,Vec value,Vec* observed) {
    __try { ++calls; set(object,&value); return get(observed,object) == observed && finite(*observed) && error(value,*observed) < 0.00002f; }
    __except (EXCEPTION_EXECUTE_HANDLER) { result.exception = GetExceptionCode(); return false; }
}
static bool sample(double frame, Vec* out) {
    *out = {};
    for (const auto& control_track : controls) if (!control_track.empty()) {
        vmd::BoneSample value{}; if (!vmd::SampleBone(control_track,frame,value)) return false;
        out->x += value.position.x; out->y += value.position.y; out->z += value.position.z;
    }
    return finite(*out);
}
static bool prepare(const vmd::Motion& motion) {
    if (!root_mode) return true;
    status = "root-controls-rejected";
    for (unsigned i = 0; i < 3; ++i) {
        const auto found = motion.bones.find(gimmd::fk_profile::RootTrackNames[i]);
        if (found == motion.bones.end()) continue;
        controls[i] = found->second;
        // Rotation/pivot composition is separate work. Do not silently sum rotated controls.
        for (const auto& key : controls[i]) if (std::abs(key.rotation.x)+std::abs(key.rotation.y)+std::abs(key.rotation.z) > 0.00001f) {
            status = "rotated-root-controls-unsupported"; return false;
        }
    }
    if (controls[1].empty() || !sample(0,&source_zero)) return false;
    status = "root-source-prepared"; return true;
}
static bool bind(int model_node) {
    if (!root_mode) return true;
    namespace pm = animator_observer::position_metadata;
    status = "root-position-interface-unverified";
    if (!verified || !pm::named("get_localPosition",0,reinterpret_cast<uintptr_t>(get)) ||
        !pm::named("set_localPosition",1,reinterpret_cast<uintptr_t>(set)) ||
        !pm::named("get_localScale",0,reinterpret_cast<uintptr_t>(get_scale))) return false;
    const auto& pelvis = tracks[0];
    for (unsigned i = 0; i < pelvis.length; ++i) if (!std::wcscmp(pelvis.steps[i].name,L"Bip001")) root_length = i+1;
    if (!root_length || !model_path_size || model_path_size+1 > MaxDepth+1) return false;
    for (unsigned i = 0; i < root_length; ++i) root_steps[i] = pelvis.steps[i];
    int body = -1; unsigned matches = 0;
    for (unsigned n = 0; n < count; ++n) if (nodes[n].parent == model_node && !std::wcscmp(nodes[n].name,L"Body")) { body = static_cast<int>(n); ++matches; }
    if (matches != 1 || !nodes[body].processed || !nodes[body].aliases_equal || !nodes[body].parent_valid ||
        !nodes[body].owner_valid || !nodes[body].active || !roundtrip::identity_current(nodes[body])) return false;
    body_length = model_path_size+1;
    for (unsigned i = 0; i < model_path_size; ++i) body_steps[i] = pelvis.steps[i];
    auto& step = body_steps[model_path_size]; step = {}; step.identity = nodes[body].identity; wcscpy_s(step.name,nodes[body].name); step.child = 0;
    for (int n = 0; n < body; ++n) if (nodes[n].parent == model_node) ++step.child;
    ResolutionBatch batch{}; bool active = true;
    void* root = resolve_batch_steps(&batch,&active,root_steps,root_length);
    void* mesh = resolve_batch_steps(&batch,&active,body_steps,body_length);
    Vec initial{};
    bound = root && mesh && active && read(get,root,&initial) && read(get,mesh,&body_position) && read(get_scale,mesh,&body_scale) && read_local(mesh,&body_rotation) && read_local(root,&root_rotation) &&
        std::abs(body_scale.x) > 0.000001f && std::abs(body_scale.y) > 0.000001f && std::abs(body_scale.z) > 0.000001f;
    status = bound ? "visual-root-bound" : "visual-root-read-refused"; return bound;
}
static bool preflight(ResolutionBatch* batch,double frame,double blend) {
    if (!root_mode) return true;
    bool active = true; Vec current{}, scale{}, source{}, body_now{}; Quat rotation{}, root_now{};
    void* root = resolve_batch_steps(batch,&active,root_steps,root_length);
    void* mesh = resolve_batch_steps(batch,&active,body_steps,body_length);
    if (!bound || !root || !mesh || !active || !read(get,root,&current) || !read(get_scale,mesh,&scale) ||
        !read_local(mesh,&rotation) || !read_local(root,&root_now) || (leg_mode && (!read(get,mesh,&body_now)||error(body_now,body_position)>0.00002f)) ||
        (leg_mode && captured && roundtrip::error(root_now,root_rotation)>0.00002f) ||
        error(scale,body_scale) > 0.00002f || roundtrip::error(rotation,body_rotation) > 0.00002f || !sample(frame,&source)) {
        status = "visual-root-preflight-refused"; return false;
    }
    if (leg_mode) {
        Vec root_scale{}; const auto& reference=gimmd::fk_profile::AvatarReference[1];
        if (!read(get_scale,root,&root_scale)||error(root_scale,{reference.scale[0],reference.scale[1],reference.scale[2]})>0.00002f) {
            status="visual-root-reference-scale-mismatch"; return false;
        }
    }
    if (dirty) {
        if (error(current,applied) < 0.00002f) ++retained;
        else { ++overwrites; status = "visual-root-external-write"; return false; }
    }
    if (!captured) { baseline = current; root_rotation=root_now; captured = true; }
    auto delta = gimmd::root_translation::offset(source,source_zero,gimmd::fk_profile::RootMotionScale,
        {body_rotation.x,body_rotation.y,body_rotation.z,body_rotation.w},body_scale);
    pending = {baseline.x+delta.x*static_cast<float>(blend),baseline.y+delta.y*static_cast<float>(blend),baseline.z+delta.z*static_cast<float>(blend)};
    return finite(pending);
}
static bool apply(ResolutionBatch* batch) {
    if (!root_mode) return true;
    bool active = true; void* root = resolve_batch_steps(batch,&active,root_steps,root_length); Vec observed{};
    if (!root || !active) return false;
    dirty = true; applied = pending;
    if (!write(root,applied,&observed)) { status = "visual-root-write-failed"; return false; }
    const Vec delta{applied.x-baseline.x,applied.y-baseline.y,applied.z-baseline.z};
    maximum_offset = std::max(maximum_offset,std::sqrt(double(delta.x)*delta.x+double(delta.y)*delta.y+double(delta.z)*delta.z));
    ++writes; status = "playing-visual-root-translation"; return true;
}
static bool restore(ResolutionBatch* batch) {
    if (!dirty) return true;
    bool active = true; void* root = resolve_batch_steps(batch,&active,root_steps,root_length); Vec observed{};
    if (!root || attempts >= 8) return false;
    ++attempts;
    if (!write(root,baseline,&observed)) return false;
    restore_error = error(baseline,observed); dirty = false; restored = true; status = "visual-root-restored"; return true;
}
static void report(std::ostream& out) {
    out << "{\"enabled\":" << (root_mode ? "true" : "false") << ",\"status\":\"" << status << "\",\"visualRootOnly\":true"
        << ",\"sourceFrameZeroAnchored\":true,\"motionScale\":" << gimmd::fk_profile::RootMotionScale
        << ",\"writeCount\":" << writes << ",\"setterCalls\":" << calls << ",\"retainedChecks\":" << retained << ",\"overwrites\":" << overwrites
        << ",\"maximumOffsetLocalUnits\":" << maximum_offset << ",\"restored\":" << (restored ? "true" : "false")
        << ",\"cleanupPending\":" << (dirty ? "true" : "false") << ",\"restoreAttempts\":" << attempts << ",\"restoreComponentError\":" << restore_error << '}';
}
}
static void configure_root(uintptr_t base) { root_motion::configure(base); }
