// GameComposite: see the header. Original work, MIT.
#include "GameComposite.h"
#include "ProxyLog.h"
#include "ShaderManager.h"

#include <cstdint>
#include <cstring>

namespace SB::Proxy
{
namespace
{
constexpr uint64_t kCompositeHash     = 0xa9facbe07de6f5d0ull;   // ps_a9facbe07de6f5d0 in reference captures
constexpr uint64_t kCompositeHash1170 = 0xc29d75356434a10aull;   // Skyrim SE 1.6.1170 live composite PS (t0=R11G11B10F, t1=AO, t2=Depth)
constexpr uint64_t kTonemapHash       = 0x99d22a3d198a3b69ull;   // Skyrim tone map PS (never intercept as composite)

struct SrvInfo
{
    bool valid = false;
    ID3D11Resource* res = nullptr;   // compared by address only
    DXGI_FORMAT srvFmt = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT texFmt = DXGI_FORMAT_UNKNOWN;
    UINT w = 0, h = 0;
};

struct State
{
    GameCompositeSwitches sw;
    ID3D11Resource* scene = nullptr;     // compared by address only
    UINT sceneW = 0, sceneH = 0;
    ID3D11ShaderResourceView* white = nullptr;   // 1x1 RGBA8 of 1: no occlusion
    ID3D11ShaderResourceView* farTex = nullptr;    // 1x1 R32F of 1: depth of the sky, no fog
    ID3D11ShaderResourceView* saved[2] = {};     // the game's t1 and t2, held for the draw
    bool swapped = false;
    uint64_t totalMatches = 0;
    uint32_t framesSeen = 0;
    uint64_t loggedHashes[8] = {};
    uint32_t loggedHashCount = 0;
    uint64_t diagHashes[5] = {};
    uint32_t diagLogged = 0;
};
State& S() { static State s; return s; }

bool IsDepthFormat(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
           f == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS ||
           f == DXGI_FORMAT_R32_FLOAT ||
           f == DXGI_FORMAT_R24G8_TYPELESS ||
           f == DXGI_FORMAT_R32G8X24_TYPELESS ||
           f == DXGI_FORMAT_D24_UNORM_S8_UINT ||
           f == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
           f == DXGI_FORMAT_D32_FLOAT;
}

bool IsHdrColorFormat(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           f == DXGI_FORMAT_R11G11B10_FLOAT;
}

bool IsFullSizeHdr16F(ID3D11Resource* r, UINT w, UINT h)
{
    if (!r || w == 0 || h == 0) return false;
    D3D11_RESOURCE_DIMENSION dim{};
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    D3D11_TEXTURE2D_DESC td{};
    static_cast<ID3D11Texture2D*>(r)->GetDesc(&td);
    return td.Width == w && td.Height == h && IsHdrColorFormat(td.Format) && td.SampleDesc.Count == 1;
}

void InspectSrv(ID3D11ShaderResourceView* srv, SrvInfo& out)
{
    out = {};
    if (!srv) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    srv->GetDesc(&sd);
    out.srvFmt = sd.Format;
    ID3D11Resource* r = nullptr;
    srv->GetResource(&r);
    if (!r) return;
    out.res = r;
    D3D11_RESOURCE_DIMENSION dim{};
    r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC td{};
        static_cast<ID3D11Texture2D*>(r)->GetDesc(&td);
        out.texFmt = td.Format;
        out.w = td.Width;
        out.h = td.Height;
        out.valid = (td.Width > 0 && td.Height > 0);
    }
    r->Release();
}

bool RecordUniqueHash(uint64_t* arr, uint32_t& count, uint32_t cap, uint64_t hash)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (arr[i] == hash) return false;
    }
    if (count >= cap) return false;
    arr[count++] = hash;
    return true;
}

bool Make(ID3D11DeviceContext* real)
{
    State& s = S();
    if (s.white && s.farTex) return true;
    ID3D11Device* dev = nullptr;
    real->GetDevice(&dev);
    if (!dev) return false;
    const auto one = [&](DXGI_FORMAT f, const void* texel, ID3D11ShaderResourceView** out) {
        D3D11_TEXTURE2D_DESC td{1, 1, 1, 1, f, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA init{texel, 4, 4};
        ID3D11Texture2D* t = nullptr;
        if (SUCCEEDED(dev->CreateTexture2D(&td, &init, &t))) { dev->CreateShaderResourceView(t, nullptr, out); t->Release(); }
    };
    const uint32_t white = 0xFFFFFFFFu;
    const float farDepth = 1.0f;
    if (!s.white) one(DXGI_FORMAT_R8G8B8A8_UNORM, &white, &s.white);
    if (!s.farTex) one(DXGI_FORMAT_R32_FLOAT, &farDepth, &s.farTex);
    dev->Release();
    return s.white && s.farTex;
}
} // namespace

void GameComposite_Set(const GameCompositeSwitches& sw) { S().sw = sw; }

void GameComposite_SetScene(ID3D11Resource* sceneHdr)
{
    State& s = S();
    s.scene = sceneHdr;
    s.sceneW = 0;
    s.sceneH = 0;
    if (sceneHdr) {
        D3D11_RESOURCE_DIMENSION dim{};
        sceneHdr->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            D3D11_TEXTURE2D_DESC td{};
            static_cast<ID3D11Texture2D*>(sceneHdr)->GetDesc(&td);
            s.sceneW = td.Width;
            s.sceneH = td.Height;
        }
    }
}

