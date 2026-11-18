// Initial pipeline state: read back through the real context's Get methods
// at the start of a capture and written as ordinary Set records between
// OP_STATE_BEGIN and OP_STATE_END. Full slot ranges are written, so a replay
// reaches the same state whatever the replay context had bound before.
#include "rec_emit.h"
#include "rec_util.h"

namespace raw::rec
{
namespace
{
constexpr u32 kCB = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;      // 14
constexpr u32 kSRV = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;          // 128
constexpr u32 kSamp = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;                // 16
constexpr u32 kVB = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;              // 32
constexpr u32 kRT = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;                 // 8
constexpr u32 kUAV = D3D11_PS_CS_UAV_REGISTER_COUNT;                        // 8
constexpr u32 kVP = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; // 16

template <class T> void ReleaseAll(T** a, u32 n)
{
    for (u32 i = 0; i < n; ++i) SafeRelease(a[i]);
}

#define RAW_REC_GET_STAGE(P, T)                                                        \
    {                                                                                  \
        T* s = nullptr;                                                                \
        UINT n = 0;                                                                    \
        c->P##GetShader(&s, nullptr, &n);                                              \
        *shader = s;                                                                   \
        c->P##GetConstantBuffers(0, kCB, cb);                                          \
        c->P##GetShaderResources(0, kSRV, srv);                                        \
        c->P##GetSamplers(0, kSamp, samp);                                             \
    }

void GetStage(ID3D11DeviceContext* c, u32 stage, ID3D11DeviceChild** shader, ID3D11Buffer** cb,
              ID3D11ShaderResourceView** srv, ID3D11SamplerState** samp)
{
    switch (stage) {
    case ST_VS: RAW_REC_GET_STAGE(VS, ID3D11VertexShader) break;
    case ST_HS: RAW_REC_GET_STAGE(HS, ID3D11HullShader) break;
    case ST_DS: RAW_REC_GET_STAGE(DS, ID3D11DomainShader) break;
    case ST_GS: RAW_REC_GET_STAGE(GS, ID3D11GeometryShader) break;
    case ST_PS: RAW_REC_GET_STAGE(PS, ID3D11PixelShader) break;
    default:    RAW_REC_GET_STAGE(CS, ID3D11ComputeShader) break;
    }
}
} // namespace

void Recorder::Snapshot()
{
    { Emit e(*this, OP_STATE_BEGIN); }
    SnapshotIA();
    for (u32 stage = 0; stage < ST_COUNT; ++stage) SnapshotStage(stage);
    SnapshotOM();
    SnapshotRS();
    SnapshotMisc();
    { Emit e(*this, OP_STATE_END); }
}

void Recorder::SnapshotIA()
{
    ID3D11InputLayout* il = nullptr;
    m_ctx->IAGetInputLayout(&il);
    { Emit e(*this, OP_IA_SET_INPUT_LAYOUT); e.id(il, K_INPUT_LAYOUT); }
    SafeRelease(il);

    ID3D11Buffer* vb[kVB] = {};
    UINT strides[kVB] = {}, offsets[kVB] = {};
    m_ctx->IAGetVertexBuffers(0, kVB, vb, strides, offsets);
    {
        Emit e(*this, OP_IA_SET_VERTEX_BUFFERS);
        e.u(0).u(kVB).ids(kVB, vb, K_BUFFER).us(kVB, strides, 0).us(kVB, offsets, 0);
    }
    ReleaseAll(vb, kVB);

    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT offset = 0;
    m_ctx->IAGetIndexBuffer(&ib, &fmt, &offset);
    { Emit e(*this, OP_IA_SET_INDEX_BUFFER); e.id(ib, K_BUFFER).u(fmt).u(offset); }
    SafeRelease(ib);

    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    m_ctx->IAGetPrimitiveTopology(&topo);
    { Emit e(*this, OP_IA_SET_TOPOLOGY); e.u(topo); }
}

void Recorder::SnapshotStage(u32 stage)
{
    ID3D11DeviceChild* shader = nullptr;
    ID3D11Buffer* cb[kCB] = {};
    ID3D11ShaderResourceView* srv[kSRV] = {};
    ID3D11SamplerState* samp[kSamp] = {};
    GetStage(m_ctx, stage, &shader, cb, srv, samp);
    { Emit e(*this, OP_SET_SHADER); e.u(stage).u(0).id(shader, static_cast<Kind>(K_VS + stage)); }
    { Emit e(*this, OP_SET_CONSTANT_BUFFERS); e.u(stage).u(0).u(kCB).u(0).ids(kCB, cb, K_BUFFER); }
    { Emit e(*this, OP_SET_SHADER_RESOURCES); e.u(stage).u(0).u(kSRV).u(0).ids(kSRV, srv, K_SRV); }
    { Emit e(*this, OP_SET_SAMPLERS); e.u(stage).u(0).u(kSamp).u(0).ids(kSamp, samp, K_SAMPLER); }
    SafeRelease(shader);
    ReleaseAll(cb, kCB);
    ReleaseAll(srv, kSRV);
    ReleaseAll(samp, kSamp);
}

void Recorder::SnapshotOM()
{
    ID3D11RenderTargetView* rtv[kRT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11UnorderedAccessView* uav[kUAV] = {};
    m_ctx->OMGetRenderTargets(kRT, rtv, &dsv);
    m_ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, kUAV, uav);
    u32 numRT = 0, firstUav = kUAV;
    for (u32 i = 0; i < kRT; ++i) if (rtv[i]) numRT = i + 1;
    for (u32 i = kUAV; i-- > 0;) if (uav[i]) firstUav = i;
    if (firstUav < kUAV && firstUav >= numRT) {
        Emit e(*this, OP_OM_SET_RTS_AND_UAVS);
        e.u(numRT).u(0).ids(numRT, rtv, K_RTV).id(dsv, K_DSV);
        e.u(firstUav).u(kUAV - firstUav).ids(kUAV - firstUav, uav + firstUav, K_UAV)
            .us(kUAV - firstUav, nullptr, kKeep);
    } else {
        Emit e(*this, OP_OM_SET_RENDER_TARGETS);
        e.u(kRT).u(0).ids(kRT, rtv, K_RTV).id(dsv, K_DSV);
    }
    for (u32 i = 0; i < kRT; ++i)
        if (rtv[i]) { NoteRenderTarget(rtv[i]); break; }
    ReleaseAll(rtv, kRT);
    ReleaseAll(uav, kUAV);
    SafeRelease(dsv);

    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4] = {};
    UINT mask = 0, ref = 0;
    m_ctx->OMGetBlendState(&blend, factor, &mask);
    { Emit e(*this, OP_OM_SET_BLEND_STATE); e.id(blend, K_BLEND).fs(4, factor, 1.0f).u(mask); }
    SafeRelease(blend);

