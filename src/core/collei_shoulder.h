#pragma once
#include "cloth_surface.h"
#include "skinned_cloth.h"
#include "leg_ik.h"

namespace gimmd::collei_shoulder {
namespace bc=body_collision;
namespace skin=skinned_cloth;
// Called once per 60 Hz physics step. Keep an established contact through
// two missed detections. Damp retraction and direction reversals,
// while letting a growing collision use the original response and speed cap.
struct ContactDamping {
    unsigned hold_steps=0;
    bc::Vec previous_change{};
    float response(bc::Vec current,bc::Vec target,float attack,float step_limit){
        const auto change=bc::sub(target,current);
        const float distance=bc::length(change);
        const bool touching=bc::dot(target,target)>1e-12f;
        if(touching)hold_steps=2;
        else if(hold_steps){--hold_steps;return 0;}
        const bool retracting=bc::dot(target,current)>0&&bc::dot(change,current)<0;
        const bool reversing=touching&&bc::dot(change,previous_change)<0;
        previous_change=change;
        const float gain=(!touching||retracting||reversing)?attack*.55f:attack;
        const float cap=reversing?step_limit*.55f:step_limit;
        return std::min(gain,cap/std::max(distance,.000001f));
    }
};
struct Vertex {
    bc::Vec flap,side;
    float flap_weight;
    bc::Vec side_front{};
    float side_front_weight=0;
};
// Right-side inverse bind coordinates. The left joint frames mirror local Z.
// Vertex order runs along the inner and outer rows of the ornamented edge.
// Three back corners also follow the front side joint (AmiceS A21).
inline constexpr Vertex trim[2][4]={
    {{{-.04981f,-.00729f,.04964f},{-.06415f,-.00904f,-.02758f},.2500f},
     {{-.11480f,.00202f,.06320f},{-.12949f,-.01519f,-.04125f},.5173f},
     {{-.06048f,.00259f,.01503f},{-.06077f,-.03346f,-.05590f},.7199f},
     {{-.12740f,.00603f,.02593f},{-.12674f,-.04554f,-.06645f},.6793f}},
    {{{-.06903f,-.00840f,-.05022f},{-.07496f,.01318f,.00451f},.2685f,{-.07134f,-.02227f,.05115f},.1149f},
     {{-.13109f,-.01112f,-.08282f},{-.14410f,.00955f,-.00676f},.3814f,{-.14104f,-.01665f,.05672f},.0825f},
     {{-.09270f,-.00858f,-.03671f},{-.09290f,.00903f,.02461f},.5235f,{-.08706f,-.04277f,.05983f},.0462f},
     {{-.15128f,-.00425f,-.04910f},{-.15243f,.00983f,.03226f},.5835f}}
};
// Three rows across the central scallops, from the sewn part to the outer
// edge. Influences here are side A21, side A31 and (at one point) front A21.
inline constexpr Vertex rim[6]={
    {{-.06644f,.00146f,-.01423f},{-.05628f,-.03270f,-.04456f},.9025f,{-.04576f,-.01250f,.06546f},.0475f},
    {{-.13305f,-.00259f,-.02739f},{-.11908f,-.05665f,-.05501f},.9634f},
    {{-.06793f,.00633f,-.00136f},{-.06009f,-.01943f,-.04347f},.8849f},
    {{-.13734f,.01183f,.00027f},{-.12817f,-.02653f,-.05632f},.8085f},
    {{-.06932f,.00578f,.02767f},{-.06721f,.00558f,-.03048f},.4750f},
    {{-.14027f,.00753f,.02948f},{-.13699f,-.00330f,-.04010f},.4007f}
};
template<unsigned N>
inline void sample(const Vertex (&vertices)[N],bool right,const skin::Frame& flap,const skin::Frame& side,const skin::Frame& side_front,
                   bc::Vec (&panel)[N]){
    for(unsigned i=0;i<N;++i){
        auto a=vertices[i].flap,b=vertices[i].side,c=vertices[i].side_front;
        if(!right){a.z=-a.z;b.z=-b.z;c.z=-c.z;}
        const float w=vertices[i].flap_weight,v=vertices[i].side_front_weight;
        panel[i]=bc::add(bc::scale(skin::point(flap,a),w),bc::scale(skin::point(side,b),1-w-v));
        if(v>0)panel[i]=bc::add(panel[i],bc::scale(skin::point(side_front,c),v));
    }
}
// Upper-arm mesh measures about 4.1 cm from the axis in the middle
// of the 21.2 cm arm. Restrict the wider profile to that part of the cuff.
inline bc::Capsule cuff(const bc::Capsule& arm){
    const auto axis=bc::sub(arm.b,arm.a);
    return {bc::add(arm.a,bc::scale(axis,.26f)),bc::add(arm.a,bc::scale(axis,.70f)),arm.radius*1.21f};
}
inline float patch_depth(const bc::Vec (&panel)[4],const bc::Capsule& arm){
    return std::max(cloth_surface::side_depth(panel,arm),cloth_surface::side_depth(panel,cuff(arm)));
}
// The hanging front piece is skinned to A21 and its child A31. Both
// follow an A21 rotation. Points are in left A21 bind space, in three rows.
inline constexpr bc::Vec front_piece_points[]={{-.046629f,.006452f,.020334f},{-.145088f,.009669f,.012768f},
                               {-.063884f,.011853f,.031012f},{-.143895f,.015253f,.051277f},
                               {-.058898f,.016842f,.058236f},{-.144772f,.019816f,.082481f}};
inline void front_offsets(const skin::Frame& frame,bool right,bc::Vec (&offsets)[6]){
    for(unsigned i=0;i<6;++i){
        auto p=front_piece_points[i];if(right)p.z=-p.z;
        offsets[i]=skin::vector(frame,p);
    }
}
inline bc::Vec front_piece_turn(const skin::Frame& frame,bool right,const bc::Capsule& arm,
                                bc::Vec outward,bc::Vec forward){
    const auto limb=bc::sub(arm.b,arm.a);
    const float span=bc::length(limb);
    if(span<.001f||arm.radius<=0)return {};
    const float reach=std::clamp((bc::dot(limb,forward)/span-.50f)/.28f,0.f,1.f);
    if(reach<=0)return {};
    bc::Vec offsets[6];
    front_offsets(frame,right,offsets);
    auto axis=bc::cross(bc::add(offsets[2],offsets[3]),outward);
    const float size=bc::length(axis);
    if(size<.001f)return {};
    axis=bc::scale(axis,1/size);
    const auto overlap=[&](float angle,const bc::Capsule& obstacle){
        bc::Vec points[6];
        for(unsigned i=0;i<6;++i)
            points[i]=bc::add(frame.position,angle==0?offsets[i]:skin::axial_turn(offsets[i],axis,angle));
        float worst=0;
        for(unsigned row:{0u,2u}){
            const bc::Vec panel[]={points[row],points[row+1],points[row+2],points[row+3]};
            worst=std::max(worst,patch_depth(panel,obstacle));
        }
        return worst;
    };
    if(overlap(0,arm)<.0003f)return {};
    auto padded=arm;padded.radius+=.002f;
    float low=0,high=.96f;
    const float end=overlap(high,padded);
    if(end>.0001f){
        if(end>=overlap(0,padded))return {};
    }else for(unsigned i=0;i<6;++i){
        const float mid=(low+high)*.5f;
        if(overlap(mid,padded)>.0001f)low=mid;else high=mid;
    }
    return bc::scale(axis,high*reach);
}
struct FrontFit {bc::Vec turn{};float release=0;};
inline bc::Vec front_seam_shift(bc::Vec lift,float release){
    // Turn the sheet along the arm before lowering it through the cuff.
    return bc::scale(lift,-.45f*release*release);
}
inline FrontFit fit_front_drape(const skin::Frame& frame,bool right,const bc::Capsule& arm,
                               bc::Vec outward,bc::Vec forward,bc::Vec seam_lift){
    namespace ik=leg_ik;
    const auto escape=front_piece_turn(frame,right,arm,outward,forward);
    const FrontFit fallback{escape,0};
    const float angle=bc::length(escape);
    if(angle<.001f||bc::length(seam_lift)<.001f)return fallback;
    const auto unit=[](bc::Vec v){return bc::scale(v,1/std::max(1e-8f,bc::length(v)));};
    bc::Vec p[6];front_offsets(frame,right,p);
    bc::Vec along{},center{};
    for(unsigned i=0;i<6;i+=2){
        along=bc::add(along,bc::sub(p[i+1],p[i]));
        center=bc::add(center,bc::add(p[i+1],p[i]));
    }
    along=unit(along);
    const auto across=bc::sub(bc::add(p[4],p[5]),bc::add(p[0],p[1]));
    const auto face=bc::scale(unit(bc::cross(along,across)),right?-1.f:1.f);
    // Release part of the seam lift; keep the front sheet tangent to the
    // outside of the sleeve, with its free end directed along the upper arm.
    const auto pivot=bc::add(frame.position,front_seam_shift(seam_lift,1));
    const auto limb=bc::sub(arm.b,arm.a),axis=unit(limb),offset=bc::sub(pivot,arm.a);
    const auto radial=bc::sub(offset,bc::scale(axis,bc::dot(offset,axis)));
    const float distance=bc::length(radial);
    const float plane_distance=cuff(arm).radius+.005f-bc::dot(center,face)/6;
    if(distance<=std::abs(plane_distance)||bc::length(face)<.99f)return fallback;
    const auto radial_unit=bc::scale(radial,1/distance);
    auto tangent=bc::cross(axis,radial_unit);
    if(bc::dot(tangent,outward)<0)tangent=bc::scale(tangent,-1);
    const float h=plane_distance/distance;
    const auto normal=bc::add(bc::scale(radial_unit,h),bc::scale(tangent,std::sqrt(std::max(0.f,1-h*h))));
    const auto aim=bc::sub(bc::add(arm.a,bc::scale(limb,.4f)),pivot);
    const auto hem=unit(bc::sub(aim,bc::scale(normal,bc::dot(aim,normal))));
    if(bc::length(hem)<.99f)return fallback;
    const auto basis=[](bc::Vec x,bc::Vec z){
        const auto y=bc::cross(z,x);
        return ik::quaternion({{{x.x,x.y,x.z},{y.x,y.y,y.z},{z.x,z.y,z.z}}});
    };
    const auto src=basis(along,face),dst=basis(hem,normal);
    const auto delta=rotation_preview::multiply(dst,{-src.x,-src.y,-src.z,src.w});
    const auto rotation=ik::rotation(delta);
    for(auto& v:p){
        const auto d=ik::vector(rotation,{v.x,v.y,v.z});
        v=bc::add(pivot,{d.x,d.y,d.z});
    }
    for(unsigned row:{0u,2u}){
        const bc::Vec panel[]={p[row],p[row+1],p[row+2],p[row+3]};
        if(patch_depth(panel,arm)>.0003f)return fallback;
    }
    const bc::Vec imaginary{delta.x,delta.y,delta.z};
    const float theta=2*std::atan2(bc::length(imaginary),std::abs(delta.w));
    const auto turn=bc::scale(unit(imaginary),delta.w<0?-theta:theta);
    const float blend=std::clamp(angle/.55f,0.f,1.f)*std::clamp(bc::length(seam_lift)/arm.radius-.4f,0.f,1.f);
    return {bc::add(escape,bc::scale(bc::sub(turn,escape),blend)),blend};
}
inline bc::Vec rim_support(const bc::Vec (&points)[6],const bc::Capsule& arm,bc::Vec up,bc::Vec forward){
    const auto axis=bc::sub(arm.b,arm.a);
    const float span=bc::length(axis);
    if(span<.001f||arm.radius<=0)return {};
    const float reach=std::clamp((bc::dot(axis,forward)/span-.65f)/.20f,0.f,1.f);
    if(reach<=0)return {};
    bc::Vec correction{};
    for(unsigned row:{0u,2u}){
        const bc::Vec panel[]={points[row],points[row+1],points[row+2],points[row+3]};
        for(const auto& obstacle:{arm,cuff(arm)}){
            const auto shift=cloth_surface::shoulder_cover_shift(panel,obstacle,up);
            if(bc::length(shift)>bc::length(correction))correction=shift;
        }
    }
    // Only the two side joints move. The least mobile vertex follows them
    // with weight 0.9525; all other sampled vertices have unit total weight.
    return bc::scale(correction,reach/(1-rim[0].side_front_weight));
}
inline bc::Vec trim_shift(const Vertex (&vertices)[4],const bc::Vec (&panel)[4],const bc::Capsule& arm,
                          bc::Vec up,bc::Vec facing){
    const auto axis=bc::sub(arm.b,arm.a);
    const float span=bc::length(axis);
    if(span<.001f||arm.radius<=0)return {};
    const auto unit=bc::scale(axis,1/span);
    const float active=std::clamp((bc::dot(unit,up)+.95f)/.22f,0.f,1.f);
    if(active==0)return {};
    const auto sleeve=cuff(arm);
    // All trial offsets are perpendicular to the arm. A patch entirely
    // between the cuff endpoints stays inside that axial interval.
    bool check_full_arm=false;
    for(const auto& point:panel){
        const float along=bc::dot(bc::sub(point,arm.a),unit);
        if(along<.26f*span||along>.70f*span){check_full_arm=true;break;}
    }
    const auto depth=[&](const bc::Vec (&points)[4]){
        const float at_cuff=cloth_surface::side_depth(points,sleeve);
        return check_full_arm?std::max(at_cuff,cloth_surface::side_depth(points,arm)):at_cuff;
    };
    const float start=depth(panel);
    if(start<.0003f)return {};
    const auto transverse=[&](bc::Vec v){return bc::sub(v,bc::scale(unit,bc::dot(v,unit)));};
    auto normal=transverse(bc::add(facing,bc::scale(up,.5f)));
    // When the arm points forward, both garment edges can project onto the
    // same upward direction. Follow the side of the arm carrying this patch.
    const float forward=std::clamp((std::abs(bc::dot(unit,facing))-.65f)/.25f,0.f,1.f);
    if(forward>0){
        const auto center=bc::scale(bc::add(bc::add(panel[0],panel[1]),bc::add(panel[2],panel[3])),.25f);
        const auto radial=transverse(bc::sub(center,arm.a));
        const float distance=bc::length(radial);
        // Fade near the center line, where the radial direction is ambiguous.
        if(distance>.0001f){
            const float blend=forward*std::min(1.f,distance/(.5f*arm.radius));
            normal=bc::add(bc::scale(normal,1-blend),bc::scale(radial,blend/distance));
        }
    }
    if(bc::length(normal)<.001f)normal=transverse(up);
    const float size=bc::length(normal);
    if(size<.001f)return {};
    normal=bc::scale(normal,1/size);
    const auto shifted_depth=[&](float shift){
        bc::Vec trial[4];
        for(unsigned i=0;i<4;++i)trial[i]=bc::add(panel[i],bc::scale(normal,shift*vertices[i].flap_weight));
        return depth(trial);
    };
    float low=0,high=1.6f*arm.radius;
    const float end=shifted_depth(high);
    if(end>=start)return {};
    if(end<.0001f)for(unsigned i=0;i<7;++i){
        const float mid=(low+high)*.5f;
        if(shifted_depth(mid)>.0001f)low=mid;else high=mid;
    }
    return bc::scale(normal,active*high);
}
}
