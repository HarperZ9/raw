#include "raw/ssao.hpp"
#include "raw/raster.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(48,48);
    GBuffer g = rasterize(s,48,48);
    Buffer<float> ao = computeSSAO(g, 16, 2.0f);
    double sum=0; int n=0; float mn=1;
    for (int y=0;y<48;++y) for (int x=0;x<48;++x) if (g.mask.at(x,y)){
        float a=ao.at(x,y); CHECK(a>=0.0f && a<=1.0001f); sum+=a; ++n; if(a<mn)mn=a; }
    CHECK(n>0);
    CHECK(sum/n > 0.0 && sum/n <= 1.0);
    CHECK(mn < 0.99);  // some darkening somewhere
    return raw_test_summary();
}
