// RawAsyncCompute.cpp: Hardware DX12 Async Compute Queue & GPU-side cross-queue fence sync.
// Original work, MIT.
#include "RawAsyncCompute.h"

namespace raw::bridge
{

bool RawAsyncCompute::Init(ID3D12Device* dev)
{
    if (!dev) return false;
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)))) return false;
    for (uint32_t i = 0; i < kFrames; ++i) {
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_alloc[i])))) return false;
    }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_cmd)))) return false;
    m_cmd->Close();
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_ready = true;
    return true;
}

void RawAsyncCompute::GpuWaitOn(ID3D12Fence* graphicsFence, uint64_t value)
{
    if (m_ready && graphicsFence && value > 0) m_queue->Wait(graphicsFence, value);
}

ID3D12GraphicsCommandList* RawAsyncCompute::BeginFrame(uint32_t frameSlot)
{
    if (!m_ready) return nullptr;
    const uint32_t s = frameSlot % kFrames;
    if (m_slotVal[s] > 0 && m_fence->GetCompletedValue() < m_slotVal[s] && m_event) {
        m_fence->SetEventOnCompletion(m_slotVal[s], m_event);
        WaitForSingleObject(m_event, 2000);
    }
    m_alloc[s]->Reset();
    m_cmd->Reset(m_alloc[s].Get(), nullptr);
    return m_cmd.Get();
}

uint64_t RawAsyncCompute::SubmitAndSignal()
{
    if (!m_ready) return 0;
    m_cmd->Close();
    ID3D12CommandList* lists[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, lists);
    const uint64_t v = ++m_fenceVal;
    m_queue->Signal(m_fence.Get(), v);
    m_slotVal[v % kFrames] = v;
    return v;
}

void RawAsyncCompute::GraphicsWaitOnCompute(ID3D12CommandQueue* graphicsQueue, uint64_t computeValue)
{
    if (m_ready && graphicsQueue && computeValue > 0) graphicsQueue->Wait(m_fence.Get(), computeValue);
}

void RawAsyncCompute::Shutdown()
{
    if (m_ready && m_fenceVal > 0 && m_fence && m_fence->GetCompletedValue() < m_fenceVal && m_event) {
        m_fence->SetEventOnCompletion(m_fenceVal, m_event);
        WaitForSingleObject(m_event, 2000);
    }
    if (m_event) { CloseHandle(m_event); m_event = nullptr; }
    m_cmd.Reset();
    for (auto& a : m_alloc) a.Reset();
    m_fence.Reset();
    m_queue.Reset();
    m_ready = false;
}

} // namespace raw::bridge
