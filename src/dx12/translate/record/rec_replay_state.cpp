// Replayer: pipeline-state records.
#include "rec_replayer.h"
#include "rec_util.h"

namespace raw::rec
{
namespace
{
constexpr u32 kKeepAll = 0xFFFFFFFFu;
constexpr u32 kMaxSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;     // 128, the widest range

template <class T> T** As(ID3D11DeviceChild** a) { return reinterpret_cast<T**>(a); }
} // namespace

HRESULT Replayer::State(const Record& r)
{
    Cursor c(r);
    switch (r.op) {
    case OP_SET_SHADER: return SetShader(c);
    case OP_SET_CONSTANT_BUFFERS: case OP_SET_SHADER_RESOURCES: case OP_SET_SAMPLERS:
        return StageSlots(r.op, c);
    case OP_IA_SET_INPUT_LAYOUT: case OP_IA_SET_VERTEX_BUFFERS: case OP_IA_SET_INDEX_BUFFER:
    case OP_IA_SET_TOPOLOGY:
        return InputAssembler(r.op, c);
    case OP_OM_SET_RENDER_TARGETS: case OP_OM_SET_RTS_AND_UAVS: return OmTargets(r.op, c);
    case OP_OM_SET_BLEND_STATE: case OP_OM_SET_DEPTH_STENCIL_STATE: case OP_RS_SET_STATE:
    case OP_RS_SET_VIEWPORTS: case OP_RS_SET_SCISSORS:
        return OmRsState(r.op, c);
    default: return MiscState(r.op, c);
    }
}

HRESULT Replayer::SetShader(Cursor& c)
{
    const u32 stage = c.U32();
    c.U32();                            // class-instance count: instances are not recorded
    const u64 id = c.U64();
    if (!c.ok || stage >= ST_COUNT) return Short();
    ID3D11DeviceChild* s = Get(id, K_VS + stage, K_VS + stage);
    switch (stage) {
    case ST_VS: m_ctx->VSSetShader(static_cast<ID3D11VertexShader*>(s), nullptr, 0); break;
    case ST_HS: m_ctx->HSSetShader(static_cast<ID3D11HullShader*>(s), nullptr, 0); break;
    case ST_DS: m_ctx->DSSetShader(static_cast<ID3D11DomainShader*>(s), nullptr, 0); break;
    case ST_GS: m_ctx->GSSetShader(static_cast<ID3D11GeometryShader*>(s), nullptr, 0); break;
    case ST_PS: m_ctx->PSSetShader(static_cast<ID3D11PixelShader*>(s), nullptr, 0); break;
    default:    m_ctx->CSSetShader(static_cast<ID3D11ComputeShader*>(s), nullptr, 0); break;
    }
    return S_OK;
}

#define RAW_REC_SLOTS(P)                                                                       \
    if (op == OP_SET_CONSTANT_BUFFERS) m_ctx->P##SetConstantBuffers(start, n, As<ID3D11Buffer>(a)); \
    else if (op == OP_SET_SHADER_RESOURCES) m_ctx->P##SetShaderResources(start, n, As<ID3D11ShaderResourceView>(a)); \
    else m_ctx->P##SetSamplers(start, n, As<ID3D11SamplerState>(a));                           \
    break;

HRESULT Replayer::StageSlots(u32 op, Cursor& c)
{
    const u32 stage = c.U32(), start = c.U32(), n = c.U32();
    c.U32();
    if (!c.ok || stage >= ST_COUNT || n > kMaxSlots) return Short();
    const u32 kind = op == OP_SET_CONSTANT_BUFFERS ? K_BUFFER : op == OP_SET_SHADER_RESOURCES ? K_SRV : K_SAMPLER;
    ID3D11DeviceChild* a[kMaxSlots];
    if (!Ids(c, n, a, kind, kind)) return Short();
    switch (stage) {
    case ST_VS: RAW_REC_SLOTS(VS)
    case ST_HS: RAW_REC_SLOTS(HS)
    case ST_DS: RAW_REC_SLOTS(DS)
    case ST_GS: RAW_REC_SLOTS(GS)
    case ST_PS: RAW_REC_SLOTS(PS)
    default:    RAW_REC_SLOTS(CS)
    }
    return S_OK;
}

HRESULT Replayer::InputAssembler(u32 op, Cursor& c)
{
    if (op == OP_IA_SET_INPUT_LAYOUT) {
        const u64 id = c.U64();
        if (!c.ok) return Short();
        m_ctx->IASetInputLayout(static_cast<ID3D11InputLayout*>(Get(id, K_INPUT_LAYOUT, K_INPUT_LAYOUT)));
    } else if (op == OP_IA_SET_INDEX_BUFFER) {
        const u64 id = c.U64();
        const u32 fmt = c.U32(), offset = c.U32();
        if (!c.ok) return Short();
        m_ctx->IASetIndexBuffer(static_cast<ID3D11Buffer*>(Get(id, K_BUFFER, K_BUFFER)),
                                static_cast<DXGI_FORMAT>(fmt), offset);
    } else if (op == OP_IA_SET_TOPOLOGY) {
        const u32 topo = c.U32();
        if (!c.ok) return Short();
        m_ctx->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(topo));
    } else {
        const u32 start = c.U32(), n = c.U32();
        ID3D11DeviceChild* vb[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT], offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        if (!c.ok || n > D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return Short();
        Ids(c, n, vb, K_BUFFER, K_BUFFER);
        for (u32 i = 0; i < n; ++i) strides[i] = c.U32();
        for (u32 i = 0; i < n; ++i) offsets[i] = c.U32();
        if (!c.ok) return Short();
        m_ctx->IASetVertexBuffers(start, n, As<ID3D11Buffer>(vb), strides, offsets);
    }
    return S_OK;
}

HRESULT Replayer::OmTargets(u32 op, Cursor& c)
{
    ID3D11DeviceChild *rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT], *uav[D3D11_PS_CS_UAV_REGISTER_COUNT];
    UINT counts[D3D11_PS_CS_UAV_REGISTER_COUNT];
    const u32 numRtv = c.U32();
    c.U32();
    const u32 rtvs = numRtv == kKeepAll ? 0 : numRtv;
    if (!c.ok || rtvs > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) return Short();
    Ids(c, rtvs, rtv, K_RTV, K_RTV);
    ID3D11DepthStencilView* dsv = static_cast<ID3D11DepthStencilView*>(Get(c.U64(), K_DSV, K_DSV));
    if (op == OP_OM_SET_RENDER_TARGETS) {
        if (!c.ok) return Short();
        m_ctx->OMSetRenderTargets(rtvs, As<ID3D11RenderTargetView>(rtv), dsv);
        return S_OK;
    }
    const u32 uavStart = c.U32(), numUav = c.U32();
    const u32 uavs = numUav == kKeepAll ? 0 : numUav;
    if (!c.ok || uavs > D3D11_PS_CS_UAV_REGISTER_COUNT) return Short();
    Ids(c, uavs, uav, K_UAV, K_UAV);
    for (u32 i = 0; i < uavs; ++i) counts[i] = c.U32();
    if (!c.ok) return Short();
    m_ctx->OMSetRenderTargetsAndUnorderedAccessViews(numRtv, As<ID3D11RenderTargetView>(rtv), dsv, uavStart,
                                                     numUav, As<ID3D11UnorderedAccessView>(uav), counts);
    return S_OK;
}

