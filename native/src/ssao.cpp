#include "raw/ssao.hpp"
#include <cmath>
#include <cstdint>
namespace raw {
static float hash01(uint32_t x, uint32_t y, uint32_t s){
    uint32_t h = x*374761393u + y*668265263u + s*2246822519u;
    h = (h ^ (h>>13)) * 1274126177u; h ^= h>>16;
    return (h & 0xFFFFFFu) / float(0x1000000);
}
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius, Arena* arena){
    Buffer<float> ao(arena); ao.resize(g.w,g.h);
    const int R = 6; // screen-space sampling radius in pixels
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ ao.at(x,y)=1.0f; continue; }
        Vec3 p = g.position.at(x,y);
        Vec3 n = g.normal.at(x,y);
        int occ=0, used=0;
        for (int sidx=0; sidx<samples; ++sidx){
            float a = hash01(x,y,(uint32_t)sidx)*6.2831853f;
            float rad = (0.3f + 0.7f*hash01(x,y,(uint32_t)(sidx+97)))*R;
            int sx = x + (int)std::lround(std::cos(a)*rad);
            int sy = y + (int)std::lround(std::sin(a)*rad);
            if (sx<0||sy<0||sx>=g.w||sy>=g.h) continue;
            if (!g.mask.at(sx,sy)) continue;
            ++used;
            Vec3 d = g.position.at(sx,sy) - p;
            float dist = length(d);
            if (dist < 1e-4f || dist > radius) continue;
            float ndl = dot(n, normalize(d));
            if (ndl > 0.15f) ++occ; // neighbor rises into the hemisphere => occluding
        }
        float a = used>0 ? 1.0f - (float)occ/(float)used : 1.0f;
        ao.at(x,y) = a < 0 ? 0 : a;
    }
    return ao;
}
}
