#include "raw/composite.hpp"
#include <algorithm>
namespace raw {
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s){
    Buffer<Vec3> img; img.resize(g.w,g.h);
    Vec3 ldir = s.lights.empty() ? Vec3{0,-1,0} : s.lights[0].dir;
    float li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ img.at(x,y)={0,0,0}; continue; }
        Vec3 n = g.normal.at(x,y);
        float ndl = std::max(0.0f, dot(n, ldir*-1.0f)) * li;
        float ambient = 0.2f;
        float a = ao.at(x,y);
        float lit = (ambient + ndl) * a;
        Vec3 alb = g.albedo.at(x,y);
        img.at(x,y) = { std::clamp(alb.x*lit,0.0f,1.0f),
                        std::clamp(alb.y*lit,0.0f,1.0f),
                        std::clamp(alb.z*lit,0.0f,1.0f) };
    }
    return img;
}
}
