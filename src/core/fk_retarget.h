#pragma once
#include "rotation_preview.h"
namespace gimmd::fk_retarget {
struct Binding { BetterEndfieldNext::Vmd::Quaternion rest; float axes[9]; };
inline BetterEndfieldNext::Vmd::Quaternion evaluate(const Binding& binding, BetterEndfieldNext::Vmd::Quaternion source) {
    source = BetterEndfieldNext::Vmd::Normalize(source);
    const auto& m = binding.axes;
    const BetterEndfieldNext::Vmd::Quaternion delta{m[0]*source.x+m[1]*source.y+m[2]*source.z,
        m[3]*source.x+m[4]*source.y+m[5]*source.z, m[6]*source.x+m[7]*source.y+m[8]*source.z, source.w};
    // Delta is expressed in the target parent's reference coordinates.
    return rotation_preview::multiply(delta,binding.rest);
}
}
