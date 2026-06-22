#pragma once
#include "raw/gbuffer.hpp"
#include "raw/arena.hpp"
namespace raw {
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius, Arena* arena = nullptr);
}
