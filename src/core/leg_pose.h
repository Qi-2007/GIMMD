#pragma once
#include "leg_ik.h"
#include "generated/fk_profile.h"
namespace gimmd::leg_pose {
namespace ik=leg_ik;
using Vec=ik::Vec;
using Quat=ik::Quat;
constexpr unsigned Count=std::size(fk_profile::AvatarReference);
constexpr unsigned Hip[2]={10,33}, Knee[2]={11,34}, Foot[2]={12,35};
constexpr unsigned Toe[2]={13,36};
struct Geometry { bool active[Count]{}; Vec position[Count]{},scale[Count]{}; };
struct Rig {
    Quat local[Count]{};
    ik::Transform world[Count]{};
    Vec root_delta{};
    const Geometry* geometry=nullptr;
    void reset(const Quat* rotations,Vec delta,const Quat* root_override=nullptr,const Geometry* measured=nullptr) {
        geometry=measured;
        root_delta=delta;
        for (unsigned i=0;i<Count;++i) { const auto& r=fk_profile::AvatarReference[i]; local[i]={r.rotation[0],r.rotation[1],r.rotation[2],r.rotation[3]}; }
        if (rotations) for (unsigned i=0;i<std::size(fk_profile::AvatarIndices);++i) local[fk_profile::AvatarIndices[i]]=rotations[i];
        if (root_override) local[1]=*root_override;
        update();
    }
    void update() {
        for (unsigned i=0;i<Count;++i) {
            const auto& r=fk_profile::AvatarReference[i]; Vec p{r.position[0],r.position[1],r.position[2]},s{r.scale[0],r.scale[1],r.scale[2]};
            if (geometry&&geometry->active[i]) { p=geometry->position[i]; s=geometry->scale[i]; }
            if (i==1) p=ik::add(p,root_delta);
            world[i]=ik::compose(r.parent<0 ? ik::Transform{}:world[r.parent],p,local[i],s);
        }
    }
};
struct FootSample { bool enabled=false; Vec delta{}; Quat rotation{}; float support_weight=0; bool toe_enabled=false; Vec toe_offset{}; };
struct Result {
    unsigned solved=0,clamped=0,toe_solved=0,toe_corrected=0,toe_degenerate=0;
    float maximum_residual=0,maximum_requested_miss=0,root_correction_y=0;
    float maximum_toe_radial_miss=0,maximum_toe_direction_error=0,maximum_toe_correction_degrees=0;
};
inline float support_weight(float source_height_units) {
    const float t=std::clamp((source_height_units-0.02f)/0.04f,0.0f,1.0f);
    return 1-t*t*(3-2*t);
}
inline bool solve(Quat* rotations,Vec root_delta,Vec master_delta,const FootSample (&feet)[2],Result* result,const Quat* root_override=nullptr,const Geometry* geometry=nullptr) {
    Rig rig{},rest{}; rig.reset(rotations,root_delta,root_override,geometry); rest.reset(nullptr,{},nullptr,geometry); *result={};
    // Approximate flat support only: lower the visual root to keep a low foot within reach.
    // No collider/raycast is involved; lifted feet and jumps do not impose this constraint.
    for (unsigned side=0;side<2;++side) if (feet[side].enabled&&feet[side].support_weight>0) {
        const unsigned h=fk_profile::AvatarIndices[Hip[side]],k=fk_profile::AvatarIndices[Knee[side]],f=fk_profile::AvatarIndices[Foot[side]];
        const auto target=ik::add(rest.world[f].position,ik::add(feet[side].delta,master_delta));
        const auto gap=ik::sub(rig.world[h].position,target);
        const float reach=ik::length(ik::sub(rig.world[k].position,rig.world[h].position))+ik::length(ik::sub(rig.world[f].position,rig.world[k].position))-0.002f;
        const float max_vertical=std::sqrt(std::max(0.0f,reach*reach-gap.x*gap.x-gap.z*gap.z));
        if (gap.y>max_vertical) result->root_correction_y=std::min(result->root_correction_y,
            std::max(-0.25f,(max_vertical-gap.y)*std::clamp(feet[side].support_weight,0.0f,1.0f)));
    }
    root_delta.y+=result->root_correction_y; rig.reset(rotations,root_delta,root_override,geometry);
    for (unsigned side=0;side<2;++side) {
        if (!feet[side].enabled) continue;
        const unsigned h=fk_profile::AvatarIndices[Hip[side]], k=fk_profile::AvatarIndices[Knee[side]], f=fk_profile::AvatarIndices[Foot[side]];
        const int parent=fk_profile::AvatarReference[h].parent;
        Vec pole_local{};
        if (parent<0 || fk_profile::AvatarReference[k].parent!=int(h)||fk_profile::AvatarReference[f].parent!=int(k)||
            !ik::inverse_vector(rest.world[parent].frame,{0,0,1},&pole_local)) return false;
        const auto target=ik::add(rest.world[f].position,ik::add(feet[side].delta,master_delta));
        ik::Solution solution{};
        if (!ik::solve(rig.world[h].position,rig.world[k].position,rig.world[f].position,target,
            ik::vector(rig.world[parent].frame,pole_local),&solution)) return false;
        if (!ik::aim(rig.world[parent],rig.world[h].position,rig.world[k].position,solution.knee,rig.local[h],&rig.local[h])) return false;
        rig.update();
        if (!ik::aim(rig.world[h],rig.world[k].position,rig.world[f].position,solution.ankle,rig.local[k],&rig.local[k])) return false;
        rig.update();
        // Foot IK rotates an ankle target independently of its solved parent chain.
        const auto q=BetterEndfieldNext::Vmd::Normalize(feet[side].rotation);
        const auto desired=ik::multiply(ik::rotation({-q.x,q.y,-q.z,q.w}),rest.world[f].frame);
        const auto& reference=fk_profile::AvatarReference[f];
        const Vec foot_scale=geometry&&geometry->active[f] ? geometry->scale[f]:Vec{reference.scale[0],reference.scale[1],reference.scale[2]};
        if (!ik::local_frame(rig.world[k].frame,desired,foot_scale,&rig.local[f])) return false;
        rig.update();
        if (feet[side].toe_enabled) {
            const unsigned t=fk_profile::AvatarIndices[Toe[side]];
            if (fk_profile::AvatarReference[t].parent!=int(f)||!ik::finite(feet[side].toe_offset)) return false;
            // Toe IK is an ankle-relative child target. Use the projected ankle as its pivot,
            // so an unreachable leg does not create a spurious toe rotation at zero offset.
            const auto direction=ik::vector(ik::rotation({-q.x,q.y,-q.z,q.w}),
                ik::add(ik::sub(rest.world[t].position,rest.world[f].position),feet[side].toe_offset));
            const auto pivot=rig.world[f].position;
            const float wanted_length=ik::length(direction),toe_length=ik::length(ik::sub(rig.world[t].position,pivot));
            if (wanted_length<1e-6f||toe_length<1e-6f) ++result->toe_degenerate;
            else {
                const auto old=rig.local[f];
                // Exact neutral controls retain the already verified ankle orientation.
                if (ik::length(feet[side].toe_offset)>1e-8f) {
                    if (!ik::aim(rig.world[k],pivot,rig.world[t].position,ik::add(pivot,direction),old,&rig.local[f])) return false;
                    rig.update();
                }
                const float error=ik::length(ik::sub(ik::unit(ik::sub(rig.world[t].position,pivot)),ik::unit(direction)));
                if (!std::isfinite(error)||error>0.002f) return false;
                const float alignment=std::clamp(std::abs(old.x*rig.local[f].x+old.y*rig.local[f].y+old.z*rig.local[f].z+old.w*rig.local[f].w),0.0f,1.0f);
                const float degrees=ik::length(feet[side].toe_offset)>1e-8f ? 2*std::acos(alignment)*57.295779513f : 0;
                ++result->toe_solved; if (degrees>0.05f) ++result->toe_corrected;
                result->maximum_toe_correction_degrees=std::max(result->maximum_toe_correction_degrees,degrees);
                result->maximum_toe_direction_error=std::max(result->maximum_toe_direction_error,error);
                result->maximum_toe_radial_miss=std::max(result->maximum_toe_radial_miss,std::abs(wanted_length-toe_length));
            }
        }
        const float residual=ik::length(ik::sub(rig.world[f].position,solution.ankle));
        if (!std::isfinite(residual)||residual>0.002f) return false;
        ++result->solved; result->clamped+=unsigned(solution.clamped); result->maximum_residual=std::max(result->maximum_residual,residual);
        result->maximum_requested_miss=std::max(result->maximum_requested_miss,ik::length(ik::sub(rig.world[f].position,target)));
        rotations[Hip[side]]=rig.local[h]; rotations[Knee[side]]=rig.local[k]; rotations[Foot[side]]=rig.local[f];
    }
    return true;
}
}
