#pragma once
// Included inside local_actors after driver_lease. Current playable model only.
namespace secondary_physics {
namespace motion=hierarchy::playback;
namespace position=hierarchy::playback::multi::root_motion;
namespace contact=gimmd::body_collision;
constexpr unsigned BodyCapsules=13;
using Quat=hierarchy::Quat;
using Method=void(__fastcall*)(void*);
using StepMethod=void(__fastcall*)(void*,float);
using VerletMethod=void(__fastcall*)(void*,float,float);
using ScaleMethod=void(__fastcall*)(void*,Vec3*);
static Method reset_transforms=nullptr,reset_particles=nullptr,apply_particles=nullptr,cache_pose=nullptr,constraints=nullptr,flush_pose=nullptr;
static VerletMethod integrate=nullptr;static ScaleMethod lossy_scale=nullptr;
static StepMethod simulate=nullptr;
struct Path {int parent_node=-1,child=-1;uintptr_t identity=0;wchar_t name[96]{};};
struct Component {uintptr_t identity=0,owner=0;unsigned path=0;};
struct Array {Component component;unsigned count=0,first=0,collider_count=0,collider_first=0;float weights[4]{},damping=0,elasticity=0,stiffness=0,inert=0,override_position=1,override_rotation=1;
    float scale_before=0;position::Vec move_before{},reference_move_before{};bool controls_dirty=false;};
struct Particle {uintptr_t token=0,bone=0;unsigned path=0;bool real=false;int parent_index=-1,parent_global=-1;float parameters[4]{},rest_length=0;
    bool synthetic=false,front_flap_tip=false;contact::Vec current{},previous{},end_local{};
    unsigned bone_index=0;double maximum_world_error=0,last_world_error=0;
    bool cloth=false,skirt=false;float skirt_radius=0,skirt_right=0,skirt_forward=0;
    contact::Vec rest_direction{},last_direction{};float radii[BodyCapsules]{},segment_radii[BodyCapsules]{};
    contact::Vec normals[BodyCapsules]{},segment_normals[BodyCapsules]{};};
struct Bone {unsigned path=0;Quat rotation{},last_rotation{};position::Vec local{},last_local{};bool dirty=false;double maximum_step_angle=0,maximum_reference_angle=0;};
struct UpperSkirt {unsigned bone=0,child_path=0;float radius[BodyCapsules]{},segment_radius[BodyCapsules]{};
    bool front_flap=false;
    bool mobile_root=false;
    int side_panel=-1;
    float root_lift=0,max_root_lift=0;
    float front_weight=0,angle_limit=0,padding=0,step_limit=0,response=0,azimuth=0,height=0,requested_lift=0;int left=-1,right=-1;};
struct ClothLink {unsigned a=0,b=0;float length=0,radii[BodyCapsules]{};};
struct ClothPanel {unsigned nodes[3]{};float radii[BodyCapsules]{};};
struct ClothArch {unsigned center=0,left=0,right=0;float rest=0;};
struct FrontSeam {
    bool bound=false,applied=false;
    unsigned bone[4]{},tip[2]{},upper_index[2]{};
    float shared_root_target=0;
    contact::Vec bind_local[3][4]{};
    float twist[4]{},max_twist=0,uncorrected_overlap=0,corrected_overlap=0;
    Quat before[4]{};
};
struct SidePanel {
    bool bound=false;
    unsigned nodes[4]{},thigh=0;
    float sign=0,depth=0;
    contact::Vec current{},target{}; // hip-local offsets, applied to both roots
};
struct SettledSample {double seconds=0,penetration=0,panel_penetration=0,particle_error=0;};
struct CollisionFrame {void* objects[512]{};contact::Vec points[512]{},previous[512]{},offsets[512]{},original[512]{},reference[512]{};float native_lengths[512]{};};
static CollisionFrame collision_frame{};
static contact::Capsule diagnostic_capsules[BodyCapsules]{};
struct Context {
    FrontSeam front_seam{};
    SidePanel side_panels[2]{};
    gimmd::physics_controls::Settings settings{};
    bool requested=false,bound=false,dirty=false;uintptr_t model=0;
    Path paths[768]{};unsigned path_count=0;
    Array arrays[32]{};unsigned array_count=0,unsupported=0;
    Particle particles[512]{};unsigned particle_count=0;
    unsigned synthetic_first=0,synthetic_count=0;
    Component colliders[256]{};unsigned collider_count=0;
    Bone bones[512]{};unsigned bone_count=0,restores=0;
    UpperSkirt upper_skirt[32]{};unsigned upper_count=0,upper_writes=0,upper_contacts=0;bool upper_calibrated=false;
    double maximum_upper_correction=0,maximum_upper_penetration=0;
    ClothLink links[256]{};unsigned link_count=0,link_contacts=0,link_iterations=0;bool upper_ring=false;
    ClothPanel panels[512]{};unsigned panel_count=0,panel_contacts=0;double current_panel_penetration=0;
    ClothArch arches[32]{};unsigned arch_count=0,arch_contacts=0;
    double maximum_link_stretch=0;
    unsigned end_hold_samples=0,end_hold_resets=0;bool last_pose=false;double maximum_hold_error=0;
    unsigned settle_remaining=0,seek_settle_steps=0;
    double current_penetration=0;SettledSample settled_samples[16]{};unsigned settled_count=0;
    unsigned transported_ends=0;
    bool mesh_diagnostic=false,mesh_diagnostic_done=false;
    double current_particle_error=0,maximum_particle_error=0;
    gimmd::secondary_physics::Clock clock{};
    unsigned steps=0,resets=0,samples=0,moved=0,failures=0;double total_ms=0,max_ms=0,max_delta=0;
    unsigned playing_samples=0,playing_steps=0;double playing_ms=0,playing_max_ms=0;
    double maximum_reference_angle=0,maximum_step_angle=0;
    bool collisions=true,contacts_calibrated=false,reapply_only=false;uintptr_t body_ids[52]{};float body_radii[BodyCapsules]{};
    unsigned contacts=0,segment_contacts=0,contact_limits=0,temporal_limits=0;float cloth_margin=0;
    double maximum_contact=0,maximum_stretch=0,maximum_remaining_penetration=0;
    unsigned worst_contact_particle=0,worst_contact_capsule=0;bool worst_contact_segment=false;
    contact::Vec hip_origin{},hip_up{},hip_right{},hip_forward{};unsigned skirt_supports=0;float minimum_skirt_radius_ratio=1;
    const char* status="disabled";
};
static Context s{};
namespace skin_proxy {static void reset();}
static const Context empty_context{};
static void reset_context(){s=empty_context;}
static bool prepare() {
    if(s.dirty)return false;reset_context();skin_proxy::reset();
    s.requested=std::filesystem::exists(gimmd::player_session::directory/L"physics-enabled.flag");
    s.collisions=!std::filesystem::exists(gimmd::player_session::directory/L"physics-native-only.flag");
    s.reapply_only=std::filesystem::exists(gimmd::player_session::directory/L"physics-reapply-check.flag");
    s.mesh_diagnostic=std::filesystem::exists(gimmd::player_session::directory/L"physics-mesh-snapshot.flag");
    s.status=s.requested?"prepared":"disabled";
    const auto controls=gimmd::player_session::directory/L"physics-controls.txt";
    if(s.requested&&std::filesystem::exists(controls)){
        std::ifstream input(controls);if(!gimmd::physics_controls::read(input,s.settings)){s.status="physics-controls-invalid";return false;}}
    if(s.requested&&(gimmd::player_session::stabilize_cloth||gimmd::player_session::stabilize_braid)){
        s.status="static-stabilization-conflict";return false;}
    return true;
}
static int find_path(uintptr_t native) {
    for(unsigned i=0;i<s.path_count;++i)if(s.paths[i].identity==native)return int(i);return -1;
}
static bool list(void* object,unsigned offset,uintptr_t& array,unsigned& count,bool nullable=false) {
    uintptr_t managed=0,length=0;int size=0;
    if(!read_bytes(reinterpret_cast<uintptr_t>(object)+offset,&managed,8))return false;
    if(!managed){array=0;count=0;return nullable;}
    if(!read_bytes(managed+0x18,&size,4)||size<0||size>512||!read_bytes(managed+0x10,&array,8))return false;
    count=unsigned(size);if(!count)return true;
    return array&&read_bytes(array+0x18,&length,8)&&length>=count&&length<=4096;
}
static bool item(uintptr_t array,unsigned index,void*& value) {
    return read_bytes(array+0x20+index*8,&value,8)&&value;
}
static bool particle_fields(void* particle,void*& bone,int& parent_node,bool& real) {
    char name[96]{};
    if(!class_name(particle,name)||std::strcmp(name,"VerletParticle")||
        !read_bytes(reinterpret_cast<uintptr_t>(particle)+0x18,&bone,8)||
        !read_bytes(reinterpret_cast<uintptr_t>(particle)+0x16c,&parent_node,4))return false;
    real=bone&&identity(bone);return true;
}
static bool bind(void* model,void* const* body,unsigned body_count) {
    reset_transforms=reinterpret_cast<Method>(entry("DynamicBoneResetTransforms",resource_base));
    reset_particles=reinterpret_cast<Method>(entry("DynamicBoneResetParticles",resource_base));
    simulate=reinterpret_cast<StepMethod>(entry("DynamicBoneStep",resource_base));
    apply_particles=reinterpret_cast<Method>(entry("DynamicBoneApply",resource_base));
    cache_pose=reinterpret_cast<Method>(entry("DynamicBoneCachePose",resource_base));
    integrate=reinterpret_cast<VerletMethod>(entry("DynamicBoneVerlet",resource_base));
    constraints=reinterpret_cast<Method>(entry("DynamicBoneConstraints",resource_base));flush_pose=reinterpret_cast<Method>(entry("DynamicBoneFlushPose",resource_base));
    lossy_scale=reinterpret_cast<ScaleMethod>(entry("DynamicBoneOwnerLossyScale",resource_base));
    if(!reset_transforms||!reset_particles||!simulate||!apply_particles||!cache_pose||!integrate||!constraints||!flush_pose||!lossy_scale||!entry("DynamicBoneThreadApply",resource_base)||!collect(model))return false;
    if(s.collisions){if(body_count!=52)return false;for(unsigned i=0;i<52;++i){wchar_t name[96]{};
        if(!hierarchy::string_read(hierarchy::name_a(body[i]),name)||std::wcscmp(name,gimmd::motion_profile::Mappings[i].target))return false;
        s.body_ids[i]=identity(body[i]);}}
    s.model=identity(model);s.path_count=tree_count;
    for(unsigned i=0;i<tree_count;++i){auto& path=s.paths[i];path.parent_node=tree[i].parent;path.identity=tree[i].identity;wcscpy_s(path.name,tree[i].name);
        if(i){path.child=0;for(unsigned j=1;j<i;++j)if(tree[j].parent==path.parent_node)++path.child;}}
    for(unsigned d=0;d<driver_lease::size;++d){const auto& lease=driver_lease::items[d];
        if(!std::strcmp(lease.name,"DynamicBoneRigidArray")){++s.unsupported;continue;}
        if(std::strcmp(lease.name,"DynamicBoneArray")||!lease.before)continue;
        void* component=nullptr;
        if(wrap(&component,reinterpret_cast<void*>(lease.component))!=&component||identity(component)!=lease.component||animator_enabled(component))return false;
        void* owner=hierarchy::component_game_object(component);const int path=find_path(identity(transform(owner)));
        if(path<0||identity(owner)!=lease.owner||s.array_count>=32)return false;
        auto& a=s.arrays[s.array_count++];a.component={lease.component,lease.owner,unsigned(path)};a.first=s.particle_count;
        const auto address=reinterpret_cast<uintptr_t>(component);uintptr_t override_param=0;
        if(!read_bytes(address+0x2c,a.weights,12)||!read_bytes(address+0x1f8,a.weights+3,4)||
            !read_bytes(address+0x60,&a.damping,4)||!read_bytes(address+0x74,&a.elasticity,4)||
            !read_bytes(address+0x80,&a.stiffness,4)||!read_bytes(address+0x94,&a.inert,4)||
            !read_bytes(address+0x100,&override_param,8))return false;
        if(!read_bytes(address+0x1bc,&a.scale_before,4)||!read_bytes(address+0x1d8,&a.move_before,12)||!read_bytes(address+0x1cc,&a.reference_move_before,12))return false;
        if(override_param&&(!read_bytes(override_param+0x60,&a.override_position,4)||!read_bytes(override_param+0x64,&a.override_rotation,4)))return false;
        uintptr_t values=0;if(!list(component,0x200,values,a.count)||!a.count||a.count>256||s.particle_count+a.count>512)return false;
        for(unsigned p=0;p<a.count;++p){void* value=nullptr,*bone=nullptr;int parent_node=-1;bool real=false;
            if(!item(values,p,value)||!particle_fields(value,bone,parent_node,real)||parent_node>=int(p)||parent_node< -1||(!real&&parent_node<0))return false;
            auto& particle=s.particles[s.particle_count++];particle.token=reinterpret_cast<uintptr_t>(value);particle.real=real;particle.parent_index=parent_node;
            particle.parent_global=parent_node<0?-1:int(a.first)+parent_node;
            for(unsigned k=0;k<4;++k){const unsigned offset[]={0x11c,0x104,0x170,0x1c8};
                if(!read_bytes(particle.token+offset[k],particle.parameters+k,4)||!std::isfinite(particle.parameters[k]))return false;}
            if(!real)continue;
            particle.bone=identity(bone);const int bone_path=find_path(particle.bone);if(bone_path<0)return false;particle.path=unsigned(bone_path);
            const auto* bone_name=s.paths[particle.path].name;
            particle.skirt=std::wcsstr(bone_name,L"Skirt")!=nullptr;
            particle.cloth=std::wcsstr(bone_name,L"Skirt")||std::wcsstr(bone_name,L"Amice")||std::wcsstr(bone_name,L"Coat");
            // Native application rotates particle parents too. Refuse any overlap
            // with the 52 MMD body targets instead of weakening overwrite checks.
            for(unsigned k=0;k<body_count;++k)if(identity(body[k])==particle.bone){s.status="body-bone-overlap-refused";return false;}
            bool seen=false;for(unsigned b=0;b<s.bone_count;++b)if(s.bones[b].path==particle.path){seen=true;particle.bone_index=b;}
            if(!seen){particle.bone_index=s.bone_count;auto& b=s.bones[s.bone_count++];b.path=particle.path;
                if(!motion::read_local(bone,&b.rotation)||!position::read(position::get,bone,&b.local))return false;}
        }
        a.collider_first=s.collider_count;
        if(!list(component,0x130,values,a.collider_count,true)||a.collider_count>64||s.collider_count+a.collider_count>256)return false;
        for(unsigned c=0;c<a.collider_count;++c){void* collider=nullptr;char name[96]{};
            if(!item(values,c,collider)||!class_name(collider,name)||
                (std::strcmp(name,"DynamicBoneCapsuleCollider")&&std::strcmp(name,"DynamicBoneSphereCollider")))return false;
            void* go=hierarchy::component_game_object(collider);const int owner_path=find_path(identity(transform(go)));
            if(owner_path<0||!identity(collider)||component_membership(go,identity(collider))!=1)return false;
            s.colliders[s.collider_count++]={identity(collider),identity(go),unsigned(owner_path)};
        }
    }
    // Barbara's central front panel is skinned to a separate CF chain that is
    // absent from DynamicBoneArray. Leaving it at rest creates a stationary dent
    // between the two simulated front panels. Own the complete missing chain.
    s.synthetic_first=s.particle_count;
    if(s.collisions)for(unsigned path=0;path<s.path_count;++path){
        if(!std::wcsstr(s.paths[path].name,L"SkirtF CF"))continue;
        const int parent_path=s.paths[path].parent_node;
        if(parent_path>=0&&std::wcsstr(s.paths[parent_path].name,L"SkirtF CF"))continue;
        unsigned chain[12]{path},length=1,current=path;
        for(;;){int next=-1;for(unsigned i=0;i<s.path_count;++i)if(s.paths[i].parent_node==int(current)){
                if(next>=0)return false;next=int(i);}
            if(next<0)break;if(length>=12||!std::wcsstr(s.paths[next].name,L"SkirtF CF"))return false;
            chain[length++]=unsigned(next);current=unsigned(next);}
        if(length<3)continue;bool present=false;
        for(unsigned p=0;p<s.particle_count;++p)for(unsigned i=0;i<length;++i)present=present||(s.particles[p].real&&s.particles[p].path==chain[i]);
        if(present)continue;if(s.particle_count+length>512||s.bone_count+length>512)return false;
        const unsigned first=s.particle_count;
        for(unsigned i=1;i<length;++i){auto& p=s.particles[s.particle_count++];p.synthetic=p.real=p.skirt=p.cloth=true;p.path=chain[i];p.bone=s.paths[p.path].identity;
            for(unsigned k=0;k<body_count;++k)if(identity(body[k])==p.bone)return false;
            p.parent_index=p.parent_global=i==1?-1:int(s.particle_count)-2;p.bone_index=s.bone_count;
            auto& b=s.bones[s.bone_count++];b.path=p.path;void* bone=tree[p.path].transform;
            if(!motion::read_local(bone,&b.rotation)||!position::read(position::get,bone,&b.local))return false;
        }
        auto& end=s.particles[s.particle_count++];end.synthetic=end.skirt=end.cloth=true;end.parent_index=end.parent_global=int(s.particle_count)-2;
        const auto& last=s.bones[s.particles[end.parent_global].bone_index];end.end_local={last.local.x,last.local.y,last.local.z};
        s.synthetic_count+=s.particle_count-first;
    }
    // Some rigs start native particles at the second skirt bone. Their parent
    // controls the upper panel and otherwise never receives a collision solve.
    if(s.collisions)for(unsigned p=0;p<s.particle_count;++p){const auto& particle=s.particles[p];
        if(!particle.real||!particle.skirt||particle.parent_index>=0)continue;
        const int parent_path=s.paths[particle.path].parent_node;
        if(parent_path<0||!std::wcsstr(s.paths[parent_path].name,L"Skirt"))continue;
        bool owned=false;for(unsigned b=0;b<s.bone_count;++b)owned=owned||s.bones[b].path==unsigned(parent_path);
        if(owned)continue;
        for(unsigned k=0;k<body_count;++k)if(identity(body[k])==s.paths[parent_path].identity)return false;
        void* parent_bone=tree[parent_path].transform;
        if(hierarchy::child_count(parent_bone)!=1||s.upper_count>=32||s.bone_count>=512)return false;
        auto& upper=s.upper_skirt[s.upper_count++];upper.bone=s.bone_count;upper.child_path=particle.path;
        auto& bone=s.bones[s.bone_count++];bone.path=unsigned(parent_path);
        if(!motion::read_local(parent_bone,&bone.rotation)||!position::read(position::get,parent_bone,&bone.local))return false;
    }
    if(!s.array_count||!s.bone_count)return false;s.bound=true;s.status="bound";return true;
}
// A skirt tip drives the orientation of its last real bone. On an open
// single-bone flap (Nahida's front E22s), it is the only movable particle.
static bool skirt_tip_contact(unsigned index) {
    const auto& tip=s.particles[index];
    if(tip.real||tip.parent_global<0)return false;
    const auto& parent_particle=s.particles[tip.parent_global];
    if(!parent_particle.real||!parent_particle.skirt)return false;
    if(s.upper_ring)return true;
    if(parent_particle.parent_global>=0)return false;
    for(unsigned i=0;i<s.particle_count;++i)
        if(i!=index&&s.particles[i].parent_global==tip.parent_global)return false;
    return true;
}
static bool surface_particle(unsigned p) {
    return s.particles[p].real||s.particles[p].skirt;
}
static void* resolve_path(unsigned index,void* model,void* (&cache)[768]) {
    if(index>=s.path_count)return nullptr;if(cache[index])return cache[index];const auto& path=s.paths[index];void* target=model;
    if(index){if(path.parent_node<0||unsigned(path.parent_node)>=index)return nullptr;void* parent_node=resolve_path(unsigned(path.parent_node),model,cache);if(!parent_node)return nullptr;
        const int count=hierarchy::child_count(parent_node);if(path.child<0||path.child>=count||count>128)return nullptr;target=hierarchy::get_child(parent_node,path.child);}
    wchar_t name[96]{};
    if(identity(target)!=path.identity||!hierarchy::string_read(hierarchy::name_a(target),name)||std::wcscmp(path.name,name))return nullptr;
    cache[index]=target;return target;
}
static void* resolve_component(const Component& c,void* model,void* (&cache)[768]) {
    void* target=resolve_path(c.path,model,cache);if(!target)return nullptr;
    void* owner=hierarchy::component_game_object(target),*value=nullptr;
    if(identity(owner)!=c.owner||component_membership(owner,c.identity)!=1||
        wrap(&value,reinterpret_cast<void*>(c.identity))!=&value||identity(value)!=c.identity||
        identity(hierarchy::component_game_object(value))!=c.owner)return nullptr;
    return value;
}
static bool resolve(void* model,void* (&arrays)[32],void* (&bones)[512]) {
    if(!model||identity(model)!=s.model)return false;void* cache[768]{};
    for(unsigned b=0;b<s.bone_count;++b){bones[b]=resolve_path(s.bones[b].path,model,cache);if(!bones[b])return false;}
    for(unsigned d=0;d<s.array_count;++d){const auto& a=s.arrays[d];arrays[d]=resolve_component(a.component,model,cache);
        if(!arrays[d]||animator_enabled(arrays[d]))return false;
        uintptr_t values=0;unsigned count=0;
        if(!list(arrays[d],0x200,values,count)||count!=a.count)return false;
        for(unsigned p=0;p<count;++p){void* value=nullptr,*bone=nullptr;int parent_node=-1;bool real=false;const auto& expected=s.particles[a.first+p];
            if(!item(values,p,value)||reinterpret_cast<uintptr_t>(value)!=expected.token||!particle_fields(value,bone,parent_node,real)||
                parent_node!=expected.parent_index||real!=expected.real||(real&&identity(bone)!=expected.bone))return false;
        }
        if(!list(arrays[d],0x130,values,count,true)||count!=a.collider_count)return false;
        for(unsigned c=0;c<count;++c){void* value=nullptr;const auto& expected=s.colliders[a.collider_first+c];
            if(!item(values,c,value)||identity(value)!=expected.identity||!resolve_component(expected,model,cache))return false;}
    }
    return true;
}
static bool put_pose(void* bone,const Bone& saved) {
    Quat actual{};if(!motion::write_local(bone,&saved.rotation,&actual))return false;
    position::set(bone,&saved.local);position::Vec observed{};
    return position::read(position::get,bone,&observed)&&position::error(observed,saved.local)<.00002f;
}
static bool body_capsules(void* const* body,unsigned count,contact::Capsule (&capsules)[BodyCapsules]) {
    if(count!=52)return false;
    constexpr unsigned endpoints[BodyCapsules][2]={{0,3},{5,5},{7,8},{8,9},{30,31},{31,32},{10,11},{11,12},{33,34},{34,35},{10,33},{10,11},{33,34}};
    contact::Vec world[52]{};bool needed[52]{};needed[4]=true;
    for(const auto& e:endpoints){needed[e[0]]=true;needed[e[1]]=true;}
    for(unsigned i=0;i<52;++i)if(needed[i]){if(identity(body[i])!=s.body_ids[i])return false;Vec3 value{};position_get(&value,body[i]);
        world[i]={value.x,value.y,value.z};if(!contact::finite(world[i]))return false;}
    const float shoulder_width=contact::length(contact::sub(world[7],world[30]));
    if(!s.contacts_calibrated)s.cloth_margin=std::clamp(contact::length(contact::sub(world[10],world[33]))*.6f,.015f,.09f);
    const auto head_axis=contact::sub(world[5],world[4]);const float head_length=contact::length(head_axis);
    if(shoulder_width<.08f||shoulder_width>2||head_length<.01f||head_length>.8f)return false;
    s.hip_origin=world[0];s.hip_up=contact::sub(world[3],world[0]);
    const float torso_length=contact::length(s.hip_up);if(torso_length<.02f)return false;s.hip_up=contact::scale(s.hip_up,1/torso_length);
    s.hip_right=contact::radial({},s.hip_up,contact::sub(world[33],world[10]));const float hip_width=contact::length(s.hip_right);
    if(hip_width<.02f)return false;s.hip_right=contact::scale(s.hip_right,1/hip_width);s.hip_forward=contact::cross(s.hip_right,s.hip_up);
    for(unsigned c=0;c<BodyCapsules;++c){auto& capsule=capsules[c];capsule.a=world[endpoints[c][0]];capsule.b=world[endpoints[c][1]];
        if(c==1){capsule.a=contact::add(capsule.a,contact::scale(head_axis,.35f));capsule.b=capsule.a;}
        if(c>=11){const auto thigh=contact::sub(capsule.b,capsule.a);capsule.b=contact::add(capsule.a,contact::scale(thigh,.35f));
            capsule.a=contact::sub(capsule.a,contact::scale(thigh,.12f));}
        if(!s.contacts_calibrated){const float segment=contact::length(contact::sub(capsule.b,capsule.a));
            s.body_radii[c]=c==0?shoulder_width*.34f:c==1?head_length*.8f:c==10?hip_width*.38f:c>=11?segment/.47f*.28f:segment*(c>=6?.2f:.16f);}
        capsule.radius=s.body_radii[c];
    }
    return true;
}
static float inner_clearance(contact::Vec at,const contact::Capsule (&capsules)[BodyCapsules]) {
    const float side=contact::dot(contact::sub(at,s.hip_origin),s.hip_right);
    const auto inner=contact::sub(at,contact::scale(s.hip_right,.8f*side));
    return std::max(contact::inner_flap_lift(at,inner,s.hip_up,capsules[6]),
                    contact::inner_flap_lift(at,inner,s.hip_up,capsules[8]));
}
static float next_root_lift(const UpperSkirt& upper,float clearance,float segment_length) {
    const float target=std::min(clearance,std::min(.06f,segment_length*.5f));
    // Match the speed of the rotational contact response at the child.
    const float step=segment_length*upper.step_limit;
    return upper.root_lift+std::clamp((target-upper.root_lift)*upper.response,-step,step);
}
static contact::Vec lift_front_flap(contact::Vec anchor,contact::Vec direction,float segment_length,
                                    const contact::Capsule (&capsules)[BodyCapsules]) {
    const float size=contact::length(direction);
    if(size<1e-6f)return direction;
    const auto unit=contact::scale(direction,1/size);
    const float lift=inner_clearance(contact::add(anchor,contact::scale(unit,segment_length)),capsules);
    if(lift<=0||segment_length<1e-6f)return direction;
    const float height=contact::dot(unit,s.hip_up);
    const auto lateral=contact::sub(unit,contact::scale(s.hip_up,height));
    const float span=contact::length(lateral);
    if(span<1e-6f)return direction;
    const float y=std::clamp(height+lift/segment_length,-1.f,1.f);
    return contact::scale(contact::add(contact::scale(s.hip_up,y),
                         contact::scale(lateral,std::sqrt(1-y*y)/span)),segment_length);
}
static bool upper_skirt_contacts(void* model,void* const* bones,const contact::Capsule (&capsules)[BodyCapsules],bool calibrate) {
    namespace rp=gimmd::rotation_preview;namespace ik=gimmd::leg_ik;void* cache[768]{};
    float lift_history[32]{};for(unsigned i=0;i<s.upper_count;++i)lift_history[i]=s.upper_skirt[i].requested_lift;
    Quat model_rotation{};contact::Vec facing{};
    if(calibrate){hierarchy::rotation_world(&model_rotation,model);if(!hierarchy::unit_quat(model_rotation))return false;
        const auto forward=ik::vector(ik::rotation({model_rotation.x,model_rotation.y,model_rotation.z,model_rotation.w}),{0,0,1});
        facing=contact::radial({},s.hip_up,{forward.x,forward.y,forward.z});const float size=contact::length(facing);if(size<.01f)return false;facing=contact::scale(facing,1/size);}
    if(!calibrate)for(auto& panel:s.side_panels)if(panel.bound){
        const auto change=contact::scale(contact::sub(panel.target,panel.current),.32f);
        const float size=contact::length(change);
        panel.current=contact::add(panel.current,contact::scale(change,size>.008f?.008f/size:1.f));
    }
    for(unsigned i=0;i<s.upper_count;++i){auto& upper=s.upper_skirt[i];const auto& saved=s.bones[upper.bone];
        void* bone=bones[upper.bone],*child=resolve_path(upper.child_path,model,cache);if(!child)return false;
        Quat previous_local{};if(!motion::read_local(bone,&previous_local)||!put_pose(bone,saved))return false;
        Vec3 origin{},end{};position_get(&origin,bone);position_get(&end,child);
        const contact::Vec anchor{origin.x,origin.y,origin.z},original{end.x,end.y,end.z};
        const auto reference=contact::sub(original,anchor);const float segment_length=contact::length(reference);
        if(!contact::finite(anchor)||!contact::finite(original)||segment_length<.001f||segment_length>2)return false;
        if(calibrate){const auto radial=contact::radial(s.hip_origin,s.hip_up,original);const float size=contact::length(radial);
            upper.azimuth=std::atan2(contact::dot(radial,s.hip_forward),contact::dot(radial,s.hip_right));
            upper.height=contact::dot(contact::sub(original,s.hip_origin),s.hip_up);
            upper.front_weight=size>.001f?std::clamp(.5f+.5f*contact::dot(radial,facing)/size,0.0f,1.0f):.5f;
            const auto* name=s.paths[saved.path].name;
            if(std::wcsstr(name,L"SkirtB"))upper.front_weight=0;else if(std::wcsstr(name,L"SkirtF"))upper.front_weight=1;
            const float front=upper.front_weight;
            const auto& settings=s.settings;
            upper.angle_limit=(settings.rear_angle+(settings.front_angle-settings.rear_angle)*front)*.017453293f;
            upper.padding=(settings.rear_padding_cm+(settings.front_padding_cm-settings.rear_padding_cm)*front)*.01f;
            upper.step_limit=(1.5f+2.5f*front)*.017453293f;
            const float response_ms=settings.rear_response_ms+(settings.front_response_ms-settings.rear_response_ms)*front;
            upper.response=1-std::exp(-float(gimmd::secondary_physics::Clock::Step)/(response_ms*.001f));
            for(unsigned c=0;c<BodyCapsules;++c){
                const auto pair=contact::closest_segment(anchor,original,capsules[c],.05f);
                const float radius=capsules[c].radius+(c>=6?upper.padding:.002f);
                upper.radius[c]=std::min(radius,std::max(0.0f,contact::distance(capsules[c],original)-.002f));
                upper.segment_radius[c]=std::min(radius,std::max(0.0f,contact::length(contact::sub(pair.point,pair.axis))-.002f));}
            continue;}
        auto point=original,previous=original;auto outward=contact::radial(s.hip_origin,s.hip_up,original);
        if(contact::length(outward)<1e-6f)continue;
        const bool front_flap=!s.upper_ring&&upper.front_weight>.7f;
        const auto left=front_flap?contact::flap_contact_direction(outward,s.hip_up,capsules[6]):outward;
        const auto right=front_flap?contact::flap_contact_direction(outward,s.hip_up,capsules[8]):outward;
        for(unsigned iteration=0;iteration<6;++iteration){contact::length_constraint(anchor,segment_length,point,reference);
            for(unsigned c=0;c<BodyCapsules;++c){
                auto push=outward;
                if(c==6||c==7||c==11)push=left;
                if(c==8||c==9||c==12)push=right;
                if(contact::project_outward(capsules[c],upper.radius[c],point,previous,push))++s.upper_contacts;
                if(contact::project_segment_outward(capsules[c],upper.segment_radius[c],anchor,anchor,point,previous,push,.05f))++s.upper_contacts;}}
        const auto outward_unit=contact::scale(outward,1/contact::length(outward));
        upper.requested_lift=std::max(0.0f,contact::dot(contact::sub(point,original),outward_unit));
        if(upper.left>=0&&upper.right>=0){const float shared=std::max({upper.requested_lift,lift_history[upper.left]*.55f,lift_history[upper.right]*.55f});
            point=contact::add(point,contact::scale(outward_unit,shared-upper.requested_lift));}
        auto direction=contact::sub(point,anchor);
        if(upper.front_flap)direction=lift_front_flap(anchor,direction,segment_length,capsules);
        contact::limit_direction(reference,direction,upper.angle_limit);
        Quat world{};hierarchy::rotation_world(&world,bone);if(!hierarchy::unit_quat(world))return false;
        const auto delta=ik::from_to({reference.x,reference.y,reference.z},{direction.x,direction.y,direction.z});
        const auto local_delta=rp::multiply(rp::multiply({-world.x,-world.y,-world.z,world.w},delta),{world.x,world.y,world.z,world.w});
        auto desired=rp::multiply({saved.rotation.x,saved.rotation.y,saved.rotation.z,saved.rotation.w},local_delta);
        const BetterEndfieldNext::Vmd::Quaternion last{previous_local.x,previous_local.y,previous_local.z,previous_local.w};
        const double cosine=std::clamp(std::abs(double(last.x)*desired.x+double(last.y)*desired.y+double(last.z)*desired.z+double(last.w)*desired.w),0.0,1.0);
        // Fixed-step exponential response eases both contact and release. Rear
        // panels respond more slowly so a hip bend does not pop them upward.
        const double angle=2*std::acos(cosine);
        const double blend=angle>1e-6?std::min(double(upper.response),double(upper.step_limit)/angle):1;
        desired=BetterEndfieldNext::Vmd::Slerp(last,desired,blend);
        const Quat requested{desired.x,desired.y,desired.z,desired.w};Quat observed{};
        if(!motion::write_local(bone,&requested,&observed))return false;++s.upper_writes;
        position_get(&end,child);contact::Vec actual{end.x,end.y,end.z};
        if(!contact::finite(actual)||std::abs(contact::length(contact::sub(actual,anchor))-segment_length)>.001f)return false;
        auto solved_anchor=anchor;
        contact::Vec translation{};
        if(upper.mobile_root&&upper.angle_limit>0){
            // put_pose restored the saved attachment before the angle solve.
            // Measure the remaining overlap there, so offsets never accumulate.
            const float required=std::max(inner_clearance(actual,capsules),s.front_seam.shared_root_target);
            upper.root_lift=next_root_lift(upper,required,segment_length);
            translation=contact::scale(s.hip_up,upper.root_lift);
            upper.max_root_lift=std::max(upper.max_root_lift,upper.root_lift);
        }
        if(upper.side_panel>=0){
            const auto shift=s.side_panels[upper.side_panel].current;
            translation=contact::add(translation,contact::add(contact::scale(s.hip_right,shift.x),
                contact::add(contact::scale(s.hip_up,shift.y),contact::scale(s.hip_forward,shift.z))));
        }
        if(contact::length(translation)>.00001f){
            solved_anchor=contact::add(anchor,translation);
            const Vec3 desired_position{solved_anchor.x,solved_anchor.y,solved_anchor.z};
            position_set(bone,&desired_position);
            position_get(&end,child);
            const contact::Vec moved{end.x,end.y,end.z};
            if(!contact::finite(moved)||contact::length(contact::sub(moved,contact::add(actual,translation)))>.001f)return false;
            actual=moved;
        }
        s.maximum_upper_correction=std::max(s.maximum_upper_correction,double(contact::length(contact::sub(actual,original))));
        for(unsigned c=0;c<BodyCapsules;++c){const auto pair=contact::closest_segment(solved_anchor,actual,capsules[c],.05f);
            s.maximum_upper_penetration=std::max(s.maximum_upper_penetration,double(upper.segment_radius[c]-contact::length(contact::sub(pair.point,pair.axis))));}
    }
    if(calibrate){gimmd::cloth_links::Vertex vertices[32]{};
        for(unsigned i=0;i<s.upper_count;++i)vertices[i]={i,s.upper_skirt[i].azimuth,s.upper_skirt[i].height};
        s.upper_ring=gimmd::cloth_links::closed_ring(vertices,s.upper_count,std::max(.05f,s.body_radii[10]*3));
        if(!s.upper_ring)for(unsigned p=0;p<s.particle_count;++p){
            if(!skirt_tip_contact(p))continue;
            const unsigned root=s.particles[s.particles[p].parent_global].path;
            for(unsigned i=0;i<s.upper_count;++i){
                auto& upper=s.upper_skirt[i];
                if(upper.child_path==root&&upper.front_weight>.7f){
                    upper.front_flap=true;
                    s.particles[p].front_flap_tip=true;
                    const auto& path=s.paths[s.bones[upper.bone].path];
                    upper.mobile_root=!std::wcscmp(s.paths[0].name,L"Avatar_Loli_Catalyst_Nahida")&&
                        path.parent_node>=0&&!std::wcscmp(s.paths[path.parent_node].name,L"Bip001 Spine1")&&
                        (!std::wcscmp(path.name,L"+SkirtS L E21")||!std::wcscmp(path.name,L"+SkirtS R E21"));
                }
            }
        }
        if(s.upper_ring)for(unsigned i=0;i<s.upper_count;++i){auto& upper=s.upper_skirt[vertices[i].index];
            upper.left=int(vertices[(i+s.upper_count-1)%s.upper_count].index);upper.right=int(vertices[(i+1)%s.upper_count].index);}}
    return true;
}
static bool calibrate_links(const contact::Capsule (&capsules)[BodyCapsules]) {
    s.link_count=0;s.panel_count=0;s.arch_count=0;if(!s.upper_ring)return true;
    int branches[512]{};float heights[512]{};
    for(unsigned p=0;p<s.particle_count;++p){branches[p]=-1;const auto& particle=s.particles[p];
        if(!surface_particle(p)||!particle.skirt||particle.parent_global<0)continue;
        unsigned root=p;while(s.particles[root].parent_global>=0)root=unsigned(s.particles[root].parent_global);
        for(unsigned i=0;i<s.upper_count;++i)if(s.upper_skirt[i].child_path==s.particles[root].path)branches[p]=int(i);
        heights[p]=contact::dot(contact::sub(collision_frame.points[p],s.hip_origin),s.hip_up);}
    for(unsigned p=0;p<s.particle_count;++p){if(branches[p]<0)continue;const auto& upper=s.upper_skirt[branches[p]];
        for(int neighbor:{upper.left,upper.right}){if(neighbor<0)continue;unsigned nearest=512;float best=100;
            for(unsigned q=0;q<s.particle_count;++q)if(branches[q]==neighbor){const float gap=std::abs(heights[p]-heights[q]);
                if(gap<best){best=gap;nearest=q;}}
            if(nearest==512||best>std::max(.05f,s.body_radii[10]*3))continue;
            bool duplicate=false;for(unsigned i=0;i<s.link_count;++i){const auto& link=s.links[i];
                duplicate=duplicate||(link.a==p&&link.b==nearest)||(link.a==nearest&&link.b==p);}
            if(duplicate)continue;if(s.link_count>=256)return false;auto& link=s.links[s.link_count++];link.a=p;link.b=nearest;
            const auto a=collision_frame.points[link.a],b=collision_frame.points[link.b];link.length=contact::length(contact::sub(b,a));
            if(link.length<.001f)return false;
            for(unsigned c=0;c<BodyCapsules;++c){const auto pair=contact::closest_segment(a,b,capsules[c]);
                link.radii[c]=std::min(capsules[c].radius+.008f,std::max(0.0f,contact::length(contact::sub(pair.point,pair.axis))-.002f));}}
    }
    for(unsigned p=s.synthetic_first;p<s.particle_count;++p){if(s.particles[p].parent_global<0)continue;unsigned neighbors[2]{},count=0;
        for(unsigned i=0;i<s.link_count;++i){const auto& link=s.links[i];if(link.a!=p&&link.b!=p)continue;
            if(count>=2)return false;neighbors[count++]=link.a==p?link.b:link.a;}
        if(count!=2)continue;if(s.arch_count>=32)return false;auto& arch=s.arches[s.arch_count++];arch.center=p;arch.left=neighbors[0];arch.right=neighbors[1];
        const auto center=collision_frame.points[p],midpoint=contact::scale(contact::add(collision_frame.points[arch.left],collision_frame.points[arch.right]),.5f);
        auto outward=contact::radial(s.hip_origin,s.hip_up,center);outward=contact::scale(outward,1/std::max(contact::length(outward),1e-6f));arch.rest=contact::dot(contact::sub(center,midpoint),outward);}
    for(unsigned i=0;i<s.link_count;++i){const auto& link=s.links[i];
        const int pa=s.particles[link.a].parent_global,pb=s.particles[link.b].parent_global;
        if(pa<0||pb<0||pa==pb)continue;
        const unsigned triangles[2][3]={{unsigned(pa),unsigned(pb),link.b},{unsigned(pa),link.b,link.a}};
        for(const auto& nodes:triangles){if(s.panel_count>=512)return false;auto& panel=s.panels[s.panel_count++];
            contact::Vec triangle[3]{};for(unsigned k=0;k<3;++k){panel.nodes[k]=nodes[k];triangle[k]=collision_frame.points[nodes[k]];}
            for(unsigned c=6;c<BodyCapsules;++c){const auto nearest=gimmd::cloth_surface::nearest_capsule(triangle,capsules[c]);
                panel.radii[c]=std::min(capsules[c].radius+.02f,std::max(0.0f,contact::length(contact::sub(nearest.point,nearest.axis))-.001f));}}}
    return true;
}
static void solve_panels(const contact::Capsule (&capsules)[BodyCapsules]) {
    auto& f=collision_frame;
    for(unsigned i=0;i<s.panel_count;++i){const auto& panel=s.panels[i];contact::Vec points[3]{},previous[3]{},center{};bool movable[3]{};
        for(unsigned k=0;k<3;++k){const unsigned p=panel.nodes[k];points[k]=f.points[p];previous[k]=f.previous[p];center=contact::add(center,points[k]);movable[k]=s.particles[p].parent_global>=0;}
        const auto outward=contact::radial(s.hip_origin,s.hip_up,contact::scale(center,1.0f/3));
        for(unsigned c=6;c<BodyCapsules;++c)if(gimmd::cloth_surface::project(capsules[c],panel.radii[c],points,previous,movable,outward))++s.panel_contacts;
        for(unsigned k=0;k<3;++k){const unsigned p=panel.nodes[k];f.points[p]=points[k];f.previous[p]=previous[k];}}
}
static void solve_links(const contact::Capsule (&capsules)[BodyCapsules]) {
    auto& f=collision_frame;
    for(unsigned i=0;i<s.arch_count;++i){const auto& arch=s.arches[i];
        if(gimmd::cloth_links::arch_constraint(f.points[arch.center],f.previous[arch.center],f.points[arch.left],f.previous[arch.left],f.points[arch.right],f.previous[arch.right],
            contact::radial(s.hip_origin,s.hip_up,f.points[arch.center]),arch.rest))++s.arch_contacts;}
    float lift[512]{},target[512]{};contact::Vec normals[512]{};
    for(unsigned p=0;p<s.particle_count;++p)if(s.particles[p].skirt&&s.particles[p].parent_global>=0){
        auto normal=contact::radial(s.hip_origin,s.hip_up,f.original[p]);const float size=contact::length(normal);
        if(size>.001f){normals[p]=contact::scale(normal,1/size);lift[p]=target[p]=std::max(0.0f,contact::dot(contact::sub(f.points[p],f.original[p]),normals[p]));}}
    // Spread a contact outward to the adjacent panel without averaging the
    // contacting point back into the leg. Read all lifts before applying any.
    for(unsigned i=0;i<s.link_count;++i){const auto& link=s.links[i];
        target[link.a]=std::max(target[link.a],lift[link.b]*.55f);target[link.b]=std::max(target[link.b],lift[link.a]*.55f);}
    for(unsigned p=0;p<s.particle_count;++p)if(target[p]>lift[p]){const auto delta=contact::scale(normals[p],(target[p]-lift[p])*.5f);
        f.points[p]=contact::add(f.points[p],delta);f.previous[p]=contact::add(f.previous[p],delta);}
    for(unsigned i=0;i<s.link_count;++i){const auto& link=s.links[i];
        gimmd::cloth_links::distance_constraint(f.points[link.a],f.previous[link.a],f.points[link.b],f.previous[link.b],link.length,.65f);
        const auto midpoint=contact::scale(contact::add(f.points[link.a],f.points[link.b]),.5f);
        const auto outward=contact::radial(s.hip_origin,s.hip_up,midpoint);
        for(unsigned c=6;c<BodyCapsules;++c)if(gimmd::cloth_links::project_pair(capsules[c],link.radii[c],f.points[link.a],f.previous[link.a],
            f.points[link.b],f.previous[link.b],outward))++s.link_contacts;
        ++s.link_iterations;}
}
#include "skinned_cloth_proxy.h"
static bool calibrate_front_seam(void* const* bones) {
    auto& seam=s.front_seam;
    if(s.settings.front_angle<=0)return true;
    unsigned side=0;
    for(unsigned u=0;u<s.upper_count;++u){
        const auto& upper=s.upper_skirt[u];
        if(!upper.mobile_root)continue;
        for(unsigned p=0;p<s.particle_count;++p){
            const auto& tip=s.particles[p];
            if(!tip.front_flap_tip||tip.parent_global<0)continue;
            const auto& joint=s.particles[tip.parent_global];
            if(joint.path!=upper.child_path)continue;
            if(side>=2||hierarchy::child_count(bones[joint.bone_index])!=0)return true;
            seam.bone[side*2]=upper.bone;seam.bone[side*2+1]=joint.bone_index;
            seam.upper_index[side]=u;
            seam.tip[side++]=p;
        }
    }
    if(side!=2)return true;
    // Register the static front-hem profile to this rig's neutral hem midpoint.
    // Upper rows are ~5.4/9.1 cm higher and 1.5/2.9 cm further back.
    const auto tip_mid=contact::scale(contact::add(collision_frame.points[seam.tip[0]],collision_frame.points[seam.tip[1]]),.5f);
    constexpr float height[]={0,.054f,.091f},depth[]={0,-.015f,-.029f};
    for(unsigned b=0;b<4;++b){
        gimmd::skinned_cloth::Frame base{};
        if(!skin_proxy::frame(bones[seam.bone[b]],base))return false;
        for(unsigned row=0;row<3;++row){
            const auto vertex=contact::add(tip_mid,contact::add(contact::scale(s.hip_up,height[row]),contact::scale(s.hip_forward,depth[row])));
            if(!gimmd::skinned_cloth::inverse(base,vertex,seam.bind_local[row][b]))return false;
        }
    }
    seam.bound=true;
    for(unsigned i=0;i<2;++i){
        auto& front=s.upper_skirt[seam.upper_index[i]];
        const bool left=!std::wcscmp(s.paths[s.bones[front.bone].path].name,L"+SkirtS L E21");
        const unsigned slot=left?0:1;
        for(unsigned u=0;u<s.upper_count;++u){
            auto& flank=s.upper_skirt[u];
            if(std::wcscmp(s.paths[s.bones[flank.bone].path].name,left?L"+SkirtS L C21":L"+SkirtS R C21"))continue;
            for(unsigned p=0;p<s.particle_count;++p){
                const auto& lower=s.particles[p];
                if(!lower.real||lower.parent_global<0||s.particles[lower.parent_global].path!=flank.child_path)continue;
                if(std::wcscmp(s.paths[lower.path].name,left?L"+SkirtS L C23":L"+SkirtS R C23"))continue;
                auto& panel=s.side_panels[slot];
                panel.nodes[0]=unsigned(s.particles[seam.tip[i]].parent_global);
                panel.nodes[1]=seam.tip[i];
                panel.nodes[2]=unsigned(lower.parent_global);panel.nodes[3]=p;
                panel.sign=left?-1.f:1.f;panel.thigh=left?6:8;panel.bound=true;
                front.side_panel=flank.side_panel=int(slot);
            }
        }
    }
    return true;
}
static void measure_side_panels(const contact::Capsule (&capsules)[BodyCapsules]){
    for(auto& panel:s.side_panels)if(panel.bound){
        contact::Vec points[4]{};
        for(unsigned i=0;i<4;++i)points[i]=collision_frame.points[panel.nodes[i]];
        const auto& thigh=capsules[panel.thigh];
        panel.depth=gimmd::cloth_surface::side_depth(points,thigh);
        const auto applied=contact::add(contact::scale(s.hip_right,panel.current.x),
            contact::add(contact::scale(s.hip_up,panel.current.y),contact::scale(s.hip_forward,panel.current.z)));
        for(auto& point:points)point=contact::sub(point,applied);
        const auto offset=gimmd::cloth_surface::side_offset(points,thigh,contact::scale(s.hip_right,panel.sign),
                                                            s.hip_up,std::min(.085f,thigh.radius*1.35f));
        panel.target={contact::dot(offset,s.hip_right),contact::dot(offset,s.hip_up),contact::dot(offset,s.hip_forward)};
    }
}
static bool support_front_seam(void* const* bones,const contact::Capsule (&capsules)[BodyCapsules]) {
    namespace sc=gimmd::skinned_cloth;
    auto& seam=s.front_seam;if(!seam.bound)return true;
    sc::FrontPose pose{};Quat original[4]{};
    for(unsigned b=0;b<4;++b){
        void* bone=bones[seam.bone[b]];sc::Frame base{};
        if(!skin_proxy::frame(bone,base)||!motion::read_local(bone,&seam.before[b]))return false;
        hierarchy::rotation_world(&original[b],bone);if(!hierarchy::unit_quat(original[b]))return false;
        pose.origin[b]=base.position;
        const unsigned tip=seam.tip[b/2];
        const auto end=collision_frame.points[b%2?tip:unsigned(s.particles[tip].parent_global)];
        const auto direction=contact::sub(end,base.position);const float length=contact::length(direction);
        if(length<.001f)return true;
        pose.axis[b]=contact::scale(direction,1/length);
        for(unsigned row=0;row<3;++row)pose.offset[row][b]=sc::vector(base,seam.bind_local[row][b]);
    }
    sc::support_front(pose,s.hip_up,capsules[6],capsules[8],std::min(.523599f,s.settings.front_angle*.0174533f),seam.twist);
    const float neutral[4]{};
    seam.uncorrected_overlap=sc::front_overlap(pose,neutral,s.hip_up,capsules[6],capsules[8]);
    seam.corrected_overlap=sc::front_overlap(pose,seam.twist,s.hip_up,capsules[6],capsules[8]);
    // Each modeled seam sample has half its weight on each flap. Remove
    // the translation already applied to obtain an absolute target for the
    // next fixed step, including when the current pose just clears the thigh.
    const float applied_lift=.5f*(s.upper_skirt[seam.upper_index[0]].root_lift+
                                  s.upper_skirt[seam.upper_index[1]].root_lift);
    seam.shared_root_target=sc::front_overlap(pose,seam.twist,s.hip_up,capsules[6],capsules[8],applied_lift);
    for(unsigned b=0;b<4;++b){
        // Child world orientation is independently based on its pre-turn pose.
        // Turning the parent about the line to its child preserves that joint.
        if(seam.twist[b]==0&&(b%2==0||seam.twist[b-1]==0))continue;
        const auto xyz=contact::scale(pose.axis[b],std::sin(seam.twist[b]*.5f));
        const auto& q=original[b];
        const auto value=gimmd::rotation_preview::multiply({xyz.x,xyz.y,xyz.z,std::cos(seam.twist[b]*.5f)},{q.x,q.y,q.z,q.w});
        const Quat requested{value.x,value.y,value.z,value.w};
        rotation_set(bones[seam.bone[b]],&requested);seam.applied=true;
        seam.max_twist=std::max(seam.max_twist,std::abs(seam.twist[b]));
    }
    return true;
}
static contact::Vec transported_reference(unsigned p,const CollisionFrame& f) {
    const int parent_index=s.particles[p].parent_global;
    if(parent_index<0)return {};
    // Native arrays carry their own bending stiffness. Supplemental chains must
    // use the reset reference shape; using their already integrated positions
    // here would accept a collapsed/inverted bend as the next step's baseline.
    const auto& reference=s.particles[p].synthetic?f.reference:f.original;
    auto direction=contact::sub(reference[p],reference[parent_index]);
    const int grandparent=s.particles[parent_index].parent_global;
    if(s.particles[parent_index].skirt&&grandparent>=0)
        direction=contact::transport_direction(contact::sub(reference[parent_index],reference[grandparent]),
            contact::sub(f.points[parent_index],f.points[grandparent]),direction);
    return direction;
}
static bool collision_pass(void* const* arrays,void* const* bones,const contact::Capsule (&capsules)[BodyCapsules],bool calibrate,bool reset_history=false) {
    for(unsigned c=0;c<BodyCapsules;++c)diagnostic_capsules[c]=capsules[c];
    const unsigned count=s.particle_count;
    auto& particles=collision_frame.objects;auto& points=collision_frame.points;auto& previous=collision_frame.previous;
    auto& offsets=collision_frame.offsets;auto& original=collision_frame.original;auto& native_lengths=collision_frame.native_lengths;
    contact::Vec synthetic_targets[32]{};
    if(s.synthetic_count>32)return false;
    for(unsigned p=s.synthetic_first;p<count;++p){auto& particle=s.particles[p];contact::Vec target{};
        if(particle.real){Vec3 world{};position_get(&world,bones[particle.bone_index]);target={world.x,world.y,world.z};}
        else {gimmd::skinned_cloth::Frame matrix{};if(!skin_proxy::frame(bones[s.particles[particle.parent_global].bone_index],matrix))return false;
            target=gimmd::skinned_cloth::point(matrix,particle.end_local);}
        synthetic_targets[p-s.synthetic_first]=target;
        collision_frame.reference[p]=target;
        if(calibrate||reset_history||particle.parent_global<0)particle.current=particle.previous=target;
        else {
            const auto next=gimmd::skinned_cloth::predict_tip(particle.current,particle.previous,target,particle.rest_length);
            particle.previous=particle.current;
            particle.current=next;
        }
        points[p]=original[p]=particle.current;previous[p]=particle.previous;offsets[p]={};
        if(particle.parent_global>=0){const auto direction=contact::sub(target,synthetic_targets[particle.parent_global-s.synthetic_first]);
            if(calibrate)particle.rest_length=contact::length(direction);native_lengths[p]=particle.rest_length;
            if(calibrate||reset_history)particle.last_direction=direction;}
    }
    for(unsigned d=0;d<s.array_count;++d){const auto& a=s.arrays[d];uintptr_t values=0;unsigned size=0;
        if(!list(arrays[d],0x200,values,size)||size!=a.count)return false;
        for(unsigned local_index=0;local_index<size;++local_index){const unsigned p=a.first+local_index;
        const auto& expected=s.particles[p];contact::Vec local{},old{};native_lengths[p]=0;
        if(!item(values,local_index,particles[p])||reinterpret_cast<uintptr_t>(particles[p])!=expected.token)return false;
        const auto address=reinterpret_cast<uintptr_t>(particles[p]);
        if(!read_bytes(address+0x108,offsets+p,12)||!read_bytes(address+0x160,&local,12)||!read_bytes(address+0x150,&old,12))return false;
        points[p]=contact::add(local,offsets[p]);previous[p]=contact::add(old,offsets[p]);original[p]=points[p];
        if(!contact::finite(points[p])||!contact::finite(previous[p]))return false;
        if(expected.parent_global>=0)native_lengths[p]=contact::length(contact::sub(points[p],points[expected.parent_global]));
        if((calibrate||reset_history)&&expected.parent_global>=0)s.particles[p].last_direction=contact::sub(points[p],points[expected.parent_global]);
        if(calibrate){auto& particle=s.particles[p];const int parent_index=particle.parent_global;
            if(skirt_tip_contact(p))particle.skirt=particle.cloth=true;
            if(particle.skirt){const auto radial=contact::radial(s.hip_origin,s.hip_up,points[p]);particle.skirt_radius=contact::length(radial);
                particle.skirt_right=contact::dot(radial,s.hip_right);particle.skirt_forward=contact::dot(radial,s.hip_forward);}
            if(parent_index>=0){particle.rest_direction=contact::sub(points[p],points[parent_index]);particle.rest_length=contact::length(particle.rest_direction);}
            for(unsigned c=0;c<BodyCapsules;++c){particle.normals[c]=contact::sub(points[p],contact::nearest(capsules[c],points[p]));
                // Preserve the model's fitted neutral pose; a coarse body proxy
                // must not push clothing outward merely because it starts inside.
                const float margin=particle.cloth&&c>=6?s.cloth_margin:.002f;
                particle.radii[c]=std::min(capsules[c].radius+margin,std::max(0.0f,contact::length(particle.normals[c])-.002f));
                if(parent_index>=0){const auto pair=contact::closest_segment(points[parent_index],points[p],capsules[c],.2f);
                    particle.segment_normals[c]=contact::sub(pair.point,pair.axis);
                    particle.segment_radii[c]=std::min(capsules[c].radius+margin,std::max(0.0f,contact::length(particle.segment_normals[c])-.002f));}}
        }
    }}
    if(calibrate)for(unsigned p=s.synthetic_first;p<count;++p){auto& particle=s.particles[p];const int parent_index=particle.parent_global;
        const auto radial=contact::radial(s.hip_origin,s.hip_up,points[p]);particle.skirt_radius=contact::length(radial);
        particle.skirt_right=contact::dot(radial,s.hip_right);particle.skirt_forward=contact::dot(radial,s.hip_forward);
        if(parent_index>=0)particle.rest_direction=contact::sub(points[p],points[parent_index]);
        for(unsigned c=0;c<BodyCapsules;++c){particle.normals[c]=contact::sub(points[p],contact::nearest(capsules[c],points[p]));
            particle.radii[c]=std::min(capsules[c].radius+(c>=6?.015f:.002f),std::max(0.0f,contact::length(particle.normals[c])-.002f));
            if(parent_index>=0){const auto pair=contact::closest_segment(points[parent_index],points[p],capsules[c],.2f);particle.segment_normals[c]=contact::sub(pair.point,pair.axis);
                particle.segment_radii[c]=std::min(capsules[c].radius+(c>=6?.015f:.002f),std::max(0.0f,contact::length(particle.segment_normals[c])-.002f));}}}
    if(calibrate)return calibrate_links(capsules)&&calibrate_front_seam(bones);
    if(reset_history)return true;
    s.current_penetration=0;
    unsigned hits=0,segment_hits=0;
    for(unsigned iteration=0;iteration<8;++iteration){solve_links(capsules);solve_panels(capsules);for(unsigned p=0;p<count;++p){const auto& particle=s.particles[p];
        if(particle.parent_global<0)continue;
        const auto original_direction=transported_reference(p,collision_frame);
        // Virtual endpoints encode orientation. Carry them with their real parent
        // instead of independently projecting an artificial point into the body.
        if(!surface_particle(p)||native_lengths[p]<1e-6f){points[p]=contact::add(points[particle.parent_global],original_direction);continue;}
        contact::length_constraint(points[particle.parent_global],native_lengths[p],points[p],original_direction);
        const auto rest_outward=contact::add(contact::scale(s.hip_right,particle.skirt_right),contact::scale(s.hip_forward,particle.skirt_forward));
        float support_radius=particle.skirt_radius*s.settings.support_percent*.01f;
        if(s.upper_ring&&particle.skirt&&s.settings.support_percent>0){const auto& parent_particle=s.particles[particle.parent_global];
            const float parent_radius=contact::length(contact::radial(s.hip_origin,s.hip_up,points[particle.parent_global]));
            support_radius=std::max(support_radius,particle.skirt_radius+std::max(0.0f,parent_radius-parent_particle.skirt_radius)*.85f);}
        if(particle.skirt&&contact::support_radius(s.hip_origin,s.hip_up,support_radius,rest_outward,points[p],previous[p]))++s.skirt_supports;
        auto outward=contact::radial(s.hip_origin,s.hip_up,points[p]);
        if(contact::length(outward)<1e-6f)outward=rest_outward;
        for(unsigned c=0;c<BodyCapsules;++c){
            if(particle.skirt&&c>=6){
                if(contact::project_outward(capsules[c],particle.radii[c],points[p],previous[p],outward))++hits;
                if(contact::project_segment_outward(capsules[c],particle.segment_radii[c],points[particle.parent_global],previous[particle.parent_global],points[p],previous[p],outward)){++hits;++segment_hits;}
                continue;}
            if(contact::project(capsules[c],particle.radii[c],points[p],previous[p],particle.normals[c]))++hits;
            if(particle.cloth&&contact::project_segment(capsules[c],particle.segment_radii[c],points[particle.parent_global],previous[particle.parent_global],
                points[p],previous[p],particle.segment_normals[c])){++hits;++segment_hits;}}
    }}
    for(unsigned p=0;p<count;++p){const auto& particle=s.particles[p];if(particle.parent_global<0)continue;
        if(!surface_particle(p)||native_lengths[p]<1e-6f){
            points[p]=contact::add(points[particle.parent_global],transported_reference(p,collision_frame));
            if(s.particles[particle.parent_global].skirt)++s.transported_ends;
            previous[p]=contact::add(previous[p],contact::sub(points[p],original[p]));continue;}
        auto direction=contact::sub(points[p],points[particle.parent_global]);
        const auto reference=transported_reference(p,collision_frame);
        const float length=contact::length(direction);
        if(length>1e-6f)direction=contact::scale(direction,native_lengths[p]/length);
        if(particle.front_flap_tip)direction=lift_front_flap(points[particle.parent_global],direction,native_lengths[p],capsules);
        // Skirts need room to move around a bent thigh. Hair keeps the tighter
        // cone; neither path permits a strand to reverse direction in one solve.
        const bool connected_child=s.upper_ring&&particle.skirt&&s.particles[particle.parent_global].parent_global>=0;
        if(contact::limit_direction(reference,direction,connected_child?.34906585f:particle.cloth?.78539816f:.20943951f))++s.contact_limits;
        // Bound actual consecutive simulation steps as well as the native-pose
        // correction. A contact changing side must not snap the skirt back.
        if(particle.cloth&&contact::limit_direction(particle.last_direction,direction,.17453293f))++s.temporal_limits;
        const auto constrained=contact::add(points[particle.parent_global],direction);
        previous[p]=contact::add(previous[p],contact::sub(constrained,points[p]));points[p]=constrained;
        s.particles[p].last_direction=direction;
    }
    // All corrections are checked before the first managed particle write.
    for(unsigned p=0;p<count;++p){const auto& particle=s.particles[p];if(particle.parent_global<0)continue;
        const float correction=contact::length(contact::sub(points[p],original[p]));
        if(!contact::finite(points[p])||!contact::finite(previous[p])||correction>std::max(.3f,particle.rest_length*4))return false;
        s.maximum_contact=std::max(s.maximum_contact,double(correction));
        if(native_lengths[p]>1e-5f)s.maximum_stretch=std::max(s.maximum_stretch,double(std::abs(contact::length(contact::sub(points[p],points[particle.parent_global]))/native_lengths[p]-1)));
        if(particle.skirt&&particle.skirt_radius>.001f)s.minimum_skirt_radius_ratio=std::min(s.minimum_skirt_radius_ratio,
            contact::length(contact::radial(s.hip_origin,s.hip_up,points[p]))/particle.skirt_radius);
        if(surface_particle(p))for(unsigned c=0;c<BodyCapsules;++c){
            const auto remaining=[&](float depth,bool segment){s.current_penetration=std::max(s.current_penetration,double(depth));if(depth>s.maximum_remaining_penetration){
                s.maximum_remaining_penetration=depth;s.worst_contact_particle=p;s.worst_contact_capsule=c;s.worst_contact_segment=segment;}};
            remaining(particle.radii[c]-contact::distance(capsules[c],points[p]),false);
            if(particle.cloth){const auto pair=contact::closest_segment(points[particle.parent_global],points[p],capsules[c],.2f);
                remaining(particle.segment_radii[c]-contact::length(contact::sub(pair.point,pair.axis)),true);}}
    }
    for(unsigned p=0;p<count&&!s.reapply_only;++p){if(s.particles[p].parent_global<0)continue;
        if(s.particles[p].synthetic){s.particles[p].current=points[p];s.particles[p].previous=previous[p];continue;}
        const auto local=contact::sub(points[p],offsets[p]),old=contact::sub(previous[p],offsets[p]);
        auto* address=static_cast<uint8_t*>(particles[p]);std::memcpy(address+0x160,&local,12);std::memcpy(address+0x150,&old,12);
    }
    for(unsigned i=0;i<s.link_count;++i){const auto& link=s.links[i];
        s.maximum_link_stretch=std::max(s.maximum_link_stretch,double(std::abs(contact::length(contact::sub(points[link.a],points[link.b]))/link.length-1)));}
    s.current_panel_penetration=0;
    for(unsigned i=0;i<s.panel_count;++i){const auto& panel=s.panels[i];contact::Vec triangle[3]{};
        for(unsigned k=0;k<3;++k)triangle[k]=points[panel.nodes[k]];
        for(unsigned c=6;c<BodyCapsules;++c){const auto nearest=gimmd::cloth_surface::nearest_capsule(triangle,capsules[c]);
            s.current_panel_penetration=std::max(s.current_panel_penetration,double(panel.radii[c]-contact::length(contact::sub(nearest.point,nearest.axis))));}}
    s.contacts+=hits;s.segment_contacts+=segment_hits;return true;
}
static bool apply_synthetic(void* const* bones){
    namespace rp=gimmd::rotation_preview;namespace ik=gimmd::leg_ik;
    for(unsigned p=s.synthetic_first;p<s.particle_count;++p){const auto& particle=s.particles[p];if(particle.parent_global<0)continue;
        const auto& ancestor=s.particles[particle.parent_global];void* bone=bones[ancestor.bone_index];gimmd::skinned_cloth::Frame matrix{};
        if(!skin_proxy::frame(bone,matrix))return false;contact::Vec reference{};
        if(particle.real){Vec3 child{};position_get(&child,bones[particle.bone_index]);reference=contact::sub({child.x,child.y,child.z},matrix.position);}
        else reference=gimmd::skinned_cloth::vector(matrix,particle.end_local);
        const auto direction=contact::sub(collision_frame.points[p],collision_frame.points[particle.parent_global]);
        const auto delta=ik::from_to({reference.x,reference.y,reference.z},{direction.x,direction.y,direction.z});
        Quat world{};hierarchy::rotation_world(&world,bone);if(!hierarchy::unit_quat(world))return false;
        const auto rotated=rp::multiply(delta,{world.x,world.y,world.z,world.w});const Quat requested{rotated.x,rotated.y,rotated.z,rotated.w};rotation_set(bone,&requested);
        if(particle.real){const auto point=collision_frame.points[p];const Vec3 desired{point.x,point.y,point.z};position_set(bones[particle.bone_index],&desired);}
    }
    return true;
}
static bool integrate_with_contacts(void* component,Array& a) {
    // Match the owned portion of UpdateDynamicBones, inserting contacts before
    // its single ApplyParticlesToTransforms call. Reapplying is not idempotent.
    auto* address=static_cast<uint8_t*>(component);void* owner=transform(hierarchy::component_game_object(component));
    if(!owner||identity(owner)!=s.paths[a.component.path].identity)return false;
    Vec3 world{},scale{};position_get(&world,owner);lossy_scale(owner,&scale);
    position::Vec previous{};int mode=0;unsigned char correct_rotation=0;
    if(!read_bytes(reinterpret_cast<uintptr_t>(component)+0x1e4,&previous,12)||!read_bytes(reinterpret_cast<uintptr_t>(component)+0x5c,&mode,4)||
        !read_bytes(reinterpret_cast<uintptr_t>(component)+0x39,&correct_rotation,1)||mode<0||mode>4||correct_rotation>1)return false;
    const position::Vec current{world.x,world.y,world.z},move{world.x-previous.x,world.y-previous.y,world.z-previous.z},zero{};
    const float object_scale=std::abs(scale.x);
    if(!position::finite(current)||!position::finite(move)||!std::isfinite(object_scale)||object_scale<.001f||object_scale>100)return false;
    a.controls_dirty=true;std::memcpy(address+0x1bc,&object_scale,4);std::memcpy(address+0x1cc,&zero,12);
    std::memcpy(address+0x1d8,&move,12);std::memcpy(address+0x1e4,&current,12);
    const float step=float(gimmd::secondary_physics::Clock::Step);
    integrate(component,step,mode==3?step:-1.0f);constraints(component);std::memcpy(address+0x1d8,&zero,12);
    cache_pose(component);if(!correct_rotation)reset_transforms(component);
    return true;
}
// Opt-in, one-shot diagnostics on a paused pose. Bake into an owned mesh copy;
// never replace a renderer mesh or write to a shared game asset.
static void dump_mesh(void* renderer,void* copy,std::ostream& out) {
    using Bake=void(__fastcall*)(void*,void*);using Matrix=void(__fastcall*)(void*,float*);
    const auto bake=reinterpret_cast<Bake>(entry("DiagnosticMeshBake",resource_base));
    const auto vertices=reinterpret_cast<GetTransformFn>(entry("DiagnosticMeshVertices",resource_base));
    const auto triangles=reinterpret_cast<GetTransformFn>(entry("DiagnosticMeshTriangles",resource_base));
    const auto matrix=reinterpret_cast<Matrix>(entry("DiagnosticRendererMatrix",resource_base));
    const auto readable=reinterpret_cast<ActiveFn>(entry("DiagnosticMeshReadable",resource_base));
    if(!bake||!vertices||!triangles||!matrix||!readable)return;
    bake(renderer,copy);readable(copy,true);float m[16]{};matrix(renderer,m);
    for(float v:m)if(!std::isfinite(v))return;
    void* values=vertices(copy);uintptr_t count=0;
    if(!values||!read_bytes(reinterpret_cast<uintptr_t>(values)+0x18,&count,8)||count>100000)return;
    std::vector<contact::Vec> points(count);
    if(count&&!read_bytes(reinterpret_cast<uintptr_t>(values)+0x20,points.data(),count*sizeof(contact::Vec)))return;
    if(skin_proxy::state.ready&&identity(renderer)==skin_proxy::state.renderer){
        for(unsigned i=0;i<skin_proxy::state.count;++i){const auto& sample=skin_proxy::state.samples[i];if(sample.vertex>=points.size())continue;const auto p=points[sample.vertex];
            const contact::Vec world{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
            skin_proxy::state.mesh_error=std::max(skin_proxy::state.mesh_error,double(contact::length(contact::sub(world,skin_proxy::evaluate(sample)))));}}
    out<<",\"vertices\":[";
    for(size_t i=0;i<points.size();++i){const auto p=points[i];const contact::Vec world{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
        const auto d=contact::sub(world,s.hip_origin);out<<(i?",":"")<<'['<<contact::dot(d,s.hip_right)<<','<<contact::dot(d,s.hip_up)<<','<<contact::dot(d,s.hip_forward)<<']';}
    out<<']';values=triangles(copy);count=0;
    if(!values||!read_bytes(reinterpret_cast<uintptr_t>(values)+0x18,&count,8)||count>600000)return;
    std::vector<unsigned> indices(count);if(count&&!read_bytes(reinterpret_cast<uintptr_t>(values)+0x20,indices.data(),count*4))return;
    out<<",\"indices\":[";for(size_t i=0;i<indices.size();++i)out<<(i?",":"")<<indices[i];out<<']';
}
static void dump_mesh_guarded(void* renderer,void* source,std::ostream& out) {
    const auto copy_mesh=reinterpret_cast<GetTransformFn>(entry("DiagnosticMeshClone",resource_base));if(!copy_mesh)return;
    void* copy=nullptr;
    __try {copy=copy_mesh(source);if(copy&&identity(copy)&&identity(copy)!=identity(source))dump_mesh(renderer,copy,out);}
    __finally {if(copy&&identity(copy)&&identity(copy)!=identity(source))destroy(copy,false);}
}
static void mesh_snapshot(void* model) {
    s.mesh_diagnostic_done=true;if(!collect(model))return;
    std::ofstream out(gimmd::player_session::directory/L"physics-mesh-snapshot.json");out<<"[";bool first=true;
    for(unsigned i=0;i<tree_count;++i){void* items[64]{};unsigned count=0;
        if(!components(hierarchy::component_game_object(tree[i].transform),items,count))continue;
        for(unsigned j=0;j<count;++j){char name[96]{};if(!class_name(items[j],name)||std::strcmp(name,"SkinnedMeshRenderer"))continue;
            void* source=mesh_get(items[j]);if(!identity(source))continue;
            out<<(first?"":",")<<"{\"renderer\":";first=false;hierarchy::json_string(out,tree[i].name);
            dump_mesh_guarded(items[j],source,out);out<<'}';}}
    out<<"]";
}
static bool run(void* model,double seconds,uint64_t epoch,bool playing,void* const* body,unsigned count) {
    if(!s.requested)return true;
    if(!s.bound&&!bind(model,body,count))return false;
    auto plan=s.clock.sample(seconds,epoch,playing);if(!plan.valid)return false;
    if(playing)s.settle_remaining=0;
    else if(plan.reset)s.settle_remaining=24;
    if(!playing&&s.settle_remaining){const unsigned settle=std::min(4u,s.settle_remaining);
        plan.steps+=settle;s.settle_remaining-=settle;s.seek_settle_steps+=settle;}
    void* arrays[32]{},*bones[512]{};if(!resolve(model,arrays,bones))return false;
    contact::Capsule capsules[BodyCapsules]{};if(s.collisions&&!body_capsules(body,count,capsules))return false;
    const bool end_hold=gimmd::player_session::holding_end.load();
    if(end_hold){++s.end_hold_samples;if(plan.reset)++s.end_hold_resets;}
    if(!plan.reset&&!plan.steps){
        if(s.mesh_diagnostic&&!s.mesh_diagnostic_done&&!playing&&std::abs(seconds-1.84)<.001)mesh_snapshot(model);
        if(!playing&&s.last_pose)for(unsigned b=0;b<s.bone_count;++b){Quat rotation{};position::Vec local{};
            if(!motion::read_local(bones[b],&rotation)||!position::read(position::get,bones[b],&local))return false;
            s.maximum_hold_error=std::max(s.maximum_hold_error,std::max(double(hierarchy::roundtrip::error(rotation,s.bones[b].last_rotation)),double(position::error(local,s.bones[b].last_local))));}
        s.status=playing?"waiting-for-step":"paused";return true;}
    s.dirty=true;for(unsigned b=0;b<s.bone_count;++b)s.bones[b].dirty=true;
    if(plan.reset){
        s.front_seam.applied=false;s.front_seam.shared_root_target=0;
        for(auto& panel:s.side_panels){panel.current={};panel.target={};}
        for(float& angle:s.front_seam.twist)angle=0;
        for(unsigned i=0;i<s.upper_count;++i){s.upper_skirt[i].requested_lift=0;s.upper_skirt[i].root_lift=0;}
        for(unsigned b=0;b<s.bone_count;++b)if(!put_pose(bones[b],s.bones[b]))return false;
        // Seed particles from the same native rest transforms used by the next
        // integration step, not from the Avatar reference snapshot. Mixing the
        // two can inject a large apparent velocity after a paused seek.
        for(unsigned d=0;d<s.array_count;++d){reset_transforms(arrays[d]);reset_particles(arrays[d]);}++s.resets;
    }
    if(s.collisions&&!s.upper_calibrated){if(!upper_skirt_contacts(model,bones,capsules,true))return false;s.upper_calibrated=true;}
    if(s.collisions&&!s.contacts_calibrated){if(!collision_pass(arrays,bones,capsules,true))return false;s.contacts_calibrated=true;}
    else if(s.collisions&&plan.reset){if(!collision_pass(arrays,bones,capsules,false,true))return false;}
    if(s.collisions&&!skin_proxy::calibrate(model,bones,capsules))return false;
    position::Vec before[512]{};Quat before_rotation[512]{};
    for(unsigned b=0;b<s.bone_count;++b)if(!position::read(position::get,bones[b],&before[b])||!motion::read_local(bones[b],&before_rotation[b]))return false;
    for(unsigned step=0;step<plan.steps;++step){
        if(s.front_seam.applied){
            for(unsigned b=0;b<4;++b){
                Quat observed{};
                if(!motion::write_local(bones[s.front_seam.bone[b]],&s.front_seam.before[b],&observed))return false;
            }
            s.front_seam.applied=false;
        }
        if(s.collisions&&!upper_skirt_contacts(model,bones,capsules,false))return false;
        for(unsigned p=s.synthetic_first;p<s.particle_count;++p)if(s.particles[p].real){const unsigned b=s.particles[p].bone_index;if(!put_pose(bones[b],s.bones[b]))return false;}
        for(unsigned d=0;d<s.array_count;++d){reset_transforms(arrays[d]);
            if(s.collisions){if(!integrate_with_contacts(arrays[d],s.arrays[d]))return false;}
            else simulate(arrays[d],float(gimmd::secondary_physics::Clock::Step));}
        if(s.collisions){if(!skin_proxy::capture(bones)||!collision_pass(arrays,bones,capsules,false))return false;
            for(unsigned d=0;d<s.array_count;++d){apply_particles(arrays[d]);flush_pose(arrays[d]);}
            if(!apply_synthetic(bones))return false;
            if(!support_front_seam(bones,capsules))return false;
            measure_side_panels(capsules);
            if(!skin_proxy::validate_frames(bones))return false;
            s.current_particle_error=0;
            for(unsigned p=0;p<s.particle_count;++p){auto& particle=s.particles[p];if(!particle.real)continue;
                Vec3 value{};position_get(&value,bones[particle.bone_index]);const contact::Vec actual{value.x,value.y,value.z};if(!contact::finite(actual))return false;
                particle.last_world_error=contact::length(contact::sub(actual,collision_frame.points[p]));
                particle.maximum_world_error=std::max(particle.maximum_world_error,particle.last_world_error);
                s.current_particle_error=std::max(s.current_particle_error,particle.last_world_error);}
            s.maximum_particle_error=std::max(s.maximum_particle_error,s.current_particle_error);}
        ++s.steps;
    }
    if(!playing&&plan.steps&&!s.settle_remaining&&s.settled_count<16)s.settled_samples[s.settled_count++]={seconds,s.current_penetration,s.current_panel_penetration,s.current_particle_error};
    for(unsigned b=0;b<s.bone_count;++b){Quat rotation{};position::Vec current{};
        if(!motion::read_local(bones[b],&rotation)||!position::read(position::get,bones[b],&current))return false;
        const double distance=position::error(current,s.bones[b].local);
        if(!std::isfinite(distance)||distance>5)return false;
        const double delta=position::error(current,before[b]);s.max_delta=std::max(s.max_delta,delta);
        const auto angle=[](const Quat& a,const Quat& v){return 114.591559*std::acos(std::clamp(std::abs(double(a.x)*v.x+double(a.y)*v.y+double(a.z)*v.z+double(a.w)*v.w),0.0,1.0));};
        s.maximum_reference_angle=std::max(s.maximum_reference_angle,angle(rotation,s.bones[b].rotation));
        s.maximum_step_angle=std::max(s.maximum_step_angle,angle(rotation,before_rotation[b]));
        s.bones[b].maximum_step_angle=std::max(s.bones[b].maximum_step_angle,angle(rotation,before_rotation[b]));
        s.bones[b].maximum_reference_angle=std::max(s.bones[b].maximum_reference_angle,angle(rotation,s.bones[b].rotation));
        if(delta>.00001||hierarchy::roundtrip::error(rotation,s.bones[b].rotation)>.00001)++s.moved;
        s.bones[b].last_rotation=rotation;s.bones[b].last_local=current;
    }
    s.last_pose=true;s.status="simulating";return true;
}
static bool tick(void* model,double seconds,uint64_t epoch,bool playing,void* const* body,unsigned count) {
    if(!s.requested)return true;LARGE_INTEGER start{},end{},freq{};QueryPerformanceCounter(&start);QueryPerformanceFrequency(&freq);bool ok=false;
    const unsigned steps_before=s.steps,resets_before=s.resets;
    __try {ok=run(model,seconds,epoch,playing,body,count);}__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    for(auto& node:tree)node={};tree_count=0;
    QueryPerformanceCounter(&end);const double ms=double(end.QuadPart-start.QuadPart)*1000/freq.QuadPart;
    ++s.samples;s.total_ms+=ms;s.max_ms=std::max(s.max_ms,ms);
    // Exclude seeks, initialization and paused/held samples from playback cost.
    // Counting substeps also reveals catch-up work when rendering slows down.
    if(ok&&playing&&s.resets==resets_before&&s.steps>steps_before){
        ++s.playing_samples;s.playing_steps+=s.steps-steps_before;s.playing_ms+=ms;s.playing_max_ms=std::max(s.playing_max_ms,ms);}
    if(!ok){++s.failures;if(!std::strcmp(s.status,"prepared")||!std::strcmp(s.status,"bound")||s.dirty)s.status="physics-refused";}
    return ok;
}
static bool restore(void* model) {
    if(!s.dirty)return true;bool ok=false;
    __try {void* arrays[32]{},*bones[512]{};if(resolve(model,arrays,bones)){
        ok=true;for(unsigned b=0;b<s.bone_count;++b)if(s.bones[b].dirty){
            if(put_pose(bones[b],s.bones[b])){s.bones[b].dirty=false;++s.restores;}else ok=false;}
        if(ok){for(unsigned d=0;d<s.array_count;++d){reset_particles(arrays[d]);auto& a=s.arrays[d];if(a.controls_dirty){auto* address=static_cast<uint8_t*>(arrays[d]);
                std::memcpy(address+0x1bc,&a.scale_before,4);std::memcpy(address+0x1d8,&a.move_before,12);std::memcpy(address+0x1cc,&a.reference_move_before,12);a.controls_dirty=false;}}
            s.dirty=false;}
    }}__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    s.status=ok?"restored":"restore-pending";return ok;
}
static bool release_ready(){return !s.dirty;}
static void report(std::ostream& out) {
    out<<",\n  \"secondaryPhysics\":{\"requested\":"<<(s.requested?"true":"false")<<",\"status\":\""<<s.status
       <<"\",\"arrays\":"<<s.array_count<<",\"particles\":"<<s.particle_count<<",\"bones\":"<<s.bone_count
       <<",\"colliders\":"<<s.collider_count<<",\"unsupportedRigidArrays\":"<<s.unsupported<<",\"steps\":"<<s.steps
       <<",\"resets\":"<<s.resets<<",\"restores\":"<<s.restores<<",\"movedSamples\":"<<s.moved<<",\"failures\":"<<s.failures
       <<",\"averageMs\":"<<(s.samples?s.total_ms/s.samples:0)<<",\"maximumMs\":"<<s.max_ms<<",\"maximumLocalDelta\":"<<s.max_delta
       <<",\"playingSamples\":"<<s.playing_samples<<",\"playingSteps\":"<<s.playing_steps
       <<",\"playingAverageMs\":"<<(s.playing_samples?s.playing_ms/s.playing_samples:0)<<",\"playingMaximumMs\":"<<s.playing_max_ms
       <<",\"playingMsPerStep\":"<<(s.playing_steps?s.playing_ms/s.playing_steps:0)
       <<",\"maximumReferenceAngle\":"<<s.maximum_reference_angle<<",\"maximumStepAngle\":"<<s.maximum_step_angle
       <<",\"reapplyOnly\":"<<(s.reapply_only?"true":"false")
       <<",\"bodyCapsules\":"<<(s.contacts_calibrated?BodyCapsules:0)<<",\"contacts\":"<<s.contacts<<",\"contactLimits\":"<<s.contact_limits<<",\"maximumContactCorrection\":"<<s.maximum_contact<<",\"maximumRelativeStretch\":"<<s.maximum_stretch
       <<",\"segmentContacts\":"<<s.segment_contacts<<",\"clothMargin\":"<<s.cloth_margin<<",\"maximumRemainingPenetration\":"<<s.maximum_remaining_penetration
       <<",\"temporalLimits\":"<<s.temporal_limits
       <<",\"skirtSupports\":"<<s.skirt_supports<<",\"minimumSkirtRadiusRatio\":"<<s.minimum_skirt_radius_ratio
       <<",\"upperSkirtBones\":"<<s.upper_count<<",\"upperSkirtWrites\":"<<s.upper_writes<<",\"upperSkirtContacts\":"<<s.upper_contacts
       <<",\"frontSeamBound\":"<<(s.front_seam.bound?"true":"false")<<",\"frontSeamMaxTwistDegrees\":"<<s.front_seam.max_twist*57.2957795
       <<",\"frontSeamEstimatedOverlapBefore\":"<<s.front_seam.uncorrected_overlap<<",\"frontSeamEstimatedOverlapAfter\":"<<s.front_seam.corrected_overlap
       <<",\"frontSeamRequestedRootLift\":"<<s.front_seam.shared_root_target
       <<",\"sidePanelOverlapLR\":["<<s.side_panels[0].depth<<','<<s.side_panels[1].depth<<']'
       <<",\"sidePanelOffsetLR\":["<<contact::length(s.side_panels[0].current)<<','<<contact::length(s.side_panels[1].current)<<']'
       <<",\"sidePanelBoundLR\":["<<int(s.side_panels[0].bound)<<','<<int(s.side_panels[1].bound)<<']'
       <<",\"closedSkirtRing\":"<<(s.upper_ring?"true":"false")<<",\"lateralLinks\":"<<s.link_count<<",\"lateralContacts\":"<<s.link_contacts<<",\"lateralIterations\":"<<s.link_iterations<<",\"maximumLateralStretch\":"<<s.maximum_link_stretch
       <<",\"clothPanels\":"<<s.panel_count<<",\"panelContacts\":"<<s.panel_contacts<<",\"currentPanelPenetration\":"<<s.current_panel_penetration
       <<",\"endHoldSamples\":"<<s.end_hold_samples<<",\"endHoldResets\":"<<s.end_hold_resets<<",\"maximumHoldPoseError\":"<<s.maximum_hold_error
       <<",\"seekSettleSteps\":"<<s.seek_settle_steps
       <<",\"transportedClothEnds\":"<<s.transported_ends
       <<",\"maximumParticleBoneError\":"<<s.maximum_particle_error
       <<",\"supplementalSkirtParticles\":"<<s.synthetic_count
       <<",\"clothArches\":"<<s.arch_count<<",\"archCorrections\":"<<s.arch_contacts
       <<",\"skinProxy\":{\"status\":\""<<skin_proxy::state.status<<"\",\"samples\":"<<skin_proxy::state.count<<",\"nodes\":"<<skin_proxy::state.node_count
       <<",\"contacts\":"<<skin_proxy::state.contacts<<",\"frameError\":"<<skin_proxy::state.frame_error<<",\"meshError\":"<<skin_proxy::state.mesh_error
       <<",\"penetration\":"<<skin_proxy::state.penetration<<",\"calibrationMs\":"<<skin_proxy::state.calibration_ms<<"}"
       <<",\"maximumUpperCorrection\":"<<s.maximum_upper_correction<<",\"maximumUpperPenetration\":"<<s.maximum_upper_penetration
       <<",\"worstContactCapsule\":"<<s.worst_contact_capsule<<",\"worstContactSegment\":"<<(s.worst_contact_segment?"true":"false")<<",\"worstContactBone\":";
    hierarchy::json_string(out,s.paths[s.particles[s.worst_contact_particle].path].name);
    out<<",\"cleanupPending\":"<<(s.dirty?"true":"false")<<",\"chains\":[";
    for(unsigned d=0;d<s.array_count;++d){const auto& a=s.arrays[d];out<<(d?",":"")<<"{\"root\":";
        hierarchy::json_string(out,s.paths[s.particles[a.first].path].name);
        out<<",\"particles\":"<<a.count<<",\"weights\":["<<a.weights[0]<<','<<a.weights[1]<<','<<a.weights[2]<<','<<a.weights[3]
           <<"],\"damping\":"<<a.damping<<",\"elasticity\":"<<a.elasticity<<",\"stiffness\":"<<a.stiffness<<",\"inert\":"<<a.inert
           <<",\"positionWeight\":"<<a.override_position<<",\"rotationWeight\":"<<a.override_rotation<<",\"particleParameters\":[";
        for(unsigned p=0;p<a.count;++p){const auto& particle=s.particles[a.first+p];out<<(p?",":"")<<'['<<particle.parameters[0]<<','<<particle.parameters[1]<<','<<particle.parameters[2]<<','<<particle.parameters[3]<<']';}
        out<<"]}";}
    out<<"],\"boneAngles\":[";
    for(unsigned b=0;b<s.bone_count;++b){const auto& bone=s.bones[b];out<<(b?",":"")<<"{\"name\":";hierarchy::json_string(out,s.paths[bone.path].name);
        out<<",\"step\":"<<bone.maximum_step_angle<<",\"reference\":"<<bone.maximum_reference_angle<<"}";}
    out<<"],\"settledSamples\":[";
    for(unsigned i=0;i<s.settled_count;++i)out<<(i?",":"")<<"{\"seconds\":"<<s.settled_samples[i].seconds<<",\"penetration\":"<<s.settled_samples[i].penetration<<",\"panelPenetration\":"<<s.settled_samples[i].panel_penetration<<",\"particleBoneError\":"<<s.settled_samples[i].particle_error<<"}";
    out<<"],\"settings\":{\"frontAngle\":"<<s.settings.front_angle<<",\"rearAngle\":"<<s.settings.rear_angle
       <<",\"frontPaddingCm\":"<<s.settings.front_padding_cm<<",\"rearPaddingCm\":"<<s.settings.rear_padding_cm
       <<",\"frontResponseMs\":"<<s.settings.front_response_ms<<",\"rearResponseMs\":"<<s.settings.rear_response_ms
       <<",\"skirtSupportPercent\":"<<s.settings.support_percent<<"},\"upperSkirtProfiles\":[";
    for(unsigned i=0;i<s.upper_count;++i){const auto& upper=s.upper_skirt[i];out<<(i?",":"")<<"{\"name\":";
        hierarchy::json_string(out,s.paths[s.bones[upper.bone].path].name);
        out<<",\"frontWeight\":"<<upper.front_weight<<",\"angleLimitDegrees\":"<<upper.angle_limit*57.2957795
           <<",\"padding\":"<<upper.padding<<",\"stepLimitDegrees\":"<<upper.step_limit*57.2957795<<",\"response\":"<<upper.response
           <<",\"mobileRoot\":"<<(upper.mobile_root?"true":"false")<<",\"rootLift\":"<<upper.root_lift<<",\"maximumRootLift\":"<<upper.max_root_lift<<"}";}
    out<<"],\"particleBoneErrors\":[";bool first=true;
    for(unsigned p=0;p<s.particle_count;++p){const auto& particle=s.particles[p];if(!particle.real)continue;
        out<<(first?"":",")<<"{\"name\":";first=false;hierarchy::json_string(out,s.paths[particle.path].name);
        out<<",\"maximum\":"<<particle.maximum_world_error<<",\"last\":"<<particle.last_world_error<<"}";}
    out<<"],\"collisionGeometry\":{\"particles\":[";
    const auto vector=[&](contact::Vec p){const auto d=contact::sub(p,s.hip_origin);out<<'['<<contact::dot(d,s.hip_right)<<','<<contact::dot(d,s.hip_up)<<','<<contact::dot(d,s.hip_forward)<<']';};
    for(unsigned p=0;p<s.particle_count;++p){const auto& particle=s.particles[p];out<<(p?",":"")<<"{\"name\":";
        hierarchy::json_string(out,particle.real?s.paths[particle.path].name:L"endpoint");out<<",\"parent\":"<<particle.parent_global<<",\"restRadius\":"<<particle.skirt_radius<<",\"native\":";
        vector(collision_frame.original[p]);out<<",\"solved\":";vector(collision_frame.points[p]);out<<'}';}
    out<<"],\"capsules\":[";
    for(unsigned c=0;c<BodyCapsules;++c){out<<(c?",":"")<<"{\"a\":";vector(diagnostic_capsules[c].a);out<<",\"b\":";vector(diagnostic_capsules[c].b);out<<",\"radius\":"<<diagnostic_capsules[c].radius<<'}';}
    out<<"]}}";
}
}
