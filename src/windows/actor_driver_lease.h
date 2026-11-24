#pragma once
// Included inside local_actors. Never destroy components on the playable model.
namespace driver_lease {
struct Item { uintptr_t owner=0,component=0;char name[96]{};bool before=false,dirty=false; };
static Item items[32]{};
static unsigned size=0,checks=0,restored=0;
static bool captured=false;
static uintptr_t model_id=0;
static const char* status="idle";
static bool selected(const char* name) {
    return !std::strcmp(name,"DynamicBoneArray")||!std::strcmp(name,"DynamicBoneRigidArray")||!std::strcmp(name,"MonoAnimationIKEventHelper");
}
static bool apply(void* model,bool restore) {
    if(!configured||!model||!identity(model))return false;
    if(captured&&model_id!=identity(model))return false;
    bool ok=true;unsigned seen=0;
    __try {
        if(!collect(model))return false;
        if(!captured){size=checks=restored=0;model_id=identity(model);captured=true;for(auto& item:items)item={};}
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
                    if(!item->dirty){item->dirty=true;animator_enable(list[j],false);}
                    ++checks;if(animator_enabled(list[j]))ok=false;
                }
            }
        }
        if(seen!=size)ok=false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {state.exception=GetExceptionCode();ok=false;}
    for(auto& node:tree)node={};tree_count=0;
    status=ok?(restore?"drivers-restored":"drivers-paused"):(restore?"driver-restore-pending":"driver-pause-refused");
    return ok;
}
static bool begin(void* model) {face_motion::inspect(model);return apply(model,false);}
static bool release() {
    if(!captured)return true;
    bool active=true;void* model=hierarchy::animator_lease::model?hierarchy::animator_lease::model(&active):nullptr;
    const bool result=apply(model,true);if(result)captured=false;return result;
}
static void report(std::ostream& out) {
    out<<",\n  \"auxiliaryDrivers\":{\"status\":\""<<status<<"\",\"count\":"<<size<<",\"checks\":"<<checks<<",\"restored\":"<<restored<<",\"components\":[";
    for(unsigned i=0;i<size;++i)out<<(i?",":"")<<"{\"class\":\""<<items[i].name<<"\",\"before\":"<<(items[i].before?"true":"false")<<",\"cleanupPending\":"<<(items[i].dirty?"true":"false")<<"}";
    out<<"]}";
}
}
