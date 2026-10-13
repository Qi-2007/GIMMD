#pragma once
#include <sstream>
#include <string>
#include <cmath>
#include <cstdint>
namespace gimmd {
struct CameraControl {
    uint64_t sequence=0;
    float right=0,up=0,forward=0,yaw=0,pitch=0,roll=0,fov=0;
};
inline bool parse_camera_control(const std::string& text,CameraControl* result) {
    if (!result||text.size()>256) return false;
    CameraControl c; std::string extra; std::istringstream in(text);
    if (!(in>>c.sequence>>c.right>>c.up>>c.forward>>c.yaw>>c.pitch>>c.roll>>c.fov)||
        !c.sequence||c.sequence>0xffffffff||in>>extra) return false;
    for (auto value:{c.right,c.up,c.forward}) if (!std::isfinite(value)||std::abs(value)>100) return false;
    for (auto value:{c.yaw,c.pitch,c.roll}) if (!std::isfinite(value)||std::abs(value)>180) return false;
    if (!std::isfinite(c.fov)||(c.fov!=0&&(c.fov<1||c.fov>179))) return false;
    *result=c; return true;
}
}
