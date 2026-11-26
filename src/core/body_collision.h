#pragma once
#include <algorithm>
#include <cmath>
namespace gimmd::body_collision {
struct Vec {float x=0,y=0,z=0;};
inline Vec add(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec sub(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec scale(Vec a,float k){return {a.x*k,a.y*k,a.z*k};}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float length(Vec a){return std::sqrt(dot(a,a));}
inline bool finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
struct Capsule {Vec a,b;float radius=0;};
// As the thigh approaches horizontal, pushing a flap along the thigh can
// slide it toward the knee. Tilt the contact direction over the raised leg.
inline Vec flap_contact_direction(Vec outward,Vec up,const Capsule& thigh) {
    const auto axis=sub(thigh.b,thigh.a);
    const float size=length(axis);
    if(size<1e-6f)return outward;
    const float lift=std::clamp((dot(axis,up)/size+.5f)*2,0.0f,1.0f);
    return add(outward,scale(up,length(outward)*lift));
}
inline Vec nearest(const Capsule& c,Vec p){const auto axis=sub(c.b,c.a);const auto d=dot(axis,axis);
    return add(c.a,scale(axis,d>1e-10f?std::clamp(dot(sub(p,c.a),axis)/d,0.0f,1.0f):0));}
inline float distance(const Capsule& c,Vec p){return length(sub(p,nearest(c,p)));}
struct SegmentContact {Vec point,axis;float fraction=0;};
// Closest pair on two finite segments, including parallel and zero-length cases.
inline SegmentContact closest_segment(Vec a,Vec b,const Capsule& capsule,float minimum_fraction=0) {
    const float start=std::clamp(minimum_fraction,0.0f,1.0f);
    const auto origin=add(a,scale(sub(b,a),start));
    const auto u=sub(b,origin),v=sub(capsule.b,capsule.a),w=sub(origin,capsule.a);
    const float uu=dot(u,u),vv=dot(v,v),uv=dot(u,v),uw=dot(u,w),vw=dot(v,w);
    float s=0,t=0;
    if(uu<=1e-10f){if(vv>1e-10f)t=std::clamp(vw/vv,0.0f,1.0f);}
    else if(vv<=1e-10f)s=std::clamp(-uw/uu,0.0f,1.0f);
    else {const float denominator=uu*vv-uv*uv;
        if(denominator>1e-6f*uu*vv)s=std::clamp((uv*vw-uw*vv)/denominator,0.0f,1.0f);
        t=(uv*s+vw)/vv;
        if(t<0){t=0;s=std::clamp(-uw/uu,0.0f,1.0f);}
        else if(t>1){t=1;s=std::clamp((uv-uw)/uu,0.0f,1.0f);}}
    return {add(origin,scale(u,s)),add(capsule.a,scale(v,t)),start+(1-start)*s};
}
// Approximate the inner half of an open flap by a horizontal strip. Find the
// thigh beneath that strip in the hip plane, then recover its vertical height.
inline float inner_flap_lift(Vec at,Vec inner_edge,Vec up,const Capsule& thigh) {
    const Vec axis=sub(thigh.b,thigh.a);
    const float size=length(axis);
    if(size<1e-6f||thigh.radius<=0)return 0;
    const float raised=std::clamp(2*(dot(axis,up)/size+.5f),0.f,1.f);
    if(raised<=0)return 0;
    const auto flatten=[&](Vec p){return sub(p,scale(up,dot(p,up)));};
    const Vec flat_axis=flatten(axis);
    const float squared=dot(flat_axis,flat_axis);
    if(squared<1e-8f)return 0;
    const Capsule flat_thigh{{},flat_axis,thigh.radius};
    const auto pair=closest_segment(flatten(sub(at,thigh.a)),flatten(sub(inner_edge,thigh.a)),flat_thigh);
    const Vec gap=sub(pair.point,pair.axis);
    const float thickness2=thigh.radius*thigh.radius-dot(gap,gap);
    if(thickness2<=0)return 0;
    const float t=std::clamp(dot(pair.axis,flat_axis)/squared,0.f,1.f);
    const float above=dot(sub(add(thigh.a,scale(axis,t)),at),up);
    const float thickness=std::sqrt(thickness2);
    const float overlap=std::max(0.f,above+thickness);
    return raised*std::min(overlap,2*thickness);
}
// Project a secondary particle out of a capsule. Preserve tangential velocity,
// but remove inward normal velocity so contact does not continually inject energy.
inline bool project(const Capsule& capsule,float allowed_radius,Vec& point,Vec& previous,Vec rest_direction) {
    const auto center=nearest(capsule,point);auto normal=sub(point,center);const float d=length(normal);
    if(allowed_radius<=0||d>=allowed_radius)return false;
    if(d>1e-6f)normal=scale(normal,1/d);
    else {const auto axis=sub(capsule.b,capsule.a);const float axis_length=dot(axis,axis);
        normal=rest_direction;if(axis_length>1e-10f)normal=sub(normal,scale(axis,dot(normal,axis)/axis_length));
        if(length(normal)<1e-6f){normal={1,0,0};if(axis_length>1e-10f)normal=sub(normal,scale(axis,dot(normal,axis)/axis_length));}
        if(length(normal)<1e-6f)normal={0,1,0};normal=scale(normal,1/std::max(length(normal),1e-6f));}
    auto velocity=sub(point,previous);const float inward=dot(velocity,normal);
    if(inward<0)velocity=sub(velocity,scale(normal,inward));
    point=add(center,scale(normal,allowed_radius));previous=sub(point,scale(velocity,.95f));return true;
}
// The parent is fixed for this solve. Ignore the first fifth next to that anchor:
// its fitted position cannot be corrected by moving the child a bounded amount.
inline bool project_segment(const Capsule& capsule,float radius,Vec anchor,Vec previous_anchor,
                            Vec& point,Vec& previous,Vec rest_direction) {
    const auto pair=closest_segment(anchor,point,capsule,.2f);
    auto position=pair.point;
    auto old=add(previous_anchor,scale(sub(previous,previous_anchor),pair.fraction));
    const auto original_old=old;
    if(!project(capsule,radius,position,old,rest_direction))return false;
    point=add(point,scale(sub(position,pair.point),1/pair.fraction));
    previous=add(previous,scale(sub(old,original_old),1/pair.fraction));return true;
}
// Move to the outside along a chosen garment direction. Nearest-surface contact
// alone can eject a skirt particle between the legs and collapse the silhouette.
inline bool project_outward(const Capsule& capsule,float radius,Vec& point,Vec& previous,Vec outward) {
    if(radius<=0||distance(capsule,point)>=radius)return false;
    const float magnitude=length(outward);if(magnitude<1e-6f)return project(capsule,radius,point,previous,outward);
    outward=scale(outward,1/magnitude);
    float low=0,high=2*radius+length(sub(capsule.b,capsule.a))+.001f;
    for(unsigned i=0;i<12;++i){const float mid=(low+high)*.5f;
        if(distance(capsule,add(point,scale(outward,mid)))<radius)low=mid;else high=mid;}
    const auto correction=scale(outward,high);point=add(point,correction);previous=add(previous,correction);
    auto velocity=sub(point,previous);const float inward=dot(velocity,outward);
    if(inward<0)previous=add(previous,scale(outward,inward));return true;
}
inline bool project_segment_outward(const Capsule& capsule,float radius,Vec anchor,Vec previous_anchor,
                                    Vec& point,Vec& previous,Vec outward,float minimum_fraction=.2f) {
    const auto pair=closest_segment(anchor,point,capsule,std::clamp(minimum_fraction,.05f,1.0f));auto position=pair.point;
    auto old=add(previous_anchor,scale(sub(previous,previous_anchor),pair.fraction));const auto original_old=old;
    if(!project_outward(capsule,radius,position,old,outward))return false;
    point=add(point,scale(sub(position,pair.point),1/pair.fraction));
    previous=add(previous,scale(sub(old,original_old),1/pair.fraction));return true;
}
inline Vec radial(Vec origin,Vec unit_axis,Vec point){const auto delta=sub(point,origin);return sub(delta,scale(unit_axis,dot(delta,unit_axis)));}
// A one-sided silhouette constraint: preserve the fitted skirt's opening while
// permitting outward motion. It does not rigidly pin the particle to its rest pose.
inline bool support_radius(Vec origin,Vec unit_axis,float minimum,Vec fallback,Vec& point,Vec& previous) {
    auto direction=radial(origin,unit_axis,point);const float radius=length(direction);
    if(minimum<=0||radius>=minimum)return false;
    if(radius>1e-6f)direction=scale(direction,1/radius);
    else {direction=radial({},unit_axis,fallback);const float size=length(direction);if(size<1e-6f)return false;direction=scale(direction,1/size);}
    const auto correction=scale(direction,minimum-radius);point=add(point,correction);previous=add(previous,correction);
    const float inward=dot(sub(point,previous),direction);if(inward<0)previous=add(previous,scale(direction,inward));return true;
}
inline bool length_constraint(Vec anchor,float rest_length,Vec& point,Vec fallback) {
    if(rest_length<=1e-6f)return false;auto direction=sub(point,anchor);float size=length(direction);
    if(size<1e-6f){direction=fallback;size=length(direction);if(size<1e-6f)return false;}
    point=add(anchor,scale(direction,rest_length/size));return true;
}
// Transport a child/end direction through a parent's collision rotation. Keeping
// the old world direction would counter-rotate the child and fold the mesh inward.
inline Vec transport_direction(Vec from,Vec to,Vec vector) {
    const float a=length(from),b=length(to);if(a<1e-6f||b<1e-6f)return vector;
    from=scale(from,1/a);to=scale(to,1/b);const float cosine=std::clamp(dot(from,to),-1.0f,1.0f);
    auto axis=cross(from,to);
    if(cosine<-.99999f){axis=cross(from,std::abs(from.x)<.8f?Vec{1,0,0}:Vec{0,1,0});axis=scale(axis,1/std::max(length(axis),1e-6f));
        return sub(scale(axis,2*dot(axis,vector)),vector);}
    // Rodrigues formula with an unnormalized cross product.
    return add(add(vector,cross(axis,vector)),scale(cross(axis,cross(axis,vector)),1/std::max(1+cosine,1e-6f)));
}
// Bound the collision correction angle, not the animation or native simulation.
// Short strands must not flip to the other side of a body proxy in one step.
inline bool limit_direction(Vec reference,Vec& direction,float radians) {
    const float r=length(reference),d=length(direction);if(r<1e-6f||d<1e-6f)return false;
    const auto from=scale(reference,1/r),to=scale(direction,1/d);const float cosine=std::clamp(dot(from,to),-1.0f,1.0f);
    if(cosine>=std::cos(radians))return false;
    auto tangent=sub(to,scale(from,cosine));float t=length(tangent);
    if(t<1e-6f){tangent=sub(Vec{1,0,0},scale(from,from.x));t=length(tangent);}
    if(t<1e-6f){tangent=sub(Vec{0,1,0},scale(from,from.y));t=length(tangent);}
    tangent=scale(tangent,1/std::max(t,1e-6f));
    direction=scale(add(scale(from,std::cos(radians)),scale(tangent,std::sin(radians))),d);return true;
}

// Near the pinned end, moving a contact by 2 mm at 5% of the link can
// displace its free end by 4 cm. Retract along the proposed bend, keeping
// the clearance already achieved at the tip and over the outer two thirds.
template<unsigned N>
inline Vec relax_rear_bend(Vec anchor,Vec rest,Vec proposed,
                           const Capsule (&capsules)[N],const float (&tip_radius)[N],const float (&segment_radius)[N]) {
    const float span=length(rest);
    if(span<1e-6f||length(sub(rest,proposed))<.0005f)return proposed;
    struct Clearance {float tip,whole,outer;};
    Clearance required[N]{};
    const auto measure=[&](Vec end,unsigned i,float start){
        const auto pair=closest_segment(anchor,end,capsules[i],start);
        return length(sub(pair.point,pair.axis));
    };
    const auto full=add(anchor,proposed);
    for(unsigned i=0;i<N;++i){
        if(tip_radius[i]>0)required[i].tip=std::min(tip_radius[i],distance(capsules[i],full));
        if(segment_radius[i]>0){
            required[i].whole=std::min(segment_radius[i],measure(full,i,.05f));
            required[i].outer=std::min(segment_radius[i],measure(full,i,1.f/3));
        }
    }
    const float seam_allowance=std::min(.003f,.025f*span);
    const auto clears=[&](Vec bend){
        const auto end=add(anchor,bend);
        for(unsigned i=0;i<N;++i){
            if(required[i].tip>0&&distance(capsules[i],end)+.0001f<required[i].tip)return false;
            if(segment_radius[i]<=0)continue;
            if(measure(end,i,1.f/3)+.0001f<required[i].outer)return false;
            const auto hit=closest_segment(anchor,end,capsules[i],.05f);
            const float allowance=seam_allowance*std::max(0.f,1.f-3.f*hit.fraction);
            if(length(sub(hit.point,hit.axis))+allowance+.0001f<required[i].whole)return false;
        }
        return true;
    };
    if(clears(rest))return rest;
    const auto delta=sub(proposed,rest);
    const auto bend_at=[&](float t){
        const auto v=add(rest,scale(delta,t));
        const float size=length(v);
        return size>1e-6f?scale(v,span/size):proposed;
    };
    float low=0,high=1;
    Vec best=proposed;
    for(unsigned step=0;step<7;++step){
        const float mid=.5f*(low+high);
        const auto bend=bend_at(mid);
        if(clears(bend)){best=bend;high=mid;}else low=mid;
    }
    return best;
}
}
