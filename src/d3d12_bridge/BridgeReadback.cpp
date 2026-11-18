// D3D12-side reads for captures and tests: what a shared texture holds as the
// D3D12 device sees it, and the engine pass's visibility. Original work, MIT.
#include "BridgeImpl.h"

namespace raw::bridge
{

bool D3D12Bridge::TuneEngine(const EngineTuning& t) { return m->pass && EnginePassTune(m->pass.get(), t); }

bool D3D12Bridge::ReadEngineVisibility(std::vector<float>* out)
{
    return m->ready && out && EnginePassReadVisibility(m->pass.get(), out);
}

bool D3D12Bridge::ReadShared(Shared which, std::vector<uint8_t>* out, UINT* rowBytes)
{
    ID3D12Resource* tex = m->shared[static_cast<UINT>(which)].res12.Get();
    if (!m->ready || !tex || !out) return false;
    // Everything queued so far, on both devices, ends first.
    m->ctx11->Flush();
    if (!m->pass->Retire(m->cfg.cpuWaitTimeout)) return false;
    return m->ReadTexture12(tex, 0, out, rowBytes);
}

// Copies a D3D12 texture into a readback buffer on the bridge's queue. With
// waitValue the queue first waits for the shared fence to reach it.
bool D3D12Bridge::Impl::ReadTexture12(ID3D12Resource* tex, uint64_t waitValue, std::vector<uint8_t>* out, UINT* rowBytes)
{
    ID3D12Device* dev = dev12.Get();
    auto* m = this;
    if (!m->readList) {
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m->readAlloc))) ||
            FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m->readAlloc.Get(), nullptr,
                                          IID_PPV_ARGS(&m->readList))) ||
            FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m->readFence))))
            return false;
        m->readList->Close();
    }

    const D3D12_RESOURCE_DESC td = tex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    UINT64 rowSize = 0, total = 0;
    dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, &rows, &rowSize, &total);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buf;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                            nullptr, IID_PPV_ARGS(&buf))))
        return false;

    auto* cl = m->readList.Get();
    m->readAlloc->Reset();
    cl->Reset(m->readAlloc.Get(), nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = tex;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = buf.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cl->ResourceBarrier(1, &b);
    if (FAILED(cl->Close())) return false;
    ID3D12CommandList* lists[] = {cl};
    if (waitValue) m->queue->Wait(m->fence12.Get(), waitValue);
    m->queue->ExecuteCommandLists(1, lists);
    m->queue->Signal(m->readFence.Get(), ++m->readValue);
    if (!WaitFence(m->readFence.Get(), m->readValue, m->cpuEvent, m->cfg.cpuWaitTimeout)) return false;

    void* p = nullptr;
    if (FAILED(buf->Map(0, nullptr, &p))) return false;
    out->resize(size_t(rowSize) * rows);
    for (UINT y = 0; y < rows; ++y)
        memcpy(out->data() + size_t(y) * rowSize, static_cast<const uint8_t*>(p) + size_t(y) * fp.Footprint.RowPitch,
               size_t(rowSize));
    D3D12_RANGE none{0, 0};
    buf->Unmap(0, &none);
    if (rowBytes) *rowBytes = static_cast<UINT>(rowSize);
    return true;
}

} // namespace raw::bridge
