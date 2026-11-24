#pragma once
// Included within multi. The body mesh weights this sibling of the pelvis;
// it does not inherit pelvis motion when the Animator is leased.
namespace pelvis_helper {
static Track track;
static bool bound=false;
static vmd::Quaternion reference{0,0,0,1};
static unsigned overwritten=0;
static const char* status="not-bound";
static void reset() { track={}; bound=false; overwritten=0; status="not-bound";
    const auto* q=gimmd::fk_profile::AvatarReference[123].rotation;reference={q[0],q[1],q[2],q[3]}; }
static bool bind() {
    if (!full_mode) return true;
    int helper=-1,pelvis=-1; unsigned matches=0;
    for (unsigned n=0;n<count;++n) if (nodes[n].identity==tracks[0].steps[tracks[0].length-1].identity) pelvis=int(n);
    if (pelvis<0) return false;
    for (unsigned n=0;n<count;++n) if (nodes[n].parent==nodes[pelvis].parent&&!std::wcscmp(nodes[n].name,L"+PelvisTwist CF A01")) { helper=int(n); ++matches; }
    if (!matches) { status="helper-not-present"; return true; }
    namespace bp=animator_observer::bind_pose;
    if (matches!=1||!nodes[helper].processed||!nodes[helper].active||!nodes[helper].aliases_equal||!nodes[helper].owner_valid||
        !nodes[helper].parent_valid||!nodes[helper].rotation_relation_valid||!roundtrip::identity_current(nodes[helper])) return false;
    if(adaptive_reference) {
        const int ref=bp::avatar_links[helper],p=bp::avatar_links[pelvis];
        if(ref<0||p<0||unsigned(ref)>=bp::avatar_rest_count||unsigned(p)>=bp::avatar_rest_count||bp::avatar_parents[ref]!=bp::avatar_parents[p])return false;
        const auto* q=bp::avatar_local[ref]+4;reference={q[0],q[1],q[2],q[3]};
    } else {
        if(bp::avatar_links[helper]!=123||gimmd::fk_profile::AvatarReference[123].parent!=gimmd::fk_profile::AvatarReference[gimmd::fk_profile::AvatarIndices[0]].parent)return false;
        const auto* q=gimmd::fk_profile::AvatarReference[123].rotation;reference={q[0],q[1],q[2],q[3]};
    }
    track.length=tracks[0].length;
    for (unsigned i=0;i<track.length;++i) track.steps[i]=tracks[0].steps[i];
    auto& leaf=track.steps[track.length-1]; leaf.identity=nodes[helper].identity; wcscpy_s(leaf.name,nodes[helper].name); leaf.child=0;
    for (int n=0;n<helper;++n) if (nodes[n].parent==nodes[helper].parent) ++leaf.child;
    bound=true; status="pelvis-helper-bound"; return true;
}
static bool preflight(ResolutionBatch* batch) {
    if (!bound) return true;
    bool active=true; void* bone=resolve_batch_steps(batch,&active,track.steps,track.length); Quat observed{};
    if (!bone||!active||!read_local(bone,&observed)) return false;
    if (track.dirty&&roundtrip::error(track.applied,observed)>0.00002f) { ++overwritten; status="pelvis-helper-overwritten"; return false; }
    if (!track.captured) { track.baseline=observed; track.captured=true; }
    return true;
}
static bool apply(ResolutionBatch* batch,Quat pelvis,double blend=1) {
    if (!bound) return true;
    const auto& p=bindings[0].rest;
    const auto delta=gimmd::rotation_preview::multiply({pelvis.x,pelvis.y,pelvis.z,pelvis.w},{-p.x,-p.y,-p.z,p.w});
    const auto desired=gimmd::rotation_preview::multiply(delta,reference);
    const auto applied=vmd::Slerp({track.baseline.x,track.baseline.y,track.baseline.z,track.baseline.w},desired,blend);
    track.applied={applied.x,applied.y,applied.z,applied.w}; track.dirty=true;
    bool active=true; void* bone=resolve_batch_steps(batch,&active,track.steps,track.length); Quat observed{};
    if (!bone||!active||!write_local(bone,&track.applied,&observed)) return false;
    ++track.writes; status="pelvis-helper-following"; return true;
}
static bool restore(ResolutionBatch* batch) {
    if (!track.dirty) return true;
    bool active=true; void* bone=resolve_batch_steps(batch,&active,track.steps,track.length); Quat observed{};
    if (!bone||track.attempts>=8) return false;
    ++track.attempts;
    if (!write_local(bone,&track.baseline,&observed)) return false;
    track.dirty=false; track.restored=true; status="pelvis-helper-restored"; return true;
}
static void report(std::ostream& out) {
    out<<",\"pelvisHelper\":{\"bound\":"<<(bound ? "true":"false")<<",\"status\":\""<<status
        <<"\",\"writes\":"<<track.writes<<",\"overwrites\":"<<overwritten<<",\"restored\":"<<(track.restored ? "true":"false")
        <<",\"cleanupPending\":"<<(track.dirty ? "true":"false")<<",\"restoreAttempts\":"<<track.attempts<<'}';
}
}
