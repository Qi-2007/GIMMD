#pragma once
// Main-thread only; saved identities are re-resolved through the live hierarchy.
namespace shooting {
struct Hidden { uintptr_t identity=0;bool enabled=false,dirty=false; };
static Hidden hidden[64]{};
static uintptr_t hidden_model=0;
static void* player_model(bool saved=false) {
    void* root=transform(find_object(find_string("/EntityRoot/AvatarRoot")));
    if(!root)return nullptr;
    const int count=hierarchy::child_count(root);if(count<0||count>128)return nullptr;
    void* result=nullptr;
    for(int i=0;i<count;++i) {
        void* actor=hierarchy::get_child(root,i);void* model=find_model(actor);
        if(!model)continue;
        if(saved) {if(identity(hierarchy::component_game_object(model))==hidden_model)return model;}
        else if(hierarchy::get_active(hierarchy::component_game_object(actor))) {if(result)return nullptr;result=model;}
    }
    return result;
}
static bool visibility(bool hide) {
    using Get=bool(__fastcall*)(void*);using Set=void(__fastcall*)(void*,bool);
    auto get=reinterpret_cast<Get>(entry("RendererEnabledGet",resource_base));
    auto set=reinterpret_cast<Set>(entry("RendererEnabledSet",resource_base));
    if(!get||!set)return false;
    if(hide&&state.hidden_renderers)return true;
    if(!hide&&!state.hidden_renderers)return true;
    bool ok=true;
    __try {
        void* model=player_model(!hide);if(!model||!collect(model))return false;
        if(hide)hidden_model=identity(hierarchy::component_game_object(model));
        for(unsigned i=0;i<tree_count;++i) {
            void* list[64]{};unsigned count=0;
            if(!components(hierarchy::component_game_object(tree[i].transform),list,count)){ok=false;break;}
            for(unsigned j=0;j<count;++j) {
                char name[96]{};if(!class_name(list[j],name)){ok=false;break;}
                if(std::strcmp(name,"SkinnedMeshRenderer")&&std::strcmp(name,"MeshRenderer"))continue;
                if(hide) {
                    if(state.hidden_renderers>=64){ok=false;break;}
                    auto& item=hidden[state.hidden_renderers++];item={identity(list[j]),get(list[j]),true};
                    publish("player-hide-pending");mutated.store(true);set(list[j],false);
                    if(get(list[j]))ok=false;
                } else for(auto& item:hidden) if(item.dirty&&item.identity==identity(list[j])) {
                    set(list[j],item.enabled);if(get(list[j])==item.enabled)item.dirty=false;else ok=false;
                }
            }
        }
        if(!hide) {
            unsigned pending=0;for(const auto& item:hidden)if(item.dirty)++pending;
            state.hidden_renderers=pending;
            if(!pending){hidden_model=0;for(auto& item:hidden)item={};}else ok=false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {state.exception=GetExceptionCode();ok=false;}
    for(auto& node:tree)node={};tree_count=0;
    publish(ok?(hide?"player-hidden":"player-shown"):"player-visibility-pending");return ok;
}
static bool position(unsigned slot,Vec3* out) {
    if(slot<1||slot>3)return false;
    __try {
        const auto& target=state.slots[slot-1];
        void* go=find_object(find_string(target.path));
        if(!target.identity||identity(go)!=target.identity||!hierarchy::get_active(go))return false;
        void* model=transform(go);void* root=nullptr;
        const int count=hierarchy::child_count(model);if(count<0||count>128)return false;
        for(int i=0;i<count;++i){void* child=hierarchy::get_child(model,i);wchar_t name[96]{};
            if(hierarchy::string_read(hierarchy::name_a(child),name)&&!std::wcscmp(name,L"Bip001"))root=child;}
        if(!root)return false;
        position_get(out,root);return std::isfinite(out->x)&&std::isfinite(out->y)&&std::isfinite(out->z);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static void camera(unsigned slot,bool follow) {
    if(!camera_playback::manual_only||!camera_playback::acquired||!gimmd::player_session::camera_requested.load()) {publish("actor-camera-needs-manual");return;}
    Vec3 target{},origin{};
    __try {
        void* model=player_model();if(!model||!position(slot,&target)){publish("actor-camera-target-missing");return;}
        void* pelvis=nullptr;const int children=hierarchy::child_count(model);
        for(int i=0;i<children&&i<128;++i){void* child=hierarchy::get_child(model,i);wchar_t name[96]{};
            if(hierarchy::string_read(hierarchy::name_a(child),name)&&!std::wcscmp(name,L"Bip001"))pelvis=child;}
        if(!pelvis){publish("actor-camera-target-missing");return;}
        position_get(&origin,pelvis);
        camera_playback::actor_position=position;camera_playback::actor_origin=origin;
        camera_playback::actor_shift={target.x-origin.x,target.y-origin.y,target.z-origin.z};

        camera_playback::actor_target=slot;camera_playback::actor_follow=follow;
        publish(follow?"actor-camera-follow":"actor-camera-moved");
    } __except(EXCEPTION_EXECUTE_HANDLER) {publish("actor-camera-target-missing");}
}
static bool front(Vec3* output,hierarchy::Quat* rotation) {
    __try {
        void* model=player_model();if(!model||!collect(model))return false;
        Vec3 feet{},head{};hierarchy::Quat facing{};unsigned heads=0;
        position_get(&feet,model);hierarchy::rotation_world(&facing,model);
        for(unsigned i=0;i<tree_count;++i)if(!std::wcscmp(tree[i].name,L"Bip001 Head")){position_get(&head,tree[i].transform);++heads;}
        for(auto& node:tree)node={};tree_count=0;
        if(heads!=1||!hierarchy::unit_quat(facing))return false;
        BetterEndfieldNext::Vmd::Vec3 p{};BetterEndfieldNext::Vmd::Quaternion q{};
        if(!gimmd::camera_front::pose({feet.x,feet.y,feet.z},{facing.x,facing.y,facing.z,facing.w},head.y-feet.y,&p,&q))return false;
        *output={p.x,p.y,p.z};*rotation={q.x,q.y,q.z,q.w};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
