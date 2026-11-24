#pragma once
// Included inside multi, after root_motion. Reference and rollback are distinct.
namespace reference_pose {
struct Bone { Step path[MaxDepth+1]{};unsigned length=0,attempts=0;Quat reference{},before{};root_motion::Vec position{},before_position{};bool dirty=false; };
static Bone bones[512]{};
static unsigned size=0,writes=0,restores=0;
static bool enabled=false,initialized=false;
static const char* status="disabled";
static void reset() {for(auto& bone:bones)bone={};size=writes=restores=0;initialized=false;status="disabled";}
static bool bind(int model) {
    if(!enabled||!full_mode)return true;
    namespace bp=animator_observer::bind_pose;
    status="reference-pose-binding-refused";
    if(std::strcmp(bp::avatar_rest_status,"avatar-local-reference-observed")||!root_motion::verified)return false;
    for(unsigned i=0;i<count;++i) {
        const int ref=bp::avatar_links[i];if(ref<0||i==unsigned(model))continue;
        bool under=false;for(int p=nodes[i].parent;p>=0;p=nodes[p].parent)if(p==model)under=true;
        if(!under)continue;
        if(unsigned(ref)>=bp::avatar_rest_count||size>=512)return false;
        auto& bone=bones[size];const auto* pose=bp::avatar_local[ref];
        bone.reference={pose[4],pose[5],pose[6],pose[7]};bone.position={pose[0],pose[1],pose[2]};
        if(!unit_quat(bone.reference)||!root_motion::finite(bone.position))return false;
        Step reverse[MaxDepth+1]{};unsigned length=0;
        for(int p=int(i);p>=0;p=nodes[p].parent) {
            if(length>=MaxDepth+1||!nodes[p].processed||!nodes[p].aliases_equal||!nodes[p].owner_valid||!nodes[p].parent_valid||!roundtrip::identity_current(nodes[p]))return false;
            auto& step=reverse[length++];step.identity=nodes[p].identity;wcscpy_s(step.name,nodes[p].name);
            if(nodes[p].parent>=0){step.child=0;for(int n=0;n<p;++n)if(nodes[n].parent==nodes[p].parent)++step.child;}
        }
        bone.length=length;for(unsigned n=0;n<length;++n)bone.path[n]=reverse[length-n-1];++size;
    }
    status=size?"reference-pose-bound":"reference-pose-empty";return size>0;
}
static bool position(void* target,const root_motion::Vec& value) {
    __try {root_motion::set(target,&value);root_motion::Vec actual{};
        return root_motion::read(root_motion::get,target,&actual)&&root_motion::error(actual,value)<0.00002f;
    } __except(EXCEPTION_EXECUTE_HANDLER){result.exception=GetExceptionCode();return false;}
}
static bool initialize() {
    if(!enabled||!full_mode||initialized)return true;
    ResolutionBatch batch{};void* resolved[512]{};
    // Take all rollback snapshots before any default-pose write.
    for(unsigned i=0;i<size;++i) {
        auto& bone=bones[i];bool active=true;resolved[i]=resolve_batch_steps(&batch,&active,bone.path,bone.length);
        if(!resolved[i]||!read_local(resolved[i],&bone.before)||!root_motion::read(root_motion::get,resolved[i],&bone.before_position))return false;
    }
    for(unsigned i=0;i<size;++i) {
        auto& bone=bones[i];bone.dirty=true;dirty.store(true);Quat actual{};
        if(!write_local(resolved[i],&bone.reference,&actual)||!position(resolved[i],bone.position)){status="reference-pose-write-failed";return false;}++writes;
    }
    initialized=true;status="reference-pose-initialized";return true;
}
static bool restore(ResolutionBatch* batch) {
    bool ok=true;
    for(unsigned reverse=size;reverse;--reverse) {
        auto& bone=bones[reverse-1];if(!bone.dirty)continue;
        if(bone.attempts>=8){ok=false;continue;}++bone.attempts;
        bool active=true;void* target=resolve_batch_steps(batch,&active,bone.path,bone.length);Quat actual{};
        if(!target||!write_local(target,&bone.before,&actual)||!position(target,bone.before_position)){ok=false;continue;}
        bone.dirty=false;++restores;
    }
    if(size)status=ok?"reference-pose-restored":"reference-pose-restore-pending";return ok;
}
static void report(std::ostream& out) {
    bool pending=false;for(unsigned i=0;i<size;++i)pending=pending||bones[i].dirty;
    out<<",\"referencePose\":{\"status\":\""<<status<<"\",\"bones\":"<<size<<",\"initialized\":"<<(initialized?"true":"false")<<",\"writes\":"<<writes<<",\"restores\":"<<restores<<",\"cleanupPending\":"<<(pending?"true":"false")<<"}";
}
}
