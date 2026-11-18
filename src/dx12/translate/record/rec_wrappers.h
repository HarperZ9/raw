#pragma once
// RecDevice and RecContext: forwarding wrappers around a real ID3D11Device
// and its immediate context. Child objects are not wrapped. The two wrappers
// share one lifetime: both are destroyed when the last reference to either
// one is released.
#include "rec_recorder.h"
#include <atomic>

namespace raw::rec
{
class RecContext;

#define RAW_REC_M(ret) ret STDMETHODCALLTYPE

class RecDevice final : public ID3D11Device
{
public:
    RecDevice(ID3D11Device* real, ID3D11DeviceContext* realCtx);
    Recorder* GetRecorder() { return &m_rec; }
    RecContext* Context() { return m_ctx; }
    ULONG AddShared();
    ULONG ReleaseShared();

    RAW_REC_M(HRESULT) QueryInterface(REFIID riid, void** ppv) override;
    RAW_REC_M(ULONG) AddRef() override;
    RAW_REC_M(ULONG) Release() override;

    RAW_REC_M(HRESULT) CreateBuffer(const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**) override;
    RAW_REC_M(HRESULT) CreateTexture1D(const D3D11_TEXTURE1D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture1D**) override;
    RAW_REC_M(HRESULT) CreateTexture2D(const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**) override;
    RAW_REC_M(HRESULT) CreateTexture3D(const D3D11_TEXTURE3D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D**) override;
    RAW_REC_M(HRESULT) CreateShaderResourceView(ID3D11Resource*, const D3D11_SHADER_RESOURCE_VIEW_DESC*, ID3D11ShaderResourceView**) override;
    RAW_REC_M(HRESULT) CreateUnorderedAccessView(ID3D11Resource*, const D3D11_UNORDERED_ACCESS_VIEW_DESC*, ID3D11UnorderedAccessView**) override;
    RAW_REC_M(HRESULT) CreateRenderTargetView(ID3D11Resource*, const D3D11_RENDER_TARGET_VIEW_DESC*, ID3D11RenderTargetView**) override;
    RAW_REC_M(HRESULT) CreateDepthStencilView(ID3D11Resource*, const D3D11_DEPTH_STENCIL_VIEW_DESC*, ID3D11DepthStencilView**) override;
    RAW_REC_M(HRESULT) CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D11InputLayout**) override;
    RAW_REC_M(HRESULT) CreateVertexShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**) override;
    RAW_REC_M(HRESULT) CreateGeometryShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11GeometryShader**) override;
    RAW_REC_M(HRESULT) CreateGeometryShaderWithStreamOutput(const void*, SIZE_T, const D3D11_SO_DECLARATION_ENTRY*, UINT, const UINT*, UINT, UINT, ID3D11ClassLinkage*, ID3D11GeometryShader**) override;
    RAW_REC_M(HRESULT) CreatePixelShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**) override;
    RAW_REC_M(HRESULT) CreateHullShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11HullShader**) override;
    RAW_REC_M(HRESULT) CreateDomainShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11DomainShader**) override;
    RAW_REC_M(HRESULT) CreateComputeShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**) override;
    RAW_REC_M(HRESULT) CreateClassLinkage(ID3D11ClassLinkage**) override;
    RAW_REC_M(HRESULT) CreateBlendState(const D3D11_BLEND_DESC*, ID3D11BlendState**) override;
    RAW_REC_M(HRESULT) CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC*, ID3D11DepthStencilState**) override;
    RAW_REC_M(HRESULT) CreateRasterizerState(const D3D11_RASTERIZER_DESC*, ID3D11RasterizerState**) override;
    RAW_REC_M(HRESULT) CreateSamplerState(const D3D11_SAMPLER_DESC*, ID3D11SamplerState**) override;
    RAW_REC_M(HRESULT) CreateQuery(const D3D11_QUERY_DESC*, ID3D11Query**) override;
    RAW_REC_M(HRESULT) CreatePredicate(const D3D11_QUERY_DESC*, ID3D11Predicate**) override;
    RAW_REC_M(HRESULT) CreateCounter(const D3D11_COUNTER_DESC*, ID3D11Counter**) override;
    RAW_REC_M(HRESULT) CreateDeferredContext(UINT, ID3D11DeviceContext**) override;
    RAW_REC_M(HRESULT) OpenSharedResource(HANDLE, REFIID, void**) override;
    RAW_REC_M(HRESULT) CheckFormatSupport(DXGI_FORMAT, UINT*) override;
    RAW_REC_M(HRESULT) CheckMultisampleQualityLevels(DXGI_FORMAT, UINT, UINT*) override;
    RAW_REC_M(void) CheckCounterInfo(D3D11_COUNTER_INFO*) override;
    RAW_REC_M(HRESULT) CheckCounter(const D3D11_COUNTER_DESC*, D3D11_COUNTER_TYPE*, UINT*, LPSTR, UINT*, LPSTR, UINT*, LPSTR, UINT*) override;
    RAW_REC_M(HRESULT) CheckFeatureSupport(D3D11_FEATURE, void*, UINT) override;
    RAW_REC_M(HRESULT) GetPrivateData(REFGUID, UINT*, void*) override;
    RAW_REC_M(HRESULT) SetPrivateData(REFGUID, UINT, const void*) override;
    RAW_REC_M(HRESULT) SetPrivateDataInterface(REFGUID, const IUnknown*) override;
    RAW_REC_M(D3D_FEATURE_LEVEL) GetFeatureLevel() override;
    RAW_REC_M(UINT) GetCreationFlags() override;
    RAW_REC_M(HRESULT) GetDeviceRemovedReason() override;
    RAW_REC_M(void) GetImmediateContext(ID3D11DeviceContext**) override;
    RAW_REC_M(HRESULT) SetExceptionMode(UINT) override;
    RAW_REC_M(UINT) GetExceptionMode() override;

