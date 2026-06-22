#include "raw/gbuffer.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    std::array<std::uint8_t, 65536> backing{};
    Arena a(backing.data(), backing.size());
    GBuffer g(&a);
    g.resize(16, 16);
    CHECK(g.w == 16 && g.h == 16);
    // all five planes drew from the arena
    CHECK(a.stats().used >= 16 * 16 * (sizeof(float) + 3 * sizeof(Vec3) + sizeof(std::uint8_t)));
    CHECK(g.mask.at(0, 0) == 0);              // zero-filled
    g.mask.at(1, 1) = 1;  CHECK(g.mask.at(1, 1) == 1);
    g.depth.at(2, 2) = 5.0f;  CHECK(g.depth.at(2, 2) == 5.0f);

    // default GBuffer is still heap and works
    GBuffer h; h.resize(4, 4);
    CHECK(h.w == 4 && h.h == 4);
    return raw_test_summary();
}
