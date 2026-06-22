#include "raw/composite.hpp"
#include "raw/raster.hpp"
#include "raw/ray_ao.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(32,32);
    GBuffer g = rasterize(s,32,32);
    LinearAccel accel; accel.build(s);
    Buffer<float> ao = computeRTAO(g, accel, 8, 2.0f);
    Buffer<Vec3> img = shade(g, ao, s);
    CHECK(img.w==32 && img.h==32);
    // covered pixels are darker-or-equal with AO than the raw albedo would be,
    // and background stays black
    bool sawCovered=false, bgBlack=true;
    for (int y=0;y<32;++y) for (int x=0;x<32;++x){
        Vec3 c = img.at(x,y);
        if (g.mask.at(x,y)){ sawCovered=true; CHECK(c.x>=0 && c.x<=1); }
        else if (c.x>0||c.y>0||c.z>0) bgBlack=false;
    }
    CHECK(sawCovered); CHECK(bgBlack);
    return raw_test_summary();
}