private:
    ~RecDevice();
    template <class T> HRESULT Done(HRESULT hr, T** out, Kind k)
    {
        if (SUCCEEDED(hr) && out && *out) m_rec.OnCreated(*out, k);
        return hr;
    }
    template <class T> HRESULT DoneShader(HRESULT hr, T** out, Kind k, const void* code, SIZE_T bytes);

    ID3D11Device*        m_real;
    ID3D11DeviceContext* m_realCtx;
    Recorder             m_rec;
    RecContext*          m_ctx;
    std::atomic<ULONG>   m_refs{1};
    std::atomic<long>    m_shared{0};
};

#define RAW_REC_STAGE_DECL(P, T)                                                                   \
    RAW_REC_M(void) P##SetShader(T*, ID3D11ClassInstance* const*, UINT) override;                  \
    RAW_REC_M(void) P##SetConstantBuffers(UINT, UINT, ID3D11Buffer* const*) override;              \
    RAW_REC_M(void) P##SetShaderResources(UINT, UINT, ID3D11ShaderResourceView* const*) override;  \
    RAW_REC_M(void) P##SetSamplers(UINT, UINT, ID3D11SamplerState* const*) override;               \
    RAW_REC_M(void) P##GetShader(T**, ID3D11ClassInstance**, UINT*) override;                      \
    RAW_REC_M(void) P##GetConstantBuffers(UINT, UINT, ID3D11Buffer**) override;                    \
    RAW_REC_M(void) P##GetShaderResources(UINT, UINT, ID3D11ShaderResourceView**) override;        \
    RAW_REC_M(void) P##GetSamplers(UINT, UINT, ID3D11SamplerState**) override;

class RecContext final : public ID3D11DeviceContext
{
public:
    RecContext(RecDevice* dev, ID3D11DeviceContext* real, Recorder* rec)
        : m_dev(dev), m_real(real), m_rec(rec) {}
    ID3D11DeviceContext* GetReal() const { return m_real; }

    RAW_REC_M(HRESULT) QueryInterface(REFIID riid, void** ppv) override;
    RAW_REC_M(ULONG) AddRef() override;
    RAW_REC_M(ULONG) Release() override;
    RAW_REC_M(void) GetDevice(ID3D11Device**) override;
    RAW_REC_M(HRESULT) GetPrivateData(REFGUID, UINT*, void*) override;
    RAW_REC_M(HRESULT) SetPrivateData(REFGUID, UINT, const void*) override;
    RAW_REC_M(HRESULT) SetPrivateDataInterface(REFGUID, const IUnknown*) override;