    ID3D11DepthStencilState* depth = nullptr;
    m_ctx->OMGetDepthStencilState(&depth, &ref);
    { Emit e(*this, OP_OM_SET_DEPTH_STENCIL_STATE); e.id(depth, K_DEPTH_STENCIL).u(ref); }
    SafeRelease(depth);
}

void Recorder::SnapshotRS()
{
    ID3D11RasterizerState* rs = nullptr;
    m_ctx->RSGetState(&rs);
    { Emit e(*this, OP_RS_SET_STATE); e.id(rs, K_RASTERIZER); }
    SafeRelease(rs);

    D3D11_VIEWPORT vp[kVP] = {};
    UINT numVp = kVP;
    m_ctx->RSGetViewports(&numVp, vp);
    { Emit e(*this, OP_RS_SET_VIEWPORTS); e.u(numVp).u(0).raw(vp, numVp * sizeof(D3D11_VIEWPORT)); }

    D3D11_RECT sc[kVP] = {};
    UINT numSc = kVP;
    m_ctx->RSGetScissorRects(&numSc, sc);
    { Emit e(*this, OP_RS_SET_SCISSORS); e.u(numSc).u(0).raw(sc, numSc * sizeof(D3D11_RECT)); }
}

void Recorder::SnapshotMisc()
{
    ID3D11UnorderedAccessView* uav[kUAV] = {};
    m_ctx->CSGetUnorderedAccessViews(0, kUAV, uav);
    { Emit e(*this, OP_CS_SET_UAVS); e.u(0).u(kUAV).ids(kUAV, uav, K_UAV).us(kUAV, nullptr, kKeep); }
    ReleaseAll(uav, kUAV);

    ID3D11Buffer* so[D3D11_SO_BUFFER_SLOT_COUNT] = {};
    m_ctx->SOGetTargets(D3D11_SO_BUFFER_SLOT_COUNT, so);
    {
        Emit e(*this, OP_SO_SET_TARGETS);
        e.u(D3D11_SO_BUFFER_SLOT_COUNT).u(0).ids(D3D11_SO_BUFFER_SLOT_COUNT, so, K_BUFFER)
            .us(D3D11_SO_BUFFER_SLOT_COUNT, nullptr, kKeep);
    }
    ReleaseAll(so, D3D11_SO_BUFFER_SLOT_COUNT);

    ID3D11Predicate* pred = nullptr;
    BOOL value = FALSE;
    m_ctx->GetPredication(&pred, &value);
    { Emit e(*this, OP_SET_PREDICATION); e.id(pred, K_PREDICATE).u(value ? 1 : 0); }
    SafeRelease(pred);
}
} // namespace raw::rec
