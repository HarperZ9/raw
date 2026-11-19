#pragma once
// RtSky: how much of the sky each pixel sees, from the game's own geometry (docs/RAYTRACING.md).
//
// The game's ambient is one directional term per cell (sky above, ground below). Under eaves, in
// alleys, below trees and inside the bend of a rock it is just as bright as on an open field. The
// shadow trace (RtShadow.hlsl) also sends one ray a pixel into the upper hemisphere around the
// surface, cosine-weighted, alpha-tested like the shadow rays; a ray that leaves the scene within
// skyLength sees sky. Rays below the horizon count as seen: they stand for the ground term, light
// that bounced, which the game's ambient carries itself. One ray is noise, so RtSkyFilter.hlsl
// accumulates it over frames along the game's motion vectors (rejecting history at another depth)
// with a depth-aware spatial mean while the history is short, and writes the result into the alpha
// of the light-index output, which the lighting pass reads.
//
// One bounce of indirect light rides on the same ray. A sky ray that hits geometry meets a surface
// the game has already lit this frame: where that point is on screen and not hidden, its colour in
// the game's frame is the light it sends back (screen-space radiance, ray-traced visibility).
// Off screen or hidden, a dim estimate from the cell's ambient stands in. The bounce is filtered with
// the visibility and goes to its own output; the lighting pass adds it to the exterior ambient, which
// the visibility has already reduced where the sky is blocked. Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>
#include <string>

namespace raw::bridge
{
class RtSky
{
public:
    // raw, A, B | raw, B, A (two parities) | bounce A, B, out | B, A, out | motion | the game's colour
    static constexpr uint32_t Descriptors() { return 14; }
    // base: the first of this pass's descriptors in the shader-visible heap; the heap's descriptors 0
    // (depth) and 4 (the light-index output, as a UAV) are the shadow trace's.
    bool Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, UINT width, UINT height, std::string& err);
    uint32_t RawDescriptor() const { return m_base; }
    uint32_t ColourDescriptor() const { return m_base + 13; }
    ID3D12Resource* Bounce() const { return m_bounce.Get(); }   // RGBA16F, left in COMMON
    void SetColour(ID3D12Resource* colour);                       // the game's frame, for the trace
    // After the trace: the filter, which writes the light-index output's alpha.
    void Record(ID3D12GraphicsCommandList4* cl, D3D12_GPU_VIRTUAL_ADDRESS uniform, ID3D12Resource* motion, uint64_t frame,
                float strength);
    void Reset() { m_reset = true; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device5> m_dev;
    ID3D12DescriptorHeap* m_heap = nullptr;
    uint32_t m_base = 0, m_inc = 0;
    UINT m_w = 0, m_h = 0;
    bool m_reset = true;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_raw, m_hist[2], m_bhist[2], m_bounce;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

} // namespace raw::bridge
