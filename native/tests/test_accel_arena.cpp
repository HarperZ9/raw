#include "raw/accel.hpp"
#include "raw/scene.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    Scene s = buildTestScene(64, 64);          // heap scene is a fine input
    LinearAccel h; h.build(s);                  // heap accel
    CHECK(h.tris.size() > 0);

    std::array<std::uint8_t, 65536> backing{};
    Arena a(backing.data(), backing.size());
    LinearAccel ab; ab.build(s, &a);            // arena accel
    CHECK(ab.tris.size() == h.tris.size());     // same triangles
    CHECK(a.stats().used >= ab.tris.size() * sizeof(Tri));

    // occlusion is identical: a downward ray over the box hits in both
    Ray probe{ Vec3{0,5,0}, Vec3{0,-1,0} };
    CHECK(ab.occluded(probe, 100.0f) == h.occluded(probe, 100.0f));
    return raw_test_summary();
}
