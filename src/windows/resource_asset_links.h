#pragma once
// Read-only reverse lookup from persistent shared mesh instance IDs to bundle hashes.
namespace asset_links {
struct Target { wchar_t model[96]{}, mesh[96]{}; uint32_t instance = 0; bool game_object = false; };
struct Link { Target target{}; wchar_t bundle[96]{}; uint32_t hash = 0; };
static Target targets[8]{};
static Link links[32]{};
static uintptr_t image_base = 0;
static unsigned target_count = 0, link_count = 0;
struct Cache { wchar_t source_file[384]{}; uint64_t source_offset=0, reader_data_start=0; uint32_t content_bytes=0, reader_header_size=0, reader_version=0; bool source_verified=false; wchar_t bundle[96]{}; char key[9]{}, name[96]{}, type[64]{}; uint32_t hash = 0, source_id = 0, getter_size = 0, getter_format = 0; uint64_t descriptors = 0, size48 = 0, size50 = 0; };
static Cache caches[256]{};
static unsigned cache_count = 0, cache_samples = 0, spawners = 0;
// Layout proven by AssetBundle.GetBinDataCount -> bundle+188, ASBLD open -> loader+f0,
// FileReader constructor and initialization. No pointer scans or engine calls.
static void source_file(Cache& row,uintptr_t bundle_native) {
    uintptr_t loader=0,reader=0,table=0; uint32_t magic=0,initialized=0;
    // Async ASBLD subclass retains its original path and offset after closing FileReader.
    if (read_bytes(bundle_native+0x188,&loader,8) && loader && read_bytes(loader,&table,8) && table==image_base+0x22025c0) {
        uint64_t length=0;uintptr_t text=0;char path[384]{};
        if (read_bytes(loader+0x288,&length,8) && length && length<383 && read_bytes(loader+0x270,&text,8) &&
            (text || length<16) && read_bytes(text ? text:loader+0x278,path,static_cast<size_t>(length+1)) && !path[length] &&
            std::strstr(path,"blocks") && std::strstr(path,".blk") && read_bytes(loader+0x298,&row.source_offset,8) &&
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,row.source_file,384)) {
            read_bytes(bundle_native+0x194,&row.content_bytes,4);
            row.source_verified=true;return;
        }
    }

