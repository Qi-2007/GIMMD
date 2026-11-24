#pragma once
namespace face_motion {
using Count=int(__fastcall*)(void*);
using Name=void*(__fastcall*)(void*,int);
using Get=float(__fastcall*)(void*,int);
using Set=void(__fastcall*)(void*,int,float);
struct Shape {wchar_t renderer[96]{},name[96]{};float weight=0;};
static Shape diagnostic[512]{};
static unsigned diagnostic_count=0;
static bool inspected=false;
static const char* status="not-inspected";
static void inspect(void* model) {
    if(inspected)return;inspected=true;status="inspection-refused";
    auto count=reinterpret_cast<Count>(entry("MeshBlendShapeCount",resource_base));
    auto name=reinterpret_cast<Name>(entry("MeshBlendShapeName",resource_base));
    auto get=reinterpret_cast<Get>(entry("RendererBlendShapeGet",resource_base));
    if(!count||!name||!get)return;
    __try {
        if(!collect(model))return;
        for(unsigned i=0;i<tree_count;++i) {
            void* list[64]{};unsigned length=0;
            if(!components(hierarchy::component_game_object(tree[i].transform),list,length))return;
            for(unsigned j=0;j<length;++j) {
                char type[96]{};if(!class_name(list[j],type))return;
                if(std::strcmp(type,"SkinnedMeshRenderer"))continue;
                void* mesh=mesh_get(list[j]);if(!mesh)continue;
                const int total=count(mesh);if(total<0||total>512)return;
                for(int k=0;k<total&&diagnostic_count<512;++k){auto& item=diagnostic[diagnostic_count];
                    wcscpy_s(item.renderer,tree[i].name);if(!hierarchy::string_read(name(mesh,k),item.name))return;
                    item.weight=get(list[j],k);if(!std::isfinite(item.weight))return;++diagnostic_count;}
            }
        }
        status="inspected";
    } __except(EXCEPTION_EXECUTE_HANDLER){status="inspection-exception";}
    for(auto& node:tree)node={};tree_count=0;
}
static void diagnostic_report(std::ostream& out) {
    out<<",\n  \"faceMotion\":{\"status\":\""<<status<<"\",\"shapes\":[";
    for(unsigned i=0;i<diagnostic_count;++i){out<<(i?",":"")<<"{\"renderer\":";hierarchy::json_string(out,diagnostic[i].renderer);
        out<<",\"name\":";hierarchy::json_string(out,diagnostic[i].name);out<<",\"weight\":"<<diagnostic[i].weight<<"}";}
    out<<"]}";
}
namespace fp=gimmd::face_profile;
namespace vmd=BetterEndfieldNext::Vmd;
struct FacePath {int parent=-1,child=-1;uintptr_t identity=0;wchar_t name[96]{};};
struct Renderer {uintptr_t identity=0,owner=0,mesh=0;unsigned path=0;};
struct Channel {unsigned renderer=0,index=0,group=0;wchar_t name[96]{};float before=0,applied=0;bool dirty=false;};
struct Driver {uintptr_t identity=0,owner=0;bool manager=false,before=false,dirty=false;unsigned path=0;};
struct Binding {unsigned driver=0,index=0,channel=0;uintptr_t token=0;bool before=false,dirty=false;};
struct Context {
    std::vector<vmd::MorphKey> sources[std::size(fp::Mappings)];
    bool groups[3]{},bound=false,active=false;uintptr_t model=0;
    Renderer renderers[8]{};Channel channels[256]{};Driver drivers[16]{};
    Binding bindings[512]{};unsigned binding_count=0,binding_restores=0;
    FacePath paths[768]{};unsigned path_count=0,samples=0;double total_ms=0,max_ms=0;
    ULONGLONG last_check=0;
    unsigned renderer_count=0,channel_count=0,driver_count=0,source_count=0,matched_count=0,writes=0,restores=0,overwrites=0;
    const char* status="idle";
};
static Context contexts[2];
using TickGet=bool(__fastcall*)(void*);
using TickSet=void(__fastcall*)(void*,bool);
static TickGet tick_getters[2]{};static TickSet tick_setters[2]{};static Get weight_get=nullptr;static Set weight_set=nullptr;
static TickSet ban_apply=nullptr;
static TickGet tick_get(bool manager){return tick_getters[manager?1:0];}
static TickSet tick_set(bool manager){return tick_setters[manager?1:0];}
// Freshly resolve the manager's list on every access; saved managed addresses
// are comparison tokens only. Layout and BanBindApply were reviewed in 7.2.
static bool binding_list(void* manager,uintptr_t& array,unsigned& length) {
    uintptr_t list=0,capacity=0;int count=0;
    if(!read_bytes(reinterpret_cast<uintptr_t>(manager)+0x60,&list,8))return false;
    if(!list){array=0;length=0;return true;}
    if(!read_bytes(list+0x18,&count,4)||count<0||count>512||!read_bytes(list+0x10,&array,8))return false;
    length=unsigned(count);if(!length)return true;
    return array&&read_bytes(array+0x18,&capacity,8)&&capacity>=length&&capacity<=4096;
}
static bool binding_fields(uintptr_t array,unsigned index,void*& item,int& type,void*& renderer,int& shape,bool& banned) {
    uintptr_t ptr=0;unsigned char flag=0;
    if(!read_bytes(array+0x20+index*8,&ptr,8)||!ptr)return false;
    char name[96]{};item=reinterpret_cast<void*>(ptr);
    if(!class_name(item,name)||std::strcmp(name,"CurveBinding")||!read_bytes(ptr+0x10,&type,4)||
        !read_bytes(ptr+0x20,&renderer,8)||!read_bytes(ptr+0x30,&shape,4)||!read_bytes(ptr+0x44,&flag,1)||flag>1)return false;
    banned=flag!=0;return true;
}
static bool prepare(bool actor,const vmd::Motion& motion) {
    auto& s=contexts[actor?1:0];if(s.active)return false;
    s=Context{};s.source_count=unsigned(motion.morphs.size());
    if(std::filesystem::exists(gimmd::player_session::directory/L"face-disabled.flag")){s.status="disabled";return true;}
    for(unsigned i=0;i<std::size(fp::Mappings);++i) {const auto& m=fp::Mappings[i];auto it=motion.morphs.find(m.source);
        if(it!=motion.morphs.end()){s.sources[i]=it->second;s.groups[m.group]=true;}}
    s.status=s.source_count?"prepared":"no-morph-tracks";return true;
}
// Snapshot the whole controlled groups and all ticking flags before pausing any
// user: EmoMgrUser.set_tickable also changes its manager's flag.
static bool bind(Context& s,void* model) {
    ban_apply=reinterpret_cast<TickSet>(entry("EmoBindingBanApply",resource_base));
    for(unsigned i=0;i<2;++i){tick_getters[i]=reinterpret_cast<TickGet>(entry(i?"EmoManagerTickGet":"EmoUserTickGet",resource_base));tick_setters[i]=reinterpret_cast<TickSet>(entry(i?"EmoManagerTickSet":"EmoUserTickSet",resource_base));}
    weight_get=reinterpret_cast<Get>(entry("RendererBlendShapeGet",resource_base));weight_set=reinterpret_cast<Set>(entry("RendererBlendShapeSet",resource_base));
    auto count=reinterpret_cast<Count>(entry("MeshBlendShapeCount",resource_base));
    auto name=reinterpret_cast<Name>(entry("MeshBlendShapeName",resource_base));
    auto get=reinterpret_cast<Get>(entry("RendererBlendShapeGet",resource_base));
    if(!ban_apply||!entry("EmoBindingApply",resource_base)||!count||!name||!get||!tick_get(true)||!tick_set(true)||!tick_get(false)||!tick_set(false)||!collect(model))return false;
    s.model=identity(model);
    s.path_count=tree_count;
    for(unsigned i=0;i<tree_count;++i){auto& path=s.paths[i];path.parent=tree[i].parent;path.identity=tree[i].identity;wcscpy_s(path.name,tree[i].name);
        if(i){path.child=0;for(unsigned j=1;j<i;++j)if(tree[j].parent==path.parent)++path.child;}}
    for(unsigned i=0;i<tree_count;++i){void* go=hierarchy::component_game_object(tree[i].transform);void* list[64]{};unsigned n=0;
        if(!components(go,list,n))return false;
        for(unsigned j=0;j<n;++j){char type[96]{};if(!class_name(list[j],type))return false;
            const bool manager=!std::strcmp(type,"ClipShapeManager");
            if(manager||!std::strcmp(type,"EyeCtrl")||!std::strcmp(type,"EmoSync")){
                if(s.driver_count>=16)return false;auto& driver=s.drivers[s.driver_count++];
                driver={identity(list[j]),identity(go),manager,tick_get(manager)(list[j]),false,i};
            }
            if(std::strcmp(type,"SkinnedMeshRenderer"))continue;
            void* mesh=mesh_get(list[j]);if(!mesh)continue;const int total=count(mesh);
            if(total<0||total>512)return false;if(!total)continue;
            if(s.renderer_count>=8)return false;const unsigned r=s.renderer_count++;
            s.renderers[r]={identity(list[j]),identity(go),identity(mesh),i};
            for(int k=0;k<total;++k){wchar_t label[96]{};if(!hierarchy::string_read(name(mesh,k),label))return false;
                unsigned group=!std::wcsncmp(label,L"Eye_",4)?0:!std::wcsncmp(label,L"Mouth_",6)?1:!std::wcsncmp(label,L"Brow_",5)?2:3;
                if(group>=3||!s.groups[group])continue;if(s.channel_count>=256)return false;
                auto& channel=s.channels[s.channel_count++];channel.renderer=r;channel.index=k;channel.group=group;wcscpy_s(channel.name,label);
                channel.before=get(list[j],k);if(!std::isfinite(channel.before))return false;
            }
        }
    }
    for(unsigned i=0;i<std::size(fp::Mappings);++i){if(s.sources[i].empty())continue;bool exists=false,previous=false;
        for(unsigned c=0;c<s.channel_count;++c)exists=exists||!std::wcscmp(s.channels[c].name,fp::Mappings[i].target);
        for(unsigned j=0;j<i;++j)if(!std::strcmp(fp::Mappings[j].source,fp::Mappings[i].source))previous=true;
        if(exists&&!previous)++s.matched_count;
    }
    for(unsigned d=0;d<s.driver_count;++d){if(!s.drivers[d].manager)continue;
        void* manager=nullptr;if(wrap(&manager,reinterpret_cast<void*>(s.drivers[d].identity))!=&manager||identity(manager)!=s.drivers[d].identity)return false;
        uintptr_t array=0;unsigned length=0;if(!binding_list(manager,array,length))return false;
        for(unsigned b=0;b<length;++b){void* item=nullptr,*renderer=nullptr;int type=-1,shape=-1;bool banned=false;
            if(!binding_fields(array,b,item,type,renderer,shape,banned))return false;if(type!=0)continue;
            for(unsigned c=0;c<s.channel_count;++c){const auto& channel=s.channels[c];
                if(shape<0||unsigned(shape)!=channel.index||identity(renderer)!=s.renderers[channel.renderer].identity)continue;
                if(s.binding_count>=512)return false;
                s.bindings[s.binding_count++]={d,b,c,reinterpret_cast<uintptr_t>(item),banned,false};break;
            }
        }
    }
    s.bound=true;s.status=s.matched_count?"bound":"no-supported-morphs";return true;
}
static void* resolve_path(Context& s,unsigned index,void* model,void* (&cache)[768]) {
    if(index>=s.path_count)return nullptr;if(cache[index])return cache[index];const auto& path=s.paths[index];
    void* target=model;
    if(index){if(path.parent<0||unsigned(path.parent)>=index)return nullptr;
        void* owner_transform=resolve_path(s,unsigned(path.parent),model,cache);if(!owner_transform)return nullptr;
        const int count=hierarchy::child_count(owner_transform);if(count<0||count>128||path.child<0||path.child>=count)return nullptr;
        target=hierarchy::get_child(owner_transform,path.child);}
    wchar_t name[96]{};if(identity(target)!=path.identity||!hierarchy::string_read(hierarchy::name_a(target),name)||std::wcscmp(name,path.name))return nullptr;
    cache[index]=target;return target;
}
static bool resolve(Context& s,void* model,void* (&renderers)[8],void* (&drivers)[16]) {
    if(!model||identity(model)!=s.model)return false;
    void* cache[768]{};
    for(unsigned k=0;k<s.renderer_count+s.driver_count;++k){const unsigned index=k<s.renderer_count?s.renderers[k].path:s.drivers[k-s.renderer_count].path;
        if(index>=s.path_count)return false;
        void* target=resolve_path(s,index,model,cache);if(!target)return false;
        void* go=hierarchy::component_game_object(target);
        const bool renderer=k<s.renderer_count;const unsigned d=k-s.renderer_count;
        const auto id=renderer?s.renderers[k].identity:s.drivers[d].identity;
        const auto owner=renderer?s.renderers[k].owner:s.drivers[d].owner;
        if(identity(go)!=owner||component_membership(go,id)!=1)return false;
        void* managed=nullptr;if(wrap(&managed,reinterpret_cast<void*>(id))!=&managed||identity(managed)!=id||identity(hierarchy::component_game_object(managed))!=owner)return false;
        if(renderer){if(identity(mesh_get(managed))!=s.renderers[k].mesh)return false;renderers[k]=managed;}else drivers[d]=managed;
    }
    for(unsigned r=0;r<s.renderer_count;++r)if(!renderers[r])return false;
    for(unsigned d=0;d<s.driver_count;++d)if(!drivers[d])return false;return true;
}
static bool set_bindings(Context& s,void* (&drivers)[16],bool restoring) {
    uintptr_t arrays[16]{};unsigned lengths[16]{};
    for(unsigned d=0;d<s.driver_count;++d)if(s.drivers[d].manager&&!binding_list(drivers[d],arrays[d],lengths[d]))return false;
    for(unsigned b=0;b<s.binding_count;++b){auto& binding=s.bindings[b];if(restoring&&!binding.dirty)continue;
        if(binding.index>=lengths[binding.driver])return false;
        void* item=nullptr,*renderer=nullptr;int type=-1,shape=-1;bool banned=false;const auto& channel=s.channels[binding.channel];
        if(!binding_fields(arrays[binding.driver],binding.index,item,type,renderer,shape,banned)||
            reinterpret_cast<uintptr_t>(item)!=binding.token||type!=0||shape<0||unsigned(shape)!=channel.index||
            identity(renderer)!=s.renderers[channel.renderer].identity)return false;
        const bool desired=restoring?binding.before:true;
        if(!restoring)binding.dirty=true;
        if(banned!=desired)ban_apply(item,desired);
        unsigned char actual=0;if(!read_bytes(reinterpret_cast<uintptr_t>(item)+0x44,&actual,1)||actual!=unsigned(desired))return false;
        if(restoring){binding.dirty=false;++s.binding_restores;}
    }
    return true;
}
static void values_for(Context& s,double seconds,float (&values)[256]) {
    float maximum[3]{};
    for(unsigned c=0;c<s.channel_count;++c){auto& channel=s.channels[c];
        for(unsigned i=0;i<std::size(fp::Mappings);++i)if(!s.sources[i].empty()&&!std::wcscmp(channel.name,fp::Mappings[i].target))
            values[c]=std::max(values[c],100*fp::bounded(vmd::SampleMorph(s.sources[i],seconds*30)));
        maximum[channel.group]=std::max(maximum[channel.group],values[c]);}
    for(unsigned c=0;c<s.channel_count;++c)if(std::wcsstr(s.channels[c].name,L"_Default"))values[c]=100-maximum[s.channels[c].group];
}
static bool update(bool actor,void* model,double seconds,bool restoring) {
    auto& s=contexts[actor?1:0];
    if(restoring&&!s.active)return true;
    if(!restoring&&!s.groups[0]&&!s.groups[1]&&!s.groups[2])return true;
    bool ok=false;
    __try {do {
        if(!s.bound&&!bind(s,model))break;
        if(!s.matched_count){ok=true;break;}
        void* renderers[8]{},*drivers[16]{};if(!resolve(s,model,renderers,drivers))break;
        auto get=weight_get;auto set=weight_set;if(!get||!set)break;
        if(!restoring) {
            s.active=true;
            for(unsigned d=0;d<s.driver_count;++d){auto& driver=s.drivers[d];if(!driver.dirty){driver.dirty=true;tick_set(driver.manager)(drivers[d],false);}if(tick_get(driver.manager)(drivers[d]))__leave;}
            // tickable=false stops time, but ElementManager.LateTick still
            // calls Apply. Temporarily ban only the channels owned by MMD.
            if(!set_bindings(s,drivers,false))break;
            float values[256]{};values_for(s,seconds,values);
            for(unsigned c=0;c<s.channel_count;++c){auto& channel=s.channels[c];
                const float before=get(renderers[channel.renderer],channel.index);
                if(!std::isfinite(before))__leave;
                if(channel.dirty&&std::abs(before-channel.applied)>.01f)++s.overwrites;
                if(channel.dirty&&std::abs(before-values[c])<.001f){channel.applied=values[c];continue;}
                channel.dirty=true;set(renderers[channel.renderer],channel.index,values[c]);
                const float applied=get(renderers[channel.renderer],channel.index);
                if(!std::isfinite(applied)||std::abs(applied-values[c])>.01f)__leave;
                channel.applied=values[c];++s.writes;
            }
            s.status="playing";ok=true;
        } else {
            for(unsigned c=0;c<s.channel_count;++c){auto& channel=s.channels[c];if(!channel.dirty)continue;
                set(renderers[channel.renderer],channel.index,channel.before);
                const float applied=get(renderers[channel.renderer],channel.index);
                if(!std::isfinite(applied)||std::abs(applied-channel.before)>.01f)__leave;
                channel.dirty=false;++s.restores;
            }
            if(!set_bindings(s,drivers,true))break;
            for(unsigned pass=0;pass<2;++pass)for(unsigned d=0;d<s.driver_count;++d){auto& driver=s.drivers[d];
                if(driver.manager!=(pass==1)||!driver.dirty)continue;
                tick_set(driver.manager)(drivers[d],driver.before);
                if(tick_get(driver.manager)(drivers[d])!=driver.before)__leave;driver.dirty=false;
            }
            s.active=false;s.status="restored";ok=true;
        }
    }while(false);}__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    for(auto& node:tree)node={};tree_count=0;
    if(!ok)s.status=restoring?"restore-pending":"write-refused";return ok;
}
static bool tick(bool actor,void* model,double seconds){
    auto& s=contexts[actor?1:0];const auto now=GetTickCount64();
    if(s.bound&&s.active&&now-s.last_check<200){float values[256]{};values_for(s,seconds,values);bool changed=false;
        for(unsigned c=0;c<s.channel_count;++c)changed=changed||std::abs(values[c]-s.channels[c].applied)>.001f;
        if(!changed)return true;}
    s.last_check=now;
    LARGE_INTEGER start{},end{},frequency{};QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);
    const bool result=update(actor,model,seconds,false);QueryPerformanceCounter(&end);
    const double elapsed=double(end.QuadPart-start.QuadPart)*1000/frequency.QuadPart;++s.samples;s.total_ms+=elapsed;s.max_ms=std::max(s.max_ms,elapsed);return result;
}
static bool restore(bool actor,void* model){return update(actor,model,0,true);}
static unsigned matched(bool actor){return contexts[actor?1:0].matched_count;}
static void report(std::ostream& out) {
    diagnostic_report(out);
    out<<",\n  \"facialPlayback\":[";
    for(unsigned i=0;i<2;++i){const auto& s=contexts[i];out<<(i?",":"")<<"{\"target\":\""<<(i?"actor":"player")<<"\",\"status\":\""<<s.status<<"\",\"sourceTracks\":"<<s.source_count<<",\"mappedTracks\":"<<s.matched_count<<",\"channels\":"<<s.channel_count<<",\"writes\":"<<s.writes<<",\"restores\":"<<s.restores<<",\"overwrites\":"<<s.overwrites<<",\"averageMs\":"<<(s.samples?s.total_ms/s.samples:0)<<",\"maximumMs\":"<<s.max_ms<<",\"cleanupPending\":"<<(s.active?"true":"false")<<"}";}
    out<<"],\n  \"facialBindingLease\":[";
    for(unsigned i=0;i<2;++i){const auto& s=contexts[i];unsigned pending=0;for(unsigned b=0;b<s.binding_count;++b)pending+=s.bindings[b].dirty?1:0;
        out<<(i?",":"")<<"{\"bindings\":"<<s.binding_count<<",\"restores\":"<<s.binding_restores<<",\"pending\":"<<pending<<"}";}
    out<<"]";
}
}
