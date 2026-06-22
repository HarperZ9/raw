#include "raw/primitives.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Tri tri{{-1,-1,0},{1,-1,0},{0,1,0}};
    float t,u,v;
    // ray from +z toward -z hits the triangle at t=5
    CHECK(intersectTri(Ray{{0,0,5},{0,0,-1}}, tri, t,u,v));
    CHECK_NEAR(t, 5.0, 1e-4);
    // ray that misses
    CHECK(!intersectTri(Ray{{5,5,5},{0,0,-1}}, tri, t,u,v));
    AABB box{{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
    box.grow({1,2,3}); box.grow({-1,0,5});
    CHECK_NEAR(box.mn.x,-1,1e-6); CHECK_NEAR(box.mx.z,5,1e-6);
    return raw_test_summary();
}
