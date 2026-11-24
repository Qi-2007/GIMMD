#pragma once
#include <array>
#include "body_collision.h"
namespace gimmd::cloth_surface {
namespace bc=body_collision;
struct Contact {bc::Vec point{},axis{};float weights[3]{1,0,0};};
inline Contact nearest_point(const bc::Vec (&triangle)[3],bc::Vec point) {
    const auto ab=bc::sub(triangle[1],triangle[0]),ac=bc::sub(triangle[2],triangle[0]),ap=bc::sub(point,triangle[0]);
    const float aa=bc::dot(ab,ab),bb=bc::dot(ac,ac),abac=bc::dot(ab,ac),denominator=aa*bb-abac*abac;
    if(denominator>1e-12f){const float apab=bc::dot(ap,ab),apac=bc::dot(ap,ac);
        const float v=(bb*apab-abac*apac)/denominator,w=(aa*apac-abac*apab)/denominator;
        if(v>=0&&w>=0&&v+w<=1)return {bc::add(triangle[0],bc::add(bc::scale(ab,v),bc::scale(ac,w))),point,{1-v-w,v,w}};}
    Contact result{};float best=1e30f;
    for(unsigned i=0;i<3;++i){const unsigned j=(i+1)%3;const auto edge=bc::sub(triangle[j],triangle[i]);const float squared=bc::dot(edge,edge);
        const float t=squared>1e-12f?std::clamp(bc::dot(bc::sub(point,triangle[i]),edge)/squared,0.0f,1.0f):0;
        const auto candidate=bc::add(triangle[i],bc::scale(edge,t)),delta=bc::sub(candidate,point);const float distance=bc::dot(delta,delta);
        if(distance<best){best=distance;result={candidate,point,{0,0,0}};result.weights[i]=1-t;result.weights[j]=t;}}
    return result;
}
inline Contact nearest_capsule(const bc::Vec (&triangle)[3],const bc::Capsule& capsule) {
    auto result=nearest_point(triangle,capsule.a);float best=bc::length(bc::sub(result.point,result.axis));
    const auto consider=[&](const Contact& candidate){const float distance=bc::length(bc::sub(candidate.point,candidate.axis));if(distance<best){best=distance;result=candidate;}};
    consider(nearest_point(triangle,capsule.b));
    const auto normal=bc::cross(bc::sub(triangle[1],triangle[0]),bc::sub(triangle[2],triangle[0]));
    const auto axis=bc::sub(capsule.b,capsule.a);const float denominator=bc::dot(axis,normal);
    if(std::abs(denominator)>1e-10f){const float t=bc::dot(bc::sub(triangle[0],capsule.a),normal)/denominator;
        if(t>=0&&t<=1)consider(nearest_point(triangle,bc::add(capsule.a,bc::scale(axis,t))));}
    for(unsigned i=0;i<3;++i){const unsigned j=(i+1)%3;const auto pair=bc::closest_segment(triangle[i],triangle[j],capsule);
        Contact candidate{pair.point,pair.axis,{0,0,0}};candidate.weights[i]=1-pair.fraction;candidate.weights[j]=pair.fraction;consider(candidate);}
    return result;
}
// Two triangles approximate the fabric between the front and side chains.
// Resolve along the outward normal of the extended thigh, so a sliding
// contact does not push the dress all the way along the leg to the knee.
inline float side_depth(const bc::Vec (&panel)[4],const bc::Capsule& thigh,bc::Vec translation={}) {
    float depth=0;
    for(const auto indices:{std::array<unsigned,3>{0,1,2},std::array<unsigned,3>{2,1,3}}){
        bc::Vec triangle[3];
        for(unsigned i=0;i<3;++i)triangle[i]=bc::add(panel[indices[i]],translation);
        const auto hit=nearest_capsule(triangle,thigh);
        depth=std::max(depth,thigh.radius-bc::length(bc::sub(hit.point,hit.axis)));
    }
    return depth;
}
inline bc::Vec side_offset(const bc::Vec (&panel)[4],const bc::Capsule& thigh,bc::Vec outward,bc::Vec up,float limit) {
    const auto axis=bc::sub(thigh.b,thigh.a);
    const float length=bc::length(axis);
    if(length<.001f||limit<=0)return {};
    const auto unit=bc::scale(axis,1/length);
    const float extension=bc::dot(unit,outward);
    const float activation=std::clamp((extension-.18f)/.32f,0.f,1.f);
    if(activation<=0||side_depth(panel,thigh)<=0)return {};
    // Bias toward hip-up as the extended leg becomes horizontal.
    const auto preferred=bc::add(outward,bc::scale(up,std::clamp((extension-.65f)/.35f,0.f,1.f)));
    auto push=bc::sub(preferred,bc::scale(unit,bc::dot(preferred,unit)));
    const float span=bc::length(push);
    if(span<.001f)return {};
    push=bc::scale(push,1/span);
    float low=0,high=limit;
    for(unsigned i=0;i<9;++i){
        const float mid=(low+high)*.5f;
        if(side_depth(panel,thigh,bc::scale(push,mid))>0)low=mid;
        else high=mid;
    }
    return bc::scale(push,high*activation);
}
inline bool project(const bc::Capsule& capsule,float radius,bc::Vec (&points)[3],bc::Vec (&previous)[3],const bool (&movable)[3],bc::Vec outward) {
    const auto contact=nearest_capsule(points,capsule);float denominator=0;bc::Vec old{};
    for(unsigned i=0;i<3;++i){old=bc::add(old,bc::scale(previous[i],contact.weights[i]));if(movable[i])denominator+=contact.weights[i]*contact.weights[i];}
    // Contacts almost entirely on a pinned waist cannot be resolved by a huge
    // displacement of the remaining tip; leave those to the upper-panel solve.
    if(denominator<.04f)return false;
    auto point=contact.point;const auto old_before=old;
    if(!bc::project_outward(capsule,radius,point,old,outward))return false;
    for(unsigned i=0;i<3;++i)if(movable[i]){const float weight=contact.weights[i]/denominator;
        points[i]=bc::add(points[i],bc::scale(bc::sub(point,contact.point),weight));
        previous[i]=bc::add(previous[i],bc::scale(bc::sub(old,old_before),weight));}
    return true;
}
}
