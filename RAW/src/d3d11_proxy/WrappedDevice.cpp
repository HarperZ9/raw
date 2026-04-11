//=============================================================================
//  WrappedDevice.cpp — ID3D11Device wrapper implementation
//=============================================================================

#include "WrappedDevice.h"
#include "WrappedContext.h"
#include "ShaderManager.h"
#include "MaterialPipeline.h"
#include "DepthIntercept.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <unordered_set>

namespace SB::Proxy
{

// ═════════════════════════════════════════════════════════════════════════════
//  Depth Interception — upgrade depth textures at creation for SRV access
// ═════════════════════════════════════════════════════════════════════════════

namespace {
    // Set of all textures whose format was upgraded from typed depth → typeless
    std::unordered_set<ID3D11Texture2D*> s_upgradedDepthTextures;

    // The main (largest) depth texture — exposed via ProxyInterface
    ID3D11Texture2D*          s_mainDepthTex  = nullptr;
    ID3D11ShaderResourceView* s_mainDepthSRV  = nullptr;
    uint32_t                  s_mainDepthArea = 0; // width * height

    // Depth format → typeless format
    DXGI_FORMAT ToTypeless(DXGI_FORMAT fmt) {
        switch (fmt) {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:      return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT:              return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:   return DXGI_FORMAT_R32G8X24_TYPELESS;
        case DXGI_FORMAT_D16_UNORM:              return DXGI_FORMAT_R16_TYPELESS;
        default:                                 return DXGI_FORMAT_UNKNOWN;
        }
    }

    // Depth format → SRV format
    DXGI_FORMAT ToSRVFormat(DXGI_FORMAT fmt) {
        switch (fmt) {
        case DXGI_FORMAT_D24_UNORM_S8_UINT:      return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT:              return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:   return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_D16_UNORM:              return DXGI_FORMAT_R16_UNORM;
        default:                                 return DXGI_FORMAT_UNKNOWN;
        }
    }

    // Is this a typed depth format that can be upgraded?
    bool IsDepthFormat(DXGI_FORMAT fmt) {
        return fmt == DXGI_FORMAT_D24_UNORM_S8_UINT ||
               fmt == DXGI_FORMAT_D32_FLOAT ||
               fmt == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
               fmt == DXGI_FORMAT_D16_UNORM;
    }
} // anon namespace

// ── Depth interception accessors (declared in DepthIntercept.h) ─────────────

ID3D11ShaderResourceView* DepthIntercept_GetSRV()       { return s_mainDepthSRV; }
bool                      DepthIntercept_IsActive()      { return s_mainDepthSRV != nullptr; }
bool                      DepthIntercept_WasUpgraded(ID3D11Texture2D* tex) {
    return tex && s_upgradedDepthTextures.count(tex) > 0;
}

DXGI_FORMAT DepthIntercept_GetDSVFormat(DXGI_FORMAT typelessFmt) {
    switch (typelessFmt) {
    case DXGI_FORMAT_R24G8_TYPELESS:    return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case DXGI_FORMAT_R32_TYPELESS:      return DXGI_FORMAT_D32_FLOAT;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case DXGI_FORMAT_R16_TYPELESS:      return DXGI_FORMAT_D16_UNORM;
    default:                            return typelessFmt;
    }
}

// ── Constructor / Destructor ────────────────────────────────────────────────

WrappedDevice::WrappedDevice(ID3D11Device* real, WrappedContext* wrappedCtx)
    : m_real(real)
    , m_wrappedCtx(wrappedCtx)
    , m_refCount(1)
{
    Log("WrappedDevice created  real=%p  wrappedCtx=%p", m_real, m_wrappedCtx);
}

WrappedDevice::~WrappedDevice()
{
    Log("WrappedDevice destroyed  real=%p", m_real);
}

// ── IUnknown ────────────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(ID3D11Device) || riid == __uuidof(IUnknown))
    {
        *ppv = static_cast<ID3D11Device*>(this);
        AddRef();
        return S_OK;
    }

