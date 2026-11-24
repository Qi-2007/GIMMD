#pragma once
#include "leg_ik.h"
namespace gimmd::camera_front {
inline bool pose(BetterEndfieldNext::Vmd::Vec3 feet,BetterEndfieldNext::Vmd::Quaternion facing,
    float height,BetterEndfieldNext::Vmd::Vec3* position,BetterEndfieldNext::Vmd::Quaternion* rotation) {
    namespace li=leg_ik;
    if(!position||!rotation||!li::finite(feet)||!std::isfinite(height)||height<.4f||height>4)return false;
    auto forward=li::vector(li::rotation(facing),{0,0,1});forward.y=0;
    const float length=li::length(forward);if(!std::isfinite(length)||length<.001f)return false;
    forward=li::scale(forward,1/length);
    const float distance=std::clamp(height*1.5f+.4f,1.8f,6.0f);
    *position={feet.x+forward.x*distance,feet.y+height*.55f,feet.z+forward.z*distance};
    const float yaw=std::atan2(-forward.x,-forward.z)*.5f,pitch=std::atan2(height*.05f,distance)*.5f;
    *rotation=rotation_preview::multiply({0,std::sin(yaw),0,std::cos(yaw)},{std::sin(pitch),0,0,std::cos(pitch)});
    return true;
}
}
