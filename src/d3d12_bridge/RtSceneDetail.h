#pragma once
// RtScene's internals shared by RtScene.cpp and RtSceneRecord.cpp. Original work, MIT.
#include <d3d12.h>
#include <cstdint>

namespace raw::bridge::rtdetail
{
constexpr uint32_t kMaxInstances = 16384;
constexpr uint32_t kLodLandBit = 0x800000u;   // InstanceID: a distant-land chunk (RtShadow.hlsl, LOD_LAND)
constexpr uint64_t kBlasHeap = 768ull << 20, kScratch = 256ull << 20, kTlas = 32ull << 20, kCb = 64 * 1024;
constexpr uint64_t Align(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

inline D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter = b;
    return x;
}
inline D3D12_RESOURCE_BARRIER Uav(ID3D12Resource* r = nullptr)
{
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    x.UAV.pResource = r;
    return x;
}
} // namespace raw::bridge::rtdetail
