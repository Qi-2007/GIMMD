#pragma once
// Included inside secondary_physics. Calibration reads an owned baked mesh and
// measures subtree weights with reversible translations. Playback skins only
// sparse CPU samples; it does not bake meshes or allocate managed arrays per step.
namespace skin_proxy {
namespace sc=gimmd::skinned_cloth;
using MatrixFn=void(__fastcall*)(void*,float*);
using BakeFn=void(__fastcall*)(void*,void*);
struct Node {unsigned bone=0;int particle=-1,child=-1,parent=-1;sc::Frame base{},predicted{};contact::Vec reference{};};
struct Influence {unsigned node=0;float weight=0;contact::Vec local{};};
struct Sample {unsigned vertex=0,count=0;Influence influences[8]{};float radii[BodyCapsules]{};};
struct State {Node nodes[32]{};unsigned node_count=0,count=0,contacts=0;Sample samples[768]{};
    bool attempted=false,ready=false,observe=false;uintptr_t renderer=0;double mesh_error=0,frame_error=0,penetration=0,calibration_ms=0;
    const char* status="not-calibrated";};
static State state{};static const State empty{};
static void reset(){state=empty;}
static bool frame(void* object,sc::Frame& result,bool renderer=false){
    const auto get=reinterpret_cast<MatrixFn>(entry(renderer?"DiagnosticRendererMatrix":"ClothTransformMatrix",resource_base));if(!get)return false;
    float m[16]{};get(object,m);for(float v:m)if(!std::isfinite(v))return false;
    result={{{m[0],m[1],m[2]},{m[4],m[5],m[6]},{m[8],m[9],m[10]}},{m[12],m[13],m[14]}};return true;
}
static bool bake(void* renderer,void* copy,std::vector<contact::Vec>& points){
    const auto call=reinterpret_cast<BakeFn>(entry("DiagnosticMeshBake",resource_base));
    const auto readable=reinterpret_cast<ActiveFn>(entry("DiagnosticMeshReadable",resource_base));
    const auto vertices=reinterpret_cast<GetTransformFn>(entry("DiagnosticMeshVertices",resource_base));
    if(!call||!readable||!vertices)return false;call(renderer,copy);readable(copy,true);
    void* values=vertices(copy);uintptr_t count=0;sc::Frame matrix{};
    if(!values||!read_bytes(reinterpret_cast<uintptr_t>(values)+0x18,&count,8)||count==0||count>100000||!frame(renderer,matrix,true))return false;
    points.resize(count);if(!read_bytes(reinterpret_cast<uintptr_t>(values)+0x20,points.data(),count*sizeof(contact::Vec)))return false;
    for(auto& p:points){p=sc::point(matrix,p);if(!contact::finite(p))return false;}return true;
}
static bool displaced(void* renderer,void* copy,void* bone,position::Vec original,std::vector<contact::Vec>& points){
    bool ok=false;const position::Vec moved{original.x+.01f,original.y,original.z};
    __try {position::set(bone,&moved);ok=bake(renderer,copy,points);}
    __finally {position::set(bone,&original);}
    position::Vec observed{};return ok&&position::read(position::get,bone,&observed)&&position::error(observed,original)<.00002f;
}
static bool calibrate_impl(void* renderer,void* copy,void* const* bones,const contact::Capsule (&capsules)[BodyCapsules]){
    std::vector<contact::Vec> rest,moved;if(!bake(renderer,copy,rest))return false;
    std::vector<std::vector<float>> cumulative(state.node_count,std::vector<float>(rest.size()));
    for(unsigned n=0;n<state.node_count;++n){auto& node=state.nodes[n];void* bone=bones[node.bone];position::Vec original{};
        if(!frame(bone,node.base)||!position::read(position::get,bone,&original))return false;
        void* parent_node=hierarchy::get_parent(bone);sc::Frame parent_frame{};if(!frame(parent_node,parent_frame))return false;
        const auto delta=sc::vector(parent_frame,{.01f,0,0});const float squared=contact::dot(delta,delta);if(squared<1e-8f)return false;
        if(!displaced(renderer,copy,bone,original,moved)||moved.size()!=rest.size())return false;
        for(size_t v=0;v<rest.size();++v)cumulative[n][v]=std::clamp(contact::dot(contact::sub(moved[v],rest[v]),delta)/squared,0.0f,1.0f);
    }
    // Translating a parent also moves descendants. Subtract direct child subtree
    // responses to recover each bone's own linear skinning weight.
    std::vector<contact::Vec> selected;std::ofstream diagnostic;
    if(s.mesh_diagnostic){diagnostic.open(gimmd::player_session::directory/L"physics-skin-weights.json");diagnostic<<'[';}
    for(size_t v=0;v<rest.size();++v){float weights[32]{},sum=0;unsigned used=0;
        for(unsigned n=0;n<state.node_count;++n){float weight=cumulative[n][v];
            for(unsigned child=0;child<state.node_count;++child)if(state.nodes[child].parent==int(n))weight-=cumulative[child][v];
            weights[n]=weight>.002f?weight:0;sum+=weights[n];if(weights[n]>0)++used;}
        if(diagnostic){diagnostic<<(v?",":"")<<sum;}
        if(sum<.995f||sum>1.005f||used>8||!used)continue;
        bool nearby=false;for(const auto& point:selected)if(contact::length(contact::sub(point,rest[v]))<.025f){nearby=true;break;}
        if(nearby||state.count>=768)continue;
        auto& sample=state.samples[state.count++];sample.vertex=unsigned(v);
        for(unsigned n=0;n<state.node_count;++n)if(weights[n]>0){auto& influence=sample.influences[sample.count++];influence.node=n;influence.weight=weights[n]/sum;
            if(!sc::inverse(state.nodes[n].base,rest[v],influence.local))return false;}
        for(unsigned c=6;c<BodyCapsules;++c)sample.radii[c]=std::min(capsules[c].radius+.006f,std::max(0.0f,contact::distance(capsules[c],rest[v])-.001f));
        selected.push_back(rest[v]);
    }
    if(diagnostic)diagnostic<<']';state.renderer=identity(renderer);return state.count>=30;
}
static bool calibrate_guarded(void* renderer,void* source,void* const* bones,const contact::Capsule (&capsules)[BodyCapsules]){
    const auto clone_mesh=reinterpret_cast<GetTransformFn>(entry("DiagnosticMeshClone",resource_base));if(!clone_mesh)return false;
    void* copy=nullptr;bool ok=false;
    __try {copy=clone_mesh(source);if(copy&&identity(copy)&&identity(copy)!=identity(source))ok=calibrate_impl(renderer,copy,bones,capsules);}
    __finally {if(copy&&identity(copy)&&identity(copy)!=identity(source))destroy(copy,false);}
    return ok;
}
static bool calibrate(void* model,void* const* bones,const contact::Capsule (&capsules)[BodyCapsules]){
    if(state.attempted)return true;state.attempted=true;if(!s.upper_ring){state.status="open-garment";return true;}
    if(!s.mesh_diagnostic){state.status="diagnostic-disabled";return true;}
    state.observe=std::filesystem::exists(gimmd::player_session::directory/L"physics-skin-observe.flag");
    LARGE_INTEGER begin{},end{},frequency{};QueryPerformanceCounter(&begin);QueryPerformanceFrequency(&frequency);
    for(unsigned b=0;b<s.bone_count;++b)if(std::wcsstr(s.paths[s.bones[b].path].name,L"Skirt")){
        if(state.node_count>=32)return false;auto& node=state.nodes[state.node_count++];node.bone=b;
        for(unsigned p=0;p<s.particle_count;++p)if(s.particles[p].real&&s.particles[p].bone_index==b)node.particle=int(p);
        if(node.particle>=0){for(unsigned p=0;p<s.particle_count;++p)if(s.particles[p].parent_global==node.particle){if(node.child>=0)return false;node.child=int(p);}}
    }
    for(unsigned n=0;n<state.node_count;++n){const int parent_path=s.paths[s.bones[state.nodes[n].bone].path].parent_node;
        for(unsigned p=0;p<state.node_count;++p)if(int(s.bones[state.nodes[p].bone].path)==parent_path)state.nodes[n].parent=int(p);}
    if(!collect(model))return false;bool found=false;
    for(unsigned i=0;i<tree_count&&!found;++i){if(std::wcscmp(tree[i].name,L"Body"))continue;void* items[64]{};unsigned count=0;
        if(!components(hierarchy::component_game_object(tree[i].transform),items,count))return false;
        for(unsigned j=0;j<count;++j){char name[96]{};if(!class_name(items[j],name)||std::strcmp(name,"SkinnedMeshRenderer"))continue;
            void* source=mesh_get(items[j]);if(!identity(source))return false;found=true;
            if(!calibrate_guarded(items[j],source,bones,capsules)){state.status="calibration-unavailable";return false;}break;}}
    QueryPerformanceCounter(&end);state.calibration_ms=double(end.QuadPart-begin.QuadPart)*1000/frequency.QuadPart;
    state.ready=found;state.status=found?"ready":"renderer-unavailable";return true;
}
static bool capture(void* const* bones){
    if(!state.ready)return true;
    for(unsigned n=0;n<state.node_count;++n){auto& node=state.nodes[n];if(!frame(bones[node.bone],node.base))return false;
        node.reference={};if(node.child<0)continue;const auto& child=s.particles[node.child];
        if(child.real){sc::Frame child_frame{};if(!frame(bones[child.bone_index],child_frame))return false;node.reference=contact::sub(child_frame.position,node.base.position);}
        else {contact::Vec end=child.end_local;if(!child.synthetic&&!read_bytes(child.token+0xe0,&end,12))return false;node.reference=sc::vector(node.base,end);}}
    return true;
}
static void predict_node(unsigned n,bool (&done)[32]){
    if(done[n])return;auto& node=state.nodes[n];node.predicted=node.base;auto reference=node.reference;
    if(node.parent>=0){predict_node(unsigned(node.parent),done);const auto& ancestor=state.nodes[node.parent];
        auto base=ancestor.base;auto changed=ancestor.predicted;base.position={};changed.position={};
        const auto inherit=[&](contact::Vec value){contact::Vec local{};return sc::inverse(base,value,local)?sc::vector(changed,local):value;};
        for(auto& axis:node.predicted.axis)axis=inherit(axis);reference=inherit(reference);}
    if(node.particle>=0){const auto point=collision_frame.points[node.particle];
        if(node.child>=0)node.predicted=sc::aim(node.predicted,reference,contact::sub(collision_frame.points[node.child],point),point);
        else node.predicted.position=point;}
    done[n]=true;
}
static void predict(){
    bool done[32]{};for(unsigned n=0;n<state.node_count;++n)predict_node(n,done);
}
static contact::Vec evaluate(const Sample& sample){contact::Vec result{};
    for(unsigned k=0;k<sample.count;++k){const auto& influence=sample.influences[k];result=contact::add(result,contact::scale(sc::point(state.nodes[influence.node].predicted,influence.local),influence.weight));}return result;}
static bool validate_frames(void* const* bones){
    if(!state.ready)return true;predict();
    for(unsigned n=0;n<state.node_count;++n){sc::Frame actual{};if(!frame(bones[state.nodes[n].bone],actual))return false;
        for(unsigned i=0;i<state.count;++i)for(unsigned k=0;k<state.samples[i].count;++k){const auto& influence=state.samples[i].influences[k];if(influence.node!=n)continue;
            state.frame_error=std::max(state.frame_error,double(contact::length(contact::sub(sc::point(actual,influence.local),sc::point(state.nodes[n].predicted,influence.local)))));}}
    return true;
}
}
