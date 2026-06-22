#pragma once
#include "raw/primitives.hpp"
#include "raw/scene.hpp"
#include <vector>
namespace raw {
struct LinearAccel {
    std::vector<Tri> tris;
    void build(const Scene& s);
    bool occluded(const Ray& r, float maxDist) const;
};
}
