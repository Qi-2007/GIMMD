#pragma once
#include "rotation_preview.h"
namespace gimmd::fk_retarget {
struct Binding { BetterEndfieldNext::Vmd::Quaternion rest; float axes[9]; };
// MMD UpperBody and LowerBody are siblings under Center/waist, whereas the
// target spine inherits the pelvis. Remove the inherited lower-body motion.
inline BetterEndfieldNext::Vmd::Quaternion compensate_parent(
    BetterEndfieldNext::Vmd::Quaternion parent_reference,
    BetterEndfieldNext::Vmd::Quaternion parent_current,
    BetterEndfieldNext::Vmd::Quaternion independent_child) {
    parent_current=BetterEndfieldNext::Vmd::Normalize(parent_current);
    return rotation_preview::multiply(rotation_preview::multiply(
        {-parent_current.x,-parent_current.y,-parent_current.z,parent_current.w},parent_reference),independent_child);
}
inline BetterEndfieldNext::Vmd::Quaternion evaluate(const Binding& binding, BetterEndfieldNext::Vmd::Quaternion source) {
    source = BetterEndfieldNext::Vmd::Normalize(source);
    const auto& m = binding.axes;
    const BetterEndfieldNext::Vmd::Quaternion delta{m[0]*source.x+m[1]*source.y+m[2]*source.z,
        m[3]*source.x+m[4]*source.y+m[5]*source.z, m[6]*source.x+m[7]*source.y+m[8]*source.z, source.w};
    // Delta is expressed in the target parent's reference coordinates.
    return rotation_preview::multiply(delta,binding.rest);
}
}
