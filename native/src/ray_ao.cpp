#include "raw/ray_ao.hpp"
#include "raw/primitives.hpp"
#include <cmath>
#include <cstdint>
namespace raw {
// Deterministic hash -> float in [0,1). No global RNG state.
static float hash01(uint32_t x, uint32_t y, uint32_t s){
    uint32_t h = x*374761393u + y*668265263u + s*2246822519u;
    h = (h ^ (h>>13)) * 1274126177u; h ^= h>>16;
    return (h & 0xFFFFFFu) / float(0x1000000);
}
// Build an orthonormal basis around n.
static void basis(Vec3 n, Vec3& t, Vec3& b){
    Vec3 a = std::fabs(n.x) > 0.9f ? Vec3{0,1,0} : Vec3{1,0,0};
    t = normalize(cross(a, n)); b = cross(n, t);
}
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius, Arena* arena){
    Buffer<float> ao(arena); ao.resize(g.w, g.h);
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ ao.at(x,y)=1.0f; continue; }
        Vec3 p = g.position.at(x,y);
        Vec3 n = g.normal.at(x,y);
        Vec3 t,b; basis(n,t,b);
        Vec3 origin = p + n*1e-3f; // offset to avoid self-hit
        int open=0;
        for (int sidx=0; sidx<samples; ++sidx){
            float u1 = hash01(x,y,(uint32_t)(2*sidx));
            float u2 = hash01(x,y,(uint32_t)(2*sidx+1));
            // cosine-weighted hemisphere
            float r = std::sqrt(u1); float phi = 6.2831853f*u2;
            float lx = r*std::cos(phi), ly = r*std::sin(phi);
            float lz = std::sqrt(std::max(0.0f, 1.0f - u1));
            Vec3 dir = normalize(t*lx + b*ly + n*lz);
            if (!accel.occluded(Ray{origin, dir}, radius)) ++open;
        }
        ao.at(x,y) = (float)open / (float)samples;
    }
    return ao;
}
}
