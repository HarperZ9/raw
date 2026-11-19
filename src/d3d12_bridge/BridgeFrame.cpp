// D3D12Bridge per-frame work: depth capture, the fence round trip, and GPU
// timestamps on both queues. Original work, MIT.
#include "BridgeImpl.h"
#include "LightingPass.h"

namespace raw::bridge
{

bool D3D12Bridge::Impl::CpuWait(uint64_t value, DWORD timeoutMs)
{
    return WaitFence(fence12.Get(), value, cpuEvent, timeoutMs);
}

void D3D12Bridge::Impl::OpenD11Slot(D11Slot& s, uint64_t frame)
{
    if (s.open && s.frame == frame) return;
    if (s.open) ctx11->End(s.disjoint.Get());   // stale, never executed
    ctx11->Begin(s.disjoint.Get());
    s.open = true;
    s.frame = frame;
    s.hasDepth = false;
    s.hasNormal = false;
    s.hasMotion = false;
    s.hasSurface = false;
    s.hasShadows = false;
    s.executed = false;
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

bool D3D12Bridge::CaptureNormal(ID3D11Texture2D* normalTex)
{
    if (!m->ready || !normalTex || !m->normalIn.tex11) return false;
    D3D11_TEXTURE2D_DESC d{};
    normalTex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1) return false;
    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    auto* c = m->ctx11.Get();
    c->End(s.ts[2].Get());
    c->CopyResource(m->normalIn.tex11.Get(), normalTex);
    if (m->pendingMotion) {
        c->CopyResource(m->motionIn.tex11.Get(), m->pendingMotion);
        m->pendingMotion = nullptr;
        s.hasMotion = true;
    }
    c->End(s.ts[3].Get());
    s.hasNormal = true;
    return true;
}

bool D3D12Bridge::CaptureMotion(ID3D11Texture2D* motionTex)
{
    if (!m->ready || !motionTex || !m->motionIn.tex11) return false;
    D3D11_TEXTURE2D_DESC d{};
    motionTex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1 || d.Format != m->cfg.motionFormat) return false;
    m->pendingMotion = motionTex;
    return true;
}

void D3D12Bridge::SetCamera(const CameraConstants& camera) { m->camera = camera; }

void D3D12Bridge::SetRtDraws(std::vector<RtDraw>&& draws)
{
    for (RtDraw& d : m->pendingDraws) { if (d.vb) d.vb->Release(); if (d.ib) d.ib->Release(); }
    m->pendingDraws = std::move(draws);
    if (!m->rt) {
        for (RtDraw& d : m->pendingDraws) { if (d.vb) d.vb->Release(); if (d.ib) d.ib->Release(); }
        m->pendingDraws.clear();
    }
}
void D3D12Bridge::TuneRt(const RtTuning& t) { m->rtTune = t; if (m->rt) m->rt->Tune(t); }
bool D3D12Bridge::RtStats(RtFrameStats* out)
{
    if (!m->rt || !out) return false;
    *out = m->rt->Stats();
    return true;
}

// Stores the pass's newest retired timings under the frame they belong to.
void D3D12Bridge::Impl::HarvestPassTimings()
{
    PassTimings pt;
    if (!pass || !pass->LastTimings(&pt)) return;
    D12Slot& ds = d12[pt.submission % kRing];
    ds.frameTag = pt.submission;
    ds.pass = pt;
    ds.havePass = true;
}

