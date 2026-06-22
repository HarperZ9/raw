#pragma once
#include "raw/vec.hpp"
#include <cmath>
namespace raw {
struct Mat4 { float m[16]{}; }; // row-major: element(r,c) = m[r*4+c]
inline Mat4 identity(){ Mat4 r; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
inline Mat4 mul(const Mat4& a, const Mat4& b){
    Mat4 r;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j){
        float s=0; for (int k=0;k<4;++k) s += a.m[i*4+k]*b.m[k*4+j];
        r.m[i*4+j]=s; }
    return r; }
inline Vec4 mul(const Mat4& a, Vec4 v){
    return {
        a.m[0]*v.x+a.m[1]*v.y+a.m[2]*v.z+a.m[3]*v.w,
        a.m[4]*v.x+a.m[5]*v.y+a.m[6]*v.z+a.m[7]*v.w,
        a.m[8]*v.x+a.m[9]*v.y+a.m[10]*v.z+a.m[11]*v.w,
        a.m[12]*v.x+a.m[13]*v.y+a.m[14]*v.z+a.m[15]*v.w }; }
inline Mat4 translate(Vec3 t){ Mat4 r=identity(); r.m[3]=t.x; r.m[7]=t.y; r.m[11]=t.z; return r; }
inline Mat4 perspective(float fovy, float aspect, float n, float f){
    float t = 1.0f/std::tan(fovy*0.5f);
    Mat4 r;
    r.m[0]=t/aspect; r.m[5]=t;
    r.m[10]=(f+n)/(n-f); r.m[11]=(2*f*n)/(n-f);
    r.m[14]=-1; // w_clip = -z_view
    return r; }
inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up){
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r=identity();
    r.m[0]=s.x; r.m[1]=s.y; r.m[2]=s.z; r.m[3]=-dot(s,eye);
    r.m[4]=u.x; r.m[5]=u.y; r.m[6]=u.z; r.m[7]=-dot(u,eye);
    r.m[8]=-f.x; r.m[9]=-f.y; r.m[10]=-f.z; r.m[11]=dot(f,eye);
    return r; }
}
