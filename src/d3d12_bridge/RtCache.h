#pragma once
// RtCache: a world-space radiance cache for the rays (docs/RAYTRACING.md, "Materials and the radiance
// cache"). Screen-space radiance (the game's frame at the hit) only knows what the camera sees; a ray
// that lands behind the camera or behind a wall used to get a flat guess. This cache keeps, per cell of
// the world, the mean light that arrives there from bounced surfaces (rgb) and the share of the sky the
// cell sees (a), the same two things the sky rays measure per pixel (RtSky.h). It is filled by:
//   - every sky ray a visible pixel traces (the cell of the pixel's surface);
//   - one ray more from each off-screen hit (the cell of the hit), so cells the camera never sees fill
//     too, and a hit lit through the cache passes on light that has bounced more than once.
// An off-screen hit is then shaded as the game shades a surface: its colour copy times the sun (with a
// shadow ray), every point light in reach, the directional ambient times the cell's sky share, and the
// cell's bounce. Cells are cubes of the world (absolute: the camera's place is summed from its per-frame
// step), CacheCell units across near the camera and doubling with distance, one set per facing axis.
// A cell keeps the running mean of up to 32 frames and is freed after 600 frames without a sample.
// The table is 2^20 slots (68 MB). Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>
#include <string>

namespace raw::bridge
{
class RtCache
{
public:
    static constexpr uint32_t kSlots = 1u << 20, kMaxAge = 600, kMaxFrames = 32;
    static constexpr uint32_t Descriptors() { return 3; }   // keys, accumulators, values (raw UAVs, in that order)
    bool Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err);
    uint32_t Descriptor() const { return m_base; }
    void Reset() { m_reset = true; }
    // After the trace: the resolve (or the clear after a Reset), and a copy of the live-cell count.
    void Record(ID3D12GraphicsCommandList4* cl, uint64_t frame, uint32_t slot);
    uint32_t Cells(uint32_t retiredSlot);   // the count a retired frame slot copied

private:
    Microsoft::WRL::ComPtr<ID3D12Device5> m_dev;
    ID3D12DescriptorHeap* m_heap = nullptr;
    uint32_t m_base = 0, m_inc = 0;
    bool m_reset = true;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_keys, m_accum, m_value, m_read;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

} // namespace raw::bridge
