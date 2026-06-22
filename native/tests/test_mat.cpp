#include "raw/mat.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    // translate moves a point
    Vec4 p = mul(translate({10,0,0}), Vec4{1,2,3,1});
    CHECK_NEAR(p.x, 11, 1e-5); CHECK_NEAR(p.y, 2, 1e-5);
    // lookAt: a point in front of the camera maps to negative view-space z
    Mat4 view = lookAt({0,0,5}, {0,0,0}, {0,1,0});
    Vec4 v = mul(view, Vec4{0,0,0,1});
    CHECK_NEAR(v.z, -5, 1e-4);
    // perspective: a point on the near plane center projects near NDC z in [-1,1]
    Mat4 proj = perspective(1.5708f, 1.0f, 0.1f, 100.0f);
    Vec4 c = mul(proj, Vec4{0,0,-1,1});
    CHECK(c.w > 0); // w = -view.z = 1
    return raw_test_summary();
}
