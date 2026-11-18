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
constexpr uint64_t kCompositeHash = 0xa9facbe07de6f5d0ull;   // ps_a9facbe07de6f5d0 in the captures

struct State
{
    GameCompositeSwitches sw;
    ID3D11Resource* scene = nullptr;     // compared by address only
    ID3D11ShaderResourceView* white = nullptr;   // 1x1 RGBA8 of 1: no occlusion
    ID3D11ShaderResourceView* farTex = nullptr;    // 1x1 R32F of 1: depth of the sky, no fog
    ID3D11ShaderResourceView* saved[2] = {};     // the game's t1 and t2, held for the draw
    bool swapped = false, logged = false;
};
State& S() { static State s; return s; }

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
void GameComposite_SetScene(ID3D11Resource* sceneHdr) { S().scene = sceneHdr; }

void GameComposite_BeforeDraw(ID3D11DeviceContext* real, UINT vertexCount)
{
    State& s = S();
    if ((!s.sw.noGameAo && !s.sw.noGameFog) || !s.scene || vertexCount > 6) return;   // a full-screen triangle or quad
    // Drawn into the HDR scene colour, as a six-index quad.
    ID3D11RenderTargetView* rtv = nullptr;
    real->OMGetRenderTargets(1, &rtv, nullptr);
    ID3D11Resource* rt = nullptr;
    if (rtv) { rtv->GetResource(&rt); rtv->Release(); }
    if (rt) rt->Release();
    if (rt != s.scene) return;
    // The composite's pixel shader, by the same hash the frame captures name it with: the game
    // compiled one form in every scene captured (Riverwood, the Bannered Mare). Bindings alone
    // match lit quads drawn into the same target, so they are not enough.
    ID3D11PixelShader* ps = nullptr;
    real->PSGetShader(&ps, nullptr, nullptr);
    const uint64_t hash = ps ? ShaderManager::Get().GetPSHash(ps) : 0;
    if (ps) ps->Release();
    if (hash != kCompositeHash) return;
    if (!Make(real)) return;
    // t1 is the AO (white: no occlusion); t2 is the depth the fog reads, and the shader fogs only
    // where it is below 1, so a texture of 1 switches the fog off.
    real->PSGetShaderResources(1, 2, s.saved);
    ID3D11ShaderResourceView* use[2] = {s.sw.noGameAo ? s.white : s.saved[0], s.sw.noGameFog ? s.farTex : s.saved[1]};
    real->PSSetShaderResources(1, 2, use);
    s.swapped = true;
    if (!s.logged) {
        s.logged = true;
        Log("[GameComposite] the game's composite found: its AO %s, its fog %s", s.sw.noGameAo ? "off" : "kept",
            s.sw.noGameFog ? "off" : "kept");
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
