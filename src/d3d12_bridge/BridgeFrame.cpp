// D3D12Bridge per-frame work: depth capture, the fence round trip, and GPU
// timestamps on both queues. Original work, MIT.
#include "BridgeImpl.h"

namespace raw::bridge
{

bool D3D12Bridge::Impl::CpuWait(uint64_t value, DWORD timeoutMs)
{
    if (!fence12) return false;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (fence12->GetCompletedValue() < value) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        fence12->SetEventOnCompletion(value, cpuEvent);
        // The event is auto-reset and may carry a wake from an older wait,
        // so the loop re-reads the fence instead of trusting the wake.
        WaitForSingleObject(cpuEvent, static_cast<DWORD>(deadline - now));
    }
    return true;
}

void D3D12Bridge::Impl::OpenD11Slot(D11Slot& s, uint64_t frame)
{
    if (s.open && s.frame == frame) return;
    if (s.open) ctx11->End(s.disjoint.Get());   // stale, never executed
    ctx11->Begin(s.disjoint.Get());
    s.open = true;
    s.frame = frame;
    s.hasDepth = false;
    s.executed = false;
}

static void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* r,
                       D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    cl->ResourceBarrier(1, &b);
}

bool D3D12Bridge::CaptureDepth(ID3D11ShaderResourceView* depthSrv)
{
    if (!m->ready || !depthSrv) return false;
    ComPtr<ID3D11Resource> res;
    ComPtr<ID3D11Texture2D> tex;
    depthSrv->GetResource(&res);
    if (!res || FAILED(res.As(&tex))) return false;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1)
        return false;

    auto* c = m->ctx11.Get();
    // Save the compute-stage bindings this pass overwrites.
    ComPtr<ID3D11ComputeShader> oldCS;
    ComPtr<ID3D11ShaderResourceView> oldSRV;
    ComPtr<ID3D11UnorderedAccessView> oldUAV;
    UINT numInst = 0;
    c->CSGetShader(&oldCS, nullptr, &numInst);
    c->CSGetShaderResources(0, 1, &oldSRV);
    c->CSGetUnorderedAccessViews(0, 1, &oldUAV);

    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    c->End(s.ts[0].Get());
    ID3D11UnorderedAccessView* uav = m->depthUAV.Get();
    c->CSSetShader(m->depthCS.Get(), nullptr, 0);
    c->CSSetShaderResources(0, 1, &depthSrv);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->Dispatch((d.Width + 7) / 8, (d.Height + 7) / 8, 1);
    c->End(s.ts[1].Get());

    ID3D11ShaderResourceView* srv0 = oldSRV.Get();
    ID3D11UnorderedAccessView* uav0 = oldUAV.Get();
    const UINT keepCount = ~0u;
    c->CSSetUnorderedAccessViews(0, 1, &uav0, &keepCount);
    c->CSSetShaderResources(0, 1, &srv0);
    c->CSSetShader(oldCS.Get(), nullptr, 0);
    s.hasDepth = true;
    return true;
}