    // Block higher device versions — returning the real device here would break
    // the COM identity chain (callers get unwrapped objects, can escape the wrapper).
    // Skyrim SE targets DX11.0 and doesn't need Device1-5 methods.
    if (riid == __uuidof(ID3D11Device1) ||
        riid == __uuidof(ID3D11Device2) ||
        riid == __uuidof(ID3D11Device3) ||
        riid == __uuidof(ID3D11Device4) ||
        riid == __uuidof(ID3D11Device5))
    {
        Log("[WrappedDevice] QI for ID3D11Device1-5 blocked (returning E_NOINTERFACE)");
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // DXGI interfaces (IDXGIDevice, IDXGIDevice1, IDXGIObject, etc.)
    // Forward to the real device. This is needed for DXGI adapter/factory queries.
    // NOTE: This leaks the real device through the IDXGIDevice → QI(ID3D11Device) path.
    // If this causes issues, we'll need a WrappedDXGIDevice.
    if (riid == __uuidof(IDXGIDevice) || riid == __uuidof(IDXGIDevice1))
    {
        Log("[WrappedDevice] QI for IDXGIDevice — forwarding to real device (COM leak risk)");
    }

    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedDevice::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE WrappedDevice::Release()
{
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0)
        delete this;
    return ref;
}

// ── HOOKED: CreateTexture2D ─────────────────────────────────────────────────
// Intercepts depth texture creation to upgrade format for SRV access.

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D)
{
    if (!pDesc)
        return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);

    if (pDesc->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)) {
        Log("CreateTexture2D  %ux%u  fmt=%u  bind=0x%X  mips=%u  arraySize=%u",
            pDesc->Width, pDesc->Height, (UINT)pDesc->Format,
            pDesc->BindFlags, pDesc->MipLevels, pDesc->ArraySize);
    }

    // ── Depth interception ───────────────────────────────────────────
    // Upgrade typed depth textures to typeless + BIND_SHADER_RESOURCE.
    // Criteria: depth format, DSV-bound, no existing SRV access, ≥512x512.
    bool canIntercept = !PG_IsSafeMode() &&
        IsDepthFormat(pDesc->Format) &&
        (pDesc->BindFlags & D3D11_BIND_DEPTH_STENCIL) &&
        !(pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
        pDesc->Width >= 512 && pDesc->Height >= 512 &&
        pDesc->SampleDesc.Count <= 1;  // skip MSAA (needs different SRV dimension)

    if (canIntercept) {
        DXGI_FORMAT typeless = ToTypeless(pDesc->Format);
        DXGI_FORMAT srvFmt   = ToSRVFormat(pDesc->Format);
        if (typeless == DXGI_FORMAT_UNKNOWN || srvFmt == DXGI_FORMAT_UNKNOWN)
            return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);

        D3D11_TEXTURE2D_DESC modified = *pDesc;
        modified.Format     = typeless;
        modified.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

        Log("[DepthIntercept] Upgrading %ux%u depth: fmt %u -> %u, adding BIND_SHADER_RESOURCE",
            modified.Width, modified.Height, (UINT)pDesc->Format, (UINT)typeless);

        HRESULT hr = m_real->CreateTexture2D(&modified, pInitialData, ppTexture2D);
        if (FAILED(hr)) {
            // Fallback: create with original desc if upgrade fails
            Log("[DepthIntercept] Upgrade failed (hr=0x%08X), falling back", (unsigned)hr);
            return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
        }

        if (ppTexture2D && *ppTexture2D) {
            s_upgradedDepthTextures.insert(*ppTexture2D);

            // Track the largest depth texture as the main scene depth
            uint32_t area = modified.Width * modified.Height;
            if (area >= s_mainDepthArea) {
                s_mainDepthTex  = *ppTexture2D;
                s_mainDepthArea = area;

                // Create SRV for live depth access
                if (s_mainDepthSRV) { s_mainDepthSRV->Release(); s_mainDepthSRV = nullptr; }

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = srvFmt;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                HRESULT srvHr = m_real->CreateShaderResourceView(*ppTexture2D, &srvDesc, &s_mainDepthSRV);
                if (SUCCEEDED(srvHr)) {
                    Log("[DepthIntercept] Main depth SRV created: %p (%ux%u, srvFmt=%u)",
                        s_mainDepthSRV, modified.Width, modified.Height, (UINT)srvFmt);

                    auto* pi = PG_GetProxyInterface();
                    if (pi) pi->gameDepthSRV = s_mainDepthSRV;
                } else {
                    Log("[DepthIntercept] SRV creation failed: hr=0x%08X", (unsigned)srvHr);
                }
            }
        }
        return hr;
    }

    return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
}

// ── HOOKED: CreateRenderTargetView ──────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateRenderTargetView(
    ID3D11Resource* pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    ID3D11RenderTargetView** ppRTView)
{
    Log("CreateRenderTargetView  resource=%p  desc=%p", pResource, pDesc);
    return m_real->CreateRenderTargetView(pResource, pDesc, ppRTView);
}

