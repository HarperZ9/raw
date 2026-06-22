#pragma once
#include "raw/gbuffer.hpp"
#include "raw/scene.hpp"
namespace raw {
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s);
}