HRESULT Replayer::OmRsState(u32 op, Cursor& c)
{
    if (op == OP_RS_SET_VIEWPORTS || op == OP_RS_SET_SCISSORS) {
        const u32 n = c.U32();
        c.U32();
        const size_t each = op == OP_RS_SET_VIEWPORTS ? sizeof(D3D11_VIEWPORT) : sizeof(D3D11_RECT);
        D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D11_RECT rc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        if (!c.ok || n > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) return Short();
        const u8* raw = c.Raw(n * each);
        if (!c.ok) return Short();
        if (n) memcpy(op == OP_RS_SET_VIEWPORTS ? static_cast<void*>(vp) : static_cast<void*>(rc), raw, n * each);
        if (op == OP_RS_SET_VIEWPORTS) m_ctx->RSSetViewports(n, vp);
        else m_ctx->RSSetScissorRects(n, rc);
        return S_OK;
    }
    const u64 id = c.U64();
    if (op == OP_RS_SET_STATE) {
        if (!c.ok) return Short();
        m_ctx->RSSetState(static_cast<ID3D11RasterizerState*>(Get(id, K_RASTERIZER, K_RASTERIZER)));
    } else if (op == OP_OM_SET_BLEND_STATE) {
        const FLOAT factor[4] = {c.F32(), c.F32(), c.F32(), c.F32()};
        const u32 mask = c.U32();
        if (!c.ok) return Short();
        m_ctx->OMSetBlendState(static_cast<ID3D11BlendState*>(Get(id, K_BLEND, K_BLEND)), factor, mask);
    } else {
        const u32 ref = c.U32();
        if (!c.ok) return Short();
        m_ctx->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState*>(Get(id, K_DEPTH_STENCIL, K_DEPTH_STENCIL)), ref);
    }
    return S_OK;
}

HRESULT Replayer::MiscState(u32 op, Cursor& c)
{
    if (op == OP_CLEAR_STATE) { m_ctx->ClearState(); return S_OK; }
    if (op == OP_SET_PREDICATION) {
        const u64 id = c.U64();
        const u32 value = c.U32();
        if (!c.ok) return Short();
        m_ctx->SetPredication(static_cast<ID3D11Predicate*>(Get(id, K_PREDICATE, K_PREDICATE)), value != 0);
        return S_OK;
    }
    if (op == OP_SET_RESOURCE_MIN_LOD) {
        ID3D11Resource* res = Res(c);
        const float lod = c.F32();
        if (!c.ok) return Short();
        if (res) m_ctx->SetResourceMinLOD(res, lod);
        return S_OK;
    }
    if (op != OP_CS_SET_UAVS && op != OP_SO_SET_TARGETS) return S_OK;
    ID3D11DeviceChild* a[D3D11_PS_CS_UAV_REGISTER_COUNT];
    UINT counts[D3D11_PS_CS_UAV_REGISTER_COUNT];
    const u32 first = c.U32(), second = c.U32();
    const u32 start = op == OP_CS_SET_UAVS ? first : 0, n = op == OP_CS_SET_UAVS ? second : first;
    if (!c.ok || n > D3D11_PS_CS_UAV_REGISTER_COUNT) return Short();
    Ids(c, n, a, op == OP_CS_SET_UAVS ? K_UAV : K_BUFFER, op == OP_CS_SET_UAVS ? K_UAV : K_BUFFER);
    for (u32 i = 0; i < n; ++i) counts[i] = c.U32();
    if (!c.ok) return Short();
    if (op == OP_CS_SET_UAVS) m_ctx->CSSetUnorderedAccessViews(start, n, As<ID3D11UnorderedAccessView>(a), counts);
    else m_ctx->SOSetTargets(n, As<ID3D11Buffer>(a), counts);
    return S_OK;
}
} // namespace raw::rec
