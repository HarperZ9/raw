#include "raw/raster.hpp"
#include "raw/mat.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw {
GBuffer rasterize(const Scene& scene, int w, int h){
    GBuffer g; g.resize(w,h);
    for (auto& d : g.depth.px) d = std::numeric_limits<float>::infinity();
    Mat4 view = scene.camera.view();
    Mat4 proj = scene.camera.proj();
    Mat4 vp = mul(proj, view);
    for (const Mesh& m : scene.meshes){
        for (size_t i=0; i+2 < m.indices.size(); i+=3){
            int i0=m.indices[i], i1=m.indices[i+1], i2=m.indices[i+2];
            Vec3 wp[3] = { m.positions[i0], m.positions[i1], m.positions[i2] };
            Vec3 wn[3] = { m.normals[i0],   m.normals[i1],   m.normals[i2]   };
            // clip-space then perspective divide
            Vec4 cs[3]; Vec3 ndc[3]; float invw[3];
            bool behind=false;
            for (int k=0;k<3;++k){
                cs[k] = mul(vp, Vec4{wp[k].x,wp[k].y,wp[k].z,1});
                if (cs[k].w <= 1e-6f){ behind=true; break; }
                invw[k] = 1.0f/cs[k].w;
                ndc[k] = { cs[k].x*invw[k], cs[k].y*invw[k], cs[k].z*invw[k] };
            }
            if (behind) continue;
            // to screen
            Vec3 sp[3];
            for (int k=0;k<3;++k){
                sp[k].x = (ndc[k].x*0.5f+0.5f)*w;
                sp[k].y = (1.0f-(ndc[k].y*0.5f+0.5f))*h;
                sp[k].z = cs[k].w; // view-space distance proxy (-z_view = w)
            }
            int minx = std::max(0, (int)std::floor(std::min({sp[0].x,sp[1].x,sp[2].x})));
            int maxx = std::min(w-1, (int)std::ceil (std::max({sp[0].x,sp[1].x,sp[2].x})));
            int miny = std::max(0, (int)std::floor(std::min({sp[0].y,sp[1].y,sp[2].y})));
            int maxy = std::min(h-1, (int)std::ceil (std::max({sp[0].y,sp[1].y,sp[2].y})));
            float area = (sp[1].x-sp[0].x)*(sp[2].y-sp[0].y) - (sp[1].y-sp[0].y)*(sp[2].x-sp[0].x);
            if (std::fabs(area) < 1e-9f) continue;
            for (int y=miny;y<=maxy;++y) for (int x=minx;x<=maxx;++x){
                float px=x+0.5f, py=y+0.5f;
                float w0 = ((sp[1].x-px)*(sp[2].y-py)-(sp[1].y-py)*(sp[2].x-px))/area;
                float w1 = ((sp[2].x-px)*(sp[0].y-py)-(sp[2].y-py)*(sp[0].x-px))/area;
                float w2 = 1.0f - w0 - w1;
                if (w0<0||w1<0||w2<0) continue;
                // perspective-correct interpolation
                float iw = w0*invw[0] + w1*invw[1] + w2*invw[2];
                float depth = (w0*sp[0].z + w1*sp[1].z + w2*sp[2].z); // screen-linear interpolation of view distance (correct for non-overlapping test scene; true depth-buffer would interpolate 1/w—deferred)
                if (depth >= g.depth.at(x,y)) continue;
                auto pc = [&](Vec3 a,Vec3 b,Vec3 c){
                    Vec3 r{ (w0*a.x*invw[0]+w1*b.x*invw[1]+w2*c.x*invw[2])/iw,
                            (w0*a.y*invw[0]+w1*b.y*invw[1]+w2*c.y*invw[2])/iw,
                            (w0*a.z*invw[0]+w1*b.z*invw[1]+w2*c.z*invw[2])/iw }; return r; };
                g.depth.at(x,y) = depth;
                g.position.at(x,y) = pc(wp[0],wp[1],wp[2]);
                g.normal.at(x,y) = normalize(pc(wn[0],wn[1],wn[2]));
                g.albedo.at(x,y) = m.material.albedo;
                g.mask.at(x,y) = 1;
            }
        }
    }
    return g;
}
}
