//=============================================================================
//  GBufferManager.cpp — Material ID g-buffer implementation
//
//  Creates a screen-resolution R8_UINT texture with UAV + SRV views.
//  Hooks PSSetShaderResources to inject the SRV at register(t15) during
//  ENB post-processing passes.
//=============================================================================

#include "GBufferManager.h"
#include "LuminanceHistogram.h"
#include "LUTManager.h"
#include "HiZPyramid.h"

#include <d3d11.h>
#include <dxgi.h>
#include <SKSE/SKSE.h>

namespace SB
{
    // ── PSSetShaderResources hook ────────────────────────────────────────

    namespace
    {
        // D3D11 vtable index for PSSetShaderResources
        constexpr uint32_t kVtableIndex_PSSetSRV = 8;

        // SRV injection slot — t15 is high enough to avoid game/ENB conflicts
        constexpr uint32_t kMaterialSRVSlot = 15;

        using PSSetShaderResourcesFn = void(__stdcall*)(
            ID3D11DeviceContext*,
            UINT StartSlot,
            UINT NumViews,
            ID3D11ShaderResourceView* const* ppShaderResourceViews);

        PSSetShaderResourcesFn s_originalPSSetSRV = nullptr;

        void __stdcall HookedPSSetShaderResources(
            ID3D11DeviceContext* a_ctx,
            UINT                a_startSlot,
            UINT                a_numViews,
            ID3D11ShaderResourceView* const* a_views)
        {
            // Always call the original first
            s_originalPSSetSRV(a_ctx, a_startSlot, a_numViews, a_views);

            // During ENB passes, inject SRVs
            auto& gbuf = GBufferManager::Get();
            if (!gbuf.IsENBPassActive()) return;

            // t15: Material ID
            if (gbuf.GetSRV()) {
                ID3D11ShaderResourceView* srv = gbuf.GetSRV();
                s_originalPSSetSRV(a_ctx, kMaterialSRVSlot, 1, &srv);
            }

            // t17: Luminance Histogram
            auto& hist = LuminanceHistogram::Get();
            if (hist.IsInitialized() && hist.IsEnabled()) {
                if (auto* histSrv = hist.GetHistogramSRV()) {
                    s_originalPSSetSRV(a_ctx, LuminanceHistogram::kSRVSlot, 1, &histSrv);
                }
            }

            // t18: Film LUT + s2: Trilinear sampler
            auto& lut = LUTManager::Get();
            if (lut.IsInitialized() && lut.IsEnabled()) {
                if (auto* lutSrv = lut.GetActiveSRV()) {
                    s_originalPSSetSRV(a_ctx, LUTManager::kSRVSlot, 1, &lutSrv);
                    auto* samp = lut.GetSampler();
                    a_ctx->PSSetSamplers(LUTManager::kSamplerSlot, 1, &samp);
                }
            }

            // t19: Hi-Z Depth Pyramid
            auto& hiz = HiZPyramid::Get();
            if (hiz.IsInitialized() && hiz.IsEnabled()) {
                if (auto* hizSrv = hiz.GetSRV()) {
                    s_originalPSSetSRV(a_ctx, HiZPyramid::kSRVSlot, 1, &hizSrv);
                }
            }
        }

    } // anonymous namespace

    // ── GBufferManager implementation ────────────────────────────────────

    GBufferManager& GBufferManager::Get()
    {
        static GBufferManager instance;
        return instance;
    }