bool D3D12Bridge::Execute(ID3D11Resource* colorSrc, ID3D11Resource* colorDst,
                          const PassConstants& kIn)
{
    if (!m->ready || !colorSrc || !colorDst) return false;
    const uint64_t frame = m->frames;

    // Fence values: D3D11 signals v1, the D3D12 queue waits for v1 + skew,
    // D3D12 signals v2, D3D11 waits for v2. Values only ever increase.
    const uint64_t v1 = m->nextValue;
    const uint64_t waitValue = v1 + m->skew;
    const uint64_t v2 = waitValue + 1;

    D11Slot& s = m->SlotFor(frame);
    m->OpenD11Slot(s, frame);
    FrameConstants fc;
    fc.k = kIn;
    fc.k.dims[0] = static_cast<float>(m->cfg.width);
    fc.k.dims[1] = static_cast<float>(m->cfg.height);
    fc.k.dims[2] = 1.0f / fc.k.dims[0];
    fc.k.dims[3] = 1.0f / fc.k.dims[1];
    fc.camera = m->camera;
    fc.hasNormal = s.hasNormal;
    fc.hasMotion = s.hasMotion;
    fc.rt = m->attached && m->rt && m->rt->Active() && m->cfg.lightingPass;
    if (m->cfg.lightingPass) {
        fc.lighting = &m->lighting;
        fc.hasSurface = s.hasSurface;
        fc.hasShadows = s.hasShadows;
    }

    // The pass records first, so recording time never shows up as a stall,
    // and submits only after the D3D11 Signal: in the game process D3D11
    // rejects Signal(v1) with E_INVALIDARG once D3D12 has a pending Signal(v2).
    if (!m->pass->Record(fc)) {
        // The frame before may still have a D3D11 Wait queued for a value the
        // pass will now never signal. Signal it from the CPU, or the game's
        // D3D11 queue stays blocked for good.
        m->log("[D3D12Bridge] pass '%s' failed to record; releasing fence value %llu and turning the bridge off",
               m->pass->Name(), (unsigned long long)m->lastD12);
        if (m->fence12 && m->fence12->GetCompletedValue() < m->lastD12) m->fence12->Signal(m->lastD12);
        m->ready = false;
        return false;
    }
    m->HarvestPassTimings();   // recording may have retired the frame before
    m->nextValue = v2 + 1;

    // D3D11: copy in, hand over, take back, copy out.
    auto* c = m->ctx11.Get();
    c->End(s.ts[4].Get());
    c->CopyResource(m->colorIn.tex11.Get(), colorSrc);
    c->End(s.ts[5].Get());
    if (m->attached) {
        // One queue: the layer submits what D3D11 recorded so far with every shared texture in
        // COMMON, the pass follows on the same queue, and the copy-out lands in the layer's next list.
        std::vector<ID3D11Resource*> hand;
        bool rtOk = false;
        if (fc.rt) {
            m->rt->SetDraws(std::move(m->pendingDraws), hand, m->cfg.layer.native);
            const void* u = nullptr;
            size_t ub = 0;
            if (LightingPassUniform(m->pass.get(), &u, &ub))
                rtOk = m->rt->Record(u, ub, m->depthIn.res12.Get(), m->normalIn.res12.Get(),
                                     m->shared[static_cast<UINT>(Shared::Specular)].res12.Get());
        }
        if (!m->HandToHost(hand) || (rtOk && !m->rt->Submit(m->queue.Get())) || !m->pass->Submit(0, v2)) {
            m->log("[D3D12Bridge] attached hand-over failed; bridge off");
            c->End(s.disjoint.Get());
            s.open = false;
            m->ready = false;
            return false;
        }
        m->lastWaited = 0;
        m->lastD12 = v2;
        c->End(s.ts[6].Get());
        c->CopyResource(colorDst, m->colorOut.tex11.Get());
        c->End(s.ts[7].Get());
        c->End(s.disjoint.Get());
        s.open = false;
        s.executed = true;
        ++m->frames;
        return true;
    }
    HRESULT hs = m->ctx4->Signal(m->fence11.Get(), v1);
    c->Flush();
    LARGE_INTEGER qf, q0, q1;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    if (FAILED(hs)) {
        // Never queue a GPU wait the other side cannot satisfy.
        m->log("[D3D12Bridge] D3D11 Signal failed 0x%08X; bridge off", (unsigned)hs);
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    m->ProbeHead(frame, waitValue);
    if (!m->pass->Submit(waitValue, v2)) {
        // Nothing will signal v2 now, so D3D11 must not wait for it.
        m->log("[D3D12Bridge] pass '%s' failed to submit; bridge off", m->pass->Name());
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    m->ProbeTail(frame);
    QueryPerformanceCounter(&q1);
    // Already executed on the GPU when the D3D12 list was only now submitted: the queue idles on the CPU.
    m->caughtUp += c->GetData(s.ts[5].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
    m->flushToSubmitUs += double(q1.QuadPart - q0.QuadPart) * 1e6 / double(qf.QuadPart);
    if (++m->handOvers == 300) {
        m->log("[D3D12Bridge] hand-over over 300 frames: the GPU had already reached it at submit in %u, CPU flush-to-submit mean %.1f us",
               m->caughtUp, m->flushToSubmitUs / 300.0);
        m->caughtUp = m->handOvers = 0;
        m->flushToSubmitUs = 0;
    }
    m->lastWaited = waitValue;
    m->lastD12 = v2;
    HRESULT hw = m->ctx4->Wait(m->fence11.Get(), v2);
    if (FAILED(hw)) m->log("[D3D12Bridge] D3D11 Wait failed 0x%08X", (unsigned)hw);
    c->End(s.ts[6].Get());
    c->CopyResource(colorDst, m->colorOut.tex11.Get());
    c->End(s.ts[7].Get());
    if (m->cfg.handoverFlush) c->Flush();
    c->End(s.disjoint.Get());
    s.open = false;
    s.executed = true;
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
    D12Slot& ds = m->d12[frame % kRing];
    if (s.frame != frame || !s.executed) return false;

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (!GetQuery(m->ctx11.Get(), s.disjoint.Get(), &dj, block) || dj.Disjoint) return false;
    uint64_t t[kD11Stamps]{};
    for (UINT i = 0; i < kD11Stamps; ++i) {
        if ((i < 2 && !s.hasDepth) || (i >= 2 && i < 4 && !s.hasNormal)) continue;
        if (!GetQuery(m->ctx11.Get(), s.ts[i].Get(), &t[i], block)) return false;
    }

    m->HarvestPassTimings();
    if (!(ds.havePass && ds.frameTag == frame) && block) {
        if (!m->pass->Retire(m->cfg.cpuWaitTimeout)) return false;
        m->HarvestPassTimings();
    }
    if (!(ds.havePass && ds.frameTag == frame)) return false;

    const double f11 = 1000.0 / static_cast<double>(dj.Frequency);
    BridgeTimings r{};
    r.frame = frame;
    r.depthCopyMs = s.hasDepth ? (t[1] - t[0]) * f11 : 0.0;
    r.normalCopyMs = s.hasNormal ? (t[3] - t[2]) * f11 : 0.0;
    r.copyInMs = (t[5] - t[4]) * f11;
    r.stallMs = (t[6] - t[5]) * f11;
    r.copyOutMs = (t[7] - t[6]) * f11;
    r.pass = ds.pass;
    r.passMs = ds.pass.totalMs;
    r.syncMs = r.stallMs > r.passMs ? r.stallMs - r.passMs : 0.0;
    r.bridgeCostMs = r.depthCopyMs + r.normalCopyMs + r.copyInMs + r.copyOutMs + r.syncMs;
    m->ProbeRead(frame, dj.Frequency, t[5], t[6], &r);
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
