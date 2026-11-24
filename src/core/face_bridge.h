#pragma once
namespace gimmd::face_bridge {
using Motion=BetterEndfieldNext::Vmd::Motion;
inline bool (*prepare)(bool,const Motion&)=nullptr;
inline bool (*tick)(bool,void*,double)=nullptr;
inline bool (*restore)(bool,void*)=nullptr;
inline unsigned (*matched)(bool)=nullptr;
}
