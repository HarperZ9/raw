#include "raw/vec.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Vec3 a{1,2,3}, b{4,5,6};
    CHECK_NEAR(dot(a,b), 32.0, 1e-6);
    Vec3 c = cross(Vec3{1,0,0}, Vec3{0,1,0});
    CHECK_NEAR(c.x,0,1e-6); CHECK_NEAR(c.y,0,1e-6); CHECK_NEAR(c.z,1,1e-6);
    CHECK_NEAR(length(Vec3{3,4,0}), 5.0, 1e-6);
    Vec3 n = normalize(Vec3{0,3,0});
    CHECK_NEAR(n.y, 1.0, 1e-6);
    Vec3 s = (a + b) * 0.5f;
    CHECK_NEAR(s.x, 2.5, 1e-6);
    return raw_test_summary();
}
