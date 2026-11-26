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
// Free shoulder ribbons retain the attachment's shape near the seam and
// progressively yield to world gravity toward the tip.
inline bc::Vec predict_drape(bc::Vec current,bc::Vec previous,bc::Vec anchor,
                                      bc::Vec native_direction,float span,unsigned link,float step) {
    if(!link||span<1e-6f)return anchor;
    const float shape_stiffness=58.f/float(link);
    const auto target=bc::add(anchor,native_direction);
    const auto acceleration=bc::add(bc::scale(bc::sub(target,current),shape_stiffness),{0,-9.81f,0});
    auto point=bc::add(current,bc::add(bc::scale(bc::sub(current,previous),std::exp(-8.f*step)),
                                      bc::scale(acceleration,step*step)));
    bc::length_constraint(anchor,span,point,native_direction);
    return point;
}
// A sewn seam or belt mount can swivel but keeps its original attachment.
// Preserve the fitted clearance where a coarse body capsule overlaps at rest.
struct AttachmentSwing {
    bc::Vec current{},previous{};
    bool initialized=false;
    template<unsigned N>
    bc::Vec advance(bc::Vec anchor,bc::Vec rest,float max_angle,float dt,
                    const bc::Capsule (&body)[N],const float (&radii)[N]){
        const float span=bc::length(rest);
        if(!initialized){previous=current=bc::add(anchor,rest);initialized=true;}
        auto point=predict_drape(current,previous,anchor,rest,span,1,dt);
        auto history=current;
        for(unsigned i=0;i<N;++i)if(radii[i]>0)
            bc::project(body[i],radii[i],point,history,rest);
        bc::length_constraint(anchor,span,point,rest);
        auto direction=bc::sub(point,anchor);
        bc::limit_direction(rest,direction,max_angle);
        current=bc::add(anchor,direction);
        previous=bc::add(history,bc::sub(current,point));
        return direction;
    }
};
// Share the hem correction across both links in proportion to their lever
// arms. Leave half for the lower joint so it can still clear a bent knee.
struct HemBend {float rise=0,outward=0;}; // relative displacement per unit of lower-link length
template<class Particle>
inline int hem_segment(const Particle* particles,unsigned count,unsigned root_path) {
    int root=-1,child=-1;
    for(unsigned i=0;i<count;++i){
        const auto& p=particles[i];
        if(!p.real||!p.skirt||p.parent_global>=0||p.path!=root_path)continue;
        if(root>=0)return -1;
        root=int(i);
    }
    if(root<0)return -1;
    for(unsigned i=0;i<count;++i)if(particles[i].parent_global==root){
        if(child>=0||!particles[i].skirt)return -1;
        child=int(i);
    }
    return child;
}
inline HemBend hem_bend(bc::Vec original,bc::Vec solved,bc::Vec up,bc::Vec outward,float length) {
    if(length<.001f)return {};
    const auto change=bc::scale(bc::sub(solved,original),1/length);
    return {std::clamp(bc::dot(change,up),0.f,1.f),std::clamp(bc::dot(change,outward),0.f,1.f)};
}
inline HemBend share_hem_bend(HemBend own,HemBend neighbor) {
    return {std::max(own.rise,.30f*neighbor.rise),std::max(own.outward,.30f*neighbor.outward)};
}
inline bc::Vec follow_hem(bc::Vec direction,bc::Vec up,bc::Vec outward,HemBend bend,float lower_length) {
    const float upper_length=bc::length(direction);
    if(lower_length<.001f||upper_length<1e-6f)return direction;
    const float lever=.5f*lower_length*upper_length/(upper_length+lower_length);
    return bc::add(direction,bc::scale(bc::add(bc::scale(up,bend.rise),bc::scale(outward,bend.outward)),lever));
}
// Rotate the upper panel into a lateral shift and translate its attachment
// by the residual. Rotation plus translation preserves the requested joint
// position, even when the angular limit prevents sharing the entire shift.
inline bc::Vec share_panel_shift(bc::Vec reference,bc::Vec& direction,bc::Vec shift,float limit) {
    const float length=bc::length(direction);
    if(length<1e-6f||bc::length(shift)<1e-6f)return shift;
    auto tilted=bc::add(direction,bc::scale(shift,.6f));
    const float span=bc::length(tilted);
    if(span<1e-6f)return shift;
    tilted=bc::scale(tilted,length/span);
    bc::limit_direction(reference,tilted,limit);
    const auto residual=bc::sub(shift,bc::sub(tilted,direction));
    direction=tilted;
    return residual;
}
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
    // Search adjacent joints together. With independent updates a clear
    // crease can persist: unwinding either joint alone loses contact, even
    // when moving some of that turn to its neighbour preserves clearance.
    const auto score=[&](){
        const float worst=front_overlap(pose,angle,up,left,right);
        float cost=0;
        for(float value:angle)cost+=value*value;
        float mismatch=0;
        for(unsigned b:{0u,2u}){
            const auto relative=bc::sub(bc::scale(pose.axis[b],angle[b]),bc::scale(pose.axis[b+1],angle[b+1]));
            mismatch+=bc::dot(relative,relative);
        }
        return worst+.0005f*cost+.004f*mismatch;
    };
    float best=score();
    const auto bounded=[&](float start,float change){
        const float trial=std::clamp(start+change,-limit,limit);
        return start*trial<0?0.f:trial;
    };
    for(unsigned b:{0u,2u}){
        const float upper=angle[b],lower=angle[b+1];
        float chosen_upper=upper,chosen_lower=lower;
        for(float du:{0.f,-.035f,.035f})for(float dl:{0.f,-.035f,.035f}){
            if(du==0&&dl==0)continue;
            angle[b]=bounded(upper,du);angle[b+1]=bounded(lower,dl);
            const float candidate=score();
            if(candidate<best){chosen_upper=angle[b];chosen_lower=angle[b+1];best=candidate;}
        }
        angle[b]=chosen_upper;angle[b+1]=chosen_lower;
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
