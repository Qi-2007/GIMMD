#pragma once
// Included inside local_actors. Parsed VMD data is worker-owned until request
// publication; all transform access and playback state changes run on Unity's thread.
namespace actor_motion {
namespace vmd=BetterEndfieldNext::Vmd;
static std::atomic<bool> in_use{false};
static vmd::Motion motion;
static bool prepared=false,bound=false,paused=false,held=false;
static unsigned selected_slot=0,track_count=0,node_count=0;
static double duration=0,seconds=0,anchor=0,anchor_seconds=0;
static ULONGLONG last_tick=0;
struct PathNode { int parent=-1,child=-1;uintptr_t identity=0;wchar_t name[96]{}; };
static PathNode paths[768]{};
struct Track { unsigned node=0,mapping=0;hierarchy::Quat before{0,0,0,1};bool dirty=false; };
static Track tracks[52]{};
static Track helpers[16]{};static unsigned helper_count=0,helper_writes=0;
static std::vector<vmd::BoneKey> controls[3];
static bool root_available=false,root_dirty=false;
static unsigned root_node=0,root_writes=0;
static vmd::Vec3 root_before{},source_zero{},mesh_scale{1,1,1};
static hierarchy::Quat mesh_rotation{0,0,0,1};
static double maximum_offset=0;
using PositionGet=vmd::Vec3*(__fastcall*)(vmd::Vec3*,void*);
using PositionSet=void(__fastcall*)(void*,const vmd::Vec3*);
static PositionGet position_read=nullptr,scale_read=nullptr;static PositionSet position_write=nullptr;
static const char* status="idle";
static unsigned writes=0;
static const std::vector<vmd::BoneKey>* source_keys(unsigned mapping) {
    const auto found=motion.bones.find(gimmd::motion_profile::Mappings[mapping].cp932);
    if (mapping==0&&found==motion.bones.end()) {
        const auto waist=motion.bones.find("\x8d\x98");return waist==motion.bones.end()?nullptr:&waist->second;
    }
    return found==motion.bones.end()?nullptr:&found->second;
}
static void motion_publish(const char* value) {
    strcpy_s(state.motion_status,status);state.motion_slot=selected_slot+1;state.motion_tracks=track_count;
    state.motion_writes=writes;state.motion_seconds=seconds;state.motion_duration=duration;
    state.motion_helpers=helper_count;state.motion_helper_writes=helper_writes;state.motion_root_available=root_available;
    state.motion_root_writes=root_writes;state.motion_root_offset=maximum_offset;local_actors::publish(value);
}
static bool prepare(const std::filesystem::path& directory) {
    if (in_use.exchange(true)) return false;
    prepared=false;
    try {
        std::ifstream input(directory/"actor-motion.vmd",std::ios::binary|std::ios::ate);
        const auto size=input?input.tellg():std::streampos(-1);
        if (size<=0||size>static_cast<std::streamoff>(vmd::Limits{}.file_bytes)) return true;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        vmd::Motion parsed;std::string error;
        if (!input||!vmd::Parse(bytes,parsed,error)||!parsed.last_frame||parsed.zero_quaternions) return true;
        if(!face_motion::prepare(true,parsed))return true;
        motion=std::move(parsed);
        root_available=false;
        for (unsigned i=0;i<3;++i) {
            controls[i].clear();auto found=motion.bones.find(gimmd::fk_profile::RootTrackNames[i]);
            if (found!=motion.bones.end()) controls[i]=found->second;
            for (const auto& key:controls[i]) if (!gimmd::root_controls::supported(i,key.rotation)) return true;
        }
        if (controls[1].empty()) {
            auto waist=motion.bones.find("\x8d\x98");
            if (waist!=motion.bones.end()&&!motion.bones.contains(gimmd::motion_profile::Mappings[0].cp932)) {
                controls[1]=waist->second;for (auto& key:controls[1]) key.rotation={};
            }
        }
        vmd::Quaternion center{};
        root_available=!controls[1].empty();
        if (root_available&&!gimmd::root_controls::sample(controls,0,&source_zero,&center)) return true;
        prepared=true;
    } catch (...) { prepared=false; }
    return true;
}
static void* model() {
    if (selected_slot>=3) return nullptr;
    const auto& slot=state.slots[selected_slot];
    void* object=find_object(find_string(slot.path));
    if (!slot.identity||identity(object)!=slot.identity||!hierarchy::get_active(object)) return nullptr;
    return transform(object);
}
static void* resolve(unsigned index,void* (&resolved)[768]) {
    if (index>=node_count) return nullptr;
    if (resolved[index]) return resolved[index];
    const auto& node=paths[index];
    void* value=nullptr;
    if (!index) value=model();
    else {
        if (node.parent<0||static_cast<unsigned>(node.parent)>=index) return nullptr;
        void* owner=resolve(static_cast<unsigned>(node.parent),resolved);
        if (!owner) return nullptr;
        const int children=hierarchy::child_count(owner);
        if (node.child<0||node.child>=children||children>128) return nullptr;
        value=hierarchy::get_child(owner,node.child);
    }
    wchar_t label[96]{};
    if (identity(value)!=node.identity||!hierarchy::string_read(hierarchy::name_a(value),label)||std::wcscmp(label,node.name)) return nullptr;
    resolved[index]=value;return value;
}
static bool read_position(PositionGet fn,void* target,vmd::Vec3* value) {
    __try { return fn&&fn(value,target)==value&&gimmd::leg_ik::finite(*value); }
    __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode();return false; }
}
static bool write_position(void* target,vmd::Vec3 value) {
    vmd::Vec3 observed{};
    __try {
        if (!target||!position_write||!position_read) return false;
        position_write(target,&value);
        return read_position(position_read,target,&observed)&&std::max({std::abs(value.x-observed.x),std::abs(value.y-observed.y),std::abs(value.z-observed.z)})<0.00002f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode();return false; }
}
static bool restore() {
    if (!bound) { status="idle";motion_publish("actor-motion-idle");in_use.store(false);return true; }
    bool success=true;void* resolved[768]{};
    __try {
        if (root_dirty) { auto* root=resolve(root_node,resolved);if (!write_position(root,root_before)) success=false;else root_dirty=false; }
        for (unsigned i=0;i<helper_count;++i) {
            auto& helper=helpers[i];if (!helper.dirty) continue;
            auto* bone=resolve(helper.node,resolved);hierarchy::Quat observed{};
            if (!bone||!hierarchy::playback::write_local(bone,&helper.before,&observed)) success=false;else helper.dirty=false;
        }
        for (unsigned reverse=track_count;reverse;--reverse) {
            auto& track=tracks[reverse-1];if (!track.dirty) continue;
            auto* bone=resolve(track.node,resolved);hierarchy::Quat observed{};
            if (!bone||!hierarchy::playback::write_local(bone,&track.before,&observed)) { success=false;continue; }
            track.dirty=false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode();success=false; }
    if(!face_motion::restore(true,model()))success=false;
    status=success?"restored":"restore-failed";
    if (success) bound=false;
    motion_publish(success?"actor-motion-restored":"actor-motion-restore-failed");if (success) in_use.store(false);return success;
}
static void begin(unsigned slot) {
    if (!prepared||bound||slot>=3||!state.slots[slot].identity) { status="source-or-slot-refused";motion_publish("actor-motion-source-or-slot-refused");in_use.store(false);return; }
    selected_slot=slot;track_count=node_count=0;writes=0;seconds=0;
    helper_count=helper_writes=root_writes=0;root_dirty=false;maximum_offset=0;
    bool valid=false;
    __try {
        if (auto* root=model();root&&collect(root)) {
            node_count=tree_count;
            for (unsigned i=0;i<node_count;++i) {
                auto& item=paths[i];item={};item.parent=tree[i].parent;item.identity=tree[i].identity;wcscpy_s(item.name,tree[i].name);
                if (i) { item.child=0;for (unsigned j=1;j<i;++j) if (tree[j].parent==item.parent) ++item.child; }
            }
            for (unsigned mapping=0;mapping<std::size(gimmd::motion_profile::Mappings)&&track_count<52;++mapping) {
                const auto& binding=gimmd::motion_profile::Mappings[mapping];
                const auto* keys=source_keys(mapping);if ((!keys||keys->empty())&&!(mapping==0&&root_available)) continue;
                unsigned matches=0,target=0;
                for (unsigned i=0;i<node_count;++i) if (!std::wcscmp(tree[i].name,binding.target)) { ++matches;target=i; }
                if (matches!=1) continue;
                auto& track=tracks[track_count];track={};track.node=target;track.mapping=mapping;
                if (!hierarchy::playback::read_local(tree[target].transform,&track.before)) break;
                ++track_count;
            }
            valid=track_count>=10;
            int pelvis=-1;
            for (unsigned i=0;i<track_count;++i) if (tracks[i].mapping==0) pelvis=static_cast<int>(tracks[i].node);
            if (pelvis>=0) for (unsigned i=0;i<node_count;++i) {
                if (tree[i].parent!=tree[pelvis].parent||std::wcsncmp(tree[i].name,L"+PelvisTwist",12)) continue;
                if (helper_count>=std::size(helpers)) { valid=false;break; }
                auto& helper=helpers[helper_count];helper={};helper.node=i;
                if (!hierarchy::playback::read_local(tree[i].transform,&helper.before)) { valid=false;break; }++helper_count;
            }
            if (root_available) {
                unsigned roots=0,bodies=0,body=0;
                for (unsigned i=0;i<node_count;++i) {
                    if (!std::wcscmp(tree[i].name,L"Bip001")) { root_node=i;++roots; }
                    if (!std::wcscmp(tree[i].name,L"Body")&&tree[i].parent==0) { body=i;++bodies; }
                }
                const auto base=hierarchy::animator_observer::metadata::image_base;
                position_read=reinterpret_cast<PositionGet>(hierarchy::entry("LocalPositionGet",base));
                position_write=reinterpret_cast<PositionSet>(hierarchy::entry("LocalPositionSet",base));
                scale_read=reinterpret_cast<PositionGet>(hierarchy::entry("LocalScaleGet",base));
                if (roots!=1||bodies!=1||!position_write||!hierarchy::entry("LocalPositionSetNative",base)||!hierarchy::entry("LocalPositionStore",base)||
                    !hierarchy::entry("LocalPositionGetNative",base)||!hierarchy::entry("LocalPositionRead",base)||!hierarchy::entry("LocalScaleGetNative",base)||!hierarchy::entry("LocalScaleRead",base)||
                    !read_position(position_read,tree[root_node].transform,&root_before)||!read_position(scale_read,tree[body].transform,&mesh_scale)||
                    !hierarchy::playback::read_local(tree[body].transform,&mesh_rotation)) valid=false;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode(); }
    for (auto& node:tree) node={};tree_count=0;
    if (!valid) { status="bones-refused";motion_publish("actor-motion-bones-refused");in_use.store(false);return; }
    duration=motion.last_frame/30.0;anchor=GetTickCount64()/1000.0;anchor_seconds=0;
    paused=held=false;bound=true;last_tick=0;status="playing";motion_publish("actor-motion-playing");
}
static void pause(bool value) {
    if (!bound) { motion_publish("actor-motion-idle");return; }
    if (value) { paused=true;status="paused"; }
    else { if (held) { seconds=0;held=false; }anchor_seconds=seconds;anchor=GetTickCount64()/1000.0;paused=false;status="playing"; }
    motion_publish(value?"actor-motion-paused":"actor-motion-playing");
}
static void tick() {
    if (!bound||paused||held) return;
    const auto now=GetTickCount64();if (last_tick&&now-last_tick<16) return;last_tick=now;
    seconds=std::min(duration,anchor_seconds+now/1000.0-anchor);
    void* resolved[768]{};void* bones[52]{};hierarchy::Quat desired[52]{};bool valid=true;
    void* helper_bones[16]{};hierarchy::Quat helper_desired[16]{};void* root=nullptr;vmd::Vec3 pending{},source{};vmd::Quaternion center{};
    hierarchy::Quat lower_independent{0,0,0,1};
    __try {
        if (root_available) {
            root=resolve(root_node,resolved);
            if (!root||!gimmd::root_controls::sample(controls,seconds*30,&source,&center)) valid=false;
            const auto delta=gimmd::root_translation::offset(source,source_zero,gimmd::fk_profile::RootMotionScale,
                {mesh_rotation.x,mesh_rotation.y,mesh_rotation.z,mesh_rotation.w},mesh_scale);
            const float blend=static_cast<float>(std::min(1.0,seconds/.5));
            pending={root_before.x+delta.x*blend,root_before.y+delta.y*blend,root_before.z+delta.z*blend};
            maximum_offset=std::max(maximum_offset,std::sqrt(double(delta.x)*delta.x+double(delta.y)*delta.y+double(delta.z)*delta.z));
        }
        for (unsigned i=0;valid&&i<track_count;++i) {
            auto& track=tracks[i];bones[i]=resolve(track.node,resolved);
            if (!bones[i]) { valid=false;break; }
            vmd::BoneSample sample{};
            const auto* keys=source_keys(track.mapping);
            if (keys&&!vmd::SampleBone(*keys,seconds*30,sample)) { valid=false;break; }
            if (!keys&&!(track.mapping==0&&root_available)) { valid=false;break; }
            auto binding=gimmd::fk_profile::Bindings[track.mapping];
            binding.rest={track.before.x,track.before.y,track.before.z,track.before.w};
            const auto source_rotation=track.mapping==0&&root_available?gimmd::root_controls::pelvis(center,sample.rotation):sample.rotation;
            auto target=gimmd::fk_retarget::evaluate_at_rest(binding,binding.rest,source_rotation);
            const auto blended=vmd::Slerp(binding.rest,target,std::min(1.0,seconds/0.5));
            desired[i]={blended.x,blended.y,blended.z,blended.w};
            if (track.mapping==0) {
                const auto lower_target=gimmd::fk_retarget::evaluate(binding,sample.rotation);
                const auto independent=vmd::Slerp(binding.rest,lower_target,std::min(1.0,seconds/0.5));
                lower_independent={independent.x,independent.y,independent.z,independent.w};
            }
        }
        // MMD lower/upper body tracks are parallel; the game spine inherits pelvis.
        int lower=-1,upper=-1;
        for (unsigned i=0;i<track_count;++i) { if (tracks[i].mapping==0) lower=static_cast<int>(i);if (tracks[i].mapping==1) upper=static_cast<int>(i); }
        if (valid&&lower>=0&&upper>=0) {
            const auto& rest=tracks[lower].before;const auto& pelvis=lower_independent;const auto& spine=desired[upper];
            const auto target=gimmd::fk_retarget::compensate_parent({rest.x,rest.y,rest.z,rest.w},{pelvis.x,pelvis.y,pelvis.z,pelvis.w},{spine.x,spine.y,spine.z,spine.w});
            desired[upper]={target.x,target.y,target.z,target.w};
        }
        if (valid&&helper_count&&lower<0) valid=false;
        for (unsigned i=0;valid&&i<helper_count;++i) {
            auto& helper=helpers[i];helper_bones[i]=resolve(helper.node,resolved);
            if (!helper_bones[i]) { valid=false;break; }
            const auto& rest=tracks[lower].before;const auto& pelvis=desired[lower];
            const auto delta=gimmd::rotation_preview::multiply({pelvis.x,pelvis.y,pelvis.z,pelvis.w},{-rest.x,-rest.y,-rest.z,rest.w});
            const auto target=gimmd::rotation_preview::multiply(delta,{helper.before.x,helper.before.y,helper.before.z,helper.before.w});
            helper_desired[i]={target.x,target.y,target.z,target.w};
        }
        if (valid) for (unsigned i=0;i<track_count;++i) {
            tracks[i].dirty=true;hierarchy::Quat observed{};
            if (!hierarchy::playback::write_local(bones[i],&desired[i],&observed)) { valid=false;break; }++writes;
        }
        for (unsigned i=0;valid&&i<helper_count;++i) {
            helpers[i].dirty=true;hierarchy::Quat observed{};
            if (!hierarchy::playback::write_local(helper_bones[i],&helper_desired[i],&observed)) valid=false;else ++helper_writes;
        }
        if (valid&&root_available) { root_dirty=true;if (!write_position(root,pending)) valid=false;else ++root_writes; }
    } __except(EXCEPTION_EXECUTE_HANDLER) { state.exception=GetExceptionCode();valid=false; }
    if(valid&&!face_motion::tick(true,model(),seconds))valid=false;
    if (!valid) { paused=true;status="write-refused";restore();return; }
    if (seconds>=duration) { held=true;status="held";motion_publish("actor-motion-held"); }
    else motion_publish("actor-motion-playing");
}
}
