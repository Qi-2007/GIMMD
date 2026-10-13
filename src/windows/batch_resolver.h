#pragma once
// Callback-local cache only. Each unique path prefix is fully validated once,
// before the first pose write. No engine pointer survives this batch.
struct ResolutionBatch {
    struct Node { uintptr_t identity = 0, parent = 0; int child = -1; void* object = nullptr; bool active = true; };
    Node nodes[256]{};
    unsigned count = 0, reused = 0;
    void* root = nullptr;
    bool initialized = false, active = true;
};
static void* resolve_batch_steps(ResolutionBatch* batch, bool* active, const Step* steps, unsigned length) {
    __try {
        if (!batch->initialized) {
            batch->initialized = true;
            void* root_object = find_object(find_string("/EntityRoot/AvatarRoot")); uintptr_t native = 0;
            if (!root_object || !read_bytes(reinterpret_cast<uintptr_t>(root_object)+16,&native,sizeof(native)) || !native ||
                object_transform(&batch->root,reinterpret_cast<void*>(native)) != &batch->root) return nullptr;
            const int children = child_count(batch->root);
            if (children < 0 || children > static_cast<int>(MaxChildren)) { batch->root = nullptr; return nullptr; }
            unsigned actors = 0;
            for (int i = 0; i < children; ++i) {
                void* child = get_child(batch->root,i); wchar_t name[MaxName]{};
                if (!child || !string_read(name_a(child),name)) { batch->root = nullptr; return nullptr; }
                if (!std::wcsncmp(name,L"Avatar_",7)) {
                    void* owner = component_game_object(child);
                    if (owner && get_active(owner)) ++actors;
                }
            }
            batch->active = actors == 1;
        }
        if (!batch->root || !length) return nullptr;
        void* current = batch->root; bool path_active = batch->active;
        for (unsigned i = 0; i < length; ++i) {
            const auto& step = steps[i]; const uintptr_t expected_parent = i ? steps[i-1].identity : 0;
            ResolutionBatch::Node* found = nullptr;
            for (unsigned n = 0; n < batch->count; ++n) if (batch->nodes[n].identity == step.identity) { found = &batch->nodes[n]; break; }
            if (found) {
                if (found->parent != expected_parent || found->child != step.child) return nullptr;
                current = found->object; path_active = path_active && found->active; ++batch->reused; continue;
            }
            void* parent = current;
            if (i) {
                const int children = child_count(parent);
                if (children < 0 || children > static_cast<int>(MaxChildren) || step.child < 0 || step.child >= children) return nullptr;
                current = get_child(parent,step.child);
            }
            wchar_t name[MaxName]{}; uintptr_t identity = 0;
            if (!current || !read_bytes(reinterpret_cast<uintptr_t>(current)+16,&identity,sizeof(identity)) || identity != step.identity ||
                !string_read(name_a(current),name) || std::wcscmp(name,step.name) || (i && !same_object(get_parent(current),parent))) return nullptr;
            void* owner = component_game_object(current);
            const bool node_active = owner && get_active(owner); path_active = path_active && node_active;
            if (batch->count >= std::size(batch->nodes)) return nullptr;
            batch->nodes[batch->count++] = {identity,expected_parent,step.child,current,node_active};
        }
        *active = *active && path_active; return current;
    } __except (EXCEPTION_EXECUTE_HANDLER) { result.exception = GetExceptionCode(); return nullptr; }
}
