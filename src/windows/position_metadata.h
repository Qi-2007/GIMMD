#pragma once
namespace position_metadata {
struct Method { char name[64]{}; unsigned parameters = 0; uintptr_t code = 0, invoker = 0; };
static Method methods[8]{};
static unsigned method_count = 0;
static const char* status = "disabled";
static DWORD exception = 0;
static void reset() { method_count = 0; exception = 0; status = bind_pose::Enabled ? "not-attempted" : "disabled"; }
static void capture(int model_node) {
    if (!bind_pose::Enabled || model_node < 0 || !metadata::accessors_verified) return;
    __try {
        void* transform = nodes[model_node].object; uintptr_t klass = 0, name_pointer = 0, table = 0;
        uint16_t length = 0; char class_name[64]{};
        if (!transform || !roundtrip::identity_current(nodes[model_node]) ||
            !read_bytes(reinterpret_cast<uintptr_t>(transform),&klass,sizeof(klass)) || !klass ||
            !read_bytes(klass+0x28,&name_pointer,sizeof(name_pointer)) || !read_bytes(name_pointer,class_name,sizeof(class_name)) ||
            !std::memchr(class_name,0,sizeof(class_name)) || std::strcmp(class_name,"Transform") ||
            !read_bytes(klass+0x58,&table,sizeof(table)) || !table || !read_bytes(klass+0xc0,&length,sizeof(length)) || length > 384) {
            status = "transform-class-or-table-refused"; return;
        }
        const auto deadline = GetTickCount64()+12;
        for (unsigned i = 0; i < length; ++i) {
            if (GetTickCount64() >= deadline) { status = "transform-method-scan-partial"; return; }
            uintptr_t item = 0, info[8]{}; char name[64]{};
            if (!read_bytes(table+i*8,&item,sizeof(item)) || !item || !read_bytes(item,info,sizeof(info)) || info[0] != klass) {
                status = "transform-method-owner-refused"; return;
            }
            const auto address = reinterpret_cast<uintptr_t>(metadata::method_name(item));
            if (!address || !read_bytes(address,name,sizeof(name)) || !std::memchr(name,0,sizeof(name))) continue;
            if (std::strcmp(name,"get_localPosition") && std::strcmp(name,"set_localPosition") &&
                std::strcmp(name,"INTERNAL_get_localPosition") && std::strcmp(name,"INTERNAL_set_localPosition") &&
                std::strcmp(name,"get_localScale") && std::strcmp(name,"INTERNAL_get_localScale")) continue;
            if (method_count >= 8 || !executable(info[1],metadata::image_base) || !executable(info[4],metadata::image_base)) {
                status = "transform-method-code-refused"; return;
            }
            auto& method = methods[method_count++]; method = {}; strcpy_s(method.name,name);
            method.parameters = reinterpret_cast<const uint8_t*>(info)[0x2e]; method.code = info[1]-metadata::image_base;
            method.invoker = info[4]-metadata::image_base;
        }
        status = "transform-position-methods-observed";
    } __except (EXCEPTION_EXECUTE_HANDLER) { exception = GetExceptionCode(); status = "transform-method-read-exception"; }
}
inline bool named(const char* name, unsigned parameters, uintptr_t code) {
    unsigned found = 0;
    for (unsigned i = 0; i < method_count; ++i)
        if (!std::strcmp(methods[i].name,name) && methods[i].parameters == parameters && methods[i].code+metadata::image_base == code) ++found;
    return found == 1;
}
static void report(std::ostream& out) {
    out << "{\"status\":\"" << status << "\",\"exception\":" << exception << ",\"methods\":[";
    for (unsigned i = 0; i < method_count; ++i) {
        if (i) out << ',';
        const auto& method = methods[i];
        out << "{\"name\":\"" << method.name << "\",\"parameters\":" << method.parameters
            << ",\"codeRva\":" << method.code << ",\"invokerRva\":" << method.invoker << '}';
    }
    out << "]}";
}
}
