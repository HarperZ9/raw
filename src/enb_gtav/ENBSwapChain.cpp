//=============================================================================
//  ENBSwapChain.cpp — Lightweight IDXGISwapChain Wrapper for ENB GTA V
//
//  Intercepts Present() to:
//    1. Cache backbuffer RTV/SRV
//    2. Save full D3D11 pipeline state
//    3. Run ENB post-processing via g_Pipeline.Execute()
//    4. Restore all saved state
//    5. Forward Present() to the real swapchain
//    6. Increment frame counter and invoke post-present callbacks
//=============================================================================

#include "ENBSwapChain.h"
#include "ENBState.h"
#include "PostProcessPipeline.h"
#include "ENBLog.h"

// ---------------------------------------------------------------------------
//  Forward declarations for API functions (defined in ENBExports_API.cpp)
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void API_BeforePresent();
extern "C" __declspec(dllexport) void API_AfterPresent();

// Route swapchain logs through file logger
#define SwapLog(...) g_Log.Log(__VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════

ENBSwapChain::ENBSwapChain(IDXGISwapChain* real, ID3D11Device* device, ID3D11DeviceContext* ctx)
    : m_real(real)
    , m_device(device)
    , m_ctx(ctx)
    , m_refCount(1)
{
    if (m_real) m_real->AddRef();
    SwapLog("[ENB] ENBSwapChain wrapper created (real=%p)\n", m_real);
}

