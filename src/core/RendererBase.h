#pragma once
//=============================================================================
//  RendererBase — Common state and utilities for all effect renderers
//
//  Every renderer in RAW follows the same pattern: singleton, Initialize
//  from D3D11 objects, per-frame ExecutePass, hot-reloadable shaders.
//  This base class captures the shared state to eliminate duplication.
//
//  Usage:
//    class MyRenderer : public RendererBase {
//    public:
//        static MyRenderer& Get() { static MyRenderer i; return i; }
//        bool Initialize(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*);
//        void Shutdown();
//    };
//
//  In Initialize():  if (!InitBase(dev, ctx, sc)) return false;
//  In Shutdown():     ShutdownBase(); // clears m_initialized + pointers
//=============================================================================

#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

namespace SB
{

class RendererBase
{
public:
    bool     IsInitialized() const { return m_initialized; }
    bool     IsEnabled()     const { return m_enabled; }
    void     SetEnabled(bool v)    { m_enabled = v; }
    uint32_t GetScreenW()    const { return m_screenW; }
    uint32_t GetScreenH()    const { return m_screenH; }

protected:
    RendererBase() = default;

    /// Call from subclass Initialize(). Validates pointers, extracts screen dims.
    bool InitBase(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
    {
        if (!dev || !ctx || !sc) return false;
        m_device  = dev;
        m_context = ctx;
        DXGI_SWAP_CHAIN_DESC desc;
        if (SUCCEEDED(sc->GetDesc(&desc))) {
            m_screenW = desc.BufferDesc.Width;
            m_screenH = desc.BufferDesc.Height;
        }
        return true;
    }

    /// Call from subclass Shutdown(). Resets common state.
    void ShutdownBase()
    {
        m_initialized = false;
        m_device  = nullptr;
        m_context = nullptr;
        m_screenW = 0;
        m_screenH = 0;
        m_frameIndex = 0;
    }

    bool                 m_initialized = false;
    bool                 m_enabled     = false;
    ID3D11Device*        m_device      = nullptr;
    ID3D11DeviceContext* m_context     = nullptr;
    uint32_t             m_screenW     = 0;
    uint32_t             m_screenH     = 0;
    uint32_t             m_frameIndex  = 0;
};

} // namespace SB
