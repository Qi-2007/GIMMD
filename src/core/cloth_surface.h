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
// Find the smallest shared outward shift that clears a short panel.
// Each corner follows its own attachment around the circumference of the skirt.
inline float short_panel_shift(const bc::Vec (&base)[3],const bc::Vec (&outward)[3],
                               const bc::Capsule& capsule,float radius,float limit) {
    if(radius<=0||limit<=0)return 0;
    const auto clearance=[&](float shift){
        bc::Vec triangle[3];
        for(unsigned i=0;i<3;++i)triangle[i]=bc::add(base[i],bc::scale(outward[i],shift));
        const auto hit=nearest_capsule(triangle,capsule);
        return bc::length(bc::sub(hit.point,hit.axis))-radius;
    };
    const float start=clearance(0);
    if(start>=-.0003f)return 0;
    const float end=clearance(limit);
    if(end<=start)return 0;
    if(end<0)return limit;
    float low=0,high=limit;
    for(unsigned i=0;i<7;++i){
        const float mid=(low+high)*.5f;
        if(clearance(mid)<0)low=mid;
        else high=mid;
    }
    return high;
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
// Two palm lanes cover the width of the hand without a spherical wrist.
// Finger capsules end at the last tracked joint; their rounded caps cover
// the distal end without extending a straight finger through a closed fist.
using CapeArm=std::array<bc::Capsule,9>;
// Neighboring shoulder bones share a single piece of fabric. Its center can
// intersect the arm even when both edge strips, including their tips, clear it.
inline bc::Vec shoulder_cover_shift(const bc::Vec (&panel)[4],const bc::Capsule& upper,bc::Vec up){
    const auto axis=bc::sub(upper.b,upper.a);
    const float span=bc::length(axis);
    if(upper.radius<=0||span<.001f)return {};
    const auto unit=bc::scale(axis,1/span);
    const float elevation=bc::dot(unit,up);
    const float active=std::clamp((elevation+.95f)/.22f,0.f,1.f);
    if(active<=0)return {};
    const auto push=bc::sub(up,bc::scale(unit,elevation));
    const float size=bc::length(push);
    if(size<.001f)return {};
    const auto outward=bc::scale(push,1/size);
    const float overlap=side_depth(panel,upper);
    if(overlap<.0003f)return {};
    float low=0,high=1.8f*upper.radius;
    const float at_limit=side_depth(panel,upper,bc::scale(outward,high));
    if(at_limit>=overlap)return {};
    if(at_limit<.0001f)for(unsigned i=0;i<8;++i){
        const float mid=(low+high)*.5f;
        if(side_depth(panel,upper,bc::scale(outward,mid))>.0001f)low=mid;else high=mid;
    }
    return bc::scale(outward,active*high);
}
inline CapeArm cape_arm(bc::Capsule upper,bc::Capsule forearm,bc::Vec wrist,
                        const bc::Vec (&knuckles)[5],const bc::Vec (&distal)[5]){
    CapeArm arm{};
    upper.a=bc::add(upper.a,bc::scale(bc::sub(upper.b,upper.a),.15f));
    arm[0]=upper;arm[1]=forearm;
    const auto across=bc::sub(knuckles[4],knuckles[1]);
    const float width=bc::length(across);
    for(unsigned i=0;i<2;++i){
        const float sign=i?1.f:-1.f;
        const auto middle=bc::scale(bc::add(knuckles[1],knuckles[4]),.5f);
        arm[2+i]={bc::add(wrist,bc::scale(across,sign*.13f)),
                  bc::add(middle,bc::scale(across,sign*.26f)),width*.29f};
    }
    for(unsigned i=0;i<5;++i)arm[4+i]={knuckles[i],distal[i],width*.145f};
    return arm;
}
inline float cape_panel_depth(bc::Vec anchor,bc::Vec direction,bc::Vec normal,float width,const bc::Capsule& limb){
    const auto start=bc::add(anchor,bc::scale(direction,.2f)),end=bc::add(anchor,direction);
    const auto closest=bc::closest_segment(start,end,limb);
    if(bc::length(bc::sub(closest.point,closest.axis))>=limb.radius+width)return 0;
    auto side=bc::cross(normal,direction);
    if(bc::length(side)<1e-6f)side=bc::cross(std::abs(direction.y)<.8f*bc::length(direction)?bc::Vec{0,1,0}:bc::Vec{1,0,0},direction);
    side=bc::scale(side,width/std::max(1e-6f,bc::length(side)));
    const auto seam=bc::scale(side,.65f);
    const bc::Vec corners[]={bc::sub(start,seam),bc::sub(end,side),bc::add(start,seam),bc::add(end,side)};
    return side_depth(corners,limb);
}
inline bc::Vec fit_cape_panel(bc::Vec anchor,bc::Vec reference,bc::Vec direction,bc::Vec normal,float width,
                              float angle_limit,const CapeArm (&arms)[2]){
    const float span=bc::length(direction);
    if(span<.001f||width<=0)return direction;
    const bc::Capsule* nearby[18]{};unsigned count=0;
    for(const auto& arm:arms)for(const auto& limb:arm)
        if(limb.radius>0&&bc::distance(limb,anchor)<=span+width+limb.radius)nearby[count++]=&limb;
    const auto depth=[&](bc::Vec trial){
        float worst=0;
        for(unsigned i=0;i<count;++i)worst=std::max(worst,cape_panel_depth(anchor,trial,normal,width,*nearby[i]));
        return worst;
    };
    float best=depth(direction);
    if(best<.0005f)return direction;
    const auto unit=bc::scale(direction,1/span);
    auto tangent=bc::sub(normal,bc::scale(unit,bc::dot(unit,normal)));
    if(bc::length(tangent)<.001f)tangent=bc::cross(unit,std::abs(unit.y)<.8f?bc::Vec{0,1,0}:bc::Vec{1,0,0});
    tangent=bc::scale(tangent,1/bc::length(tangent));
    const auto side=bc::cross(unit,tangent);
    auto result=direction;
    for(float angle:{.10f,.24f,.45f,.75f}){
        for(const auto axis:{tangent,bc::scale(tangent,-1),side,bc::scale(side,-1)}){
            auto trial=bc::scale(bc::add(bc::scale(unit,std::cos(angle)),bc::scale(axis,std::sin(angle))),span);
            bc::limit_direction(reference,trial,angle_limit);
            const float overlap=depth(trial);
            if(overlap+.0001f<best){result=trial;best=overlap;}
            if(best<.0005f)return result;
        }
    }
    return result;
}
// Check the mobile part of a front strip against both sides of the bent
// knee. The preceding link controls the attachment. Turn toward hip-up
// while preserving link length and the horizontal direction of the pleat.
inline bc::Vec fit_over_knee(bc::Vec anchor,bc::Vec direction,bc::Vec up,bc::Vec right,bc::Vec forward,
                            float half_width,const bc::Capsule& thigh,const bc::Capsule& shin,float padding){
    const auto axis=bc::sub(thigh.b,thigh.a);
    const float leg=bc::length(axis),span=bc::length(direction);
    if(leg<.001f||span<.001f||half_width<=0||bc::dot(axis,up)<=-.3f*leg||bc::dot(axis,forward)<.35f*leg)return direction;
    const auto end=bc::add(anchor,direction);
    const auto near_link=[&](const bc::Capsule& body){
        const auto hit=bc::closest_segment(anchor,end,body);
        return bc::length(bc::sub(hit.point,hit.axis))<=body.radius+padding+half_width;
    };
    if(!near_link(thigh)&&!near_link(shin))return direction;
    const auto edge=bc::scale(right,half_width);
    const auto depth=[&](bc::Vec v){
        const auto middle=bc::add(anchor,bc::scale(v,.35f)),tip=bc::add(anchor,v);
        const bc::Vec strip[]={bc::sub(middle,edge),bc::sub(tip,edge),bc::add(middle,edge),bc::add(tip,edge)};
        return std::max(side_depth(strip,{thigh.a,thigh.b,thigh.radius+padding}),
                        side_depth(strip,{shin.a,shin.b,shin.radius+padding}));
    };
    float best=depth(direction);
    if(best<.001f)return direction;
    const float height=bc::dot(up,direction);
    const auto lateral=bc::sub(direction,bc::scale(up,height));
    const float width=bc::length(lateral);
    if(width<1e-6f)return direction;
    const float start=std::atan2(height,width);
    const float sweep=std::min(1.22173f,1.5707963f-start);
    bc::Vec result=direction;
    for(unsigned i=1;i<=12;++i){
        const float angle=start+sweep*float(i)/12;
        const auto trial=bc::add(bc::scale(lateral,span*std::cos(angle)/width),bc::scale(up,span*std::sin(angle)));
        const float overlap=depth(trial);
        if(overlap+.0001f<best){best=overlap;result=trial;}
        if(best<.001f)break;
    }
    return result;
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
