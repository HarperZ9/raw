#pragma once
// RawAsyncCompute.h: Split-Submit Hardware DX12 Async Compute Queue (D3D12_COMMAND_LIST_TYPE_COMPUTE).
// Overlaps Hardware DXR / RayQuery / GI / Water / PDE Compute work concurrently with the Direct 3D graphics queue
// using GPU-side cross-queue ID3D12Fence Signal/Wait with zero CPU stalls. Original work, MIT.
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace raw::bridge
{

class RawAsyncCompute {
public:
    static constexpr uint32_t kFrames = 3u;

    bool Init(ID3D12Device* dev);
    bool Ready() const { return m_ready; }
    ID3D12CommandQueue* Queue() const { return m_queue.Get(); }

    // GPU-side synchronization: wait on graphics queue fence before starting compute, then signal compute completion.
    void GpuWaitOn(ID3D12Fence* graphicsFence, uint64_t value);
    ID3D12GraphicsCommandList* BeginFrame(uint32_t frameSlot);
    uint64_t SubmitAndSignal();
    void GraphicsWaitOnCompute(ID3D12CommandQueue* graphicsQueue, uint64_t computeValue);
    void Shutdown();

private:
    bool                                         m_ready = false;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>   m_queue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_alloc[kFrames];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmd;
    Microsoft::WRL::ComPtr<ID3D12Fence>          m_fence;
    uint64_t                                     m_fenceVal = 0;
    uint64_t                                     m_slotVal[kFrames] = {};
    HANDLE                                       m_event = nullptr;
};

} // namespace raw::bridge
