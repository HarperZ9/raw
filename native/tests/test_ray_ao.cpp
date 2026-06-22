#include "raw/ray_ao.hpp"
#include "raw/raster.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(48,48);
    GBuffer g = rasterize(s,48,48);
    LinearAccel accel; accel.build(s);
    Buffer<float> ao = computeRTAO(g, accel, 32, 2.0f);
    // average AO over covered pixels is in (0,1] and not fully open
    double sum=0; int n=0; float mn=1;
    for (int y=0;y<48;++y) for (int x=0;x<48;++x) if (g.mask.at(x,y)){
        float a=ao.at(x,y); sum+=a; ++n; if(a<mn)mn=a; }
    CHECK(n>0);
    double avg = sum/n;
    CHECK(avg > 0.0 && avg <= 1.0);
    CHECK(mn < 0.95);   // the crease where box meets plane is occluded
    return raw_test_summary();
}
