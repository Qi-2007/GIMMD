#pragma once
namespace script_metadata {
struct Field { wchar_t script[96]{}; char value[96]{}, native_type[64]{}; unsigned offset = 0, layout = 0; uint64_t payload_size = 0; };
static Field fields[64]{};
static unsigned used = 0;
static void capture(uintptr_t native, const wchar_t* label, uintptr_t base) {
    if (std::wcscmp(label,L"AssetBundleExternalResourceProvider") && std::wcscmp(label,L"AvatarExcelConfig") && std::wcscmp(label,L"StreamPreloadConfig")) return;
    uint32_t tag = 0; uintptr_t info = 0, type_text = 0; char type_name[64]{};
    if (!read_bytes(native + 0xc, &tag, 4) || !read_bytes(base + 0x56c1c18 + static_cast<uintptr_t>(tag >> 21) * 8, &info, 8) || !info ||
        !read_bytes(info + 0x10, &type_text, 8) || !type_text || !read_bytes(type_text, type_name, 63)) return;
    if (used < 64) {
        auto& row = fields[used++]; wcscpy_s(row.script,label); strcpy_s(row.native_type,type_name);
        row.layout=2; strcpy_s(row.value,"native-type");
    }
    uintptr_t payload = 0; uint64_t payload_size = 0;
    if (used < 64 && read_bytes(native + 0x60, &payload_size, 8) && payload_size <= 4194304 && read_bytes(native + 0x48, &payload, 8)) {
        auto& row = fields[used++]; wcscpy_s(row.script,label); strcpy_s(row.native_type,type_name);
        row.layout=3; row.offset=0x48; row.payload_size=payload_size;
        uint8_t bytes[32]{}; const auto length=static_cast<size_t>(std::min<uint64_t>(32,payload_size));
        if (!payload) payload=native+0x50;
        if (read_bytes(payload,bytes,length)) {
            const char digits[]="0123456789abcdef";
            for (size_t n=0;n<length;++n) { row.value[n*2]=digits[bytes[n]>>4]; row.value[n*2+1]=digits[bytes[n]&15]; }
        }
    }
    if (!std::strstr(type_name,"MonoScript")) return;

}
static void report(std::ostream& out) {
    out << ",\"scriptMetadataFields\":[";
    for (unsigned i=0;i<used;++i) {
        const auto& row=fields[i];
        out << (i ? ",":"") << "{\"script\":"; hierarchy::json_string(out,row.script);
        out << ",\"offset\":" << row.offset << ",\"layout\":" << row.layout << ",\"payloadSize\":" << row.payload_size << ",\"nativeType\":\"" << row.native_type << "\",\"value\":";
        wchar_t value[96]{}; for (unsigned n=0;n<95&&row.value[n];++n) value[n]=static_cast<unsigned char>(row.value[n]);
        hierarchy::json_string(out,value); out << '}';
    }
    out << ']';
}
}