    RAW_REC_STAGE_DECL(VS, ID3D11VertexShader)
    RAW_REC_STAGE_DECL(HS, ID3D11HullShader)
    RAW_REC_STAGE_DECL(DS, ID3D11DomainShader)
    RAW_REC_STAGE_DECL(GS, ID3D11GeometryShader)
    RAW_REC_STAGE_DECL(PS, ID3D11PixelShader)
    RAW_REC_STAGE_DECL(CS, ID3D11ComputeShader)
    RAW_REC_M(void) CSSetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    RAW_REC_M(void) CSGetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView**) override;

    RAW_REC_M(void) IASetInputLayout(ID3D11InputLayout*) override;
    RAW_REC_M(void) IASetVertexBuffers(UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*) override;
    RAW_REC_M(void) IASetIndexBuffer(ID3D11Buffer*, DXGI_FORMAT, UINT) override;
    RAW_REC_M(void) IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY) override;
    RAW_REC_M(void) IAGetInputLayout(ID3D11InputLayout**) override;
    RAW_REC_M(void) IAGetVertexBuffers(UINT, UINT, ID3D11Buffer**, UINT*, UINT*) override;
    RAW_REC_M(void) IAGetIndexBuffer(ID3D11Buffer**, DXGI_FORMAT*, UINT*) override;
    RAW_REC_M(void) IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY*) override;

    RAW_REC_M(void) OMSetRenderTargets(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*) override;
    RAW_REC_M(void) OMSetRenderTargetsAndUnorderedAccessViews(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    RAW_REC_M(void) OMSetBlendState(ID3D11BlendState*, const FLOAT[4], UINT) override;
    RAW_REC_M(void) OMSetDepthStencilState(ID3D11DepthStencilState*, UINT) override;
    RAW_REC_M(void) OMGetRenderTargets(UINT, ID3D11RenderTargetView**, ID3D11DepthStencilView**) override;
    RAW_REC_M(void) OMGetRenderTargetsAndUnorderedAccessViews(UINT, ID3D11RenderTargetView**, ID3D11DepthStencilView**, UINT, UINT, ID3D11UnorderedAccessView**) override;
    RAW_REC_M(void) OMGetBlendState(ID3D11BlendState**, FLOAT[4], UINT*) override;
    RAW_REC_M(void) OMGetDepthStencilState(ID3D11DepthStencilState**, UINT*) override;

    RAW_REC_M(void) RSSetState(ID3D11RasterizerState*) override;
    RAW_REC_M(void) RSSetViewports(UINT, const D3D11_VIEWPORT*) override;
    RAW_REC_M(void) RSSetScissorRects(UINT, const D3D11_RECT*) override;
    RAW_REC_M(void) RSGetState(ID3D11RasterizerState**) override;
    RAW_REC_M(void) RSGetViewports(UINT*, D3D11_VIEWPORT*) override;
    RAW_REC_M(void) RSGetScissorRects(UINT*, D3D11_RECT*) override;
    RAW_REC_M(void) SOSetTargets(UINT, ID3D11Buffer* const*, const UINT*) override;
    RAW_REC_M(void) SOGetTargets(UINT, ID3D11Buffer**) override;

    RAW_REC_M(void) DrawIndexed(UINT, UINT, INT) override;
    RAW_REC_M(void) Draw(UINT, UINT) override;
    RAW_REC_M(void) DrawIndexedInstanced(UINT, UINT, UINT, INT, UINT) override;
    RAW_REC_M(void) DrawInstanced(UINT, UINT, UINT, UINT) override;
    RAW_REC_M(void) DrawAuto() override;
    RAW_REC_M(void) DrawIndexedInstancedIndirect(ID3D11Buffer*, UINT) override;
    RAW_REC_M(void) DrawInstancedIndirect(ID3D11Buffer*, UINT) override;
    RAW_REC_M(void) Dispatch(UINT, UINT, UINT) override;
    RAW_REC_M(void) DispatchIndirect(ID3D11Buffer*, UINT) override;

    RAW_REC_M(HRESULT) Map(ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*) override;
    RAW_REC_M(void) Unmap(ID3D11Resource*, UINT) override;
    RAW_REC_M(void) UpdateSubresource(ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT) override;
    RAW_REC_M(void) CopySubresourceRegion(ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*) override;
    RAW_REC_M(void) CopyResource(ID3D11Resource*, ID3D11Resource*) override;
    RAW_REC_M(void) CopyStructureCount(ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*) override;
    RAW_REC_M(void) ResolveSubresource(ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT) override;
    RAW_REC_M(void) GenerateMips(ID3D11ShaderResourceView*) override;
    RAW_REC_M(void) SetResourceMinLOD(ID3D11Resource*, FLOAT) override;
    RAW_REC_M(FLOAT) GetResourceMinLOD(ID3D11Resource*) override;

    RAW_REC_M(void) ClearRenderTargetView(ID3D11RenderTargetView*, const FLOAT[4]) override;
    RAW_REC_M(void) ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView*, const UINT[4]) override;
    RAW_REC_M(void) ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView*, const FLOAT[4]) override;
    RAW_REC_M(void) ClearDepthStencilView(ID3D11DepthStencilView*, UINT, FLOAT, UINT8) override;
    RAW_REC_M(void) ClearState() override;
    RAW_REC_M(void) Flush() override;

    RAW_REC_M(void) Begin(ID3D11Asynchronous*) override;
    RAW_REC_M(void) End(ID3D11Asynchronous*) override;
    RAW_REC_M(HRESULT) GetData(ID3D11Asynchronous*, void*, UINT, UINT) override;
    RAW_REC_M(void) SetPredication(ID3D11Predicate*, BOOL) override;
    RAW_REC_M(void) GetPredication(ID3D11Predicate**, BOOL*) override;
    RAW_REC_M(void) ExecuteCommandList(ID3D11CommandList*, BOOL) override;
    RAW_REC_M(HRESULT) FinishCommandList(BOOL, ID3D11CommandList**) override;
    RAW_REC_M(D3D11_DEVICE_CONTEXT_TYPE) GetType() override;
    RAW_REC_M(UINT) GetContextFlags() override;

private:
    friend class RecDevice;
    ~RecContext() = default;

    RecDevice*           m_dev;
    ID3D11DeviceContext* m_real;
    Recorder*            m_rec;
    std::atomic<ULONG>   m_refs{1};
};
} // namespace raw::rec
