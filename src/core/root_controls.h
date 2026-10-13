#pragma once
#include "leg_ik.h"
#include "generated/fk_profile.h"
namespace gimmd::root_controls {
namespace vmd=BetterEndfieldNext::Vmd;
// Center orientation is composed into the pelvis. Without source PMX pivots,
// pitch/roll use a pelvis pivot approximation; master/Groove rotation is refused.
inline bool supported(unsigned index,vmd::Quaternion q) {
    return index==1 ? true:
        std::abs(q.x)+std::abs(q.y)+std::abs(q.z)<=0.00001f;
}
inline bool sample(const std::vector<vmd::BoneKey> (&tracks)[3],double frame,vmd::Vec3* position,vmd::Quaternion* center) {
    vmd::BoneSample value[3]{};
    for (unsigned i=0;i<3;++i) if (!tracks[i].empty()&&!vmd::SampleBone(tracks[i],frame,value[i])) return false;
    // Master translation + Center translation + Center-rotated child Groove translation.
    *center=value[1].rotation;
    *position=leg_ik::add(leg_ik::add(value[0].position,value[1].position),
        leg_ik::vector(leg_ik::rotation(*center),value[2].position));
    return leg_ik::finite(*position);
}
inline vmd::Quaternion pelvis(vmd::Quaternion center,vmd::Quaternion lower_body) {
    return rotation_preview::multiply(center,lower_body);
}
}
