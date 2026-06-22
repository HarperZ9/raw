#pragma once
#include "raw/primitives.hpp"
#include "raw/scene.hpp"
#include "raw/arena_allocator.hpp"
#include <vector>
namespace raw {
struct LinearAccel {
    std::vector<Tri, ArenaAllocator<Tri>> tris;
    void build(const Scene& s, Arena* arena = nullptr);
    bool occluded(const Ray& r, float maxDist) const;
};
}
