#include "raw/scene.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(64, 64);
    CHECK(s.meshes.size() >= 2);          // plane + box
    CHECK(!s.lights.empty());
    // every mesh has indices that are a multiple of 3 and normals per vertex
    for (const auto& m : s.meshes){
        CHECK(m.indices.size() % 3 == 0);
        CHECK(m.normals.size() == m.positions.size());
    }
    CHECK_NEAR(s.camera.aspect, 1.0, 1e-6);
    return raw_test_summary();
}
