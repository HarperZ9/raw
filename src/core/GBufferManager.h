#pragma once
//=============================================================================
//  GBufferManager.h — Material ID g-buffer for ground-truth material data
//
//  Creates and manages a screen-resolution R8_UINT texture where each pixel
//  stores the MaterialType (0-8) of the geometry rendered at that position.
//
//  The texture has three views:
//    UAV  — bound during opaque pass so patched BSLightingShader PSes can write
//    SRV  — bound during ENB passes so post-processing shaders can read
//    (Clear) — zeroed each frame before opaque rendering begins
//
//  Phase 2 of ENB-only material-aware rendering pipeline.
//=============================================================================

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;
struct ID3D11ShaderResourceView;
struct IDXGISwapChain;

namespace SB
{
    class GBufferManager
    {
    public:
        static GBufferManager& Get();

        // Initialize: create R8_UINT texture at swap chain resolution.
        // Call once during kDataLoaded after D3D11Hook::Init().
        bool Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain);

        // Recreate the texture if the resolution changes.
        bool Resize(ID3D11Device* a_device, uint32_t a_width, uint32_t a_height);

        // Clear the material ID texture to 0. Call once per frame at the
        // start of OnENBFrame (before the opaque pass begins).
        void Clear(ID3D11DeviceContext* a_ctx);

        // Bind the UAV at a specific slot (e.g., u4) for pixel shader writes.
        // Called by DXBCPatcher before BSLightingShader draws.
        void BindUAV(ID3D11DeviceContext* a_ctx, uint32_t a_slot = 4);

        // Unbind the UAV slot.
        void UnbindUAV(ID3D11DeviceContext* a_ctx, uint32_t a_slot = 4);

        // Hook PSSetShaderResources to inject our SRV at t15 during ENB passes.
        bool HookPSSetShaderResources(ID3D11DeviceContext* a_ctx);

        // Toggle ENB pass flag — when true, the PSSetShaderResources hook
        // also binds our SRV at t15 so ENB shaders can read material data.
        void SetENBPassActive(bool a_active) { m_enbPassActive = a_active; }
        bool IsENBPassActive() const { return m_enbPassActive; }

        // Accessors
        ID3D11ShaderResourceView*  GetSRV() const { return m_srv; }
        ID3D11UnorderedAccessView* GetUAV() const { return m_uav; }
        uint32_t GetWidth()  const { return m_width; }
        uint32_t GetHeight() const { return m_height; }
        bool IsInitialized() const { return m_initialized; }

        void Shutdown();

    private:
        GBufferManager() = default;

        bool CreateTexture(ID3D11Device* a_device, uint32_t a_width, uint32_t a_height);
        void ReleaseTexture();

        ID3D11Texture2D*           m_texture = nullptr;
        ID3D11UnorderedAccessView* m_uav     = nullptr;
        ID3D11ShaderResourceView*  m_srv     = nullptr;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;
        bool m_initialized = false;
        bool m_hooked      = false;
        bool m_enbPassActive = false;
    };

} // namespace SB
