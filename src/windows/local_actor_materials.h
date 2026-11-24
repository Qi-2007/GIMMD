#pragma once
// Included in local_actors. References live for one Unity callback only.
using MaterialsGetFn = void*(__fastcall*)(void*);
using MaterialsSetFn = void(__fastcall*)(void*,void*);
struct MaterialBinding {
    unsigned node = 0; int parent_index = -1;
    wchar_t node_name[96]{}; char renderer_class[96]{};
    void* array = nullptr; void* target = nullptr;
    uintptr_t materials[64]{}; unsigned slots = 0;
};
static MaterialBinding material_bindings[128]{};
static unsigned material_binding_count = 0, material_node_count = 0;
static void forget_materials() {
    for (auto& binding : material_bindings) binding = {};
    material_binding_count = material_node_count = 0;
}
static bool material_array(void* array, uintptr_t (&values)[64], unsigned& size) {
    uint64_t length = 0;
    if (!array || !read_bytes(reinterpret_cast<uintptr_t>(array)+24,&length,8) || length>64) return false;
    size=static_cast<unsigned>(length);
    for (unsigned j=0;j<size;++j) {
        void* material=nullptr;
        if (!read_bytes(reinterpret_cast<uintptr_t>(array)+32+j*8,&material,8) || !material || !(values[j]=identity(material))) return false;
    }
    return true;
}
static bool capture_materials(void* source) {
    forget_materials();
    if (!collect(source)) return false;
    material_node_count=tree_count;
    const auto getter=reinterpret_cast<MaterialsGetFn>(entry("SharedMaterialsGet",resource_base));
    if (!getter) return false;
    for (unsigned i=0;i<tree_count;++i) {
        void* list[64]{}; unsigned length=0;
        if (!components(hierarchy::component_game_object(tree[i].transform),list,length)) return false;
        for (unsigned j=0;j<length;++j) {
            char type_name[96]{};
            if (!class_name(list[j],type_name)) return false;
            if (std::strcmp(type_name,"SkinnedMeshRenderer") && std::strcmp(type_name,"MeshRenderer")) continue;
            if (material_binding_count>=128) return false;
            auto& binding=material_bindings[material_binding_count++];
            binding.node=i; binding.parent_index=tree[i].parent;
            wcscpy_s(binding.node_name,tree[i].name); strcpy_s(binding.renderer_class,type_name);
            binding.array=getter(list[j]);
            if (!material_array(binding.array,binding.materials,binding.slots)) return false;
        }
    }
    return material_binding_count!=0;
}
static bool material_binding_matches(const MaterialBinding& binding) {
    const auto getter=reinterpret_cast<MaterialsGetFn>(entry("SharedMaterialsGet",resource_base));
    uintptr_t values[64]{}; unsigned length=0;
    if (!getter || !material_array(getter(binding.target),values,length) || length!=binding.slots) return false;
    for (unsigned j=0;j<length;++j) if (values[j]!=binding.materials[j]) return false;
    return true;
}
static bool match_material_targets(Slot& slot) {
    if (tree_count!=material_node_count) return false;
    for (unsigned i=0;i<material_binding_count;++i) {
        auto& binding=material_bindings[i]; const auto& node=tree[binding.node];
        // Root receives our own name before this check.
        if (node.parent!=binding.parent_index || (binding.node && std::wcscmp(node.name,binding.node_name))) return false;
        void* list[64]{}; unsigned length=0;
        if (!components(hierarchy::component_game_object(node.transform),list,length)) return false;
        for (unsigned j=0;j<length;++j) {
            char type_name[96]{};
            if (!class_name(list[j],type_name)) return false;
            if (std::strcmp(type_name,binding.renderer_class)) continue;
            if (binding.target) return false; // Ambiguous renderer on this node.
            binding.target=list[j];
        }
        if (!binding.target) return false;
        if (!material_binding_matches(binding)) ++slot.clone_material_mismatches;
    }
    return true;
}
static bool restore_materials(Slot& slot) {
    const auto getter=reinterpret_cast<MaterialsGetFn>(entry("SharedMaterialsGet",resource_base));
    const auto setter=reinterpret_cast<MaterialsSetFn>(entry("SharedMaterialsSet",resource_base));
    if (!getter || !setter) return false;
    for (unsigned i=0;i<material_binding_count;++i) {
        const auto& binding=material_bindings[i];
        if (!identity(binding.target)) return false;
        if (!material_binding_matches(binding)) ++slot.stripped_material_mismatches;
        setter(binding.target,binding.array);
        uintptr_t observed[64]{}; unsigned length=0;
        if (!material_array(getter(binding.target),observed,length) || length!=binding.slots) return false;
        for (unsigned j=0;j<length;++j) if (observed[j]!=binding.materials[j]) return false;
        slot.material_slots+=length;
    }
    slot.material_renderers=material_binding_count;
    return true;
}
