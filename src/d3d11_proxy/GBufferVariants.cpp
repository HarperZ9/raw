// GBufferVariants: the patched pixel shaders and the blend-state variants.
// Original work, MIT.
#include "GBufferState.h"
#include "ProxyLog.h"
#include "SceneGeometry.h"

namespace SB::Proxy
{
namespace gbuffer
{

ID3D11PixelShader* VariantFor(State& s, ID3D11PixelShader* game, bool firstPerson, raw::dxbc::ShaderKind* kind)
{
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.shaders.find(game);
    if (it == s.shaders.end()) { *kind = raw::dxbc::ShaderKind::Unknown; return nullptr; }
    PsEntry& e = it->second;
    const int k = firstPerson ? 1 : 0;
    if (!e.tried[k]) {
        e.tried[k] = true;
        raw::dxbc::PatchOptions opt;
        opt.classFlags = firstPerson ? raw::dxbc::material::FirstPersonFlag : 0;
        opt.alphaDiscard = false;
        raw::dxbc::PatchInfo info;
        std::vector<uint8_t> out;
        if (raw::dxbc::PatchForGBuffer(e.code.data(), e.code.size(), opt, &out, &info)) {
            const HRESULT hr = s.device->CreatePixelShader(out.data(), out.size(), nullptr, &e.variant[k]);
            if (FAILED(hr)) {
                e.variant[k] = nullptr;
                if (s.createFailed++ < 8) Log("[GBuffer] the device refused a patched shader (0x%08X, %zu bytes)", hr, out.size());
            } else {
                ++s.patched;
                // The variant stays bound for the game's next draws with the same shader, so the
                // ray-tracing capture sees it: it gets the original's alpha classification.
                SceneGeometry_OnPixelShader(e.variant[k], e.code.data(), e.code.size());
            }
        } else if (s.refused++ < 8) {
            Log("[GBuffer] shader left alone: %s", info.refused ? info.refused : "?");
        }
        e.kind = info.kind;
        e.materialClass = info.materialClass;
    }
    *kind = e.kind;
    return e.variant[k];
}

ID3D11BlendState* BlendVariant(State& s, ID3D11BlendState* game, int mode)
{
    const auto key = std::make_pair(game, mode);
    if (const auto it = s.blends.find(key); it != s.blends.end()) return it->second;
    D3D11_BLEND_DESC d{};
    if (game) game->GetDesc(&d);
    else {
        // The default state: no blending, every channel written.
        d.RenderTarget[0].SrcBlend = d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        d.RenderTarget[0].DestBlend = d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        d.RenderTarget[0].BlendOp = d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    if (!d.IndependentBlendEnable)
        for (int i = 1; i < 8; ++i) d.RenderTarget[i] = d.RenderTarget[0];
    d.IndependentBlendEnable = TRUE;
    const bool blended = d.RenderTarget[0].BlendEnable != FALSE;
    const UINT8 colourMask = d.RenderTarget[0].RenderTargetWriteMask;
    using raw::dxbc::kAlbedoTarget, raw::dxbc::kEmissiveTarget, raw::dxbc::kLayerTarget, raw::dxbc::kSpecularTarget;
    for (UINT k = kAlbedoTarget; k <= kEmissiveTarget; ++k) {
        // The material targets follow the colour target's blend. A shader that has no material leaves them alone.
        d.RenderTarget[k] = d.RenderTarget[0];
        if (mode != 0) d.RenderTarget[k].RenderTargetWriteMask = 0;
    }
    // A blended draw (a decal) keeps the class of the surface under it.
    if (mode == 0 && blended) d.RenderTarget[kSpecularTarget].RenderTargetWriteMask &= ~D3D11_COLOR_WRITE_ENABLE_BLUE;
    auto& layer = d.RenderTarget[kLayerTarget];
    layer = d.RenderTarget[0];
    if (mode == 3) {
        // Exact hardware water write: ONLY ShaderKind::Water writes to s.layer.rtv (unblended RGBA32F depth + normal + 2.0 tag).
        layer.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        layer.BlendEnable = FALSE;
    } else if (mode == 2 || mode == 1 || colourMask == 0) {
        // Non-water blended effects (smoke, firepit haze, dust, mist, spells) never pollute the water layer target.
        layer.RenderTargetWriteMask = 0;
    } else {
        layer.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        layer.BlendEnable = blended;
        if (blended) {
            const bool byAlpha = d.RenderTarget[0].SrcBlend == D3D11_BLEND_SRC_ALPHA;
            layer.SrcBlend = byAlpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_ONE;
            layer.DestBlend = byAlpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ZERO;
            layer.BlendOp = D3D11_BLEND_OP_ADD;
            layer.SrcBlendAlpha = D3D11_BLEND_ONE; layer.DestBlendAlpha = D3D11_BLEND_ONE; layer.BlendOpAlpha = D3D11_BLEND_OP_MAX;
        }
    }
    ID3D11BlendState* out = nullptr;
    if (FAILED(s.device->CreateBlendState(&d, &out))) out = nullptr;
    s.blends.emplace(key, out);
    return out;
}

} // namespace gbuffer

using namespace gbuffer;

void GBuffer_OnPixelShaderCreated(ID3D11PixelShader* ps, const void* bytecode, size_t size)
{
    if (!g_gbufferActive || !ps || !bytecode) return;
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    PsEntry& e = s.shaders[ps];          // a new shader at an old address replaces the entry
    for (ID3D11PixelShader*& v : e.variant) if (v) { v->Release(); v = nullptr; }
    e = PsEntry{};
    e.code.assign(static_cast<const uint8_t*>(bytecode), static_cast<const uint8_t*>(bytecode) + size);
}

void GBuffer_OnPixelShader(ID3D11PixelShader* ps)
{
    State& s = S();
    s.gamePs = ps;
    s.dirty = true;
}

void GBuffer_OnBlendState(ID3D11BlendState* state, const float factor[4], UINT sampleMask)
{
    State& s = S();
    s.gameBlend = state;
    for (int i = 0; i < 4; ++i) s.factor[i] = factor ? factor[i] : 1.0f;
    s.sampleMask = sampleMask;
    s.dirty = true;
}

void GBuffer_BeforeDraw(ID3D11DeviceContext* real)
{
    State& s = S();
    if (s.span == Span::None || !s.dirty) return;
    s.dirty = false;
    raw::dxbc::ShaderKind kind{};
    ID3D11PixelShader* v = s.gamePs ? VariantFor(s, s.gamePs, s.span == Span::FirstPerson, &kind) : nullptr;
    const bool material = kind == raw::dxbc::ShaderKind::Lighting || kind == raw::dxbc::ShaderKind::DistantLod;
    const int blendMode = !v ? 2 : (kind == raw::dxbc::ShaderKind::Water ? 3 : (material ? 0 : 1));
    ID3D11BlendState* blend = BlendVariant(s, s.gameBlend, blendMode);
    if (v) real->PSSetShader(v, nullptr, 0);
    if (blend) real->OMSetBlendState(blend, s.factor, s.sampleMask);
    s.substituted = true;
    ++s.swaps;
    ++s.kindDraws[static_cast<int>(s.span)][v ? static_cast<int>(kind) : 0];
}

bool GBuffer_CurrentDrawIsCullableProp()
{
    State& s = S();
    if (s.span != Span::Opaque || !s.gamePs) return false;
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.shaders.find(s.gamePs);
    if (it == s.shaders.end()) return false;
    const PsEntry& e = it->second;
    // Structurally exclude Landscape terrain, DistantLod quadtree chunks, Water, and Effects at the DXBC shader level.
    return e.kind == raw::dxbc::ShaderKind::Lighting &&
           e.materialClass != raw::dxbc::material::Landscape &&
           e.materialClass != raw::dxbc::material::DistantLod &&
           e.materialClass != raw::dxbc::material::Water;
}

} // namespace SB::Proxy