    if (!read_bytes(bundle_native+0x188,&loader,8)||!loader||!read_bytes(loader+0xf0,&reader,8)||!reader||
        !read_bytes(reader,&table,8)||table!=image_base+0x21f3e30||
        !read_bytes(reader+0x10,&magic,4)||magic!=0x03626c42||
        !read_bytes(reader+0x18,&row.reader_version,4)||((row.reader_version&255)!=4&&(row.reader_version&255)!=5)||
        !read_bytes(reader+0x14,&row.reader_header_size,4)||row.reader_header_size<0x48||row.reader_header_size>0x1000000||
        !read_bytes(reader+0x158,&initialized,1)||initialized!=1||
        !read_bytes(reader+0x3a0,&row.reader_data_start,8)) return;
    const uint64_t prefix=(row.reader_version&255)==5 ? 28 : 12;
    if (row.reader_data_start<row.reader_header_size+prefix) return;
    char path[384]{};
    if (!read_bytes(reader+0x160,path,383)||!std::memchr(path,0,383)||!std::strstr(path,"blocks")||!std::strstr(path,".blk")) return;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,row.source_file,384)) return;
    row.source_offset=row.reader_data_start-row.reader_header_size-prefix;row.source_verified=true;
}
static void observe_caches(void* object, uintptr_t native) {
    uintptr_t begin = 0, end = 0;
    if (!read_bytes(native + 0x168, &begin, 8) || !read_bytes(native + 0x170, &end, 8) ||
        !begin || end < begin || (end - begin) % 16 || (end - begin) / 16 > 256) return;
    for (uintptr_t address = begin; address < end; address += 16) {
        uintptr_t cache = 0, text = 0, vtable = 0, getter = 0; uint64_t length = 0;
        if (!read_bytes(address + 8, &cache, 8) || !cache || !read_bytes(cache + 0x68, &length, 8) || length != 8 ||
            !read_bytes(cache + 0x50, &text, 8)) continue;
        char key[9]{};
        if (!read_bytes(text ? text : cache + 0x58, key, 8)) continue;
        uint32_t hash = 0; bool valid = true;
        for (const char ch : key) {
            if (!ch) break;
            const unsigned value = ch >= '0' && ch <= '9' ? ch - '0' : (ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 16);
            if (value > 15) { valid = false; break; }
            hash = (hash << 4) | value;
        }
        if (!valid || !hash) continue;
        ++cache_count;
        char label[96]{}, type_name[64]{};
        if (read_bytes(cache, &vtable, 8) && vtable && read_bytes(vtable + 0x58, &getter, 8) && executable(getter, image_base)) {
            using NativeNameFn = const char*(__fastcall*)(void*);
            const auto value = reinterpret_cast<NativeNameFn>(getter)(reinterpret_cast<void*>(cache));
            if (value) { read_bytes(reinterpret_cast<uintptr_t>(value), label, 95); label[95] = 0; }
        }
        uintptr_t data = 0; uint32_t source_id = 0;
        if (read_bytes(cache + 0x48, &data, 8) && data && read_bytes(data + 0x6c, &source_id, 4) && source_id) {
            using InstanceFn = void*(__fastcall*)(uint32_t);
            void* source = reinterpret_cast<InstanceFn>(image_base + 0x19cb2fe0)(source_id);
            wchar_t source_label[96]{};
            if (source && string_read(name_a(source), source_label)) {
                std::memset(label, 0, sizeof(label));
                for (unsigned n = 0; n < 95 && source_label[n]; ++n) label[n] = source_label[n] < 127 ? static_cast<char>(source_label[n]) : '?';
            }
        }
        uint32_t getter_size = 0, getter_format = 0; uint64_t size48 = 0, size50 = 0, descriptors_count = 0;
        uintptr_t spawner = 0, asset_text = 0; uint64_t asset_length = 0, asset_capacity = 0;
        if (read_bytes(cache + 0x88, &spawner, 8) && spawner) {
            ++spawners;
            uintptr_t template_data = 0, descriptors = 0; uint64_t descriptor_count = 0; uint8_t ready = 0;
            if (read_bytes(spawner + 0x58, &template_data, 8) && template_data &&
                read_bytes(template_data + 0x19, &ready, 1) && ready &&
                read_bytes(template_data + 0x60, &descriptors, 8) && descriptors &&
                read_bytes(template_data + 0x70, &descriptor_count, 8) && descriptor_count && descriptor_count <= 4096 &&
                read_bytes(descriptors + 0x10, &source_id, 4) && source_id) {
                using InstanceFn = void*(__fastcall*)(uint32_t);
                void* source = reinterpret_cast<InstanceFn>(image_base + 0x19cb2fe0)(source_id);
                wchar_t source_label[96]{};
                if (source && string_read(name_a(source), source_label)) {
                    std::memset(label, 0, sizeof(label));
                    for (unsigned n = 0; n < 95 && source_label[n]; ++n) label[n] = source_label[n] < 127 ? static_cast<char>(source_label[n]) : '?';
                }
            }
            if (template_data && ready) {
                uintptr_t blocks = 0; uint64_t block_count = 0;
                if (read_bytes(template_data+0x38,&blocks,8)&&blocks&&read_bytes(template_data+0x48,&block_count,8)&&block_count&&block_count<=65536) {
                    char bytes[1024]{}; unsigned copied = 0;
                    for (uint64_t n=0;n<std::min<uint64_t>(4,block_count);++n) {
                        uintptr_t block=0;
                        if (!read_bytes(blocks+n*8,&block,8)||!block||!read_bytes(block,bytes+copied,256)) break;
                        copied+=256;
                    }
                    for (unsigned n=0;n+16<copied;++n) {
                        const bool narrow=!std::memcmp(bytes+n,"Avatar_",7);
                        const uint8_t wide_prefix[]={65,0,118,0,97,0,116,0,97,0,114,0,95,0};
                        const bool wide=!std::memcmp(bytes+n,wide_prefix,sizeof(wide_prefix));
                        if (!narrow&&!wide) continue;
                        char root_name[96]{}; unsigned root_length=0;
                        for (;root_length<95&&n+root_length*(wide ? 2:1)<copied;++root_length) {
                            const char ch=bytes[n+root_length*(wide ? 2:1)];
                            if (wide&&(n+root_length*2+1>=copied||bytes[n+root_length*2+1])) break;
                            if (!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_')) break;
                            root_name[root_length]=ch;
                        }
                        if (std::strncmp(root_name,"Avatar_",7)) continue;
                        strcpy_s(label,root_name); break;
                    }
                }
                uintptr_t stream_table = 0, stream_getter = 0;
                read_bytes(template_data+0x48,&size48,8); read_bytes(template_data+0x50,&size50,8);
                read_bytes(template_data+0x70,&descriptors_count,8);
                if (read_bytes(template_data+0x20,&stream_table,8)&&stream_table) {
                    if (read_bytes(stream_table+0x28,&stream_getter,8)&&executable(stream_getter,image_base)) getter_size=static_cast<uint32_t>(stream_getter-image_base);
                    if (read_bytes(stream_table+0x40,&stream_getter,8)&&executable(stream_getter,image_base)) getter_format=static_cast<uint32_t>(stream_getter-image_base);
                }
            }
            if (!label[0] && read_bytes(spawner + 0x38, &asset_length, 8) && read_bytes(spawner + 0x40, &asset_capacity, 8) &&
                asset_length && asset_length <= asset_capacity && asset_capacity <= 1048576) {
                asset_text = spawner + 0x28;
                if (asset_capacity >= 16 && !read_bytes(asset_text, &asset_text, 8)) asset_text = 0;
                if (asset_text) {
                    std::memset(label, 0, sizeof(label));
                    read_bytes(asset_text, label, static_cast<size_t>(std::min<uint64_t>(95, asset_length)));
                }
            }
        }
        uint32_t type_index = 0; uintptr_t info = 0, type_text = 0;
        if (read_bytes(cache + 0xc, &type_index, 4) &&
            read_bytes(image_base + 0x56c1c18 + static_cast<uintptr_t>(type_index >> 21) * 8, &info, 8) && info &&
            read_bytes(info + 0x10, &type_text, 8) && type_text) {
            read_bytes(type_text, type_name, 63); type_name[63] = 0;
        }
        if (cache_samples >= std::size(caches) || (cache_samples >= 8 && !std::strstr(label, "Manekin"))) continue;
        auto& row = caches[cache_samples++]; row.hash = hash; row.source_id = source_id; row.getter_size=getter_size; row.getter_format=getter_format; row.size48=size48; row.size50=size50; row.descriptors=descriptors_count; strcpy_s(row.key, key);
        memcpy(row.name, label, sizeof(label)); memcpy(row.type, type_name, sizeof(type_name));
        string_read(name_a(object), row.bundle);
        if (std::strstr(label,"Manekin")) source_file(row,native);
    }
}
static uint64_t cursor = 0, total = 0;
static void* bundle_class = nullptr;
static const char* status = "not-attempted";
static void configure(uintptr_t base, void* klass) {
    image_base = base; bundle_class = klass;
    status = "waiting-for-preview-model";
}
static void observe_model(void* object, const wchar_t* label) {
    if (!image_base || !std::wcsstr(label, L"Manekin") || target_count == 8) return;
    const auto mesh_entry = entry("RendererMeshGet", image_base);
    if (!mesh_entry || !entry("RendererMeshGetNative", image_base)) return;
    uintptr_t native = 0; void* root = nullptr;
    if (!read_bytes(reinterpret_cast<uintptr_t>(object) + 16, &native, 8) || !native ||
        object_transform(&root, reinterpret_cast<void*>(native)) != &root || !root) return;
    uint32_t root_instance = 0;
    if (read_bytes(native + 8, &root_instance, 4) && root_instance) {
        bool duplicate = false;
        for (unsigned j = 0; j < target_count; ++j) duplicate = duplicate || targets[j].instance == root_instance;
        if (!duplicate && target_count < 8) {
            auto& row = targets[target_count++]; row.instance = root_instance; row.game_object = true;
            wcscpy_s(row.model, label); wcscpy_s(row.mesh, L"preview-root");
            cursor = total = 0; link_count = 0; cache_count = cache_samples = spawners = 0; status = "bundle-scan-pending";
        }
    }
    void* pending[512]{}; unsigned used = 1; pending[0] = root;
    void* renderer_type = find_string("SkinnedMeshRenderer");
    if (!renderer_type) return;
    const auto deadline = GetTickCount64() + 12;
    for (unsigned i = 0; i < used; ++i) {
        if (GetTickCount64() >= deadline) return;
        wchar_t name[96]{};
        if (!string_read(name_a(pending[i]), name)) return;
        if (!std::wcscmp(name, L"Body")) {
            void* renderer = animator_observer::get_component(pending[i], renderer_type);
            if (renderer) {
                using MeshFn = void*(__fastcall*)(void*);
                void* mesh = reinterpret_cast<MeshFn>(mesh_entry)(renderer);
                uintptr_t mesh_native = 0; uint32_t instance = 0;
                if (mesh && read_bytes(reinterpret_cast<uintptr_t>(mesh) + 16, &mesh_native, 8) && mesh_native &&
                    read_bytes(mesh_native + 8, &instance, 4) && instance) {
                    bool duplicate = false;
                    for (unsigned j = 0; j < target_count; ++j) duplicate = duplicate || targets[j].instance == instance;
                    if (!duplicate && target_count < 8) {
                        auto& row = targets[target_count++]; row.instance = instance;
                        wcscpy_s(row.model, label); string_read(name_a(mesh), row.mesh);
                        cursor = total = 0; link_count = 0; cache_count = cache_samples = spawners = 0; status = "bundle-scan-pending";
                    }
                }
            }
        }
        const int children = child_count(pending[i]);
        if (children < 0 || children > 128 || used + children > 512) return;
        for (int j = 0; j < children; ++j) {
            void* child = get_child(pending[i], j);
            if (!child || !same_object(get_parent(child), pending[i])) return;
            pending[used++] = child;
        }
    }
}
static void tick() {
    if (!image_base || !bundle_class || !target_count || std::strcmp(status, "bundle-scan-pending")) return;
    using TypeFn = void*(__fastcall*)(void*);
    using HashFn = uint32_t(__fastcall*)(void*, uint32_t);
    void* type = reinterpret_cast<TypeFn>(image_base + 0x52f450)(reinterpret_cast<uint8_t*>(bundle_class) + 0x68);
    if (!type) { status = "bundle-type-refused"; return; }
    void* array = reinterpret_cast<TypeFn>(image_base + 0x19ccad20)(type);
    uint64_t length = 0;
    if (!array || !read_bytes(reinterpret_cast<uintptr_t>(array) + 24, &length, 8) || length > 65536) {
        status = "bundle-array-refused"; return;
    }
    if (total != length) { cursor = 0; total = length; link_count = 0; cache_count = cache_samples = spawners = 0; }
    const auto deadline = GetTickCount64() + 12;
    for (; cursor < total; ++cursor) {
        if (GetTickCount64() >= deadline) return;
        void* object = nullptr; uintptr_t native = 0, klass = 0, map = 0, head = 0;
        if (!read_bytes(reinterpret_cast<uintptr_t>(array) + 32 + cursor * 8, &object, 8) || !object ||
            !read_bytes(reinterpret_cast<uintptr_t>(object), &klass, 8) || klass != reinterpret_cast<uintptr_t>(bundle_class) ||
            !read_bytes(reinterpret_cast<uintptr_t>(object) + 16, &native, 8) || !native ||
            !read_bytes(native + 0xa8, &map, 8) || !map || !read_bytes(map, &head, 8) || !head) continue;
        observe_caches(object, native);
        for (unsigned i = 0; i < target_count; ++i) {
            const auto hash = reinterpret_cast<HashFn>(image_base + 0x19febc0)(reinterpret_cast<void*>(native), targets[i].instance);
            if (!hash) continue;
            if (link_count == 32) { status = "asset-links-truncated"; return; }
            auto& row = links[link_count++]; row.target = targets[i]; row.hash = hash;
            string_read(name_a(object), row.bundle);
        }
    }
    status = "asset-links-observed";
}
static void report(std::ostream& out) {
    out << ",\"assetLinks\":{\"status\":\"" << status << "\",\"targets\":" << target_count
        << ",\"bundles\":" << total << ",\"scanned\":" << cursor << ",\"links\":[";
    for (unsigned i = 0; i < link_count; ++i) {
        const auto& row = links[i];
        out << (i ? "," : "") << "{\"instanceId\":" << row.target.instance << ",\"hash\":" << row.hash << ",\"kind\":\"" << (row.target.game_object ? "GameObject" : "Mesh") << "\",\"model\":";
        hierarchy::json_string(out, row.target.model); out << ",\"mesh\":"; hierarchy::json_string(out, row.target.mesh);
        out << ",\"bundle\":"; hierarchy::json_string(out, row.bundle); out << '}';
    }
    out << "],\"cacheEntries\":" << cache_count << ",\"spawners\":" << spawners << ",\"cacheSamples\":[";
    for (unsigned i = 0; i < cache_samples; ++i) {
        const auto& row = caches[i];
        out << (i ? "," : "") << "{\"hash\":" << row.hash << ",\"sourceInstanceId\":" << row.source_id << ",\"getterSizeRva\":" << row.getter_size << ",\"getterFormatRva\":" << row.getter_format
            << ",\"descriptors\":" << row.descriptors << ",\"size48\":" << row.size48 << ",\"size50\":" << row.size50 << ",\"key\":\"" << row.key << "\",\"name\":";
        wchar_t label[96]{};
        for (unsigned n = 0; n < 95 && row.name[n]; ++n) label[n] = static_cast<unsigned char>(row.name[n]);
        hierarchy::json_string(out, label); out << ",\"type\":";
        std::memset(label, 0, sizeof(label));
        for (unsigned n = 0; n < 63 && row.type[n]; ++n) label[n] = static_cast<unsigned char>(row.type[n]);
        hierarchy::json_string(out, label); out << ",\"bundle\":"; hierarchy::json_string(out, row.bundle);
        out << ",\"contentBytes\":" << row.content_bytes << ",\"sourceLayoutVerified\":" << (row.source_verified ? "true":"false") << ",\"sourceOffset\":" << row.source_offset
            << ",\"readerDataStart\":" << row.reader_data_start << ",\"readerHeaderSize\":" << row.reader_header_size << ",\"readerVersion\":" << row.reader_version << ",\"sourceFile\":";
        hierarchy::json_string(out,row.source_file);out << '}';
    }
    out << "]}";
}
}
