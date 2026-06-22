#pragma once
#include "raw/vec.hpp"
#include <algorithm>
namespace raw {
struct Ray { Vec3 o, d; };
struct Tri { Vec3 a, b, c; };
struct AABB {
    Vec3 mn, mx;
    void grow(Vec3 p){
        mn = {std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
        mx = {std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)}; }
};
// Möller–Trumbore. Returns true and fills t (distance), u,v (barycentrics).
inline bool intersectTri(const Ray& r, const Tri& tri, float& t, float& u, float& v){
    const float EPS = 1e-7f;
    Vec3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
    Vec3 p = cross(r.d, e2);
    float det = dot(e1, p);
    if (det > -EPS && det < EPS) return false;
    float inv = 1.0f/det;
    Vec3 tv = r.o - tri.a;
    u = dot(tv, p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = cross(tv, e1);
    v = dot(r.d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = dot(e2, q) * inv;
    return t > 1e-4f;
}
}
