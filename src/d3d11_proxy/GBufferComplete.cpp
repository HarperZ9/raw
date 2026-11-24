// GBufferComplete: spans, targets and the per-frame bookkeeping. See the
// header. Original work, MIT.
#include "GBufferState.h"
#include "ProxyLog.h"

#include <cstring>
#include <string>

namespace SB::Proxy
{
namespace gbuffer
{
State& S()
{
    static State s;
    return s;
}

namespace
{
struct View
{
    ID3D11Resource* res = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0, height = 0;
};

View Describe(ID3D11View* v)
{
    View out;
    if (!v) return out;
    v->GetResource(&out.res);
    if (!out.res) return out;
    out.res->Release();                           // compared by address only
    D3D11_RESOURCE_DIMENSION dim{};
    out.res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return out;
    D3D11_TEXTURE2D_DESC d{};
    static_cast<ID3D11Texture2D*>(out.res)->GetDesc(&d);
    if (d.SampleDesc.Count == 1) { out.format = d.Format; out.width = d.Width; out.height = d.Height; }
    return out;
}

void Destroy(Target& t)
{
    if (t.rtv) t.rtv->Release();
    if (t.tex) t.tex->Release();
    t = {};
}

bool Create(State& s, Target& t, DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width = s.width; d.Height = s.height; d.MipLevels = d.ArraySize = 1; d.SampleDesc.Count = 1;
    d.Format = format; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(s.device->CreateTexture2D(&d, nullptr, &t.tex)) && SUCCEEDED(s.device->CreateRenderTargetView(t.tex, nullptr, &t.rtv));
}

bool EnsureTargets(State& s, ID3D11DeviceContext* real, UINT w, UINT h)
{
    if (s.albedo.rtv && s.width == w && s.height == h) return true;
    if (!s.device) real->GetDevice(&s.device);    // kept for the life of the process
    Destroy(s.albedo); Destroy(s.specular); Destroy(s.emissive); Destroy(s.layer);
    s.width = w; s.height = h;
    const bool ok = Create(s, s.albedo, DXGI_FORMAT_R8G8B8A8_UNORM) && Create(s, s.specular, DXGI_FORMAT_R8G8B8A8_UNORM) &&
                    Create(s, s.emissive, DXGI_FORMAT_R16G16B16A16_FLOAT) && Create(s, s.layer, DXGI_FORMAT_R32G32B32A32_FLOAT);
    Log("[GBuffer] targets %ux%u: %s (albedo and specular RGBA8, emissive (or soft light) RGBA16F, layer RGBA32F, %.0f MB)", w, h,
        ok ? "created" : "FAILED", double(w) * h * (4 + 4 + 4 + 16) / (1024.0 * 1024.0));
    return ok;
}

void ClearTargets(State& s, ID3D11DeviceContext* real)
{
    const float zero[4] = {0, 0, 0, 0}, empty[4] = {1, 0, 0, 0};   // layer: depth 1, no normal, not covered
    real->ClearRenderTargetView(s.albedo.rtv, zero);
    real->ClearRenderTargetView(s.specular.rtv, zero);
    real->ClearRenderTargetView(s.emissive.rtv, zero);
    real->ClearRenderTargetView(s.layer.rtv, empty);
}

// Names the span the game opens with this target list.
Span Classify(State& s, ID3D11DeviceContext* real, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
    if (!rtvs || !dsv) return Span::None;
    while (n > 0 && !rtvs[n - 1]) --n;
    if (n == 0 || n > 4) return Span::None;
    View v[4];
    for (UINT k = 0; k < n; ++k) v[k] = Describe(rtvs[k]);
    const View depth = Describe(dsv);
    const UINT w = v[0].width, h = v[0].height;
    auto full = [&](UINT k, DXGI_FORMAT f) { return k < n && v[k].format == f && v[k].width == w && v[k].height == h; };
    // Universal opaque G-Buffer detection across ALL SkyrimPrefs.ini configurations (TAA=0/1, Snow=0/1, SAO=0/1):
    //   n=4 (TAA+Snow): [0]=RGBA16F, [1]=RG16F, [2]=RGBA8 (normals), [3]=R8G8 (mask)
    //   n=3 (TAA only): [0]=RGBA16F, [1]=RG16F, [2]=RGBA8 (normals)
    //   n=3 (Snow only): [0]=RGBA16F, [1]=RGBA8 (normals), [2]=R8G8 (mask)
    //   n=2 (minimal):  [0]=RGBA16F, [1]=RGBA8 (normals)
    int motionIdx = -1, normalIdx = -1;
    if (n >= 2 && full(0, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        if (n >= 3 && full(1, DXGI_FORMAT_R16G16_FLOAT) && full(2, DXGI_FORMAT_R8G8B8A8_UNORM) &&
            (n == 3 || full(3, DXGI_FORMAT_R8G8_UNORM))) {
            motionIdx = 1; normalIdx = 2;
        } else if (full(1, DXGI_FORMAT_R8G8B8A8_UNORM) && (n == 2 || full(2, DXGI_FORMAT_R8G8_UNORM))) {
            normalIdx = 1;
        }
    }
    if (normalIdx >= 0) {
        if (!EnsureTargets(s, real, w, h)) return Span::None;
        s.sceneHdr = v[0].res; s.motion = motionIdx >= 0 ? v[motionIdx].res : nullptr; s.normal = v[normalIdx].res; s.mainDepth = depth.res;
        if (s.normalRtv != rtvs[normalIdx]) {
            if (s.normalRtv) s.normalRtv->Release();
            s.normalRtv = rtvs[normalIdx];
            s.normalRtv->AddRef();
        }
        if (!s.opaqueSeen) { ClearTargets(s, real); s.opaqueSeen = true; }
        return Span::Opaque;
    }
    if (!s.opaqueSeen || w != s.width || h != s.height) return Span::None;
    if (n == 2 && v[0].res == s.sceneHdr && v[1].res == s.motion && depth.res != s.mainDepth) return Span::FirstPerson;
    if (n == 3 && v[0].res == s.sceneHdr && full(1, DXGI_FORMAT_R8G8_UNORM) && full(2, DXGI_FORMAT_R16G16B16A16_FLOAT)) return Span::Blended;
    if (n == 1 && v[0].res != s.sceneHdr && full(0, DXGI_FORMAT_R16G16B16A16_FLOAT) && depth.res == s.mainDepth) return Span::Water;
    return Span::None;
}
} // namespace
} // namespace gbuffer

using namespace gbuffer;

void GBuffer_LoadConfig()
{
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string ini(path);
    ini = ini.substr(0, ini.rfind('\\') + 1) + "d3d11_proxy.ini";
    g_gbufferActive = GetPrivateProfileIntA("GBuffer", "Enabled", 0, ini.c_str()) != 0;
    Log("[GBuffer] config: enabled=%d", g_gbufferActive ? 1 : 0);
}

bool GBuffer_OnTargets(ID3D11DeviceContext* real, UINT n, ID3D11RenderTargetView* const* rtvs,
                       ID3D11DepthStencilView* dsv, ID3D11RenderTargetView** out, UINT* outCount)
{
    State& s = S();
    const bool wasOpen = s.span != Span::None;
    s.span = Classify(s, real, n, rtvs, dsv);
    s.dirty = true;
    if (s.span == Span::None) {
        if (wasOpen && s.substituted) {
            // Back to the game's own shader and blend state.
            real->PSSetShader(s.gamePs, nullptr, 0);
            real->OMSetBlendState(s.gameBlend, s.factor, s.sampleMask);
            s.substituted = false;
        }
        return false;
    }
    ++s.spans[static_cast<int>(s.span)];
    for (UINT k = 0; k < 8; ++k) out[k] = nullptr;
    const UINT own = s.span == Span::Opaque ? (n < 4u ? n : 4u) : s.span == Span::Blended ? 3u : s.span == Span::FirstPerson ? 2u : 1u;
    for (UINT k = 0; k < own; ++k) out[k] = rtvs[k];
    if (s.span == Span::Opaque || s.span == Span::FirstPerson) {
        if (s.span == Span::FirstPerson) out[2] = s.normalRtv;
        out[raw::dxbc::kAlbedoTarget] = s.albedo.rtv;
        out[raw::dxbc::kSpecularTarget] = s.specular.rtv;
        out[raw::dxbc::kEmissiveTarget] = s.emissive.rtv;
        *outCount = 7;
    } else {
        out[raw::dxbc::kLayerTarget] = s.layer.rtv;
        *outCount = 8;
    }
    return true;
}

void GBuffer_OnPresent()
{
    State& s = S();
    if (!g_gbufferActive) return;
    s.opaqueSeen = false;
    s.span = Span::None;
    if (++s.frame % 600 == 0 || (s.frame < 4000 && s.spans[1] && s.frame % 60 == 0)) {
        Log("[GBuffer] frame %llu: spans in the window opaque %u, first person %u, blended %u, water %u; shader swaps %u; "
            "patched shaders %u, left alone %u, refused by the device %u; blend variants %zu",
            static_cast<unsigned long long>(s.frame), s.spans[1], s.spans[2], s.spans[3], s.spans[4], s.swaps, s.patched,
            s.refused, s.createFailed, s.blends.size());
        static const char* kSpan[5] = {"", "opaque", "first person", "blended", "water"};
        for (int k = 1; k < 5; ++k) {
            const uint32_t* d = s.kindDraws[k];
            Log("[GBuffer]   %s: state changes by shader kind: unpatched %u, lighting %u, distant %u, effect %u, water %u, generic %u",
                kSpan[k], d[0], d[1], d[2], d[3], d[4], d[5]);
        }
        memset(s.kindDraws, 0, sizeof s.kindDraws);
        for (uint32_t& c : s.spans) c = 0;
        s.swaps = 0;
    }
}

GBufferTextures GBuffer_Textures()
{
    State& s = S();
    return {s.albedo.tex, s.specular.tex, s.emissive.tex, s.layer.tex};
}

} // namespace SB::Proxy
