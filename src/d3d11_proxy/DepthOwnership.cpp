// DepthOwnership.cpp — Own the depth buffer. Game writes to ours, we control clears.
// Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

#include "DepthOwnership.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"

namespace SB::Proxy
{

// Format helpers (duplicated from WrappedSwapChain for standalone compilation)
static DXGI_FORMAT ToTypeless(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_TYPELESS;
    default: return fmt;
    }
}
static DXGI_FORMAT ToDSVFormat(DXGI_FORMAT fmt) {
    switch (ToTypeless(fmt)) {
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_D16_UNORM;
    default: return fmt;
    }
}
static DXGI_FORMAT ToSRVFormat(DXGI_FORMAT fmt) {
    switch (ToTypeless(fmt)) {
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

// State
static ID3D11Texture2D*          s_gameTex   = nullptr;  // game's original depth texture
static ID3D11DepthStencilView*   s_gameDSV   = nullptr;  // game's original DSV (for matching)
static ID3D11Texture2D*          s_ownTex    = nullptr;  // our owned depth texture
static ID3D11DepthStencilView*   s_ownDSV    = nullptr;
static ID3D11ShaderResourceView* s_ownSRV    = nullptr;
static bool s_active = false;
static bool s_allowClear = true;  // true at frame start, false after first geometry clear

static bool IsSameResource(ID3D11DepthStencilView* a, ID3D11DepthStencilView* b)
{
    if (!a || !b) return false;
    if (a == b) return true;
    ID3D11Resource *ra = nullptr, *rb = nullptr;
    a->GetResource(&ra);
    b->GetResource(&rb);
    bool same = (ra && rb && ra == rb);
    if (ra) ra->Release();
    if (rb) rb->Release();
    return same;
}

bool DepthOwn_OnClear(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv)
{
    if (!dsv || !ctx) return true;

    // DISABLED — depth ownership causes wrong values.
    // Game writes to its own depth, we read via live SRV.
    return true;

    // First time: identify game's depth, create our own
    if (!s_active) {
        ID3D11Resource* res = nullptr;
        dsv->GetResource(&res);
        if (!res) return true;

        ID3D11Texture2D* tex = nullptr;
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
        res->Release();
        if (!tex) return true;

        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);
        if (desc.Width < 512 || desc.Height < 512) { tex->Release(); return true; }

        ID3D11Device* dev = nullptr;
        ctx->GetDevice(&dev);
        if (!dev) { tex->Release(); return true; }

        DXGI_FORMAT typeless = ToTypeless(desc.Format);
        DXGI_FORMAT dsvFmt = ToDSVFormat(desc.Format);
        DXGI_FORMAT srvFmt = ToSRVFormat(desc.Format);
        if (srvFmt == DXGI_FORMAT_UNKNOWN) { dev->Release(); tex->Release(); return true; }

        // Create our depth texture
        D3D11_TEXTURE2D_DESC od = {};
        od.Width = desc.Width;
        od.Height = desc.Height;
        od.MipLevels = 1;
        od.ArraySize = 1;
        od.Format = typeless;
        od.SampleDesc = desc.SampleDesc;
        od.Usage = D3D11_USAGE_DEFAULT;
        od.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = dev->CreateTexture2D(&od, nullptr, &s_ownTex);
        if (FAILED(hr)) { Log("[DepthOwn] CreateTexture2D FAILED 0x%08X", (unsigned)hr); dev->Release(); tex->Release(); return true; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
        dd.Format = dsvFmt;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = dev->CreateDepthStencilView(s_ownTex, &dd, &s_ownDSV);
        if (FAILED(hr)) { Log("[DepthOwn] CreateDSV FAILED 0x%08X", (unsigned)hr); s_ownTex->Release(); s_ownTex = nullptr; dev->Release(); tex->Release(); return true; }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = srvFmt;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        hr = dev->CreateShaderResourceView(s_ownTex, &sd, &s_ownSRV);
        if (FAILED(hr)) { Log("[DepthOwn] CreateSRV FAILED 0x%08X", (unsigned)hr); s_ownDSV->Release(); s_ownDSV = nullptr; s_ownTex->Release(); s_ownTex = nullptr; dev->Release(); tex->Release(); return true; }

        s_gameTex = tex; // hold ref
        s_gameDSV = dsv;
        s_active = true;
        s_allowClear = true;

        // Expose our SRV/DSV to the SKSE plugin via ProxyInterface
        auto* pi = PG_GetProxyInterface();
        if (pi) {
            pi->gameDepthSRV = s_ownSRV;
            pi->gameDepthDSV = s_ownDSV;
        }

        Log("[DepthOwn] ACTIVE: own=%p DSV=%p SRV=%p game=%p (%ux%u)",
            s_ownTex, s_ownDSV, s_ownSRV, s_gameTex, desc.Width, desc.Height);

        dev->Release();
        // DON'T release tex — we keep the ref as s_gameTex

        // Do the initial clear on our texture
        ctx->ClearDepthStencilView(s_ownDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        return true; // let game's clear proceed on its own texture too
    }

    // On every clear of the game's depth: snapshot our current depth to
    // a read-copy BEFORE the clear wipes it. Then let the clear proceed.
    // This way the game renders normally, and we always have the most recent
    // complete depth available for effects to read.
    if (IsSameResource(dsv, s_gameDSV) && s_ownTex) {
        // Copy current owned depth to read-copy before clear
        // (s_ownTex has whatever geometry was drawn since last clear)
        // Effects read from s_ownSRV which is on s_ownTex directly.
        // We need to preserve the data — but we can't prevent the clear
        // because the game needs it for correct rendering.
        //
        // Solution: let the clear happen. The depth will be valid between
        // the last clear and PostGeometry time. The key insight is that
        // the game clears, draws geometry, then PostGeometry fires.
        // At PostGeometry, our texture has the geometry from the current pass.
    }

    return true;
}

ID3D11DepthStencilView* DepthOwn_SubstituteDSV(ID3D11DepthStencilView* gameDSV)
{
    return gameDSV;  // DISABLED
    if (!s_active || !s_ownDSV || !gameDSV) return gameDSV;

    // If game is binding its main depth DSV, substitute ours
    if (IsSameResource(gameDSV, s_gameDSV)) {
        static uint32_t s_subCount = 0;
        if (s_subCount++ < 10)
            Log("[DepthOwn] Substituted DSV (count=%u)", s_subCount);
        return s_ownDSV;
    }
    return gameDSV;
}

void DepthOwn_OnPresent()
{
    s_allowClear = true;  // next clear is allowed (frame boundary)
}

ID3D11ShaderResourceView* DepthOwn_GetSRV()
{
    return s_ownSRV;
}

ID3D11DepthStencilView* DepthOwn_GetDSV()
{
    return s_ownDSV;
}

bool DepthOwn_IsActive()
{
    return s_active;
}

} // namespace SB::Proxy
