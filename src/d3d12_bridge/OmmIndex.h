#pragma once
// OmmIndex: where a micro-triangle sits in an opacity micromap (DXR 1.2), shared by the micromap bake
// (RtOmm) and its GPU check (tests/omm_check).
//
// A level-L micromap splits a triangle into 4^L micro-triangles on a regular barycentric grid of n = 2^L
// steps and lists them along a space-filling curve, which the DXR specification gives only as a picture.
// The order here is read from the GPU itself: tests/omm_check --derive builds micromaps whose bit planes
// spell each index and traces a ray through every micro-triangle, writing OmmTable.h; the plain check then
// places a random pattern with the table and confirms every ray (levels 1 to 5, 1,364 micro-triangles).
// A first attempt at a closed-form curve, written from memory, matched only at levels 1 and 2 and was
// dropped. Original work, MIT.
#include <cstdint>
#include "OmmTable.h"

namespace raw::omm
{
constexpr uint32_t kMaxLevel = 5;

// The centre of micro-triangle k of grid row `row` (k even: upright, odd: inverted), as the hit's
// barycentrics (b.x on vertex 1, b.y on vertex 2).
inline void Centre(uint32_t level, uint32_t row, uint32_t k, float& bx, float& by)
{
    const float n = float(1u << level);
    const uint32_t c = k / 2;
    if ((k & 1u) == 0u) { bx = (3.0f * c + 1.0f) / (3.0f * n); by = (3.0f * row + 1.0f) / (3.0f * n); }
    else { bx = (3.0f * c + 2.0f) / (3.0f * n); by = (3.0f * row + 2.0f) / (3.0f * n); }
}

// A grid micro-triangle's ordinal in Centre order, from a point inside it.
inline uint32_t CellOf(uint32_t level, float bx, float by)
{
    const uint32_t n = 1u << level;
    auto q = [&](float x) { const float y = x * float(n); return uint32_t(y < 0.0f ? 0.0f : (y >= float(n) ? float(n) - 1.0f : y)); };
    const uint32_t iu = q(bx), iv = q(by), iw = q(1.0f - bx - by);
    const bool upright = iu + iv + iw == n - 1u;
    return iv * (2u * n - iv) + (upright ? 2u * iu : 2u * iu + 1u);
}

// The micromap index of grid cell `cell` (Centre order), as the GPU reads it (levels 1 to 5).
inline uint32_t IndexOfCell(uint32_t level, uint32_t cell)
{
    switch (level) {
    case 1: return kCellIndex1[cell];
    case 2: return kCellIndex2[cell];
    case 3: return kCellIndex3[cell];
    case 4: return kCellIndex4[cell];
    case 5: return kCellIndex5[cell];
    default: return cell;
    }
}
} // namespace raw::omm