void GameComposite_OnPresent()
{
    State& s = S();
    if (s.sw.noGameAo || s.sw.noGameFog) ++s.framesSeen;
}

void GameComposite_BeforeDraw(ID3D11DeviceContext* real, UINT vertexCount)
{
    State& s = S();
    if ((!s.sw.noGameAo && !s.sw.noGameFog) || !s.scene || vertexCount == 0 || vertexCount > 6) return;   // a full-screen triangle or quad
    // Single color RT matching the HDR scene target (or full-size RGBA16F HDR target).
    ID3D11RenderTargetView* rtvs[2] = {};
    real->OMGetRenderTargets(2, rtvs, nullptr);
    ID3D11Resource* rt0 = nullptr;
    const bool singleRt = (rtvs[0] != nullptr && rtvs[1] == nullptr);
    if (singleRt) rtvs[0]->GetResource(&rt0);
    const bool rtMatches = singleRt && (rt0 == s.scene || IsFullSizeHdr16F(rt0, s.sceneW, s.sceneH));
    if (rt0) rt0->Release();
    if (rtvs[0]) rtvs[0]->Release();
    if (rtvs[1]) rtvs[1]->Release();
    if (!rtMatches) return;

    ID3D11PixelShader* ps = nullptr;
    real->PSGetShader(&ps, nullptr, nullptr);
    if (!ps) return;
    const uint64_t hash = ShaderManager::Get().GetPSHash(ps);
    ps->Release();
    if (hash == kTonemapHash) return;

    ID3D11ShaderResourceView* srvs[3] = {};
    real->PSGetShaderResources(0, 3, srvs);
    SrvInfo info[3];
    for (int k = 0; k < 3; ++k) InspectSrv(srvs[k], info[k]);

    // Fast path: known reference & Skyrim 1.6.1170 composite hashes; structural path: all HDR composite permutations.
    const bool knownHash = (hash == kCompositeHash || hash == kCompositeHash1170);
    bool matched = knownHash;
    if (!matched) {
        const bool t0Ok = info[0].valid && info[0].w == s.sceneW && info[0].h == s.sceneH &&
                          (IsHdrColorFormat(info[0].srvFmt) || IsHdrColorFormat(info[0].texFmt));
        const bool t2Ok = info[2].valid && info[2].w == s.sceneW && info[2].h == s.sceneH &&
                          (IsDepthFormat(info[2].srvFmt) || IsDepthFormat(info[2].texFmt));
        const bool t1Ok = info[1].valid && info[1].res != info[0].res && info[1].res != info[2].res && info[0].res != info[2].res;
        matched = t0Ok && t1Ok && t2Ok;
    }

    if (!matched) {
        if (s.totalMatches == 0 && s.framesSeen >= 300 && s.diagLogged < 5) {
            if (RecordUniqueHash(s.diagHashes, s.diagLogged, 5, hash)) {
                Log("[GameComposite] diag candidate #%u (frame %u): hash=%016llx verts=%u rt=%s t0=fmt%u(%ux%u) t1=fmt%u(%ux%u) t2=fmt%u(%ux%u)",
                    s.diagLogged, s.framesSeen, (unsigned long long)hash, vertexCount,
                    (rt0 == s.scene ? "scene" : "hdr16f"),
                    (unsigned)info[0].srvFmt, info[0].w, info[0].h,
                    (unsigned)info[1].srvFmt, info[1].w, info[1].h,
                    (unsigned)info[2].srvFmt, info[2].w, info[2].h);
            }
        }
        for (auto*& v : srvs) if (v) v->Release();
        return;
    }

    if (!Make(real)) {
        for (auto*& v : srvs) if (v) v->Release();
        return;
    }

    // t1 is the AO (white: no occlusion); t2 is the depth the fog reads, and the shader fogs only
    // where it is below 1, so a texture of 1 switches the fog off.
    if (srvs[0]) srvs[0]->Release();
    s.saved[0] = srvs[1];
    s.saved[1] = srvs[2];
    ID3D11ShaderResourceView* use[2] = {s.sw.noGameAo ? s.white : s.saved[0], s.sw.noGameFog ? s.farTex : s.saved[1]};
    real->PSSetShaderResources(1, 2, use);
    s.swapped = true;
    ++s.totalMatches;
    if (RecordUniqueHash(s.loggedHashes, s.loggedHashCount, 8, hash)) {
        Log("[GameComposite] composite matched (%s, hash=%016llx, rt=%s, t0=fmt%u %ux%u, t1=fmt%u %ux%u, t2=fmt%u %ux%u): AO %s, fog %s",
            (knownHash ? "hash" : "structural"),
            (unsigned long long)hash, (rt0 == s.scene ? "scene" : "hdr16f"),
            (unsigned)info[0].srvFmt, info[0].w, info[0].h,
            (unsigned)info[1].srvFmt, info[1].w, info[1].h,
            (unsigned)info[2].srvFmt, info[2].w, info[2].h,
            s.sw.noGameAo ? "off" : "kept", s.sw.noGameFog ? "off" : "kept");
    }
}

void GameComposite_AfterDraw(ID3D11DeviceContext* real)
{
    State& s = S();
    if (!s.swapped) return;
    real->PSSetShaderResources(1, 2, s.saved);
    for (auto*& v : s.saved) if (v) { v->Release(); v = nullptr; }
    s.swapped = false;
}

} // namespace SB::Proxy