bool D3D12Bridge::Execute(ID3D11Resource* colorSrc, ID3D11Resource* colorDst,
                          const PassConstants& kIn)
{
    if (!m->ready || !colorSrc || !colorDst) return false;
    const uint64_t frame = m->frames;
    const UINT slot = static_cast<UINT>(frame % kRing);
    D12Slot& ds = m->d12[slot];
    if (!m->CpuWait(ds.fenceValue, m->cfg.cpuWaitTimeout)) {
        m->log("[D3D12Bridge] D3D12 slot %u did not retire in %u ms; bridge off",
               slot, m->cfg.cpuWaitTimeout);
        m->ready = false;
        return false;
    }

    // Fence values: D3D11 signals v1, the D3D12 queue waits for v1 + skew,
    // D3D12 signals v2, D3D11 waits for v2. Values only ever increase.
    const uint64_t v1 = m->nextValue;
    const uint64_t waitValue = v1 + m->skew;
    const uint64_t v2 = waitValue + 1;
    m->nextValue = v2 + 1;

    PassConstants k = kIn;
    k.dims[0] = static_cast<float>(m->cfg.width);
    k.dims[1] = static_cast<float>(m->cfg.height);
    k.dims[2] = 1.0f / k.dims[0];
    k.dims[3] = 1.0f / k.dims[1];

    // Record the D3D12 list first so recording time never shows up as a
    // stall. Submit it only after the D3D11 Signal: in the game process D3D11
    // rejects Signal(v1) with E_INVALIDARG once D3D12 has a pending Signal(v2).
    auto* cl = m->cl.Get();
    ds.alloc->Reset();
    cl->Reset(ds.alloc.Get(), nullptr);
    cl->EndQuery(m->tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot);
    Transition(cl, m->colorOut.res12.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
    PassIO io{};
    io.inputs = m->srvHeap->GetGPUDescriptorHandleForHeapStart();
    io.output = m->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    io.width = m->cfg.width;
    io.height = m->cfg.height;
    ID3D12DescriptorHeap* heaps[] = {m->srvHeap.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    m->pass->Record(cl, io, k);
    Transition(cl, m->colorOut.res12.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COMMON);
    cl->EndQuery(m->tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot + 1);
    cl->ResolveQueryData(m->tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot, 2,
                         m->tsReadback.Get(), sizeof(uint64_t) * 2 * slot);
    if (FAILED(cl->Close())) {
        m->log("[D3D12Bridge] command list close failed; bridge off");
        m->ready = false;
        return false;
    }

    // D3D11: copy in, hand over, take back, copy out.
    D11Slot& s = m->SlotFor(frame);
    m->OpenD11Slot(s, frame);
    auto* c = m->ctx11.Get();
    c->End(s.ts[2].Get());
    c->CopyResource(m->colorIn.tex11.Get(), colorSrc);
    c->End(s.ts[3].Get());
    HRESULT hs = c->Signal(m->fence11.Get(), v1);
    c->Flush();
    if (FAILED(hs)) {
        // Never queue a GPU wait the other side cannot satisfy.
        m->log("[D3D12Bridge] D3D11 Signal failed 0x%08X; bridge off", (unsigned)hs);
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    ID3D12CommandList* lists[] = {cl};
    m->queue->Wait(m->fence12.Get(), waitValue);
    m->queue->ExecuteCommandLists(1, lists);
    m->queue->Signal(m->fence12.Get(), v2);
    ds.fenceValue = v2;
    m->lastWaited = waitValue;
    m->lastD12 = v2;
    HRESULT hw = c->Wait(m->fence11.Get(), v2);
    if (FAILED(hw)) m->log("[D3D12Bridge] D3D11 Wait failed 0x%08X", (unsigned)hw);
    c->End(s.ts[4].Get());
    c->CopyResource(colorDst, m->colorOut.tex11.Get());
    c->End(s.ts[5].Get());
    c->End(s.disjoint.Get());
    s.open = false;
    s.executed = true;
    m->d12[slot].frameTag = frame;
    ++m->frames;
    return true;
}

template <class T>
static bool GetQuery(ID3D11DeviceContext* c, ID3D11Query* q, T* out, bool block)
{
    const ULONGLONG deadline = GetTickCount64() + 2000;
    for (;;) {
        HRESULT hr = c->GetData(q, out, sizeof(T), block ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK) return true;
        if (FAILED(hr) || !block || GetTickCount64() > deadline) return false;
        Sleep(0);
    }
}

bool D3D12Bridge::ReadTimings(uint64_t frame, bool block, BridgeTimings* out)
{
    if (!m->ready || !out) return false;
    D11Slot& s = m->SlotFor(frame);
    const UINT slot = static_cast<UINT>(frame % kRing);
    D12Slot& ds = m->d12[slot];
    if (s.frame != frame || !s.executed || ds.frameTag != frame) return false;

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (!GetQuery(m->ctx11.Get(), s.disjoint.Get(), &dj, block) || dj.Disjoint) return false;
    uint64_t t[kD11Stamps]{};
    for (UINT i = s.hasDepth ? 0 : 2; i < kD11Stamps; ++i)
        if (!GetQuery(m->ctx11.Get(), s.ts[i].Get(), &t[i], block)) return false;

    if (m->fence12->GetCompletedValue() < ds.fenceValue &&
        (!block || !m->CpuWait(ds.fenceValue, m->cfg.cpuWaitTimeout)))
        return false;
    D3D12_RANGE rr{sizeof(uint64_t) * 2 * slot, sizeof(uint64_t) * 2 * (slot + 1)};
    void* p = nullptr;
    if (FAILED(m->tsReadback->Map(0, &rr, &p))) return false;
    const uint64_t* ts12 = reinterpret_cast<const uint64_t*>(static_cast<uint8_t*>(p) + rr.Begin);
    const uint64_t a = ts12[0], b = ts12[1];
    D3D12_RANGE none{0, 0};
    m->tsReadback->Unmap(0, &none);

    const double f11 = 1000.0 / static_cast<double>(dj.Frequency);
    const double f12 = 1000.0 / static_cast<double>(m->d12Freq);
    BridgeTimings r{};
    r.frame = frame;
    r.depthCopyMs = s.hasDepth ? (t[1] - t[0]) * f11 : 0.0;
    r.copyInMs = (t[3] - t[2]) * f11;
    r.stallMs = (t[4] - t[3]) * f11;
    r.copyOutMs = (t[5] - t[4]) * f11;
    r.passMs = (b - a) * f12;
    r.syncMs = r.stallMs > r.passMs ? r.stallMs - r.passMs : 0.0;
    r.bridgeCostMs = r.depthCopyMs + r.copyInMs + r.copyOutMs + r.syncMs;
    *out = r;
    return true;
}

uint64_t D3D12Bridge::FramesExecuted() const { return m->frames; }
void D3D12Bridge::SetWaitSkew(uint64_t skew) { m->skew = skew; }
bool D3D12Bridge::WaitForD3D12(DWORD timeoutMs) { return m->CpuWait(m->lastD12, timeoutMs); }
void D3D12Bridge::ForceSignal(uint64_t value) { if (m->fence12) m->fence12->Signal(value); }
uint64_t D3D12Bridge::LastD3D12Value() const { return m->lastD12; }
uint64_t D3D12Bridge::LastWaitedValue() const { return m->lastWaited; }
uint64_t D3D12Bridge::CompletedValue() const { return m->fence12 ? m->fence12->GetCompletedValue() : 0; }

} // namespace raw::bridge
