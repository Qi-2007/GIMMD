#pragma once
#include "vmd.h"
#include "generated/motion_profile.h"
namespace gimmd::rotation_preview {
namespace vmd = BetterEndfieldNext::Vmd;
inline vmd::Quaternion multiply(vmd::Quaternion a, vmd::Quaternion b) {
    return vmd::Normalize({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x, a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
// Relative to the source's frame zero, not a claim of a calibrated rest pose.
inline vmd::Quaternion delta(vmd::Quaternion source_zero, vmd::Quaternion sample, bool* limited = nullptr) {
    source_zero = vmd::Normalize(source_zero);
    auto q = multiply({-source_zero.x,-source_zero.y,-source_zero.z,source_zero.w}, sample);
    if (q.w < 0) q = {-q.x,-q.y,-q.z,-q.w};
    const double angle = 2 * std::acos(std::clamp(double(q.w), -1.0, 1.0));
    constexpr double MaxAngle = 5.0 * 3.14159265358979323846 / 180.0;
    const bool clamp = angle > MaxAngle;
    if (limited) *limited = clamp;
    return clamp ? vmd::Slerp({}, q, MaxAngle / angle) : q;
}
}
