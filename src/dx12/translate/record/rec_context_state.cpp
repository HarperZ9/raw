// RecContext, part 1: COM plumbing and the pipeline-state setters and getters.
// Every setter writes its record (which defines first-use objects) and then
// forwards. Getters only forward: the application sees the real objects.
#include "rec_emit.h"
#include "rec_util.h"
#include "rec_wrappers.h"

namespace raw::rec
{
namespace
{
constexpr u32 kKeepAll = 0xFFFFFFFFu;   // D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL / _UNORDERED_ACCESS_VIEWS

template <class T>
void EmitSlots(Recorder* rec, u32 op, u32 stage, u32 start, u32 n, T* const* a, Kind k)
{
    Emit e(*rec, op);
    e.u(stage).u(start).u(n).u(0).ids(n, a, k);
}
} // namespace

HRESULT RecContext::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild)
        || riid == __uuidof(ID3D11DeviceContext)) {
        *ppv = static_cast<ID3D11DeviceContext*>(this);
        AddRef();
        return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);   // newer interfaces are not wrapped
}
ULONG RecContext::AddRef() { m_dev->AddShared(); return ++m_refs; }
ULONG RecContext::Release()
{
    const ULONG r = --m_refs;
    m_dev->ReleaseShared();
    return r;
}
void RecContext::GetDevice(ID3D11Device** out)
{
    if (!out) return;
    m_dev->AddRef();
    *out = m_dev;
}
HRESULT RecContext::GetPrivateData(REFGUID g, UINT* n, void* d) { return m_real->GetPrivateData(g, n, d); }
HRESULT RecContext::SetPrivateData(REFGUID g, UINT n, const void* d) { return m_real->SetPrivateData(g, n, d); }
HRESULT RecContext::SetPrivateDataInterface(REFGUID g, const IUnknown* u) { return m_real->SetPrivateDataInterface(g, u); }
D3D11_DEVICE_CONTEXT_TYPE RecContext::GetType() { return m_real->GetType(); }
UINT RecContext::GetContextFlags() { return m_real->GetContextFlags(); }

#define RAW_REC_STAGE_IMPL(P, T, ST)                                                               \
    void RecContext::P##SetShader(T* s, ID3D11ClassInstance* const* ci, UINT n)                    \
    {                                                                                              \
        if (m_rec->Capturing()) {                                                                  \
            Emit e(*m_rec, OP_SET_SHADER);                                                         \
            e.u(ST).u(n).id(s, static_cast<Kind>(static_cast<u32>(K_VS) + ST));                                      \
        }                                                                                          \
        m_real->P##SetShader(s, ci, n);                                                            \
    }                                                                                              \
    void RecContext::P##SetConstantBuffers(UINT start, UINT n, ID3D11Buffer* const* a)             \
    {                                                                                              \
        if (m_rec->Capturing()) EmitSlots(m_rec, OP_SET_CONSTANT_BUFFERS, ST, start, n, a, K_BUFFER); \
        m_real->P##SetConstantBuffers(start, n, a);                                                \
    }                                                                                              \
    void RecContext::P##SetShaderResources(UINT start, UINT n, ID3D11ShaderResourceView* const* a) \
    {                                                                                              \
        if (m_rec->Capturing()) EmitSlots(m_rec, OP_SET_SHADER_RESOURCES, ST, start, n, a, K_SRV); \
        m_real->P##SetShaderResources(start, n, a);                                                \
    }                                                                                              \
    void RecContext::P##SetSamplers(UINT start, UINT n, ID3D11SamplerState* const* a)              \
    {                                                                                              \
        if (m_rec->Capturing()) EmitSlots(m_rec, OP_SET_SAMPLERS, ST, start, n, a, K_SAMPLER);     \
        m_real->P##SetSamplers(start, n, a);                                                       \
    }                                                                                              \
    void RecContext::P##GetShader(T** s, ID3D11ClassInstance** ci, UINT* n) { m_real->P##GetShader(s, ci, n); } \
    void RecContext::P##GetConstantBuffers(UINT start, UINT n, ID3D11Buffer** a) { m_real->P##GetConstantBuffers(start, n, a); } \
    void RecContext::P##GetShaderResources(UINT start, UINT n, ID3D11ShaderResourceView** a) { m_real->P##GetShaderResources(start, n, a); } \
    void RecContext::P##GetSamplers(UINT start, UINT n, ID3D11SamplerState** a) { m_real->P##GetSamplers(start, n, a); }

