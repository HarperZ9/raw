// The D3D11 state setters. They only update the shadow state and dirty flags;
// D3D12 state is written at the next draw.
#include "tx_context.h"

namespace raw::tx {

template <class T>
void Context::SetShader(int stage, T* s)
{
    ShaderSlot& slot = s_.st[stage].sh;
    if (slot.obj == s) return;
    if (seg_) { SHead(SO_Shader, uint32_t(stage)); SObj(s); }
    T* cur = static_cast<T*>(slot.obj);   // a stage binds one shader type
    holds_.Assign(cur, s);
    slot.obj = cur;
    slot.sd = s ? s->sd : nullptr;
    slot.drop = s ? [](BindHolds& h, void* p) { T* t = static_cast<T*>(p); h.Assign<T>(t, nullptr); } : nullptr;
    slot.hold = s ? &s->imm : nullptr;
    dirty_ |= D_PSO;
}

void Context::SetCB(int stage, UINT start, UINT n, ID3D11Buffer* const* b)
{
    if (seg_) {
        SHead(SO_CB, uint32_t(stage), start, n);
        for (UINT i = 0; i < n; ++i) SBuf(b ? static_cast<TBuffer*>(b[i]) : nullptr);
    }
    for (UINT i = 0; i < n && start + i < 14; ++i) {
        BufSlot& s = s_.st[stage].cb[start + i];
        TBuffer* nb = b ? static_cast<TBuffer*>(b[i]) : nullptr;
        holds_.Assign(s.buf, nb);
        s.chunk = nb ? Pin(nb) : nullptr;
    }
}

static bool Overlap(const SubRange& a, const SubRange& b)
{
    if (!(a.planeMask & b.planeMask)) return false;
    if (a.all || b.all) return true;
    return a.mip0 < b.mip0 + b.mipN && b.mip0 < a.mip0 + a.mipN &&
           a.slice0 < b.slice0 + b.sliceN && b.slice0 < a.slice0 + a.sliceN;
}

// D3D11 rule: a resource bound for writing cannot also be bound as a shader
// input; the input side is bound as null.
bool Context::TargetConflict(const TSRV* v) const
{
    for (uint32_t i = 0; i < s_.rtCount; ++i)
        if (s_.rtv[i] && s_.rtv[i]->core == v->core && Overlap(s_.rtv[i]->range, v->range)) return true;
    const UINT ro = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    if (s_.dsv && s_.dsv->core == v->core && (s_.dsv->desc.Flags & ro) == 0) return true;
    return false;
}

void Context::UnbindSrvsOf(const ResCore* core, const SubRange& range)
{
    for (int st = 0; st < S_COUNT; ++st) {
        StageState& s = s_.st[st];
        for (uint32_t i = 0; i < s.srvHi; ++i) {
            if (!s.srv[i] || s.srv[i]->core != core || !Overlap(range, s.srv[i]->range)) continue;
            holds_.Assign<TSRV>(s.srv[i], nullptr);
            dirty_ |= D_SRV0 << st;
        }
    }
}

void Context::SetSRV(int stage, UINT start, UINT n, ID3D11ShaderResourceView* const* v)
{
    if (seg_) {
        SHead(SO_SRV, uint32_t(stage), start, n);
        for (UINT i = 0; i < n; ++i) SObj(v ? static_cast<TSRV*>(v[i]) : nullptr);
    }
    StageState& s = s_.st[stage];
    for (UINT i = 0; i < n && start + i < 128; ++i) {
        TSRV* nv = v ? static_cast<TSRV*>(v[i]) : nullptr;
        if (nv && stage != S_CS && TargetConflict(nv)) nv = nullptr;
        if (s.srv[start + i] == nv) continue;
        holds_.Assign(s.srv[start + i], nv);
        if (nv && start + i + 1 > s.srvHi) s.srvHi = start + i + 1;
        dirty_ |= (D_SRV0 << stage) | (stage < S_CS ? D_SCAN_SRV0 << stage : 0u);
        if (start + i < 64) s.srvScan |= 1ull << (start + i);
        else dirty_ |= D_SCAN_ALLSRV;
    }
}

void Context::SetSamp(int stage, UINT start, UINT n, ID3D11SamplerState* const* p)
{
    if (seg_) {
        SHead(SO_Samp, uint32_t(stage), start, n);
        for (UINT i = 0; i < n; ++i) SObj(p ? static_cast<TSampler*>(p[i]) : nullptr);
    }
    for (UINT i = 0; i < n && start + i < 16; ++i) {
        TSampler* ns = p ? static_cast<TSampler*>(p[i]) : nullptr;
        if (s_.st[stage].samp[start + i] == ns) continue;
        holds_.Assign(s_.st[stage].samp[start + i], ns);
        dirty_ |= D_SAMP0 << stage;
    }
}

#define TX_STAGE_SET(P, I, S)                                                                                  \
    STDMETHODIMP_(void) Context::P##SetConstantBuffers(UINT a, UINT n, ID3D11Buffer* const* b) { SetCB(S, a, n, b); } \
    STDMETHODIMP_(void) Context::P##SetShaderResources(UINT a, UINT n, ID3D11ShaderResourceView* const* v) { SetSRV(S, a, n, v); } \
    STDMETHODIMP_(void) Context::P##SetSamplers(UINT a, UINT n, ID3D11SamplerState* const* s) { SetSamp(S, a, n, s); } \
    STDMETHODIMP_(void) Context::P##SetShader(I* s, ID3D11ClassInstance* const*, UINT) { SetShader(S, static_cast<TShader<I>*>(s)); }
TX_STAGE_SET(VS, ID3D11VertexShader, S_VS)
TX_STAGE_SET(HS, ID3D11HullShader, S_HS)
TX_STAGE_SET(DS, ID3D11DomainShader, S_DS)
TX_STAGE_SET(GS, ID3D11GeometryShader, S_GS)
TX_STAGE_SET(PS, ID3D11PixelShader, S_PS)
TX_STAGE_SET(CS, ID3D11ComputeShader, S_CS)

STDMETHODIMP_(void) Context::IASetInputLayout(ID3D11InputLayout* l)
{
    TX_TRACE("Context::IASetInputLayout");
    if (seg_) { SHead(SO_Layout); SObj(static_cast<TInputLayout*>(l)); }
    holds_.Assign(s_.layout, static_cast<TInputLayout*>(l));
    dirty_ |= D_PSO;
}

STDMETHODIMP_(void) Context::IASetVertexBuffers(UINT start, UINT n, ID3D11Buffer* const* b, const UINT* strides, const UINT* offsets)
{
    TX_TRACE("Context::IASetVertexBuffers");
    if (seg_) {
        SHead(SO_VB, 0, start, n);
        for (UINT i = 0; i < n; ++i) {
            SBuf(b ? static_cast<TBuffer*>(b[i]) : nullptr);
            seg_->words.push_back(uint64_t(strides ? strides[i] : 0) | uint64_t(offsets ? offsets[i] : 0) << 32);
        }
    }
    bool changed = false;
    for (UINT i = 0; i < n && start + i < 32; ++i) {
        BufSlot& s = s_.vb[start + i];
        TBuffer* nb = b ? static_cast<TBuffer*>(b[i]) : nullptr;
        const UINT stride = strides ? strides[i] : 0, offset = offsets ? offsets[i] : 0;
        if (s.buf == nb && s.stride == stride && s.offset == offset) continue;   // a rename sets D_VB itself
        holds_.Assign(s.buf, nb);
        s.chunk = nb ? Pin(nb) : nullptr;
        s.stride = stride;
        s.offset = offset;
        if (nb && start + i + 1 > s_.vbHi) s_.vbHi = start + i + 1;
        changed = true;
    }
    if (changed) dirty_ |= D_VB | D_SCAN_IA;
}

STDMETHODIMP_(void) Context::IASetIndexBuffer(ID3D11Buffer* b, DXGI_FORMAT f, UINT offset)
{
    TX_TRACE("Context::IASetIndexBuffer");
    const uintptr_t bp = reinterpret_cast<uintptr_t>(b);
    if (b && (bp < 0x10000ull || bp >= 0x00007FFFFFFFFFFFull || (bp & 0x7ull) != 0)) b = nullptr;
    if (seg_) { SHead(SO_IB, uint32_t(f)); SBuf(static_cast<TBuffer*>(b)); seg_->words.push_back(offset); }
    TBuffer* nb = static_cast<TBuffer*>(b);
    if (s_.ib.buf == nb && s_.ib.offset == offset && s_.ibFmt == f) return;   // a rename sets D_IB itself
    holds_.Assign(s_.ib.buf, nb);
    s_.ib.chunk = nb ? Pin(nb) : nullptr;
    s_.ib.offset = offset;
    if (s_.ibFmt != f) dirty_ |= D_PSO;
    s_.ibFmt = f;
    dirty_ |= D_IB | D_SCAN_IA;
}

STDMETHODIMP_(void) Context::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t)
{
    TX_TRACE("Context::IASetPrimitiveTopology");
    if (seg_) SHead(SO_Topo, uint32_t(t));
    if (s_.topo == t) return;
    s_.topo = t;
    dirty_ |= D_TOPO | D_PSO;
}

}  // namespace raw::tx
