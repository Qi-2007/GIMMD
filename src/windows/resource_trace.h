#pragma once
// Bounded pass-through tracing; object names are read only on the confirmed Unity thread.
namespace resource_trace {
#if defined(GIMMD_RESOURCE_TRACE) && !defined(GIMMD_OBSERVER_TEST)
inline constexpr bool Enabled = true;
#else
inline constexpr bool Enabled = false;
#endif
using FormatFn = bool(__fastcall*)(wchar_t*, size_t, void**, int32_t, int32_t, void**);
static FormatFn original = nullptr;
static uintptr_t target = 0;
static uint8_t original_bytes[32]{};
static std::atomic<bool> recording{false};
static std::atomic<unsigned> active{0}, calls{0};
struct Record {
    std::atomic<bool> ready{false}; DWORD thread = 0;
    int32_t index = 0, category = 0; bool success = false, strings_valid = false;
    wchar_t root[96]{}, suffix[96]{}, path[256]{};
};
static Record records[256];
static const char* status = "disabled";
static bool restored = true;
static unsigned duration_ms = 120000;
using HashFn = void*(__fastcall*)(void*, uint32_t, void*, int*);
static HashFn hash_original = nullptr;
static uintptr_t hash_target = 0;
static uint8_t hash_bytes[32]{};
static bool hash_enabled = false;
static const char* hash_status = "disabled";
static std::atomic<unsigned> hash_calls{0};
struct HashRecord {
    std::atomic<bool> ready{false}; DWORD thread = 0; uint32_t hash = 0;
    bool returned_object = false, names_read = false; wchar_t bundle[96]{}, asset[96]{};
};
static HashRecord hash_records[256];
static void hash_names(HashRecord& row, void* bundle, void* asset) {
    if (row.thread != window_thread || !hierarchy::name_a) return;
    __try {
        row.names_read = hierarchy::string_read(hierarchy::name_a(bundle), row.bundle);
        if (asset) hierarchy::string_read(hierarchy::name_a(asset), row.asset);
    } __except(EXCEPTION_EXECUTE_HANDLER) { row.names_read = false; }
}
static void* __fastcall observe_hash(void* bundle, uint32_t hash, void* type, int* output) {
    active.fetch_add(1);
    void* result = hash_original(bundle, hash, type, output);
    if (recording.load()) {
        const auto index = hash_calls.fetch_add(1);
        if (index < std::size(hash_records)) {
            auto& row = hash_records[index]; row.thread = GetCurrentThreadId(); row.hash = hash;
            row.returned_object = result != nullptr;
            hash_names(row, bundle, result); row.ready.store(true);
        }
    }
    active.fetch_sub(1);
    return result;
}
static bool managed_string(void** reference, wchar_t (&output)[96]) {
    void* value = nullptr;
    return reference && read_bytes(reinterpret_cast<uintptr_t>(reference), &value, sizeof(value)) &&
        hierarchy::string_read(value, output);
}
static bool __fastcall observe(wchar_t* output, size_t capacity, void** root, int32_t index, int32_t category, void** suffix) {
    active.fetch_add(1);
    const bool success = original(output, capacity, root, index, category, suffix);
    if (recording.load()) {
        const auto number = calls.fetch_add(1);
        if (number < std::size(records)) {
            auto& row = records[number];
            row.thread = GetCurrentThreadId(); row.index = index; row.category = category; row.success = success;
            row.strings_valid = managed_string(root, row.root) && managed_string(suffix, row.suffix);
            if (success && row.strings_valid) {
                const auto length = std::wcslen(row.root) + 12 + std::wcslen(row.suffix);
                if (length < std::size(row.path) && length < capacity)
                    read_bytes(reinterpret_cast<uintptr_t>(output), row.path, (length + 1) * sizeof(wchar_t));
                row.path[255] = 0;
            }
            row.ready.store(true);
        }
    }
    active.fetch_sub(1);
    return success;
}
#include "resource_trace_pins.h"
#include "resource_trace_pipeline.h"
static void start(uintptr_t base, const std::filesystem::path& directory) {
    if (!Enabled) return;
    std::ifstream input(directory / "resource-trace-seconds.txt");
    unsigned seconds = 0; std::string extra;
    if ((input >> seconds) && !(input >> extra) && seconds >= 5 && seconds <= 600) duration_ms = seconds * 1000;
    status = "entry-rejected";
    target = base + 0x15b9340;
    if (!executable(target, base) || !read_bytes(target, original_bytes, 32) ||
        std::memcmp(original_bytes, FormatBytes, 32)) { target = 0; return; }
    status = "hook-create-failed";
    if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&observe), reinterpret_cast<void**>(&original)) != MH_OK) return;
    status = "hook-enable-failed";
    recording.store(true);
    if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK) { recording.store(false); return; }
    restored = false; status = "recording";
    pipeline_start(base);
    hash_target = base + 0x145b710; hash_status = "entry-rejected";
    if (!executable(hash_target, base) || !read_bytes(hash_target, hash_bytes, 32) || std::memcmp(hash_bytes, HashBytes, 32)) return;
    hash_status = "hook-create-failed";
    if (MH_CreateHook(reinterpret_cast<void*>(hash_target), reinterpret_cast<void*>(&observe_hash), reinterpret_cast<void**>(&hash_original)) != MH_OK) return;
    hash_status = "hook-enable-failed";
    hash_enabled = MH_EnableHook(reinterpret_cast<void*>(hash_target)) == MH_OK;
    if (hash_enabled) hash_status = "recording";
    std::ofstream ready(directory / "resource-trace.ready.json",std::ios::binary);
    unsigned pipeline_enabled=0;for (const auto& hook:pipeline_hooks) if (hook.enabled) ++pipeline_enabled;
    ready << "{\"pathHook\":true,\"hashHook\":" << (hash_enabled ? "true":"false") << ",\"pipelineHooks\":" << pipeline_enabled << "}";
}
static void stop() {
    if (!Enabled || !target || restored) return;
    recording.store(false);
    const auto disabled = MH_DisableHook(reinterpret_cast<void*>(target));
    const auto hash_disabled = hash_enabled ? MH_DisableHook(reinterpret_cast<void*>(hash_target)) : MH_OK;
    const bool pipeline_disabled=pipeline_disable();
    const auto deadline = GetTickCount64() + 1000;
    while (active.load() && GetTickCount64() < deadline) Sleep(1);
    uint8_t bytes[32]{};
    restored = (disabled == MH_OK || disabled == MH_ERROR_DISABLED) &&
        read_bytes(target, bytes, 32) && !std::memcmp(bytes, original_bytes, 32);
    if (hash_enabled) {
        uint8_t after[32]{};
        const bool hash_restored = (hash_disabled == MH_OK || hash_disabled == MH_ERROR_DISABLED) &&
            read_bytes(hash_target, after, 32) && !std::memcmp(after, hash_bytes, 32);
        restored = restored && hash_restored;
        hash_status = hash_restored ? (hash_calls.load() ? "hash-loads-observed" : "no-hash-loads-observed") : "hook-restore-failed";
    }
    restored=restored && pipeline_disabled && pipeline_verify();
    status = restored ? (calls.load() ? "trace-observed" : "no-path-calls-observed") : "hook-restore-failed";
}
static void report(std::ostream& out) {
    out << ",\n  \"resourceTrace\":{\"enabled\":" << (Enabled ? "true" : "false") << ",\"status\":\"" << status
        << "\",\"calls\":" << calls.load() << ",\"entryRestored\":" << (restored ? "true" : "false")
        << ",\"activeCallbacks\":" << active.load() << ",\"records\":[";
    bool first = true;
    for (const auto& row : records) {
        if (!row.ready.load()) continue;
        out << (first ? "" : ",") << "{\"thread\":" << row.thread << ",\"index\":" << row.index << ",\"category\":" << row.category
            << ",\"success\":" << (row.success ? "true" : "false") << ",\"stringsValid\":" << (row.strings_valid ? "true" : "false") << ",\"root\":";
        hierarchy::json_string(out, row.root); out << ",\"suffix\":"; hierarchy::json_string(out, row.suffix);
        out << ",\"path\":"; hierarchy::json_string(out, row.path); out << '}'; first = false;
    }
    out << "],\"hashStatus\":\"" << hash_status << "\",\"hashCalls\":" << hash_calls.load() << ",\"hashRecords\":[";
    first = true;
    for (const auto& row : hash_records) {
        if (!row.ready.load()) continue;
        out << (first ? "" : ",") << "{\"thread\":" << row.thread << ",\"hash\":" << row.hash
            << ",\"returnedObject\":" << (row.returned_object ? "true" : "false")
            << ",\"namesRead\":" << (row.names_read ? "true" : "false") << ",\"bundle\":";
        hierarchy::json_string(out, row.bundle); out << ",\"asset\":"; hierarchy::json_string(out, row.asset);
        out << '}'; first = false;
    }
    out << ']'; pipeline_report(out); out << '}';
}
}
