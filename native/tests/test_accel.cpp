#include "raw/accel.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(8,8);
    LinearAccel accel; accel.build(s);
    CHECK(accel.tris.size() > 10);
    // a ray straight down from high above the box origin is occluded by the box top
    CHECK(accel.occluded(Ray{{0,10,0},{0,-1,0}}, 100.0f));
    // a ray pointing up into empty sky from above everything is not occluded
    CHECK(!accel.occluded(Ray{{0,10,0},{0,1,0}}, 100.0f));
    return raw_test_summary();
}
