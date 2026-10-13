#pragma once
#include "root_translation.h"
#include "rotation_preview.h"
namespace gimmd::leg_ik {
using Vec = root_translation::Vec3;
using Quat = root_translation::Quaternion;
inline Vec add(Vec a,Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec sub(Vec a,Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec scale(Vec a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec cross(Vec a,Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec a) { return std::sqrt(dot(a,a)); }
inline bool finite(Vec a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline Vec unit(Vec a) { const float n=length(a); return n>1e-8f ? scale(a,1/n) : Vec{}; }
struct Matrix { Vec columns[3]{{1,0,0},{0,1,0},{0,0,1}}; };
inline Vec vector(const Matrix& m,Vec a) { return add(add(scale(m.columns[0],a.x),scale(m.columns[1],a.y)),scale(m.columns[2],a.z)); }
inline Matrix multiply(const Matrix& a,const Matrix& b) { return {{vector(a,b.columns[0]),vector(a,b.columns[1]),vector(a,b.columns[2])}}; }
inline bool inverse_vector(const Matrix& m,Vec v,Vec* out) {
    const auto x=cross(m.columns[1],m.columns[2]), y=cross(m.columns[2],m.columns[0]), z=cross(m.columns[0],m.columns[1]);
    const float determinant=dot(m.columns[0],x);
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-8f) return false;
    *out={dot(x,v)/determinant,dot(y,v)/determinant,dot(z,v)/determinant}; return finite(*out);
}
inline Matrix rotation(Quat q) {
    q=BetterEndfieldNext::Vmd::Normalize(q); const float x=q.x,y=q.y,z=q.z,w=q.w;
    return {{{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w)},
        {2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)},
        {2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)}}};
}
inline Quat quaternion(const Matrix& m) {
    const auto& c=m.columns; const float trace=c[0].x+c[1].y+c[2].z; Quat q{};
    if (trace>0) { const float s=std::sqrt(trace+1)*2; q={(c[1].z-c[2].y)/s,(c[2].x-c[0].z)/s,(c[0].y-c[1].x)/s,s/4}; }
    else if (c[0].x>c[1].y && c[0].x>c[2].z) { const float s=std::sqrt(1+c[0].x-c[1].y-c[2].z)*2; q={s/4,(c[1].x+c[0].y)/s,(c[2].x+c[0].z)/s,(c[1].z-c[2].y)/s}; }
    else if (c[1].y>c[2].z) { const float s=std::sqrt(1+c[1].y-c[0].x-c[2].z)*2; q={(c[1].x+c[0].y)/s,s/4,(c[2].y+c[1].z)/s,(c[2].x-c[0].z)/s}; }
    else { const float s=std::sqrt(1+c[2].z-c[0].x-c[1].y)*2; q={(c[2].x+c[0].z)/s,(c[2].y+c[1].z)/s,s/4,(c[0].y-c[1].x)/s}; }
    return BetterEndfieldNext::Vmd::Normalize(q);
}
inline Quat from_to(Vec a,Vec b) {
    a=unit(a); b=unit(b); auto v=cross(a,b); const float w=1+std::clamp(dot(a,b),-1.0f,1.0f);
    if (w<1e-6f) { v=unit(cross(a,std::abs(a.x)<0.8f ? Vec{1,0,0}:Vec{0,1,0})); return {v.x,v.y,v.z,0}; }
    return BetterEndfieldNext::Vmd::Normalize({v.x,v.y,v.z,w});
}
struct Transform { Matrix frame; Vec position; };
inline Transform compose(const Transform& parent,Vec p,Quat q,Vec s) {
    auto r=rotation(q); r.columns[0]=scale(r.columns[0],s.x); r.columns[1]=scale(r.columns[1],s.y); r.columns[2]=scale(r.columns[2],s.z);
    return {multiply(parent.frame,r),add(parent.position,vector(parent.frame,p))};
}
struct Solution { Vec knee,ankle; bool clamped=false; };
// Fixed-length two-bone solve. The pole is a direction in the same space as the joints.
inline bool solve(Vec hip,Vec knee,Vec ankle,Vec target,Vec pole,Solution* out) {
    const float a=length(sub(knee,hip)), b=length(sub(ankle,knee)); const auto delta=sub(target,hip); const float distance=length(delta);
    if (!finite(hip)||!finite(knee)||!finite(ankle)||!finite(target)||!finite(pole)||a<1e-6f||b<1e-6f||!std::isfinite(distance)) return false;
    const auto direction=distance>1e-6f ? scale(delta,1/distance):unit(sub(ankle,hip));
    if (length(direction)<0.5f) return false;
    const float d=std::clamp(distance,std::abs(a-b)+1e-5f,a+b-1e-5f);
    auto bend=sub(pole,scale(direction,dot(pole,direction)));
    if (length(bend)<1e-6f) bend=cross(direction,std::abs(direction.x)<0.8f ? Vec{1,0,0}:Vec{0,1,0});
    bend=unit(bend); const float along=(a*a-b*b+d*d)/(2*d), height=std::sqrt(std::max(0.0f,a*a-along*along));
    out->knee=add(hip,add(scale(direction,along),scale(bend,height))); out->ankle=add(hip,scale(direction,d)); out->clamped=std::abs(d-distance)>1e-6f;
    return finite(out->knee)&&finite(out->ankle);
}
// Transport directions through the full signed parent matrix; reflection is preserved.
inline bool aim(const Transform& parent,Vec origin,Vec old_end,Vec new_end,Quat old_local,Quat* local) {
    Vec from{},to{};
    if (!inverse_vector(parent.frame,sub(old_end,origin),&from)||!inverse_vector(parent.frame,sub(new_end,origin),&to)||length(from)<1e-6f||length(to)<1e-6f) return false;
    *local=rotation_preview::multiply(from_to(from,to),old_local); return true;
}
inline bool local_frame(const Matrix& parent,const Matrix& desired,Vec local_scale,Quat* out) {
    if (std::abs(local_scale.x)<1e-6f||std::abs(local_scale.y)<1e-6f||std::abs(local_scale.z)<1e-6f) return false;
    Matrix local{};
    for (unsigned i=0;i<3;++i) if (!inverse_vector(parent,desired.columns[i],&local.columns[i])) return false;
    local.columns[0]=scale(local.columns[0],1/local_scale.x); local.columns[1]=scale(local.columns[1],1/local_scale.y); local.columns[2]=scale(local.columns[2],1/local_scale.z);
    // Reject material shear/non-uniform scale instead of silently inventing a rotation.
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) if (std::abs(dot(local.columns[i],local.columns[j])-float(i==j))>0.003f) return false;
    if (dot(cross(local.columns[0],local.columns[1]),local.columns[2])<0.99f) return false;
    *out=quaternion(local); return true;
}
}
