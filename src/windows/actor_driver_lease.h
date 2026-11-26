#pragma once
// Included inside local_actors. Never destroy components on the playable model.
namespace driver_lease {
struct Item {
    uintptr_t owner=0,component=0;
    char name[96]{};
    bool before=false,dirty=false;
    unsigned char child_from_leaf[48]{};
    unsigned depth=0;
};
static Item items[32]{};
static unsigned size=0,checks=0,restored=0;
static unsigned full_scans=0,owner_checks=0;
static ULONGLONG last_scan=0;
static bool captured=false;
static uintptr_t model_id=0;
static const char* status="idle";
static bool selected(const char* name) {
    return !std::strcmp(name,"DynamicBoneArray")||!std::strcmp(name,"DynamicBoneRigidArray")||!std::strcmp(name,"MonoAnimationIKEventHelper");
}
// Child indices are hints. Rebuild them on a topology change or a periodic scan.
static void remember_owner(Item& item,unsigned node) {
    item.depth=0;
    while(node&&item.depth<std::size(item.child_from_leaf)){
        const int parent_index=tree[node].parent;
        unsigned ordinal=0;
        for(unsigned i=node-1;i>0&&tree[i].parent==parent_index;--i)++ordinal;
        item.child_from_leaf[item.depth++]=static_cast<unsigned char>(ordinal);
        node=static_cast<unsigned>(parent_index);
    }
    if(node)++item.depth; // Too deep for the hint. Keep using discovery.
}
static bool known_owners_paused(void* model) {
    for(unsigned i=0;i<size;++i){
        const auto& item=items[i];
        if(item.depth>std::size(item.child_from_leaf))return false;
        void* node=model;
        for(unsigned d=item.depth;d>0;--d){
            const int child=item.child_from_leaf[d-1];
            if(hierarchy::child_count(node)<=child)return false;
            node=hierarchy::get_child(node,child);
            if(!node)return false;
        }
        void* owner=hierarchy::component_game_object(node);
        void* list[64]{};unsigned count=0;
        if(identity(owner)!=item.owner||!components(owner,list,count))return false;
        bool found=false;
        for(unsigned j=0;j<count;++j)if(identity(list[j])==item.component){
            char name[96]{};
            found=class_name(list[j],name)&&!std::strcmp(name,item.name)&&!animator_enabled(list[j]);
            break;
        }
        if(!found)return false;
    }
    checks+=size;++owner_checks;
    return true;
}
static bool apply(void* model,bool restore,bool& changed) {
    if(!configured||!model||!identity(model))return false;
    if(captured&&model_id!=identity(model))return false;
    bool ok=true;unsigned seen=0;
    __try {
        const auto now=GetTickCount64();
        if(captured&&!restore&&now-last_scan<250&&known_owners_paused(model))return true;
        if(!collect(model))return false;
        if(!captured){size=checks=restored=full_scans=owner_checks=0;model_id=identity(model);captured=true;for(auto& item:items)item={};}
        ++full_scans;
        for(unsigned i=0;i<tree_count;++i) {
            void* go=hierarchy::component_game_object(tree[i].transform);void* list[64]{};unsigned count=0;
            if(!components(go,list,count)){ok=false;break;}
            for(unsigned j=0;j<count;++j) {
                char name[96]{};if(!class_name(list[j],name)){ok=false;break;}
                if(!selected(name))continue;
                Item* item=nullptr;
                for(unsigned k=0;k<size;++k)if(items[k].component==identity(list[j])&&items[k].owner==identity(go)&&!std::strcmp(items[k].name,name))item=&items[k];
                if(!item&&!restore) {
                    if(size>=32){ok=false;break;}
                    item=&items[size++];item->component=identity(list[j]);item->owner=identity(go);strcpy_s(item->name,name);
                    item->before=animator_enabled(list[j]);
                }
                if(!item)continue;
                ++seen;
                if(restore) {
                    if(item->dirty){animator_enable(list[j],item->before);if(animator_enabled(list[j])==item->before){item->dirty=false;++restored;}else ok=false;}
                } else {
                    remember_owner(*item,i);
                    if(!item->dirty){changed=true;item->dirty=true;animator_enable(list[j],false);}
                    ++checks;if(animator_enabled(list[j]))ok=false;
                }
            }
        }
        if(seen!=size)ok=false;
        if(ok&&!restore)last_scan=now;
    } __except(EXCEPTION_EXECUTE_HANDLER) {state.exception=GetExceptionCode();ok=false;}
    for(auto& node:tree)node={};tree_count=0;
    status=ok?(restore?"drivers-restored":"drivers-paused"):(restore?"driver-restore-pending":"driver-pause-refused");
    return ok;
}
static bool begin(void* model,bool& changed) {face_motion::inspect(model);return apply(model,false,changed);}
static bool release() {
    if(gimmd::physics_bridge::release_ready&&!gimmd::physics_bridge::release_ready())return false;
    if(!captured)return true;
    bool active=true;void* model=hierarchy::animator_lease::model?hierarchy::animator_lease::model(&active):nullptr;
    bool changed=false;const bool result=apply(model,true,changed);if(result)captured=false;return result;
}
static void report(std::ostream& out) {
    out<<",\n  \"auxiliaryDrivers\":{\"status\":\""<<status<<"\",\"count\":"<<size<<",\"checks\":"<<checks<<",\"restored\":"<<restored<<",\"components\":[";
    for(unsigned i=0;i<size;++i)out<<(i?",":"")<<"{\"class\":\""<<items[i].name<<"\",\"before\":"<<(items[i].before?"true":"false")<<",\"cleanupPending\":"<<(items[i].dirty?"true":"false")<<"}";
    out<<"],\"fullScans\":"<<full_scans<<",\"ownerChecks\":"<<owner_checks<<"}";
}
}
