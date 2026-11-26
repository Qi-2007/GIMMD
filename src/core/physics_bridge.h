#pragma once
#include <cstdint>
namespace gimmd::physics_bridge {
inline bool (*prepare)()=nullptr;
inline bool (*tick)(void* model,double seconds,uint64_t epoch,bool playing,void* const* body,unsigned count)=nullptr;
inline bool (*restore)(void* model)=nullptr;
inline bool (*release_ready)()=nullptr;
inline bool (*hold_settled)()=nullptr;
}