ENBSwapChain::~ENBSwapChain()
{
    ReleaseBackbufferResources();
    if (m_real)
    {
        m_real->Release();
        m_real = nullptr;
    }
    SwapLog("[ENB] ENBSwapChain wrapper destroyed\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Backbuffer resource management
// ═══════════════════════════════════════════════════════════════════════════

void ENBSwapChain::ReleaseBackbufferResources()
{
    if (m_frameCopySRV)  { m_frameCopySRV->Release();  m_frameCopySRV  = nullptr; }
    if (m_frameCopyTex)  { m_frameCopyTex->Release();  m_frameCopyTex  = nullptr; }
    if (m_backbufferSRV) { m_backbufferSRV->Release(); m_backbufferSRV = nullptr; }
    if (m_backbufferRTV) { m_backbufferRTV->Release(); m_backbufferRTV = nullptr; }
    if (m_backbufferTex) { m_backbufferTex->Release(); m_backbufferTex = nullptr; }
}

bool ENBSwapChain::EnsureBackbufferResources()
{
    // Already cached
    if (m_backbufferRTV && m_frameCopySRV)
        return true;

    // Get backbuffer texture
    HRESULT hr = m_real->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                   reinterpret_cast<void**>(&m_backbufferTex));
    if (FAILED(hr) || !m_backbufferTex)
    {
        SwapLog("[ENB] Failed to get backbuffer texture: 0x%08X\n", hr);
        return false;
    }

    D3D11_TEXTURE2D_DESC texDesc;
    m_backbufferTex->GetDesc(&texDesc);

    // Create RTV for the backbuffer (output target for ENB shaders)
    hr = m_device->CreateRenderTargetView(m_backbufferTex, nullptr, &m_backbufferRTV);
    if (FAILED(hr))
    {
        SwapLog("[ENB] Failed to create backbuffer RTV: 0x%08X\n", hr);
        ReleaseBackbufferResources();
        return false;
    }

    // Create a COPY of the backbuffer texture for shader input.
    // The backbuffer itself can't be bound as SRV and RTV simultaneously.
    // We CopyResource the backbuffer into this texture each frame,
    // then bind its SRV as TextureColor for the ENB shaders.
    D3D11_TEXTURE2D_DESC copyDesc = texDesc;
    copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copyDesc.MiscFlags = 0;

    hr = m_device->CreateTexture2D(&copyDesc, nullptr, &m_frameCopyTex);
    if (FAILED(hr))
    {
        SwapLog("[ENB] Failed to create frame copy texture: 0x%08X\n", hr);
        ReleaseBackbufferResources();
        return false;
    }

    // Create SRV for the copy texture
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_device->CreateShaderResourceView(m_frameCopyTex, &srvDesc, &m_frameCopySRV);
    if (FAILED(hr))
    {
        SwapLog("[ENB] Failed to create frame copy SRV: 0x%08X\n", hr);
        ReleaseBackbufferResources();
        return false;
    }

    SwapLog("[ENB] Backbuffer resources created: %ux%u fmt=%d (copy texture for TextureColor)\n",
            texDesc.Width, texDesc.Height, texDesc.Format);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  IUnknown
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    // Forward unknown interfaces to the real swapchain
    return m_real->QueryInterface(riid, ppvObject);
}

ULONG STDMETHODCALLTYPE ENBSwapChain::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE ENBSwapChain::Release()
{
    ULONG count = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (count == 0)
        delete this;
    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGIObject
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return m_real->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE ENBSwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return m_real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return m_real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetParent(REFIID riid, void** ppParent)
{
    return m_real->GetParent(riid, ppParent);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGIDeviceSubObject
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetDevice(REFIID riid, void** ppDevice)
{
    return m_real->GetDevice(riid, ppDevice);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — Present (main intercept point)
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::Present(UINT SyncInterval, UINT Flags)
{
    // Skip processing for DXGI_PRESENT_TEST
    if (Flags & DXGI_PRESENT_TEST)
        return m_real->Present(SyncInterval, Flags);

    // Reentrance guard — prevent recursive Present calls from overlay tools
    static volatile LONG s_inPresent = 0;
    if (InterlockedCompareExchange(&s_inPresent, 1, 0) != 0)
        return m_real->Present(SyncInterval, Flags);

    // Log once on first Present call
    static bool s_firstPresent = true;
    if (s_firstPresent)
    {
        SwapLog("[ENB] First Present() call — swapchain wrapper active, pipeline will execute\n");
        s_firstPresent = false;
    }

    // ── Pre-present: update game state, callbacks, input ────────────
    API_BeforePresent();

    // ── Run post-processing pipeline ────────────────────────────────
    // Phase C: Pipeline enabled
    if (g_ENB.useEffect && !InterlockedCompareExchange(&g_ENB.rawD3DMode, 0, 0))
    {
        if (EnsureBackbufferResources())
        {
            // ────────────────────────────────────────────────────────
            //  Save full D3D11 pipeline state
            // ────────────────────────────────────────────────────────

            // OM — render targets, depth stencil, blend, depth-stencil state
            ID3D11RenderTargetView*   savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            ID3D11DepthStencilView*   savedDSV = nullptr;
            m_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);

            ID3D11BlendState*         savedBlendState = nullptr;
            FLOAT                     savedBlendFactor[4] = {};
            UINT                      savedSampleMask = 0;
            m_ctx->OMGetBlendState(&savedBlendState, savedBlendFactor, &savedSampleMask);

            ID3D11DepthStencilState*  savedDepthStencilState = nullptr;
            UINT                      savedStencilRef = 0;
            m_ctx->OMGetDepthStencilState(&savedDepthStencilState, &savedStencilRef);

            // RS — viewports, scissor rects, rasterizer state
            UINT                      savedNumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_VIEWPORT            savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            m_ctx->RSGetViewports(&savedNumViewports, savedViewports);

            UINT                      savedNumScissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_RECT                savedScissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            m_ctx->RSGetScissorRects(&savedNumScissors, savedScissors);

            ID3D11RasterizerState*    savedRasterizerState = nullptr;
            m_ctx->RSGetState(&savedRasterizerState);

            // IA — input layout, vertex buffers, index buffer, topology
            ID3D11InputLayout*        savedInputLayout = nullptr;
            m_ctx->IAGetInputLayout(&savedInputLayout);

            D3D11_PRIMITIVE_TOPOLOGY  savedTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            m_ctx->IAGetPrimitiveTopology(&savedTopology);

            ID3D11Buffer*             savedVBs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
            UINT                      savedVBStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
            UINT                      savedVBOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
            // Save first 4 VB slots (sufficient for most cases, avoids huge stack)
            static constexpr UINT kMaxVBSlots = 4;
            m_ctx->IAGetVertexBuffers(0, kMaxVBSlots, savedVBs, savedVBStrides, savedVBOffsets);

            ID3D11Buffer*             savedIB = nullptr;
            DXGI_FORMAT               savedIBFormat = DXGI_FORMAT_UNKNOWN;
            UINT                      savedIBOffset = 0;
            m_ctx->IAGetIndexBuffer(&savedIB, &savedIBFormat, &savedIBOffset);

            // VS — shader, constant buffers, samplers, shader resources
            ID3D11VertexShader*       savedVS = nullptr;
            m_ctx->VSGetShader(&savedVS, nullptr, nullptr);

            static constexpr UINT kMaxCBSlots = 4;
            ID3D11Buffer*             savedVSCBs[kMaxCBSlots] = {};
            m_ctx->VSGetConstantBuffers(0, kMaxCBSlots, savedVSCBs);

            static constexpr UINT kMaxSamplerSlots = 4;
            ID3D11SamplerState*       savedVSSamplers[kMaxSamplerSlots] = {};
            m_ctx->VSGetSamplers(0, kMaxSamplerSlots, savedVSSamplers);

            static constexpr UINT kMaxSRVSlots = 8;
            ID3D11ShaderResourceView* savedVSSRVs[kMaxSRVSlots] = {};
            m_ctx->VSGetShaderResources(0, kMaxSRVSlots, savedVSSRVs);

            // PS — shader, constant buffers, samplers, shader resources
            ID3D11PixelShader*        savedPS = nullptr;
            m_ctx->PSGetShader(&savedPS, nullptr, nullptr);

            ID3D11Buffer*             savedPSCBs[kMaxCBSlots] = {};
            m_ctx->PSGetConstantBuffers(0, kMaxCBSlots, savedPSCBs);

            ID3D11SamplerState*       savedPSSamplers[kMaxSamplerSlots] = {};
            m_ctx->PSGetSamplers(0, kMaxSamplerSlots, savedPSSamplers);

            ID3D11ShaderResourceView* savedPSSRVs[kMaxSRVSlots] = {};
            m_ctx->PSGetShaderResources(0, kMaxSRVSlots, savedPSSRVs);

            // GS — shader only
            ID3D11GeometryShader*     savedGS = nullptr;
            m_ctx->GSGetShader(&savedGS, nullptr, nullptr);

            ID3D11Buffer*             savedGSCBs[kMaxCBSlots] = {};
            m_ctx->GSGetConstantBuffers(0, kMaxCBSlots, savedGSCBs);

            // ────────────────────────────────────────────────────────
            //  Execute the ENB post-processing pipeline (SEH protected)
            // ────────────────────────────────────────────────────────

            // Copy the backbuffer into our frame copy texture.
            // This is the key step: the backbuffer can't be both RTV and SRV,
            // so we copy it and use the copy as TextureColor (shader input).
            m_ctx->CopyResource(m_frameCopyTex, m_backbufferTex);

            static bool s_pipelineLogged = false;
            if (!s_pipelineLogged)
            {
                SwapLog("[ENB] Pipeline executing for first time (RTV=%p, frameCopySRV=%p)\n",
                        m_backbufferRTV, m_frameCopySRV);
                s_pipelineLogged = true;
            }

            __try
            {
                // Execute pipeline: reads from m_frameCopySRV (TextureColor),
                // writes to m_backbufferRTV (the actual backbuffer).
                g_Pipeline.Execute(m_device, m_ctx,
                                   m_backbufferRTV, m_frameCopySRV,
                                   nullptr /* depthSRV: no game depth available yet */);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Pipeline crashed — disable effects to prevent repeated crashes
                SwapLog("[ENB] CRITICAL: Pipeline Execute crashed! Disabling effects.\n");
                g_ENB.useEffect = false;
            }

            // ────────────────────────────────────────────────────────
            //  Restore full D3D11 pipeline state
            // ────────────────────────────────────────────────────────

            // OM
            m_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
            for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                if (savedRTVs[i]) savedRTVs[i]->Release();
            if (savedDSV) savedDSV->Release();

            m_ctx->OMSetBlendState(savedBlendState, savedBlendFactor, savedSampleMask);
            if (savedBlendState) savedBlendState->Release();

            m_ctx->OMSetDepthStencilState(savedDepthStencilState, savedStencilRef);
            if (savedDepthStencilState) savedDepthStencilState->Release();

            // RS
            m_ctx->RSSetViewports(savedNumViewports, savedViewports);
            m_ctx->RSSetScissorRects(savedNumScissors, savedScissors);
            m_ctx->RSSetState(savedRasterizerState);
            if (savedRasterizerState) savedRasterizerState->Release();

            // IA
            m_ctx->IASetInputLayout(savedInputLayout);
            if (savedInputLayout) savedInputLayout->Release();

            m_ctx->IASetPrimitiveTopology(savedTopology);

            m_ctx->IASetVertexBuffers(0, kMaxVBSlots, savedVBs, savedVBStrides, savedVBOffsets);
            for (UINT i = 0; i < kMaxVBSlots; i++)
                if (savedVBs[i]) savedVBs[i]->Release();

            m_ctx->IASetIndexBuffer(savedIB, savedIBFormat, savedIBOffset);
            if (savedIB) savedIB->Release();

            // VS
            m_ctx->VSSetShader(savedVS, nullptr, 0);
            if (savedVS) savedVS->Release();

            m_ctx->VSSetConstantBuffers(0, kMaxCBSlots, savedVSCBs);
            for (UINT i = 0; i < kMaxCBSlots; i++)
                if (savedVSCBs[i]) savedVSCBs[i]->Release();

            m_ctx->VSSetSamplers(0, kMaxSamplerSlots, savedVSSamplers);
            for (UINT i = 0; i < kMaxSamplerSlots; i++)
                if (savedVSSamplers[i]) savedVSSamplers[i]->Release();

            m_ctx->VSSetShaderResources(0, kMaxSRVSlots, savedVSSRVs);
            for (UINT i = 0; i < kMaxSRVSlots; i++)
                if (savedVSSRVs[i]) savedVSSRVs[i]->Release();

            // PS
            m_ctx->PSSetShader(savedPS, nullptr, 0);
            if (savedPS) savedPS->Release();

            m_ctx->PSSetConstantBuffers(0, kMaxCBSlots, savedPSCBs);
            for (UINT i = 0; i < kMaxCBSlots; i++)
                if (savedPSCBs[i]) savedPSCBs[i]->Release();

            m_ctx->PSSetSamplers(0, kMaxSamplerSlots, savedPSSamplers);
            for (UINT i = 0; i < kMaxSamplerSlots; i++)
                if (savedPSSamplers[i]) savedPSSamplers[i]->Release();

            m_ctx->PSSetShaderResources(0, kMaxSRVSlots, savedPSSRVs);
            for (UINT i = 0; i < kMaxSRVSlots; i++)
                if (savedPSSRVs[i]) savedPSSRVs[i]->Release();

            // GS
            m_ctx->GSSetShader(savedGS, nullptr, 0);
            if (savedGS) savedGS->Release();

            m_ctx->GSSetConstantBuffers(0, kMaxCBSlots, savedGSCBs);
            for (UINT i = 0; i < kMaxCBSlots; i++)
                if (savedGSCBs[i]) savedGSCBs[i]->Release();
        }
    }

    // ── Forward Present to real swapchain ───────────────────────────
    HRESULT hr = m_real->Present(SyncInterval, Flags);

    // ── Post-present: frame counter, BeginFrame callbacks ───────────
    API_AfterPresent();

    // Clear reentrance guard
    InterlockedExchange(&s_inPresent, 0);

    return hr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — GetBuffer
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    return m_real->GetBuffer(Buffer, riid, ppSurface);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — SetFullscreenState / GetFullscreenState
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    return m_real->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return m_real->GetFullscreenState(pFullscreen, ppTarget);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — GetDesc
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc)
{
    return m_real->GetDesc(pDesc);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — ResizeBuffers (must release cached backbuffer resources)
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                       DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    SwapLog("[ENB] ResizeBuffers: %ux%u\n", Width, Height);

    // Notify plugins before resize
    g_ENB.InvokeCallbacks(ENBCallback_PreReset);

    // Release cached backbuffer views — they reference the old buffers
    ReleaseBackbufferResources();

    // Forward to real swapchain
    HRESULT hr = m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (SUCCEEDED(hr))
    {
        // Update screen dimensions in global state
        if (Width > 0 && Height > 0)
        {
            g_ENB.screenWidth  = Width;
            g_ENB.screenHeight = Height;
        }
        else
        {
            // If 0 was passed, query actual size from the new desc
            DXGI_SWAP_CHAIN_DESC desc = {};
            if (SUCCEEDED(m_real->GetDesc(&desc)))
            {
                g_ENB.screenWidth  = desc.BufferDesc.Width;
                g_ENB.screenHeight = desc.BufferDesc.Height;
            }
        }

        g_ENB.UpdateRenderInfo();

        // Notify plugins after resize
        g_ENB.InvokeCallbacks(ENBCallback_PostReset);
    }
    else
    {
        SwapLog("[ENB] ResizeBuffers FAILED: 0x%08X\n", hr);
    }

    return hr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — ResizeTarget
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    return m_real->ResizeTarget(pNewTargetParameters);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IDXGISwapChain — Remaining passthrough methods
// ═══════════════════════════════════════════════════════════════════════════

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return m_real->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return m_real->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE ENBSwapChain::GetLastPresentCount(UINT* pLastPresentCount)
{
    return m_real->GetLastPresentCount(pLastPresentCount);
}