// ── HOOKED: CreateDepthStencilView ──────────────────────────────────────────
// Fixes DSV format for intercepted depth textures (typeless requires explicit format).

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDepthStencilView(
    ID3D11Resource* pResource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    ID3D11DepthStencilView** ppDepthStencilView)
{
    Log("CreateDepthStencilView  resource=%p  desc=%p", pResource, pDesc);

    // If this resource is an intercepted depth texture and the game passes
    // a null desc (expecting the format from the texture), we need to provide
    // an explicit DSV format since the texture is now typeless.
    if (pResource && !pDesc) {
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex) {
            bool upgraded = DepthIntercept_WasUpgraded(tex);
            if (upgraded) {
                D3D11_TEXTURE2D_DESC texDesc;
                tex->GetDesc(&texDesc);
                DXGI_FORMAT dsvFmt = DepthIntercept_GetDSVFormat(texDesc.Format);

                D3D11_DEPTH_STENCIL_VIEW_DESC fixedDesc = {};
                fixedDesc.Format = dsvFmt;
                fixedDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                fixedDesc.Texture2D.MipSlice = 0;

                Log("[DepthIntercept] DSV fixup: typeless %u -> DSV fmt %u",
                    (UINT)texDesc.Format, (UINT)dsvFmt);

                tex->Release();

                HRESULT hr = m_real->CreateDepthStencilView(pResource, &fixedDesc, ppDepthStencilView);
                if (SUCCEEDED(hr) && ppDepthStencilView && *ppDepthStencilView && tex == s_mainDepthTex) {
                    auto* pi = PG_GetProxyInterface();
                    if (pi) pi->gameDepthDSV = *ppDepthStencilView;
                }
                return hr;
            }
            tex->Release();
        }
    }

    return m_real->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
}

// ── HOOKED: CreateVertexShader ──────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateVertexShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11VertexShader** ppVertexShader)
{
    Log("CreateVertexShader  bytecodeLen=%zu", BytecodeLength);
    HRESULT hr = m_real->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
    if (SUCCEEDED(hr) && ppVertexShader && *ppVertexShader) {
        ShaderManager::Get().OnVertexShaderCreated(pShaderBytecode, BytecodeLength, *ppVertexShader);
    }
    return hr;
}

// ── HOOKED: CreatePixelShader ───────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreatePixelShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11PixelShader** ppPixelShader)
{
    Log("CreatePixelShader  bytecodeLen=%zu", BytecodeLength);
    HRESULT hr = m_real->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
    if (SUCCEEDED(hr) && ppPixelShader && *ppPixelShader) {
        ShaderManager::Get().OnPixelShaderCreated(pShaderBytecode, BytecodeLength, *ppPixelShader);
        MaterialPipeline::Get().OnPixelShaderCreated(
            m_real, pShaderBytecode, BytecodeLength, *ppPixelShader);
    }
    return hr;
}

// ── GetImmediateContext ─────────────────────────────────────────────────────

void STDMETHODCALLTYPE WrappedDevice::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext)
{
    if (ppImmediateContext)
    {
        *ppImmediateContext = m_wrappedCtx;
        m_wrappedCtx->AddRef();
    }
}

