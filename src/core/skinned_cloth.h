#pragma once
#include "body_collision.h"
namespace gimmd::skinned_cloth {
namespace bc=body_collision;
// A contact on a nearly pinned panel can move the solver's history point far
// outside the chain. Limit its contribution to the next fixed-step prediction.
inline bc::Vec predict_tip(bc::Vec current,bc::Vec previous,bc::Vec target,float rest_length) {
    auto velocity=bc::scale(bc::sub(current,previous),.88f);
    const float speed=bc::length(velocity);
    const float limit=rest_length*.5f;
    if(speed>limit)velocity=bc::scale(velocity,limit/speed);
    const auto spring=bc::scale(bc::sub(target,current),.04f);
    return bc::add(bc::add(current,velocity),bc::add({0,-.0004f,0},spring));
}
struct Frame {bc::Vec axis[3]{{1,0,0},{0,1,0},{0,0,1}},position{};};
inline bc::Vec vector(const Frame& f,bc::Vec p){return bc::add(bc::add(bc::scale(f.axis[0],p.x),bc::scale(f.axis[1],p.y)),bc::scale(f.axis[2],p.z));}
inline bc::Vec point(const Frame& f,bc::Vec p){return bc::add(f.position,vector(f,p));}
inline bool inverse(const Frame& f,bc::Vec p,bc::Vec& local){
    const auto a=bc::cross(f.axis[1],f.axis[2]),b=bc::cross(f.axis[2],f.axis[0]),c=bc::cross(f.axis[0],f.axis[1]);
    const float determinant=bc::dot(f.axis[0],a);if(std::abs(determinant)<1e-8f)return false;
    p=bc::sub(p,f.position);local={bc::dot(p,a)/determinant,bc::dot(p,b)/determinant,bc::dot(p,c)/determinant};return bc::finite(local);
}
inline Frame aim(Frame frame,bc::Vec from,bc::Vec to,bc::Vec position){
    for(auto& axis:frame.axis)axis=bc::transport_direction(from,to,axis);frame.position=position;return frame;
}
// Rotation-only contact gradient. Exclude twist about a bone's child direction:
// the particle actuator cannot control that degree of freedom.
inline bc::Vec angular_gradient(bc::Vec offset,bc::Vec normal,bc::Vec child,float weight){
    const float size=bc::length(child);if(size<1e-6f)return {};
    child=bc::scale(child,1/size);auto gradient=bc::cross(offset,normal);
    return bc::scale(bc::sub(gradient,bc::scale(child,bc::dot(gradient,child))),weight);
}
}
