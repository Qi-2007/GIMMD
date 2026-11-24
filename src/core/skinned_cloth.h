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
// Centerline samples from low hem to upper panel, in symmetric E21/E22
// pairs. The published dress has 40/40/10/10 at the hem and 20/20/30/30
// higher up. Each row sums to one across both sides.
inline constexpr float front_weights[3][4]={{.1f,.4f,.1f,.4f},{.23f,.27f,.23f,.27f},{.3f,.2f,.3f,.2f}};
inline bc::Vec axial_turn(bc::Vec offset,bc::Vec unit_axis,float angle){
    const float c=std::cos(angle),s=std::sin(angle);
    return bc::add(bc::add(bc::scale(offset,c),bc::scale(bc::cross(unit_axis,offset),s)),
                   bc::scale(unit_axis,bc::dot(unit_axis,offset)*(1-c)));
}
struct FrontPose {
    bc::Vec origin[4]{},axis[4]{},offset[3][4]{};
    bc::Vec sample(unsigned row,const float (&angle)[4])const {
        bc::Vec point{};
        for(unsigned b=0;b<4;++b)point=bc::add(point,bc::scale(
            bc::add(origin[b],axial_turn(offset[row][b],axis[b],angle[b])),front_weights[row][b]));
        return point;
    }
};
inline float front_overlap(const FrontPose& pose,const float (&angle)[4],bc::Vec up,
                           const bc::Capsule& left,const bc::Capsule& right,float remove_lift=0){
    float worst=0;
    for(unsigned row=0;row<3;++row){
        const auto p=bc::sub(pose.sample(row,angle),bc::scale(up,remove_lift));
        worst=std::max({worst,bc::inner_flap_lift(p,p,up,left),bc::inner_flap_lift(p,p,up,right)});
    }
    return worst;
}
inline void support_front(const FrontPose& pose,bc::Vec up,const bc::Capsule& left,const bc::Capsule& right,
                          float limit,float (&angle)[4]){
    // Bounded coordinate descent on the maximum residual across the seam.
    // Penalize rotation so a clear/standing pose returns to zero twist.
    const auto score=[&](){
        const float worst=front_overlap(pose,angle,up,left,right);
        float cost=0;
        for(float value:angle)cost+=value*value;
        return worst+.0005f*cost;
    };
    float best=score();
    for(unsigned b=0;b<4;++b){
        const float start=angle[b];float chosen=start;
        for(float change:{-.035f,.035f}){
            const float trial=std::clamp(start+change,-limit,limit);
            angle[b]=(start*trial<0)?0:trial;
            const float candidate=score();
            if(candidate<best){chosen=angle[b];best=candidate;}
        }
        angle[b]=chosen;
    }
}
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