// ── Forwarded: Resource creation ────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateBuffer(
    const D3D11_BUFFER_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Buffer** ppBuffer)
{
    return m_real->CreateBuffer(pDesc, pInitialData, ppBuffer);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture1D(
    const D3D11_TEXTURE1D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture1D** ppTexture1D)
{
    return m_real->CreateTexture1D(pDesc, pInitialData, ppTexture1D);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture3D(
    const D3D11_TEXTURE3D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture3D** ppTexture3D)
{
    return m_real->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateShaderResourceView(
    ID3D11Resource* pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
    ID3D11ShaderResourceView** ppSRView)
{
    return m_real->CreateShaderResourceView(pResource, pDesc, ppSRView);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateUnorderedAccessView(
    ID3D11Resource* pResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    ID3D11UnorderedAccessView** ppUAView)
{
    return m_real->CreateUnorderedAccessView(pResource, pDesc, ppUAView);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,
    UINT NumElements,
    const void* pShaderBytecodeWithInputSignature,
    SIZE_T BytecodeLength,
    ID3D11InputLayout** ppInputLayout)
{
    return m_real->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateGeometryShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11GeometryShader** ppGeometryShader)
{
    return m_real->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateGeometryShaderWithStreamOutput(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
    UINT NumEntries,
    const UINT* pBufferStrides,
    UINT NumStrides,
    UINT RasterizedStream,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11GeometryShader** ppGeometryShader)
{
    return m_real->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream, pClassLinkage, ppGeometryShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateHullShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11HullShader** ppHullShader)
{
    return m_real->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDomainShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11DomainShader** ppDomainShader)
{
    return m_real->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateComputeShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11ComputeShader** ppComputeShader)
{
    return m_real->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateClassLinkage(ID3D11ClassLinkage** ppLinkage)
{
    return m_real->CreateClassLinkage(ppLinkage);
}

// ── Forwarded: State objects ────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateBlendState(
    const D3D11_BLEND_DESC* pBlendStateDesc,
    ID3D11BlendState** ppBlendState)
{
    return m_real->CreateBlendState(pBlendStateDesc, ppBlendState);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDepthStencilState(
    const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    ID3D11DepthStencilState** ppDepthStencilState)
{
    return m_real->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateRasterizerState(
    const D3D11_RASTERIZER_DESC* pRasterizerDesc,
    ID3D11RasterizerState** ppRasterizerState)
{
    return m_real->CreateRasterizerState(pRasterizerDesc, ppRasterizerState);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateSamplerState(
    const D3D11_SAMPLER_DESC* pSamplerDesc,
    ID3D11SamplerState** ppSamplerState)
{
    return m_real->CreateSamplerState(pSamplerDesc, ppSamplerState);
}

// ── Forwarded: Query / Predicate / Counter ──────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateQuery(
    const D3D11_QUERY_DESC* pQueryDesc,
    ID3D11Query** ppQuery)
{
    return m_real->CreateQuery(pQueryDesc, ppQuery);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreatePredicate(
    const D3D11_QUERY_DESC* pPredicateDesc,
    ID3D11Predicate** ppPredicate)
{
    return m_real->CreatePredicate(pPredicateDesc, ppPredicate);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateCounter(
    const D3D11_COUNTER_DESC* pCounterDesc,
    ID3D11Counter** ppCounter)
{
    return m_real->CreateCounter(pCounterDesc, ppCounter);
}

// ── Forwarded: Deferred context / shared resource ───────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDeferredContext(
    UINT ContextFlags,
    ID3D11DeviceContext** ppDeferredContext)
{
    return m_real->CreateDeferredContext(ContextFlags, ppDeferredContext);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::OpenSharedResource(
    HANDLE hResource,
    REFIID ReturnedInterface,
    void** ppResource)
{
    return m_real->OpenSharedResource(hResource, ReturnedInterface, ppResource);
}

// ── Forwarded: Format / multisample / counter checks ────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckFormatSupport(
    DXGI_FORMAT Format,
    UINT* pFormatSupport)
{
    return m_real->CheckFormatSupport(Format, pFormatSupport);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckMultisampleQualityLevels(
    DXGI_FORMAT Format,
    UINT SampleCount,
    UINT* pNumQualityLevels)
{
    return m_real->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}

void STDMETHODCALLTYPE WrappedDevice::CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo)
{
    m_real->CheckCounterInfo(pCounterInfo);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckCounter(
    const D3D11_COUNTER_DESC* pDesc,
    D3D11_COUNTER_TYPE* pType,
    UINT* pActiveCounters,
    LPSTR szName, UINT* pNameLength,
    LPSTR szUnits, UINT* pUnitsLength,
    LPSTR szDescription, UINT* pDescriptionLength)
{
    return m_real->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckFeatureSupport(
    D3D11_FEATURE Feature,
    void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    return m_real->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

// ── Forwarded: Private data ─────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::GetPrivateData(
    REFGUID guid,
    UINT* pDataSize,
    void* pData)
{
    return m_real->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::SetPrivateData(
    REFGUID guid,
    UINT DataSize,
    const void* pData)
{
    return m_real->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::SetPrivateDataInterface(
    REFGUID guid,
    const IUnknown* pData)
{
    return m_real->SetPrivateDataInterface(guid, pData);
}

// ── Forwarded: Device info ──────────────────────────────────────────────────

D3D_FEATURE_LEVEL STDMETHODCALLTYPE WrappedDevice::GetFeatureLevel()
{
    return m_real->GetFeatureLevel();
}

UINT STDMETHODCALLTYPE WrappedDevice::GetCreationFlags()
{
    return m_real->GetCreationFlags();
}

HRESULT STDMETHODCALLTYPE WrappedDevice::GetDeviceRemovedReason()
{
    return m_real->GetDeviceRemovedReason();
}

HRESULT STDMETHODCALLTYPE WrappedDevice::SetExceptionMode(UINT RaiseFlags)
{
    return m_real->SetExceptionMode(RaiseFlags);
}

UINT STDMETHODCALLTYPE WrappedDevice::GetExceptionMode()
{
    return m_real->GetExceptionMode();
}

} // namespace SB::Proxy
