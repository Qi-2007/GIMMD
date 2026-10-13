#pragma once
#include "leg_ik.h"
namespace gimmd::camera_pose {
using Vec=leg_ik::Vec;
using Quat=leg_ik::Quat;
struct Pose { Vec position{}; Quat rotation{}; float fov=30; bool orthographic=false; };
// EIEM camera convention: negate Euler angles, compose Y*X*Z, then Y-180 source basis.
inline bool evaluate(const BetterEndfieldNext::Vmd::CameraSample& sample,float units,Vec anchor,Quat anchor_rotation,Pose* out) {
    if (!out||!std::isfinite(units)||units<=0||!leg_ik::finite(sample.interest)||!leg_ik::finite(sample.euler)||
        !std::isfinite(sample.distance)||!std::isfinite(sample.fov)||!leg_ik::finite(anchor)) return false;
    const float x=-sample.euler.x/2,y=-sample.euler.y/2,z=-sample.euler.z/2;
    const auto source=rotation_preview::multiply(rotation_preview::multiply({0,std::sin(y),0,std::cos(y)},
        {std::sin(x),0,0,std::cos(x)}),{0,0,std::sin(z),std::cos(z)});
    const auto p=leg_ik::add(sample.interest,leg_ik::vector(leg_ik::rotation(source),{0,0,sample.distance}));
    const auto converted=Vec{-p.x*units,p.y*units,-p.z*units};
    out->position=leg_ik::add(anchor,leg_ik::vector(leg_ik::rotation(anchor_rotation),converted));
    out->rotation=rotation_preview::multiply(anchor_rotation,rotation_preview::multiply({0,1,0,0},source));
    out->fov=std::clamp(sample.fov,1.0f,179.0f); out->orthographic=sample.orthographic;
    return leg_ik::finite(out->position);
}
}
