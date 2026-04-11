#pragma once
//=============================================================================
//  ENBSwapChain.h — Lightweight IDXGISwapChain Wrapper for ENB GTA V
//
//  Intercepts Present() to run the ENB post-processing pipeline before the
//  frame is presented. All other methods forward directly to the real
//  swapchain. Handles backbuffer RTV/SRV caching and D3D11 state
//  save/restore around pipeline execution.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

class ENBSwapChain : public IDXGISwapChain
{
public:
    ENBSwapChain(IDXGISwapChain* real, ID3D11Device* device, ID3D11DeviceContext* ctx);
    ~ENBSwapChain();

    // ── IUnknown ────────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // ── IDXGIObject ─────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;

    // ── IDXGIDeviceSubObject ────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;

    // ── IDXGISwapChain ──────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override;
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                            DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override;
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override;
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override;
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override;

    // ── Accessors ───────────────────────────────────────────────────────
    IDXGISwapChain* GetReal() const { return m_real; }

private:
    void ReleaseBackbufferResources();
    bool EnsureBackbufferResources();

    IDXGISwapChain*           m_real    = nullptr;
    ID3D11Device*             m_device  = nullptr;
    ID3D11DeviceContext*      m_ctx     = nullptr;
    volatile LONG             m_refCount = 1;

    // Cached backbuffer resources (invalidated on ResizeBuffers)
    ID3D11Texture2D*          m_backbufferTex = nullptr;
    ID3D11RenderTargetView*   m_backbufferRTV = nullptr;

    // Copy of the backbuffer for shader input (TextureColor).
    // We can't bind the backbuffer as both RTV and SRV simultaneously,
    // so we CopyResource the backbuffer into this texture, then bind
    // its SRV as TextureColor while rendering to the backbuffer RTV.
    ID3D11Texture2D*          m_frameCopyTex = nullptr;
    ID3D11ShaderResourceView* m_frameCopySRV = nullptr;

    // Original backbuffer SRV (may not be creatable on all formats)
    ID3D11ShaderResourceView* m_backbufferSRV = nullptr;
};
