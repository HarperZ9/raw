#pragma once
#include "raw/vec.hpp"
#include "raw/image.hpp"
#include <cstdint>
namespace raw {
struct GBuffer {
    int w{0}, h{0};
    Buffer<float> depth;
    Buffer<Vec3> normal, position, albedo;
    Buffer<uint8_t> mask;
    void resize(int W,int H){ w=W;h=H;
        depth.resize(W,H); normal.resize(W,H); position.resize(W,H);
        albedo.resize(W,H); mask.resize(W,H); }
};
}