    bool GBufferManager::Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain)
    {
        if (m_initialized)
            return true;

        if (!a_device || !a_swapChain) {
            SKSE::log::error("GBufferManager: null device or swap chain");
            return false;
        }

        // Get resolution from the swap chain's back buffer
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (FAILED(a_swapChain->GetDesc(&scDesc))) {
            SKSE::log::error("GBufferManager: failed to get swap chain desc");
            return false;
        }

        if (!CreateTexture(a_device, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height))
            return false;

        m_initialized = true;
        SKSE::log::info("GBufferManager: material ID texture created ({}x{}, R8_UINT, UAV u4, SRV t{})",
            m_width, m_height, kMaterialSRVSlot);
        return true;
    }

    bool GBufferManager::Resize(ID3D11Device* a_device, uint32_t a_width, uint32_t a_height)
    {
        if (a_width == m_width && a_height == m_height)
            return true;

        ReleaseTexture();
        if (!CreateTexture(a_device, a_width, a_height)) {
            SKSE::log::error("GBufferManager: resize to {}x{} failed", a_width, a_height);
            return false;
        }

        SKSE::log::info("GBufferManager: resized to {}x{}", a_width, a_height);
        return true;
    }

    void GBufferManager::Clear(ID3D11DeviceContext* a_ctx)
    {
        if (!m_uav || !a_ctx)
            return;

        // Clear to 0 (MaterialType::General)
        const UINT clearValues[4] = { 0, 0, 0, 0 };
        a_ctx->ClearUnorderedAccessViewUint(m_uav, clearValues);
    }

    void GBufferManager::BindUAV(ID3D11DeviceContext* a_ctx, uint32_t a_slot)
    {
        if (!m_uav || !a_ctx)
            return;

        // OMSetRenderTargetsAndUnorderedAccessViews with UAVStartSlot=a_slot
        // preserves the currently bound render targets (NumRTVs=D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        ID3D11UnorderedAccessView* uavs[] = { m_uav };
        const UINT initCounts[] = { 0 };
        a_ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,  // Keep current RTs
            nullptr,                                        // No RT change
            nullptr,                                        // No depth stencil change
            a_slot,                                         // UAV start slot
            1,                                              // Number of UAVs
            uavs,                                           // Our material UAV
            initCounts);                                    // No append/consume
    }

    void GBufferManager::UnbindUAV(ID3D11DeviceContext* a_ctx, uint32_t a_slot)
    {
        if (!a_ctx)
            return;

        ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
        const UINT initCounts[] = { 0 };
        a_ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
            nullptr, nullptr,
            a_slot, 1, nullUAV, initCounts);
    }

    bool GBufferManager::HookPSSetShaderResources(ID3D11DeviceContext* a_ctx)
    {
        if (m_hooked)
            return true;

        if (!a_ctx) {
            SKSE::log::error("GBufferManager: null context for vtable hook");
            return false;
        }

        auto** vtable = *reinterpret_cast<void***>(a_ctx);

        s_originalPSSetSRV = reinterpret_cast<PSSetShaderResourcesFn>(
            vtable[kVtableIndex_PSSetSRV]);

        DWORD oldProtect;
        if (VirtualProtect(&vtable[kVtableIndex_PSSetSRV],
                sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            vtable[kVtableIndex_PSSetSRV] =
                reinterpret_cast<void*>(&HookedPSSetShaderResources);
            VirtualProtect(&vtable[kVtableIndex_PSSetSRV],
                sizeof(void*), oldProtect, &oldProtect);

            m_hooked = true;
            SKSE::log::info("GBufferManager: PSSetShaderResources hooked (vtable[{}], "
                "SRV injection at t{})", kVtableIndex_PSSetSRV, kMaterialSRVSlot);
            return true;
        }

        SKSE::log::error("GBufferManager: VirtualProtect failed for PSSetShaderResources hook");
        return false;
    }

    void GBufferManager::Shutdown()
    {
        ReleaseTexture();
        m_initialized = false;
        m_hooked = false;
    }

    // ── Private ──────────────────────────────────────────────────────────

    bool GBufferManager::CreateTexture(ID3D11Device* a_device, uint32_t a_width, uint32_t a_height)
    {
        if (!a_device || a_width == 0 || a_height == 0)
            return false;

        // Create R8_UINT texture with UAV + SRV bind flags
        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width              = a_width;
        texDesc.Height             = a_height;
        texDesc.MipLevels          = 1;
        texDesc.ArraySize          = 1;
        texDesc.Format             = DXGI_FORMAT_R8_UINT;
        texDesc.SampleDesc.Count   = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage              = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags          = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags     = 0;
        texDesc.MiscFlags          = 0;

        HRESULT hr = a_device->CreateTexture2D(&texDesc, nullptr, &m_texture);
        if (FAILED(hr)) {
            SKSE::log::error("GBufferManager: CreateTexture2D failed (hr={:#X})", static_cast<uint32_t>(hr));
            return false;
        }

        // Create Unordered Access View (for pixel shader writes)
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format             = DXGI_FORMAT_R8_UINT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        hr = a_device->CreateUnorderedAccessView(m_texture, &uavDesc, &m_uav);
        if (FAILED(hr)) {
            SKSE::log::error("GBufferManager: CreateUnorderedAccessView failed (hr={:#X})",
                static_cast<uint32_t>(hr));
            ReleaseTexture();
            return false;
        }

        // Create Shader Resource View (for ENB shader reads)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = DXGI_FORMAT_R8_UINT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        hr = a_device->CreateShaderResourceView(m_texture, &srvDesc, &m_srv);
        if (FAILED(hr)) {
            SKSE::log::error("GBufferManager: CreateShaderResourceView failed (hr={:#X})",
                static_cast<uint32_t>(hr));
            ReleaseTexture();
            return false;
        }

        m_width  = a_width;
        m_height = a_height;
        return true;
    }

    void GBufferManager::ReleaseTexture()
    {
        if (m_srv)     { m_srv->Release();     m_srv     = nullptr; }
        if (m_uav)     { m_uav->Release();     m_uav     = nullptr; }
        if (m_texture) { m_texture->Release();  m_texture = nullptr; }
        m_width  = 0;
        m_height = 0;
    }

} // namespace SB
