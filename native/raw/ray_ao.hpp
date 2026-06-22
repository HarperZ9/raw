#pragma once
#include "raw/gbuffer.hpp"
#include "raw/accel.hpp"
#include "raw/arena.hpp"
namespace raw {
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius, Arena* arena = nullptr);
}
