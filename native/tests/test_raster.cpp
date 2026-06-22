#include "raw/raster.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(64,64);
    GBuffer g = rasterize(s, 64, 64);
    CHECK(g.w==64 && g.h==64);
    // at least some pixels covered, and not the whole frame (scene doesn't fill it)
    int covered=0; for (auto m : g.mask.px) covered += m;
    CHECK(covered > 100);
    CHECK(covered < 64*64);
    // covered normals are unit length
    for (int y=0;y<64;++y) for (int x=0;x<64;++x) if (g.mask.at(x,y)){
        float len = length(g.normal.at(x,y));
        CHECK_NEAR(len, 1.0, 1e-3); break; }
    return raw_test_summary();
}
