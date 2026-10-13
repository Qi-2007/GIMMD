#pragma once
#include "vmd.h"
namespace gimmd::root_translation {
using Vec3 = BetterEndfieldNext::Vmd::Vec3;
using Quaternion = BetterEndfieldNext::Vmd::Quaternion;
inline Vec3 offset(Vec3 source, Vec3 reference, float units, Quaternion mesh_rotation, Vec3 mesh_scale) {
    // EIEM canonical source basis: Y 180 degrees, then mesh-to-model TRS.
    const Vec3 p{-(source.x-reference.x)*units*mesh_scale.x,
        (source.y-reference.y)*units*mesh_scale.y, -(source.z-reference.z)*units*mesh_scale.z};
    const auto q = BetterEndfieldNext::Vmd::Normalize(mesh_rotation);
    const Vec3 t{2*(q.y*p.z-q.z*p.y),2*(q.z*p.x-q.x*p.z),2*(q.x*p.y-q.y*p.x)};
    return {p.x+q.w*t.x+q.y*t.z-q.z*t.y,p.y+q.w*t.y+q.z*t.x-q.x*t.z,p.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
}
