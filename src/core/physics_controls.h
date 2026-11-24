#pragma once
#include <cmath>
#include <istream>
#include <string>
namespace gimmd::physics_controls {
struct Settings {
    float front_angle=24,rear_angle=10,front_padding_cm=1.5f,rear_padding_cm=.4f;
    float front_response_ms=60,rear_response_ms=160,support_percent=95;
};
inline bool valid(const Settings& s) {
    const float values[]={s.front_angle,s.rear_angle,s.front_padding_cm,s.rear_padding_cm,s.front_response_ms,s.rear_response_ms,s.support_percent};
    for(float v:values)if(!std::isfinite(v))return false;
    return s.front_angle>=0&&s.front_angle<=80&&s.rear_angle>=0&&s.rear_angle<=45&&
        s.front_padding_cm>=0&&s.front_padding_cm<=5&&s.rear_padding_cm>=0&&s.rear_padding_cm<=5&&
        s.front_response_ms>=20&&s.front_response_ms<=1000&&s.rear_response_ms>=20&&s.rear_response_ms<=1000&&
        s.support_percent>=0&&s.support_percent<=100;
}
inline bool read(std::istream& in,Settings& output) {
    Settings next;std::string version,extra;
    if(!(in>>version)||version!="GIMMD_PHYSICS_V1"||
        !(in>>next.front_angle>>next.rear_angle>>next.front_padding_cm>>next.rear_padding_cm>>next.front_response_ms>>next.rear_response_ms>>next.support_percent)||
        !valid(next)||(in>>extra))return false;
    output=next;return true;
}
}