RAW_REC_STAGE_IMPL(VS, ID3D11VertexShader, ST_VS)
RAW_REC_STAGE_IMPL(HS, ID3D11HullShader, ST_HS)
RAW_REC_STAGE_IMPL(DS, ID3D11DomainShader, ST_DS)
RAW_REC_STAGE_IMPL(GS, ID3D11GeometryShader, ST_GS)
RAW_REC_STAGE_IMPL(PS, ID3D11PixelShader, ST_PS)
RAW_REC_STAGE_IMPL(CS, ID3D11ComputeShader, ST_CS)

void RecContext::CSSetUnorderedAccessViews(UINT start, UINT n, ID3D11UnorderedAccessView* const* a, const UINT* counts)
{
    if (m_rec->Capturing()) {
        Emit e(*m_rec, OP_CS_SET_UAVS);
        e.u(start).u(n).ids(n, a, K_UAV).us(n, counts, kKeep);
    }
    m_real->CSSetUnorderedAccessViews(start, n, a, counts);
}
void RecContext::CSGetUnorderedAccessViews(UINT start, UINT n, ID3D11UnorderedAccessView** a) { m_real->CSGetUnorderedAccessViews(start, n, a); }

// ---- input assembler ---------------------------------------------------------
void RecContext::IASetInputLayout(ID3D11InputLayout* il)
{
    if (m_rec->Capturing()) { Emit e(*m_rec, OP_IA_SET_INPUT_LAYOUT); e.id(il, K_INPUT_LAYOUT); }
    m_real->IASetInputLayout(il);
}
void RecContext::IASetVertexBuffers(UINT start, UINT n, ID3D11Buffer* const* vb, const UINT* strides, const UINT* offsets)
{
    if (m_rec->Capturing()) {
        Emit e(*m_rec, OP_IA_SET_VERTEX_BUFFERS);
        e.u(start).u(n).ids(n, vb, K_BUFFER).us(n, strides, 0).us(n, offsets, 0);
    }
    m_real->IASetVertexBuffers(start, n, vb, strides, offsets);
}
void RecContext::IASetIndexBuffer(ID3D11Buffer* ib, DXGI_FORMAT fmt, UINT offset)
{
    if (m_rec->Capturing()) { Emit e(*m_rec, OP_IA_SET_INDEX_BUFFER); e.id(ib, K_BUFFER).u(fmt).u(offset); }
    m_real->IASetIndexBuffer(ib, fmt, offset);
}
void RecContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t)
{
    if (m_rec->Capturing()) { Emit e(*m_rec, OP_IA_SET_TOPOLOGY); e.u(t); }
    m_real->IASetPrimitiveTopology(t);
}
void RecContext::IAGetInputLayout(ID3D11InputLayout** il) { m_real->IAGetInputLayout(il); }
void RecContext::IAGetVertexBuffers(UINT start, UINT n, ID3D11Buffer** vb, UINT* strides, UINT* offsets) { m_real->IAGetVertexBuffers(start, n, vb, strides, offsets); }
void RecContext::IAGetIndexBuffer(ID3D11Buffer** ib, DXGI_FORMAT* fmt, UINT* offset) { m_real->IAGetIndexBuffer(ib, fmt, offset); }
void RecContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* t) { m_real->IAGetPrimitiveTopology(t); }

