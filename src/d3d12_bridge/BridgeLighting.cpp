// The bridge's side of the lighting pass: the surface targets, the game's shadow mask and the
// sun cascades cross to D3D12 each frame, with the decoded lights. Original work, MIT.
#include "BridgeImpl.h"
#include "ShadowAtlas_CS.h"   // g_ShadowAtlasCS (fxc, cs_5_0)

namespace raw::bridge
{
namespace
{
constexpr D3D12_RESOURCE_FLAGS kColour = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
constexpr D3D12_RESOURCE_FLAGS kStorage = kColour | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

bool SameSize(ID3D11Texture2D* t, UINT w, UINT h, DXGI_FORMAT f)
{
    D3D11_TEXTURE2D_DESC d{};
    t->GetDesc(&d);
    return d.Width == w && d.Height == h && d.SampleDesc.Count == 1 && d.ArraySize == 1 && (f == DXGI_FORMAT_UNKNOWN || d.Format == f);
}
} // namespace

bool D3D12Bridge::Impl::CreateLightingShared()
{
    const UINT side = cfg.shadowMapSize;
    SharedTex& atlas = shared[static_cast<UINT>(Shared::ShadowAtlas)];
    if (!CreateShared(shared[static_cast<UINT>(Shared::Albedo)], DXGI_FORMAT_R8G8B8A8_UNORM, kColour, "albedo-in")
        || !CreateShared(shared[static_cast<UINT>(Shared::Specular)], DXGI_FORMAT_R8G8B8A8_UNORM, kColour, "specular-in")
        || !CreateShared(shared[static_cast<UINT>(Shared::ShadowMask)], DXGI_FORMAT_R8G8B8A8_UNORM, kColour, "shadow-mask-in")
        || !CreateShared(atlas, DXGI_FORMAT_R32_FLOAT, kStorage, "shadow-atlas", side * 2, side)
        || !CreateShared(shared[static_cast<UINT>(Shared::Emissive)], DXGI_FORMAT_R16G16B16A16_FLOAT, kColour, "emissive-in"))
        return false;
    HRESULT hr = dev11->CreateComputeShader(g_ShadowAtlasCS, sizeof(g_ShadowAtlasCS), nullptr, &shadowCS);
    if (FAILED(hr)) { log("[D3D12Bridge] shadow atlas shader 0x%08X", (unsigned)hr); return false; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_FLOAT;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    hr = dev11->CreateUnorderedAccessView(atlas.tex11.Get(), &ud, &shadowUAV);
    if (FAILED(hr)) { log("[D3D12Bridge] shadow atlas UAV 0x%08X", (unsigned)hr); return false; }
    log("[D3D12Bridge] lighting inputs shared: albedo, specular, shadow mask, a %ux%u cascade atlas (%.0f MB)",
        side * 2, side, double(side) * side * 8 / (1024.0 * 1024.0));
    return true;
}

bool D3D12Bridge::CaptureSurface(ID3D11Texture2D* albedo, ID3D11Texture2D* specular, ID3D11Texture2D* shadowMask, ID3D11Texture2D* emissive)
{
    if (!m->ready || !m->cfg.lightingPass || !albedo || !specular || !shadowMask) return false;   // a missing mask: pass any RGBA8 target
    const UINT w = m->cfg.width, h = m->cfg.height;
    if (!SameSize(albedo, w, h, DXGI_FORMAT_R8G8B8A8_UNORM) || !SameSize(specular, w, h, DXGI_FORMAT_R8G8B8A8_UNORM)
        || !SameSize(shadowMask, w, h, DXGI_FORMAT_UNKNOWN)) return false;
    D3D11_TEXTURE2D_DESC md{};
    shadowMask->GetDesc(&md);
    if (md.Format != DXGI_FORMAT_R8G8B8A8_UNORM && md.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS) return false;
    auto* c = m->ctx11.Get();
    c->CopyResource(m->shared[static_cast<UINT>(Shared::Albedo)].tex11.Get(), albedo);
    c->CopyResource(m->shared[static_cast<UINT>(Shared::Specular)].tex11.Get(), specular);
    c->CopyResource(m->shared[static_cast<UINT>(Shared::ShadowMask)].tex11.Get(), shadowMask);
    if (emissive && SameSize(emissive, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT))
        c->CopyResource(m->shared[static_cast<UINT>(Shared::Emissive)].tex11.Get(), emissive);
    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    s.hasSurface = true;
    return true;
}

bool D3D12Bridge::CaptureShadows(ID3D11Texture2D* cascades)
{
    if (!m->ready || !m->cfg.lightingPass || !cascades) return false;
    D3D11_TEXTURE2D_DESC d{};
    cascades->GetDesc(&d);
    const UINT side = m->cfg.shadowMapSize;
    if (d.Width != side || d.Height != side || d.ArraySize < 2 || d.Format != DXGI_FORMAT_R16_TYPELESS) {
        static int logged = 0;
        if (logged++ < 3) m->log("[D3D12Bridge] cascades are %ux%u x%u format %d; the atlas expects %ux%u x2 R16 (ShadowMapSize)",
                              d.Width, d.Height, d.ArraySize, (int)d.Format, side, side);
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R16_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MipLevels = 1;
    sd.Texture2DArray.ArraySize = 2;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m->dev11->CreateShaderResourceView(cascades, &sd, &srv))) return false;
    auto* c = m->ctx11.Get();
    ComPtr<ID3D11ComputeShader> oldCS;
    ComPtr<ID3D11ShaderResourceView> oldSRV;
    ComPtr<ID3D11UnorderedAccessView> oldUAV;
    c->CSGetShader(&oldCS, nullptr, nullptr);
    c->CSGetShaderResources(0, 1, &oldSRV);
    c->CSGetUnorderedAccessViews(0, 1, &oldUAV);
    ID3D11ShaderResourceView* s0 = srv.Get();
    ID3D11UnorderedAccessView* u0 = m->shadowUAV.Get();
    c->CSSetShader(m->shadowCS.Get(), nullptr, 0);
    c->CSSetShaderResources(0, 1, &s0);
    c->CSSetUnorderedAccessViews(0, 1, &u0, nullptr);
    c->Dispatch((side * 2 + 7) / 8, (side + 7) / 8, 1);
    s0 = oldSRV.Get();
    u0 = oldUAV.Get();
    const UINT keep = ~0u;
    c->CSSetUnorderedAccessViews(0, 1, &u0, &keep);
    c->CSSetShaderResources(0, 1, &s0);
    c->CSSetShader(oldCS.Get(), nullptr, 0);
    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    s.hasShadows = true;
    return true;
}

void D3D12Bridge::SetLighting(const SceneLighting& lighting) { m->lighting = lighting; }

} // namespace raw::bridge
