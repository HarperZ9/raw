#include "raw/scene.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    // heap scene (default) — unchanged behavior
    Scene heap = buildTestScene(256, 256);
    CHECK(heap.meshes.size() == 2);
    CHECK(heap.lights.size() == 1);
    std::size_t tris = 0;
    for (auto& m : heap.meshes) tris += m.indices.size() / 3;
    CHECK(tris > 0);

    // arena scene — geometry drawn from the arena, identical structure
    std::array<std::uint8_t, 65536> backing{};
    Arena a(backing.data(), backing.size());
    Scene as = buildTestScene(256, 256, &a);
    CHECK(as.meshes.size() == 2);
    CHECK(a.stats().used > 0);                 // geometry consumed arena bytes
    std::size_t tris2 = 0;
    for (auto& m : as.meshes) tris2 += m.indices.size() / 3;
    CHECK(tris2 == tris);                       // same geometry, different memory
    CHECK(as.meshes[0].positions[0].x == heap.meshes[0].positions[0].x);
    return raw_test_summary();
}
