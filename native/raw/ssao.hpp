#pragma once
#include "raw/gbuffer.hpp"
namespace raw {
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius);
}
