#pragma once
namespace bind_pose {
#ifdef GIMMD_BIND_POSE_PROBE
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using AvatarGetFn = void*(__fastcall*)(void*);
static AvatarGetFn avatar_get;
static metadata::EnabledGetFn human_get;
static const char* status = "disabled";
static bool is_human = false, available = false;
static DWORD exception = 0;
struct Method { char name[64]{}; unsigned parameters = 0; uintptr_t code = 0, invoker = 0; };
static Method methods[16]{};
static unsigned method_count = 0, declared_methods = 0;
static Method renderer_methods[16]{};
static unsigned renderer_method_count = 0;
static const char* renderer_status = "not-attempted";
static Method mesh_method;
static unsigned bone_count = 0;
static int bone_nodes[512]{};
static float bind_matrices[512][16]{};
static unsigned bind_count = 0;
struct SkinSnapshot { const char* name = nullptr; const char* status = nullptr; unsigned count = 0; int ids[512]{}; float matrices[512][16]{}; };
static SkinSnapshot extra_skins[6]{};
static unsigned extra_count = 0;
static const char* avatar_rest_status = "not-attempted";
static unsigned avatar_rest_count = 0, avatar_link_count = 0;
static float avatar_local[512][12]{};
static int avatar_parents[512]{}, avatar_links[MaxNodes]{};
static int avatar_invalid_index = -1;
static unsigned avatar_candidate_count = 0;
static void configure(uintptr_t base) {
    status = Enabled ? "not-attempted" : "disabled"; is_human = available = false;
    exception = 0; method_count = declared_methods = 0;
    renderer_method_count = 0; renderer_status = "not-attempted";
    mesh_method = {}; bone_count = bind_count = 0;
    extra_count = 0;
    avatar_rest_status = "not-attempted"; avatar_rest_count = avatar_link_count = 0;
    avatar_invalid_index = -1; avatar_candidate_count = 0;
    for (auto& link : avatar_links) link = -1;
    avatar_get = Enabled && entry("AnimatorAvatarGetNative", base) ? reinterpret_cast<AvatarGetFn>(entry("AnimatorAvatarGet", base)) : nullptr;
    human_get = Enabled && entry("AnimatorIsHumanGetNative", base) ? reinterpret_cast<metadata::EnabledGetFn>(entry("AnimatorIsHumanGet", base)) : nullptr;
}
static void capture_avatar_storage(void* avatar) {
    if (!Enabled) return;
    __try {
        const auto base = metadata::image_base;
        const auto index_code = entry("AvatarBoneIndexGet",base);
        avatar_rest_status = "avatar-storage-profile-unverified";
        if (!index_code || !entry("AvatarBoneIndexGetNative",base) || !entry("AvatarBoneIndexLookup",base) ||
            !entry("AvatarDefaultPoseStorage",base) || !entry("AvatarDefaultPositionWalk",base)) return;
        unsigned named = 0;
        for (unsigned i = 0; i < method_count; ++i)
            if (!std::strcmp(methods[i].name,"BoneNameToIndex") && methods[i].parameters == 1 && methods[i].code+base == index_code) ++named;
        if (named != 1) return;
        uintptr_t native = 0, blob = 0; uint64_t tree_offset = 0, pose_offset = 0, parent_offset = 0, trs_offset = 0;
        uint32_t tree_count = 0, hash_count = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(avatar)+16,&native,sizeof(native)) || !native ||
            !read_bytes(native+0x78,&blob,sizeof(blob)) || !blob ||
            !read_bytes(blob,&tree_offset,sizeof(tree_offset)) || !tree_offset || tree_offset > (16 << 20) ||
            !read_bytes(blob+0x10,&pose_offset,sizeof(pose_offset)) || !pose_offset || pose_offset > (16 << 20) ||
            !read_bytes(blob+0x18,&hash_count,sizeof(hash_count)) || !hash_count || hash_count > 512 ||
            !read_bytes(blob+tree_offset,&tree_count,sizeof(tree_count)) || tree_count != hash_count ||
            !read_bytes(blob+tree_offset+8,&parent_offset,sizeof(parent_offset)) || parent_offset > (16 << 20) ||
            !read_bytes(blob+pose_offset+0x18,&trs_offset,sizeof(trs_offset)) || trs_offset > (16 << 20)) {
            avatar_rest_status = "avatar-storage-header-refused"; return;
        }
        const auto trs = blob+pose_offset+0x18+trs_offset;
        const auto parents = blob+tree_offset+8+parent_offset;
        avatar_candidate_count = tree_count;
        for (unsigned i = 0; i < tree_count; ++i) {
            if (!read_bytes(trs+i*48,avatar_local[i],48) || !read_bytes(parents+i*8,&avatar_parents[i],sizeof(int)) ||
                avatar_parents[i] < -1 || avatar_parents[i] >= static_cast<int>(tree_count) || avatar_parents[i] == static_cast<int>(i)) {
                avatar_rest_status = "avatar-storage-record-refused"; return;
            }
            const auto& value = avatar_local[i];
            for (unsigned n = 0; n < 12; ++n) if (!std::isfinite(value[n]) || std::abs(value[n]) > 100000) {
                avatar_rest_status = "avatar-storage-nonfinite"; return;
            }
            const float norm = value[4]*value[4]+value[5]*value[5]+value[6]*value[6]+value[7]*value[7];
            if (std::abs(norm-1) > 0.001f || std::abs(value[8]) <= 0.000001f || std::abs(value[9]) <= 0.000001f || std::abs(value[10]) <= 0.000001f) {
                avatar_invalid_index = static_cast<int>(i);
                avatar_rest_status = "avatar-storage-trs-refused"; return;
            }
        }
        using IndexFn = int(__fastcall*)(void*,void*);
        for (unsigned n = 0; n < count; ++n) {
            bool under_model = false;
            for (int p = static_cast<int>(n); p >= 0; p = nodes[p].parent) if (p == owner_node) under_model = true;
            if (!under_model || (!std::wcsstr(nodes[n].name,L"Bip001") && nodes[n].name[0] != L'+')) continue;
            char name[MaxName]{};
            if (!WideCharToMultiByte(CP_UTF8,0,nodes[n].name,-1,name,MaxName,nullptr,nullptr)) return;
            void* managed_name = find_string(name);
            if (!managed_name) return;
            const int index = reinterpret_cast<IndexFn>(index_code)(avatar,managed_name);
            if (index < 0) continue;
            if (index >= static_cast<int>(tree_count)) { avatar_rest_status = "avatar-name-index-refused"; return; }
            avatar_links[n] = index; ++avatar_link_count;
        }
        for (unsigned n = 0; n < count; ++n) if (avatar_links[n] >= 0 && nodes[n].parent >= 0 && avatar_links[nodes[n].parent] >= 0 &&
            avatar_parents[avatar_links[n]] != avatar_links[nodes[n].parent]) {
            avatar_rest_status = "avatar-parent-link-refused"; return;
        }
        uintptr_t repeated_blob = 0;
        if (!read_bytes(native+0x78,&repeated_blob,sizeof(repeated_blob)) || repeated_blob != blob) return;
        float repeated[512][12]{};
        if (!read_bytes(trs,repeated,tree_count*48) || memcmp(repeated,avatar_local,tree_count*48)) {
            avatar_rest_status = "avatar-reference-changed"; return;
        }
        avatar_rest_count = tree_count; avatar_rest_status = "avatar-local-reference-observed";
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); avatar_rest_status = "avatar-storage-read-exception"; }
}
static void capture_renderer(int model_node, const wchar_t* skin_name = L"Body") {
    if (!Enabled || model_node < 0 || !metadata::accessors_verified) return;
    renderer_method_count = bone_count = bind_count = 0; mesh_method = {};
    __try {
        renderer_status = "body-renderer-not-found";
        void* component = nullptr; unsigned body_matches = 0;
        void* name = find_string("SkinnedMeshRenderer");
        if (!name) return;
        for (unsigned n = 0; n < count; ++n) {
            // Inactive LOD skins are valid read-only sources under the selected active model.
            if (nodes[n].parent != model_node || std::wcscmp(nodes[n].name, skin_name) ||
                !roundtrip::identity_current(nodes[n])) continue;
            void* found = get_component(nodes[n].object, name);
            if (found && same_object(component_game_object(found), component_game_object(nodes[n].object))) { component = found; ++body_matches; }
        }
        if (body_matches != 1) return;
        uintptr_t klass = 0, name_pointer = 0; char class_name[64]{};
        if (!read_bytes(reinterpret_cast<uintptr_t>(component), &klass, sizeof(klass)) || !klass ||
            !read_bytes(klass+0x28, &name_pointer, sizeof(name_pointer)) ||
            !read_bytes(name_pointer, class_name, sizeof(class_name)) || !std::memchr(class_name,0,sizeof(class_name)) ||
            std::strcmp(class_name, "SkinnedMeshRenderer")) { renderer_status = "renderer-class-refused"; return; }
        uintptr_t table = 0; uint16_t length = 0;
        if (!read_bytes(klass+0x58, &table, sizeof(table)) || !table ||
            !read_bytes(klass+0xc0, &length, sizeof(length)) || length > 384) { renderer_status = "renderer-method-table-refused"; return; }
        const auto deadline = GetTickCount64()+12;
        for (unsigned i = 0; i < length; ++i) {
            if (GetTickCount64() >= deadline) { renderer_status = "renderer-method-scan-partial"; return; }
            uintptr_t item = 0, info[8]{}; char method_name[64]{};
            if (!read_bytes(table+i*8, &item, sizeof(item)) || !item || !read_bytes(item, info, sizeof(info)) || info[0] != klass) {
                renderer_status = "renderer-method-owner-refused"; return;
            }
            const auto address = reinterpret_cast<uintptr_t>(metadata::method_name(item));
            if (!address || !read_bytes(address, method_name, sizeof(method_name)) || !std::memchr(method_name,0,sizeof(method_name))) continue;
            if (std::strcmp(method_name, "get_sharedMesh") && std::strcmp(method_name, "get_bones")) continue;
            if (renderer_method_count >= 16 || !executable(info[1],metadata::image_base) || !executable(info[4],metadata::image_base)) return;
            auto& method = renderer_methods[renderer_method_count++]; method = {}; strcpy_s(method.name, method_name);
            method.parameters = reinterpret_cast<const uint8_t*>(info)[0x2e]; method.code = info[1]-metadata::image_base;
            method.invoker = info[4]-metadata::image_base;
        }
        renderer_status = "renderer-named-methods-observed";
        uintptr_t bones_code = 0, mesh_code = 0;
        for (unsigned i = 0; i < renderer_method_count; ++i) {
            const auto& method = renderer_methods[i];
            if (!method.parameters && !std::strcmp(method.name,"get_bones")) bones_code = metadata::image_base+method.code;
            if (!method.parameters && !std::strcmp(method.name,"get_sharedMesh")) mesh_code = metadata::image_base+method.code;
        }
        if (!entry("RendererBonesGetNative", metadata::image_base) || !entry("RendererMeshGetNative", metadata::image_base) ||
            !bones_code || !mesh_code || bones_code != entry("RendererBonesGet",metadata::image_base) ||
            mesh_code != entry("RendererMeshGet",metadata::image_base)) return;
        void* array = reinterpret_cast<AvatarGetFn>(bones_code)(component);
        void* mesh = reinterpret_cast<AvatarGetFn>(mesh_code)(component);
        uint64_t array_length = 0;
        if (!array || !mesh || !read_bytes(reinterpret_cast<uintptr_t>(array)+24,&array_length,sizeof(array_length)) ||
            !array_length || array_length > 512) { renderer_status = "renderer-bones-array-refused"; return; }
        for (unsigned i = 0; i < array_length; ++i) {
            void* bone = nullptr;
            if (!read_bytes(reinterpret_cast<uintptr_t>(array)+32+i*8,&bone,sizeof(bone)) || !bone) {
                renderer_status = "renderer-bone-unreadable"; return;
            }
            uintptr_t bone_identity = 0;
            if (!read_bytes(reinterpret_cast<uintptr_t>(bone)+16,&bone_identity,sizeof(bone_identity)) || !bone_identity) return;
            unsigned bone_matches = 0; int node = -1;
            for (unsigned n = 0; n < count; ++n) if (bone_identity == nodes[n].identity) { node = static_cast<int>(n); ++bone_matches; }
            bool under_model = false;
            for (int n = node; n >= 0; n = nodes[n].parent) if (n == model_node) under_model = true;
            if (bone_matches != 1 || !under_model || !roundtrip::identity_current(nodes[node])) {
                renderer_status = "renderer-bone-owner-refused"; return;
            }
            bone_nodes[bone_count++] = node;
        }
        if (!read_bytes(reinterpret_cast<uintptr_t>(mesh),&klass,sizeof(klass)) || !klass ||
            !read_bytes(klass+0x28,&name_pointer,sizeof(name_pointer)) || !read_bytes(name_pointer,class_name,sizeof(class_name)) ||
            !std::memchr(class_name,0,sizeof(class_name)) || std::strcmp(class_name,"Mesh")) { renderer_status = "mesh-class-refused"; return; }
        if (!read_bytes(klass+0x58,&table,sizeof(table)) || !table || !read_bytes(klass+0xc0,&length,sizeof(length)) || length > 384) {
            renderer_status = "mesh-method-table-refused"; return;
        }
        for (unsigned i = 0; i < length; ++i) {
            uintptr_t item = 0, info[8]{}; char method_name[64]{};
            if (!read_bytes(table+i*8,&item,sizeof(item)) || !item || !read_bytes(item,info,sizeof(info)) || info[0] != klass) return;
            const auto address = reinterpret_cast<uintptr_t>(metadata::method_name(item));
            if (!address || !read_bytes(address,method_name,sizeof(method_name)) || !std::memchr(method_name,0,sizeof(method_name)) ||
                std::strcmp(method_name,"get_bindposes")) continue;
            if (mesh_method.name[0] || !executable(info[1],metadata::image_base) || !executable(info[4],metadata::image_base)) return;
            strcpy_s(mesh_method.name,method_name); mesh_method.parameters = reinterpret_cast<const uint8_t*>(info)[0x2e];
            mesh_method.code = info[1]-metadata::image_base; mesh_method.invoker = info[4]-metadata::image_base;
        }
        renderer_status = "mesh-bindpose-method-observed";
        const auto bind_code = entry("MeshBindPosesGet",metadata::image_base);
        if (!bind_code || !entry("MeshBindPosesGetNative",metadata::image_base) || !entry("MeshBindPosesArrayCopy",metadata::image_base) ||
            mesh_method.parameters || metadata::image_base+mesh_method.code != bind_code) return;
        void* bind_array = reinterpret_cast<AvatarGetFn>(bind_code)(mesh);
        uint64_t bind_length = 0;
        if (!bind_array || !read_bytes(reinterpret_cast<uintptr_t>(bind_array)+24,&bind_length,sizeof(bind_length)) || bind_length != bone_count) {
            renderer_status = "bindpose-count-mismatch"; return;
        }
        uintptr_t array_class = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(bind_array),&array_class,sizeof(array_class)) || !array_class ||
            !read_bytes(array_class+0x28,&name_pointer,sizeof(name_pointer)) || !read_bytes(name_pointer,class_name,sizeof(class_name)) ||
            !std::memchr(class_name,0,sizeof(class_name)) || std::strcmp(class_name,"Matrix4x4[]")) {
            renderer_status = "bindpose-array-class-refused"; return;
        }
        for (unsigned i = 0; i < bind_length; ++i) {
            if (!read_bytes(reinterpret_cast<uintptr_t>(bind_array)+32+i*64,bind_matrices[i],64)) return;
            const auto& matrix = bind_matrices[i];
            for (unsigned n = 0; n < 16; ++n) if (!std::isfinite(matrix[n]) || std::abs(matrix[n]) > 100000) {
                renderer_status = "bindpose-matrix-invalid"; return;
            }
            if (std::abs(matrix[3])+std::abs(matrix[7])+std::abs(matrix[11])+std::abs(matrix[15]-1) > 0.0001f) {
                renderer_status = "bindpose-matrix-not-affine"; return;
            }
        }
        // Reacquire both arrays and owner identities before accepting the snapshot.
        void* again = reinterpret_cast<AvatarGetFn>(bones_code)(component);
        void* repeated_bind = reinterpret_cast<AvatarGetFn>(bind_code)(mesh);
        uint64_t repeated_length = 0, repeated_bones = 0;
        if (!same_object(reinterpret_cast<AvatarGetFn>(mesh_code)(component),mesh) || !again || !repeated_bind ||
            !read_bytes(reinterpret_cast<uintptr_t>(again)+24,&repeated_bones,sizeof(repeated_bones)) || repeated_bones != bone_count ||
            !read_bytes(reinterpret_cast<uintptr_t>(repeated_bind)+24,&repeated_length,sizeof(repeated_length)) || repeated_length != bone_count) {
            renderer_status = "bindpose-changed-during-read"; return;
        }
        for (unsigned i = 0; i < bone_count; ++i) {
            float matrix[16]{}; void* bone = nullptr; uintptr_t identity = 0;
            if (!read_bytes(reinterpret_cast<uintptr_t>(repeated_bind)+32+i*64,matrix,64) || memcmp(matrix,bind_matrices[i],64) ||
                !read_bytes(reinterpret_cast<uintptr_t>(again)+32+i*8,&bone,sizeof(bone)) || !bone ||
                !read_bytes(reinterpret_cast<uintptr_t>(bone)+16,&identity,sizeof(identity)) || identity != nodes[bone_nodes[i]].identity) {
                renderer_status = "bindpose-changed-during-read"; return;
            }
        }
        bind_count = bone_count; renderer_status = "bindpose-matrices-observed";
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); renderer_status = "renderer-read-exception"; }
}
static void capture_skins(int model_node) {
    if (!Enabled) return;
    const wchar_t* names[] = {L"Brow",L"EffectMesh",L"EyeStar",L"Face",L"Face_Eye",L"BakeLod3Obj"};
    const char* labels[] = {"Brow","EffectMesh","EyeStar","Face","Face_Eye","BakeLod3Obj"};
    for (unsigned i = 0; i < 6; ++i) {
        capture_renderer(model_node,names[i]);
        auto& skin = extra_skins[extra_count++]; skin = {}; skin.name = labels[i]; skin.status = renderer_status; skin.count = bind_count;
        std::memcpy(skin.ids,bone_nodes,sizeof(bone_nodes)); std::memcpy(skin.matrices,bind_matrices,sizeof(bind_matrices));
    }
    capture_renderer(model_node);
}
static void capture(void* animator) {
    if (!Enabled) return;
    __try {
        status = "animator-avatar-methods-unverified";
        if (!avatar_get || !human_get || !metadata::accessors_verified ||
            !metadata::named_code("get_avatar", 0, reinterpret_cast<uintptr_t>(avatar_get)) ||
            !metadata::named_code("get_isHuman", 0, reinterpret_cast<uintptr_t>(human_get))) return;
        is_human = human_get(animator);
        void* avatar = avatar_get(animator);
        if (!avatar) { status = "avatar-unavailable"; return; }
        uintptr_t klass = 0, native = 0, name_pointer = 0; char class_name[64]{};
        if (!read_bytes(reinterpret_cast<uintptr_t>(avatar), &klass, sizeof(klass)) || !klass ||
            !read_bytes(reinterpret_cast<uintptr_t>(avatar)+16, &native, sizeof(native)) || !native ||
            !read_bytes(klass+0x28, &name_pointer, sizeof(name_pointer)) ||
            !read_bytes(name_pointer, class_name, sizeof(class_name)) || !std::memchr(class_name, 0, sizeof(class_name)) ||
            std::strcmp(class_name, "Avatar")) {
            status = "avatar-class-unverified"; return;
        }
        available = true;
        uintptr_t table = 0; uint16_t length = 0;
        if (!read_bytes(klass+0x58, &table, sizeof(table)) || !table ||
            !read_bytes(klass+0xc0, &length, sizeof(length)) || length > 384) { status = "avatar-method-table-refused"; return; }
        declared_methods = length;
        const auto deadline = GetTickCount64()+12;
        for (unsigned i = 0; i < length; ++i) {
            if (GetTickCount64() >= deadline) { status = "avatar-method-scan-partial"; return; }
            uintptr_t item = 0, info[8]{};
            if (!read_bytes(table+i*8, &item, sizeof(item)) || !item || !read_bytes(item, info, sizeof(info)) || info[0] != klass) {
                status = "avatar-method-owner-refused"; return;
            }
            char name[64]{};
            const auto name_address = reinterpret_cast<uintptr_t>(metadata::method_name(item));
            if (!name_address || !read_bytes(name_address, name, sizeof(name)) || !std::memchr(name, 0, sizeof(name)) || method_count >= 16) continue;
            bool printable = true;
            for (unsigned n = 0; name[n]; ++n) if (name[n] < 32 || name[n] > 126 || name[n] == '"' || name[n] == '\\') printable = false;
            if (!printable) continue;
            if (!executable(info[1], metadata::image_base) || !executable(info[4], metadata::image_base)) {
                status = "avatar-method-code-refused"; return;
            }
            auto& method = methods[method_count++]; method = {}; strcpy_s(method.name, name);
            method.parameters = reinterpret_cast<const uint8_t*>(info)[0x2e]; method.code = info[1]-metadata::image_base;
            method.invoker = info[4]-metadata::image_base;
        }
        if (avatar_get(animator) != avatar || human_get(animator) != is_human) { status = "avatar-changed-during-read"; return; }
        status = "avatar-named-methods-observed";
        capture_avatar_storage(avatar);
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); status = "avatar-read-exception"; }
}
static void report(std::ostream& out) {
    out << "{\"enabled\":" << (Enabled ? "true" : "false") << ",\"status\":\"" << status
        << "\",\"isHuman\":" << (is_human ? "true" : "false") << ",\"avatarAvailable\":" << (available ? "true" : "false")
        << ",\"declaredMethods\":" << declared_methods << ",\"exception\":" << exception << ",\"methods\":[";
    for (unsigned i = 0; i < method_count; ++i) {
        if (i) out << ',';
        out << "{\"name\":\"" << methods[i].name << "\",\"parameters\":" << methods[i].parameters
            << ",\"codeRva\":" << methods[i].code << ",\"invokerRva\":" << methods[i].invoker << '}';
    }
    out << "],\"renderer\":{\"status\":\"" << renderer_status << "\",\"methods\":[";
    for (unsigned i = 0; i < renderer_method_count; ++i) {
        if (i) out << ',';
        out << "{\"name\":\"" << renderer_methods[i].name << "\",\"parameters\":" << renderer_methods[i].parameters
            << ",\"codeRva\":" << renderer_methods[i].code << ",\"invokerRva\":" << renderer_methods[i].invoker << '}';
    }
    out << "],\"boneCount\":" << bone_count << ",\"boneNodeIds\":[";
    for (unsigned i = 0; i < bone_count; ++i) { if (i) out << ','; out << bone_nodes[i]; }
    out << "],\"bindPoseCount\":" << bind_count << ",\"inverseBindMatricesColumnMajor\":[";
    for (unsigned i = 0; i < bind_count; ++i) {
        if (i) out << ','; out << '[';
        for (unsigned n = 0; n < 16; ++n) { if (n) out << ','; out << std::setprecision(9) << bind_matrices[i][n]; }
        out << ']';
    }
    out << "],\"meshMethod\":{\"name\":\"" << mesh_method.name << "\",\"parameters\":" << mesh_method.parameters
        << ",\"codeRva\":" << mesh_method.code << ",\"invokerRva\":" << mesh_method.invoker << "}},\"additionalSkins\":[";
    for (unsigned i = 0; i < extra_count; ++i) {
        if (i) out << ','; const auto& skin = extra_skins[i];
        out << "{\"name\":\"" << skin.name << "\",\"status\":\"" << skin.status << "\",\"boneCount\":" << skin.count << ",\"boneNodeIds\":[";
        for (unsigned n = 0; n < skin.count; ++n) { if (n) out << ','; out << skin.ids[n]; }
        out << "],\"inverseBindMatricesColumnMajor\":[";
        for (unsigned n = 0; n < skin.count; ++n) {
            if (n) out << ','; out << '[';
            for (unsigned k = 0; k < 16; ++k) { if (k) out << ','; out << std::setprecision(9) << skin.matrices[n][k]; }
            out << ']';
        }
        out << "]}";
    }
    out << "],\"avatarReference\":{\"status\":\"" << avatar_rest_status << "\",\"boneCount\":" << avatar_rest_count
        << ",\"linkedNodes\":" << avatar_link_count << ",\"localTrs\":[";
    for (unsigned i = 0; i < avatar_rest_count; ++i) {
        if (i) out << ','; out << "{\"index\":" << i << ",\"parentIndex\":" << avatar_parents[i] << ",\"position\":[";
        for (unsigned n = 0; n < 3; ++n) { if (n) out << ','; out << avatar_local[i][n]; }
        out << "],\"rotation\":[";
        for (unsigned n = 0; n < 4; ++n) { if (n) out << ','; out << avatar_local[i][4+n]; }
        out << "],\"scale\":[";
        for (unsigned n = 0; n < 3; ++n) { if (n) out << ','; out << avatar_local[i][8+n]; }
        out << "]}";
    }
    out << "],\"nodeLinks\":["; bool first_link = true;
    if (avatar_rest_count) for (unsigned n = 0; n < count; ++n) if (avatar_links[n] >= 0) {
        if (!first_link) out << ','; first_link = false;
        out << "{\"nodeId\":" << n << ",\"avatarIndex\":" << avatar_links[n] << '}';
    }
    out << "],\"candidateCount\":" << avatar_candidate_count << ",\"invalidRecordIndex\":" << avatar_invalid_index << ",\"invalidTrs\":[";
    if (avatar_invalid_index >= 0) for (unsigned n = 0; n < 12; ++n) {
        if (n) out << ','; out << avatar_local[avatar_invalid_index][n];
    }
    out << "]}}";
}
}