// ---- output merger -----------------------------------------------------------
void RecContext::OMSetRenderTargets(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv)
{
    if (m_rec->Capturing()) {
        Emit e(*m_rec, OP_OM_SET_RENDER_TARGETS);
        e.u(n).u(0).ids(n, rtv, K_RTV).id(dsv, K_DSV);
        for (u32 i = 0; rtv && i < n; ++i)
            if (rtv[i]) { m_rec->NoteRenderTarget(rtv[i]); break; }
    }
    m_real->OMSetRenderTargets(n, rtv, dsv);
}
void RecContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT numRtv, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv, UINT uavStart, UINT numUav, ID3D11UnorderedAccessView* const* uav, const UINT* counts)
{
    if (m_rec->Capturing()) {
        const u32 rtvs = numRtv == kKeepAll ? 0 : numRtv, uavs = numUav == kKeepAll ? 0 : numUav;
        Emit e(*m_rec, OP_OM_SET_RTS_AND_UAVS);
        e.u(numRtv).u(0).ids(rtvs, rtv, K_RTV).id(numRtv == kKeepAll ? nullptr : dsv, K_DSV);
        e.u(uavStart).u(numUav).ids(uavs, uav, K_UAV).us(uavs, counts, kKeep);
        for (u32 i = 0; rtv && i < rtvs; ++i)
            if (rtv[i]) { m_rec->NoteRenderTarget(rtv[i]); break; }
    }
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(numRtv, rtv, dsv, uavStart, numUav, uav, counts);
}
void RecContext::OMSetBlendState(ID3D11BlendState* b, const FLOAT factor[4], UINT mask)
{
    if (m_rec->Capturing()) { Emit e(*m_rec, OP_OM_SET_BLEND_STATE); e.id(b, K_BLEND).fs(4, factor, 1.0f).u(mask); }
    m_real->OMSetBlendState(b, factor, mask);
}
void RecContext::OMSetDepthStencilState(ID3D11DepthStencilState* d, UINT ref)
{
    if (m_rec->Capturing()) { Emit e(*m_rec, OP_OM_SET_DEPTH_STENCIL_STATE); e.id(d, K_DEPTH_STENCIL).u(ref); }
    m_real->OMSetDepthStencilState(d, ref);
}
void RecContext::OMGetRenderTargets(UINT n, ID3D11RenderTargetView** rtv, ID3D11DepthStencilView** dsv) { m_real->OMGetRenderTargets(n, rtv, dsv); }
void RecContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT n, ID3D11RenderTargetView** rtv, ID3D11DepthStencilView** dsv, UINT uavStart, UINT numUav, ID3D11UnorderedAccessView** uav) { m_real->OMGetRenderTargetsAndUnorderedAccessViews(n, rtv, dsv, uavStart, numUav, uav); }
void RecContext::OMGetBlendState(ID3D11BlendState** b, FLOAT factor[4], UINT* mask) { m_real->OMGetBlendState(b, factor, mask); }
void RecContext::OMGetDepthStencilState(ID3D11DepthStencilState** d, UINT* ref) { m_real->OMGetDepthStencilState(d, ref); }

// ---- rasterizer and stream output --------------------------------------------
void RecContext::RSSetState(ID3D11RasterizerState* rs)
{
    if (m_rec->Capturing()) { Emit e(*m_rec, OP_RS_SET_STATE); e.id(rs, K_RASTERIZER); }
    m_real->RSSetState(rs);
}
void RecContext::RSSetViewports(UINT n, const D3D11_VIEWPORT* vp)
{
    if (m_rec->Capturing()) {
        Emit e(*m_rec, OP_RS_SET_VIEWPORTS);
        e.u(vp ? n : 0).u(0).raw(vp, vp ? n * sizeof(D3D11_VIEWPORT) : 0);
    }
    m_real->RSSetViewports(n, vp);
}
void RecContext::RSSetScissorRects(UINT n, const D3D11_RECT* rc)
{
    if (m_rec->Capturing()) {
        Emit e(*m_rec, OP_RS_SET_SCISSORS);
        e.u(rc ? n : 0).u(0).raw(rc, rc ? n * sizeof(D3D11_RECT) : 0);
    }
    m_real->RSSetScissorRects(n, rc);
}
void RecContext::RSGetState(ID3D11RasterizerState** rs) { m_real->RSGetState(rs); }
void RecContext::RSGetViewports(UINT* n, D3D11_VIEWPORT* vp) { m_real->RSGetViewports(n, vp); }
void RecContext::RSGetScissorRects(UINT* n, D3D11_RECT* rc) { m_real->RSGetScissorRects(n, rc); }
void RecContext::SOSetTargets(UINT n, ID3D11Buffer* const* targets, const UINT* offsets)
{
    if (m_rec->Capturing()) {
        Emit e(*m_rec, OP_SO_SET_TARGETS);
        e.u(n).u(0).ids(n, targets, K_BUFFER).us(n, offsets, kKeep);
    }
    m_real->SOSetTargets(n, targets, offsets);
}
void RecContext::SOGetTargets(UINT n, ID3D11Buffer** targets) { m_real->SOGetTargets(n, targets); }
} // namespace raw::rec
