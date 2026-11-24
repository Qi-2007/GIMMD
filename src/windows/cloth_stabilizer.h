#pragma once
// Optional static Amice-chain writer. This does not disable native physics;
// overwrite counters expose contention until a physics lease is available.
namespace cloth_stabilizer {
constexpr unsigned Limit=48;
struct Bone { Track track; unsigned reference=0; root_motion::Vec before{},applied{}; bool position_dirty=false; };
static Bone bones[Limit]; static unsigned size=0,writes=0,overwrites=0;
static const char* status="disabled";
static void reset() { for (auto& bone:bones) bone={}; size=writes=overwrites=0; status="disabled"; }
static bool bind(int model) {
    if(adaptive_reference){status="character-cloth-not-calibrated";return true;}
    if (!gimmd::player_session::stabilize_cloth&&!gimmd::player_session::stabilize_braid) return true;
    status="cloth-binding-refused";
    namespace bp=animator_observer::bind_pose;
    for (unsigned n=0;n<count;++n) {
        const bool cape=gimmd::player_session::stabilize_cloth&&!std::wcsncmp(nodes[n].name,L"+Amice",6);
        const bool braid=gimmd::player_session::stabilize_braid&&!std::wcsncmp(nodes[n].name,L"+HairB CB A",11);
        if (!cape&&!braid) continue;
        bool under=false; for (int p=int(n);p>=0;p=nodes[p].parent) if (p==model) under=true;
        if (!under) continue;
        const int reference=bp::avatar_links[n];
        if (size>=Limit||reference<0||reference>=int(std::size(gimmd::fk_profile::AvatarReference))) return false;
        auto& bone=bones[size]; bone.reference=unsigned(reference);
        Step reverse[MaxDepth+1]{}; unsigned length=0;
        for (int p=int(n);p>=0;p=nodes[p].parent) {
            if (length>=MaxDepth+1||!nodes[p].processed||!nodes[p].active||!nodes[p].aliases_equal||!nodes[p].parent_valid||
                !nodes[p].owner_valid||!roundtrip::identity_current(nodes[p])||(nodes[p].parent>=0&&!nodes[p].rotation_relation_valid)) return false;
            auto& step=reverse[length++]; step.identity=nodes[p].identity; wcscpy_s(step.name,nodes[p].name);
            if (nodes[p].parent>=0) {
                step.child=0;
                for (int previous=0;previous<p;++previous) if (nodes[previous].parent==nodes[p].parent) ++step.child;
            }
        }
        bone.track.length=length;
        for (unsigned i=0;i<length;++i) bone.track.steps[i]=reverse[length-i-1];
        ++size;
    }
    if (!size) { status="no-requested-static-bones"; return false; }
    status="cloth-static-bound"; return true;
}
static bool position(void* object,root_motion::Vec value,root_motion::Vec* observed) {
    __try {
        root_motion::set(object,&value);
        return root_motion::get(observed,object)==observed&&root_motion::finite(*observed)&&root_motion::error(value,*observed)<0.00002f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { result.exception=GetExceptionCode(); return false; }
}
static bool preflight(ResolutionBatch* batch) {
    for (unsigned i=0;i<size;++i) {
        auto& b=bones[i]; bool active=true; Quat q{}; root_motion::Vec p{};
        void* object=resolve_batch_steps(batch,&active,b.track.steps,b.track.length);
        if (!object||!active||!read_local(object,&q)||!root_motion::read(root_motion::get,object,&p)) return false;
        if (!b.track.captured) { b.track.baseline=q; b.before=p; b.track.captured=true; }
        else if ((b.track.dirty&&roundtrip::error(b.track.applied,q)>0.0001f)||
            (b.position_dirty&&root_motion::error(b.applied,p)>0.0001f)) ++overwrites;
    }
    return true;
}
static bool apply(ResolutionBatch* batch,double blend) {
    for (unsigned i=0;i<size;++i) {
        auto& b=bones[i]; const auto& r=gimmd::fk_profile::AvatarReference[b.reference];
        const auto q=vmd::Slerp({b.track.baseline.x,b.track.baseline.y,b.track.baseline.z,b.track.baseline.w},
            {r.rotation[0],r.rotation[1],r.rotation[2],r.rotation[3]},blend);
        b.track.applied={q.x,q.y,q.z,q.w};
        b.applied={b.before.x+float(blend)*(r.position[0]-b.before.x),b.before.y+float(blend)*(r.position[1]-b.before.y),
            b.before.z+float(blend)*(r.position[2]-b.before.z)};
        bool active=true; void* object=resolve_batch_steps(batch,&active,b.track.steps,b.track.length);
        if (!object||!active) return false;
        Quat observed{}; root_motion::Vec position{};
        b.track.dirty=true; if (!write_local(object,&b.track.applied,&observed)) return false;
        b.position_dirty=true; if (!cloth_stabilizer::position(object,b.applied,&position)) return false;
        ++writes;
    }
    if (size) status="cloth-static-writing"; return true;
}
static bool restore(ResolutionBatch* batch) {
    bool pending=false;
    for (unsigned i=size;i>0;--i) {
        auto& b=bones[i-1]; if (!b.track.dirty&&!b.position_dirty) continue;
        bool active=true; void* object=resolve_batch_steps(batch,&active,b.track.steps,b.track.length);
        if (!object||b.track.attempts>=8) { pending=true; continue; }
        ++b.track.attempts; Quat q{}; root_motion::Vec p{};
        if (b.track.dirty&&write_local(object,&b.track.baseline,&q)) b.track.dirty=false;
        if (b.position_dirty&&cloth_stabilizer::position(object,b.before,&p)) b.position_dirty=false;
        b.track.restored=!b.track.dirty&&!b.position_dirty; pending=pending||!b.track.restored;
    }
    if (size) status=pending ? "cloth-restore-pending":"cloth-restored";
    return !pending;
}
static void report(std::ostream& out) {
    bool cleanup_dirty=false; for (unsigned i=0;i<size;++i) cleanup_dirty=cleanup_dirty||bones[i].track.dirty||bones[i].position_dirty;
    out<<",\"clothStabilizer\":{\"requested\":"<<(gimmd::player_session::stabilize_cloth ? "true":"false")
        <<",\"braidRequested\":"<<(gimmd::player_session::stabilize_braid ? "true":"false")
        <<",\"status\":\""<<status<<"\",\"boneCount\":"<<size<<",\"writes\":"<<writes<<",\"externalWrites\":"<<overwrites
        <<",\"cleanupPending\":"<<(cleanup_dirty ? "true":"false")<<",\"physicsDisabled\":false}";
}
}
