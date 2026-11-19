// The hand-over probe: where the time goes between the D3D11 signal and the D3D11 wake. The
// D3D12 queue gets a list with one timestamp before the pass (after its own wait on the D3D11
// value) and one after the pass's signal. D3D11 and D3D12 timestamps on this GPU are read on one
// clock only if every frame shows them in order: D3D11 copy-in end <= D3D12 head <= D3D12 tail
// <= D3D11 wake; a frame out of order is not counted. Original work, MIT.
#include "BridgeImpl.h"

namespace raw::bridge
{

bool D3D12Bridge::Impl::CreateProbe()
{
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2 * kRing, 0};
    if (FAILED(dev12->CreateQueryHeap(&qd, IID_PPV_ARGS(&probeHeap)))) return false;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = 16 * kRing; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&probeRead)))) return false;
    if (FAILED(dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&probeAlloc)))) return false;
    // Recorded once: each list writes its own query and resolves it; resubmitting a closed list
    // on the one queue is allowed, and the ring keeps a slot from being read while rewritten.
    for (UINT k = 0; k < kRing; ++k) {
        for (int tail = 0; tail < 2; ++tail) {
            ComPtr<ID3D12GraphicsCommandList>& cl = tail ? probeTail[k] : probeHead[k];
            if (FAILED(dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, probeAlloc.Get(), nullptr, IID_PPV_ARGS(&cl)))) return false;
            const UINT q = 2 * k + UINT(tail);
            cl->EndQuery(probeHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, q);
            cl->ResolveQueryData(probeHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, q, 1, probeRead.Get(), 8ull * q);
            if (FAILED(cl->Close())) return false;
        }
    }
    return SUCCEEDED(queue->GetTimestampFrequency(&probeFreq));
}

void D3D12Bridge::Impl::ProbeHead(uint64_t frame, uint64_t waitValue)
{
    if (!probeHeap) return;
    queue->Wait(fence12.Get(), waitValue);   // the pass waits for the same value again; already met then
    ID3D12CommandList* l = probeHead[frame % kRing].Get();
    queue->ExecuteCommandLists(1, &l);
}

void D3D12Bridge::Impl::ProbeTail(uint64_t frame)
{
    if (!probeHeap) return;
    ID3D12CommandList* l = probeTail[frame % kRing].Get();
    queue->ExecuteCommandLists(1, &l);
    probeFrame[frame % kRing] = frame;
}

bool D3D12Bridge::Impl::ProbeRead(uint64_t frame, uint64_t d11Freq, uint64_t t5, uint64_t t6, BridgeTimings* r)
{
    if (!probeHeap || probeFrame[frame % kRing] != frame || !probeFreq || !d11Freq) return false;
    // The tail list resolves after the pass's signal, so it may still run when the D3D11 wake
    // has been read; it is done once the next frame's fence value has been reached.
    void* p = nullptr;
    const D3D12_RANGE range{16 * (frame % kRing), 16 * (frame % kRing) + 16};
    if (FAILED(probeRead->Map(0, &range, &p))) return false;
    uint64_t t[2];
    memcpy(t, static_cast<uint8_t*>(p) + range.Begin, 16);
    const D3D12_RANGE none{0, 0};
    probeRead->Unmap(0, &none);
    const double a = double(t5) * 1e3 / double(d11Freq), d = double(t6) * 1e3 / double(d11Freq);
    const double b = double(t[0]) * 1e3 / double(probeFreq), c = double(t[1]) * 1e3 / double(probeFreq);
    r->probeOk = a <= b && b <= c && c <= d && t[0] && t[1];
    r->wakeInMs = b - a;
    r->d12SpanMs = c - b;
    r->wakeOutMs = d - c;
    return r->probeOk;
}

} // namespace raw::bridge
