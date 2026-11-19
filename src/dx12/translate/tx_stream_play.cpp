// Parallel translation, the worker's side: a segment replayed on a replay context into its own D3D12
// command list (tx_stream.h). The replay context runs the same setters and the same PrepareDraw as the
// immediate context would have; it holds no references (the segment does) and binds each renamed buffer
// at the GPU address the segment recorded. Original work, MIT.
#include "tx_context.h"

namespace raw::tx {

void Context::BeginReplay(const Segment& s)
{
    override_.clear();
    for (const auto& [b, va] : s.va) override_[static_cast<TBuffer*>(b)].va = va;
    s_ = *s.start;   // BindHolds::None: no counts change
    auto fix = [this](BufSlot& c) { c.chunk = c.buf ? Pin(c.buf) : nullptr; };
    for (StageState& st : s_.st) for (BufSlot& c : st.cb) fix(c);
    for (BufSlot& v : s_.vb) fix(v);
    fix(s_.ib);
    dirty_ = D_ALL;
    root_ = croot_ = nullptr;
    pso_ = nullptr;
    cur_ = PsoEntry{};
    memset(cbVa_, 0xFF, sizeof(cbVa_));
    memset(cbBuf_, 0, sizeof(cbBuf_));
    for (auto& k : sampKey_) k = ~0ull;
}

// A buffer renamed on the application's thread inside the segment: what RenameBuffer does to the bindings.
void Context::ReplayChunk(TBuffer* b, uint64_t va)
{
    if (!b) return;
    override_[b].va = va;
    if (b->core.bind & D3D11_BIND_VERTEX_BUFFER) dirty_ |= D_VB;
    if (b->core.bind & D3D11_BIND_INDEX_BUFFER) dirty_ |= D_IB;
}

void Context::EndReplay(Segment& s)
{
    rec_.cl->Close();
    if (descBase_ != ~0u) { rec_.retire.descPages.push_back(descBase_); descBase_ = ~0u; }
    s.out = std::make_unique<Recording>(std::move(rec_));
    rec_ = Recording{};
    rec_.tracker.faults = dev_->faults;
    dev_->OpenRecording(rec_);
    BeginList();
    s_ = State{};   // the segment's objects may die once it is submitted
    override_.clear();
}

namespace {
template <class T> T* P(uint64_t w) { return reinterpret_cast<T*>(w); }
}

void ReplaySegment(Context* c, Segment& s)
{
    c->BeginReplay(s);
    ReplayWords(c, s);
    c->EndReplay(s);
}

void ReplayWords(Context* c, const Segment& s)
{
    const uint64_t* w = s.words.data();
    const uint64_t* end = w + s.words.size();
    void* objs[128];
    while (w < end) {
        const uint64_t h = *w++;
        const auto op = StreamOp(h & 0xFF);
        const UINT stage = UINT(h >> 8 & 0xFF), start = UINT(h >> 16 & 0xFFFF), n = UINT(h >> 32);
        switch (op) {
        case SO_Shader: {
            void* o = P<void>(*w++);
            switch (stage) {
            case 0: c->VSSetShader(static_cast<ID3D11VertexShader*>(static_cast<TShader<ID3D11VertexShader>*>(o)), nullptr, 0); break;
            case 1: c->HSSetShader(static_cast<ID3D11HullShader*>(static_cast<TShader<ID3D11HullShader>*>(o)), nullptr, 0); break;
            case 2: c->DSSetShader(static_cast<ID3D11DomainShader*>(static_cast<TShader<ID3D11DomainShader>*>(o)), nullptr, 0); break;
            case 3: c->GSSetShader(static_cast<ID3D11GeometryShader*>(static_cast<TShader<ID3D11GeometryShader>*>(o)), nullptr, 0); break;
            case 4: c->PSSetShader(static_cast<ID3D11PixelShader*>(static_cast<TShader<ID3D11PixelShader>*>(o)), nullptr, 0); break;
            default: c->CSSetShader(static_cast<ID3D11ComputeShader*>(static_cast<TShader<ID3D11ComputeShader>*>(o)), nullptr, 0); break;
            }
            break;
        }
        case SO_CB: {
            ID3D11Buffer* b[14];
            for (UINT i = 0; i < n; ++i, w += 2) {
                TBuffer* tb = P<TBuffer>(w[0]);
                c->SetReplayVa(tb, w[1]);
                if (i < 14) b[i] = tb;
            }
            const UINT m = n < 14 ? n : 14;
            switch (stage) {
            case 0: c->VSSetConstantBuffers(start, m, b); break;
            case 1: c->HSSetConstantBuffers(start, m, b); break;
            case 2: c->DSSetConstantBuffers(start, m, b); break;
            case 3: c->GSSetConstantBuffers(start, m, b); break;
            case 4: c->PSSetConstantBuffers(start, m, b); break;
            default: c->CSSetConstantBuffers(start, m, b); break;
            }
            break;
        }
        case SO_SRV: case SO_Samp: case SO_CSUAV: {
            const UINT m = n < 128 ? n : 128;
            for (UINT i = 0; i < n; ++i) { void* o = P<void>(*w++); if (i < 128) objs[i] = o; }
            if (op == SO_SRV) {
                auto* v = reinterpret_cast<ID3D11ShaderResourceView**>(objs);
                for (UINT i = 0; i < m; ++i) v[i] = static_cast<TSRV*>(objs[i]);
                switch (stage) {
                case 0: c->VSSetShaderResources(start, m, v); break;
                case 1: c->HSSetShaderResources(start, m, v); break;
                case 2: c->DSSetShaderResources(start, m, v); break;
                case 3: c->GSSetShaderResources(start, m, v); break;
                case 4: c->PSSetShaderResources(start, m, v); break;
                default: c->CSSetShaderResources(start, m, v); break;
                }
            } else if (op == SO_Samp) {
                auto* v = reinterpret_cast<ID3D11SamplerState**>(objs);
                for (UINT i = 0; i < m; ++i) v[i] = static_cast<TSampler*>(objs[i]);
                switch (stage) {
                case 0: c->VSSetSamplers(start, m, v); break;
                case 1: c->HSSetSamplers(start, m, v); break;
                case 2: c->DSSetSamplers(start, m, v); break;
                case 3: c->GSSetSamplers(start, m, v); break;
                case 4: c->PSSetSamplers(start, m, v); break;
                default: c->CSSetSamplers(start, m, v); break;
                }
            } else {
                auto* v = reinterpret_cast<ID3D11UnorderedAccessView**>(objs);
                for (UINT i = 0; i < m; ++i) v[i] = static_cast<TUAV*>(objs[i]);
                c->CSSetUnorderedAccessViews(start, m, v, nullptr);
            }
            break;
        }
        case SO_Layout: c->IASetInputLayout(P<TInputLayout>(*w++)); break;
        case SO_VB: {
            ID3D11Buffer* b[32];
            UINT strides[32], offsets[32];
            const UINT m = n < 32 ? n : 32;
            for (UINT i = 0; i < n; ++i, w += 3) {
                TBuffer* tb = P<TBuffer>(w[0]);
                c->SetReplayVa(tb, w[1]);
                if (i < 32) { b[i] = tb; strides[i] = UINT(w[2]); offsets[i] = UINT(w[2] >> 32); }
            }
            c->IASetVertexBuffers(start, m, b, strides, offsets);
            break;
        }
        case SO_IB: {
            TBuffer* tb = P<TBuffer>(w[0]);
            c->SetReplayVa(tb, w[1]);
            c->IASetIndexBuffer(tb, DXGI_FORMAT(stage), UINT(w[2]));
            w += 3;
            break;
        }
        case SO_Topo: c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY(stage)); break;
        case SO_RT: {
            ID3D11RenderTargetView* r[8] = {};
            const UINT m = n < 8 ? n : 8;
            for (UINT i = 0; i < m; ++i) r[i] = P<TRTV>(*w++);
            ID3D11DepthStencilView* d = P<TDSV>(*w++);
            c->OMSetRenderTargets(n, r, d);
            break;
        }
        case SO_RTUAV: {
            const UINT nUav = UINT(*w++);
            ID3D11RenderTargetView* r[8] = {};
            ID3D11DepthStencilView* d = nullptr;
            ID3D11UnorderedAccessView* u[8] = {};
            if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
                for (UINT i = 0; i < n && i < 8; ++i) r[i] = P<TRTV>(*w++);
                d = P<TDSV>(*w++);
            }
            if (nUav != D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
                for (UINT i = 0; i < nUav && start + i < 8; ++i) u[i] = P<TUAV>(*w++);
            c->OMSetRenderTargetsAndUnorderedAccessViews(n, r, d, start, nUav, u, nullptr);
            break;
        }
        case SO_Blend: {
            TBlend* b = P<TBlend>(w[0]);
            float f[4];
            memcpy(f, &w[1], 16);
            c->OMSetBlendState(b, n ? f : nullptr, UINT(w[3]));
            w += 4;
            break;
        }
        case SO_DS: c->OMSetDepthStencilState(P<TDepthStencil>(*w++), n); break;
        case SO_RS: c->RSSetState(P<TRaster>(*w++)); break;
        case SO_VP:
            if (stage) { c->RSSetViewports(n, reinterpret_cast<const D3D11_VIEWPORT*>(w)); w += n * 3; }
            else c->RSSetViewports(UINT(*w++), nullptr);
            break;
        case SO_SC:
            if (stage) { c->RSSetScissorRects(n, reinterpret_cast<const D3D11_RECT*>(w)); w += n * 2; }
            else c->RSSetScissorRects(UINT(*w++), nullptr);
            break;
        case SO_Chunk: c->ReplayChunk(P<TBuffer>(w[0]), w[1]); w += 2; break;
        case SO_Copy: c->CopyResource(P<ID3D11Resource>(w[0]), P<ID3D11Resource>(w[1])); w += 2; break;
        case SO_CopyRegion: {
            D3D11_BOX box{};
            const uint64_t* a = w + 2;
            if (n) memcpy(&box, a + 3, sizeof box);
            c->CopySubresourceRegion(P<ID3D11Resource>(w[0]), UINT(a[0]), UINT(a[1]), UINT(a[1] >> 32), UINT(a[2]),
                                     P<ID3D11Resource>(w[1]), UINT(a[0] >> 32), n ? &box : nullptr);
            w += 5 + (n ? 3 : 0);
            break;
        }
        case SO_Draw: c->Draw(n, UINT(*w++)); break;
        case SO_DrawIndexed: c->DrawIndexed(n, UINT(*w), INT(uint32_t(*w >> 32))); ++w; break;
        case SO_DrawInstanced: c->DrawInstanced(n, UINT(w[0]), UINT(w[0] >> 32), UINT(w[1])); w += 2; break;
        case SO_DrawIndexedInstanced:
            c->DrawIndexedInstanced(n, UINT(w[0]), UINT(w[0] >> 32), INT(uint32_t(w[1])), UINT(w[1] >> 32));
            w += 2;
            break;
        }
    }
}

}  // namespace raw::tx
