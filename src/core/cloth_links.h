#pragma once
#include <string_view>
#include "body_collision.h"
namespace gimmd::cloth_links {
namespace bc=body_collision;
struct Vertex {unsigned index=0;float angle=0,height=0;};
enum class RingRegion { Other, Front, Rear };
inline RingRegion ring_region(bool closed,float front_weight) {
    if(closed&&front_weight>=.8f)return RingRegion::Front;
    if(closed&&front_weight<=.2f)return RingRegion::Rear;
    return RingRegion::Other;
}
inline float panel_half_width(bc::Vec center,bc::Vec left,bc::Vec right,bc::Vec hip_right) {
    const float a=std::abs(bc::dot(bc::sub(left,center),hip_right));
    const float b=std::abs(bc::dot(bc::sub(right,center),hip_right));
    return .25f*(a+b);
}
inline float upper_neighbor_lift(float direct,float neighbor,bool fitted_rear,float padding) {
    const float carried=.55f*neighbor;
    return std::max(direct,fitted_rear?std::min(padding,carried):carried);
}
inline float rear_support_radius(float rest_radius,float parent_expansion,float support_percent) {
    // The waist can clear a bent hip without forcing the entire back panel
    // to inherit the same bulge. Direct leg contacts still run afterwards.
    return (rest_radius+.25f*std::max(0.f,parent_expansion))*support_percent*.01f;
}
inline std::wstring_view skirt_strip(std::wstring_view name) {
    if(!name.empty()&&name.front()==L'+')name.remove_prefix(1);
    for(const std::wstring_view prefix:{L"SkirtF CF ",L"SkirtF L ",L"SkirtF R ",L"SkirtS L ",L"SkirtS R ",L"SkirtB L ",L"SkirtB R "})
        if(name.substr(0,prefix.size())==prefix)return prefix;
    return {};
}
// A missing strip must be a single, wholly unowned chain. A native array
// can start at B02, so checking ownership of the root alone is insufficient.
template<class Path,class Particle>
inline unsigned missing_skirt_chain(const Path* paths,unsigned count,unsigned root,
                                    const Particle* particles,unsigned particle_count,unsigned (&chain)[12]) {
    const auto strip=skirt_strip(paths[root].name);
    const int parent=paths[root].parent_node;
    if(strip.empty()||(parent>=0&&!skirt_strip(paths[parent].name).empty()))return 0;
    unsigned current=root,length=0;
    for(;;){
        if(length==12||skirt_strip(paths[current].name)!=strip)return 0;
        for(unsigned p=0;p<particle_count;++p)
            if(particles[p].real&&particles[p].path==current)return 0;
        chain[length++]=current;
        int next=-1;
        for(unsigned i=0;i<count;++i)if(paths[i].parent_node==int(current)){
            if(next>=0)return 0;
            next=int(i);
        }
        if(next<0)break;
        current=unsigned(next);
    }
    if(length>=3&&strip.starts_with(L"SkirtF "))return length;
    // Two-bone panels such as Collei's have no separate hem joint. Their
    // terminal rotation is driven by the solver's existing virtual tip.
    if(length!=2||parent<0)return 0;
    const std::wstring_view attachment=paths[parent].name;
    return attachment==L"Bip001 Spine"||attachment==L"Bip001 Spine1"||attachment==L"Bip001 Pelvis"?length:0;
}
// Conservative geometric fallback for closed skirts. Open capes/petals and
// sparse or duplicate directions are deliberately not joined into a ring.
inline bool closed_ring(Vertex* vertices,unsigned count,float height_tolerance) {
    if(count<6||count>32)return false;
    float low=vertices[0].height,high=low;
    for(unsigned i=0;i<count;++i){if(!std::isfinite(vertices[i].angle)||!std::isfinite(vertices[i].height))return false;
        low=std::min(low,vertices[i].height);high=std::max(high,vertices[i].height);}
    if(high-low>height_tolerance)return false;
    std::sort(vertices,vertices+count,[](const Vertex& a,const Vertex& b){return a.angle<b.angle;});
    for(unsigned i=0;i<count;++i){const float gap=(i+1<count?vertices[i+1].angle:vertices[0].angle+6.283185307f)-vertices[i].angle;
        if(gap<.05f||gap>1.75f)return false;}
    return true;
}
inline bool distance_constraint(bc::Vec& a,bc::Vec& previous_a,bc::Vec& b,bc::Vec& previous_b,float rest,float stiffness) {
    const auto direction=bc::sub(b,a);const float length=bc::length(direction);
    if(rest<=1e-6f||length<1e-6f)return false;
    const auto correction=bc::scale(direction,(length-rest)/length*.5f*std::clamp(stiffness,0.0f,1.0f));
    a=bc::add(a,correction);previous_a=bc::add(previous_a,correction);
    b=bc::sub(b,correction);previous_b=bc::sub(previous_b,correction);return true;
}
// Preserve a panel's fitted arch across three neighboring chains. A newly
// contacted center shares its displacement with both sides instead of forming
// a sharp isolated peak. Contact projection runs again after this constraint.
inline bool arch_constraint(bc::Vec& center,bc::Vec& previous_center,bc::Vec& left,bc::Vec& previous_left,
                            bc::Vec& right,bc::Vec& previous_right,bc::Vec outward,float rest_arch,float allowance=.008f){
    const float magnitude=bc::length(outward);if(magnitude<1e-6f)return false;outward=bc::scale(outward,1/magnitude);
    const float excess=bc::dot(bc::sub(center,bc::scale(bc::add(left,right),.5f)),outward)-rest_arch-allowance;
    if(excess<=0)return false;const auto delta=bc::scale(outward,std::min(excess*.25f,.008f));
    center=bc::sub(center,delta);previous_center=bc::sub(previous_center,delta);
    left=bc::add(left,bc::scale(delta,.5f));previous_left=bc::add(previous_left,bc::scale(delta,.5f));
    right=bc::add(right,bc::scale(delta,.5f));previous_right=bc::add(previous_right,bc::scale(delta,.5f));return true;
}
// Resolve a contact anywhere between two movable neighboring skirt bones.
// Barycentric weights share the displacement instead of lifting just one tip.
inline bool project_pair(const bc::Capsule& capsule,float radius,bc::Vec& a,bc::Vec& previous_a,
                         bc::Vec& b,bc::Vec& previous_b,bc::Vec outward) {
    const auto pair=bc::closest_segment(a,b,capsule);const float t=pair.fraction,u=1-t,denominator=t*t+u*u;
    auto point=pair.point,old=bc::add(bc::scale(previous_a,u),bc::scale(previous_b,t));const auto old_before=old;
    if(!bc::project_outward(capsule,radius,point,old,outward))return false;
    const auto delta=bc::sub(point,pair.point),old_delta=bc::sub(old,old_before);
    a=bc::add(a,bc::scale(delta,u/denominator));previous_a=bc::add(previous_a,bc::scale(old_delta,u/denominator));
    b=bc::add(b,bc::scale(delta,t/denominator));previous_b=bc::add(previous_b,bc::scale(old_delta,t/denominator));return true;
}
}
