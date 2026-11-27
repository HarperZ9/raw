// GBufferVariants: the patched pixel shaders and the blend-state variants.
// Original work, MIT.
#include "GBufferState.h"
#include "ProxyLog.h"
#include "SceneGeometry.h"
#include "SceneGeometryDetail.h"
#include "SceneLights.h"
#include "../dx12/translate/Translate.h"

namespace SB::Proxy
{
namespace gbuffer
{

namespace
{
bool DepthWritesEnabled(ID3D11DeviceContext* real)
{
    if (!real) return true;
    ID3D11DepthStencilState* ds = nullptr;
    UINT sr = 0;
    real->OMGetDepthStencilState(&ds, &sr);
    if (!ds) return true;
    D3D11_DEPTH_STENCIL_DESC dsd{};
    ds->GetDesc(&dsd);
    ds->Release();
    return dsd.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
}

// Classifies per-draw promotion hints for generic Lighting+Lit (base==1) pixel shaders:
//   1. SkinnedActor (base==14): BLENDINDICES + BLENDWEIGHT with per-draw actor world block (dogs, horses,
//      wolves, NPC clothing/gloves, first-person arms; takes priority over foliage even on alpha-tested fur/capes).
//   2. Foliage (base==5): opaque-span non-actor alpha-tested pine saplings, pine boughs, shrubs & ferns
//      (PS discard, AlphaToCoverage, or depth-writing alpha-blended cutout cards; excludes solid tree trunks,
//      static rock/wood architecture, and non-depth-writing ground decals).
DrawClassHint ClassifyDrawHint(ID3D11DeviceContext* real, Span span, ID3D11PixelShader* gamePs, bool* outDepthBlendFoliage)
{
    if (outDepthBlendFoliage) *outDepthBlendFoliage = false;
    if (!real || (span != Span::Opaque && span != Span::FirstPerson)) return DrawClassHint::None;

    const bool isAlphaCutout = (span == Span::Opaque) &&
        ((gamePs && geom::IsDiscardPs(gamePs)) || geom::BlendAlpha(real) == raw::bridge::RtAlphaCoverage);
    const geom::Layout* bound = geom::LayoutOf(geom::BoundLayout(real));
    const bool hasSkinLayout = (bound != nullptr && bound->blendIdx.set && bound->blendW.set);
    if (hasSkinLayout) {
        if (span == Span::FirstPerson) return DrawClassHint::SkinnedActor;
        if (g_sceneLightsActive && SceneLights_DrawWorld() != nullptr) return DrawClassHint::SkinnedActor;
        // Wave 89: Always check Skyrim's 80-bone actor/creature skinning palette (>= 3840 bytes on VS CB10)
        // for solid opaque skinned meshes (horses, dogs, wolves, bears, NPCs -> NEVER SpeedTree alpha-cutout leaf cards!).
        if (!isAlphaCutout) {
            ID3D11Buffer* bb[1] = {};
            real->VSGetConstantBuffers(10, 1, bb);
            bool hasBones = false;
            if (bb[0]) {
                D3D11_BUFFER_DESC bd{};
                bb[0]->GetDesc(&bd);
                hasBones = (bd.ByteWidth >= 3840u);
                bb[0]->Release();
            }
            if (hasBones) return DrawClassHint::SkinnedActor;
        }
    }

    if (span == Span::Opaque) {
        if (gamePs && geom::IsDiscardPs(gamePs)) return DrawClassHint::Foliage;
        const uint8_t blendAlpha = geom::BlendAlpha(real);
        if (blendAlpha == raw::bridge::RtAlphaCoverage) return DrawClassHint::Foliage;
        if (blendAlpha == raw::bridge::RtAlphaBlend && DepthWritesEnabled(real)) {
            if (outDepthBlendFoliage) *outDepthBlendFoliage = true;
            return DrawClassHint::Foliage;
        }
    }
    return DrawClassHint::None;
}
} // namespace

ID3D11PixelShader* VariantFor(State& s, ID3D11PixelShader* game, bool firstPerson, raw::dxbc::ShaderKind* kind,
                              uint8_t* matClass, DrawClassHint hint)
{
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.shaders.find(game);
    if (it == s.shaders.end()) { *kind = raw::dxbc::ShaderKind::Unknown; if (matClass) *matClass = 0; return nullptr; }
    PsEntry& e = it->second;
    const int baseSlot = firstPerson ? 1 : 0;
    auto compileSlot = [&](int slot, uint8_t classOverride) {
        if (e.tried[slot]) return;
        e.tried[slot] = true;
        raw::dxbc::PatchOptions opt;
        opt.classFlags = firstPerson ? raw::dxbc::material::FirstPersonFlag : 0;
        opt.classOverride = classOverride;
        opt.alphaDiscard = false;
        raw::dxbc::PatchInfo info;
        std::vector<uint8_t> out;
        if (raw::dxbc::PatchForGBuffer(e.code.data(), e.code.size(), opt, &out, &info)) {
            const HRESULT hr = s.device->CreatePixelShader(out.data(), out.size(), nullptr, &e.variant[slot]);
            if (FAILED(hr)) {
                e.variant[slot] = nullptr;
                if (s.createFailed++ < 8) Log("[GBuffer] the device refused a patched shader (0x%08X, %zu bytes)", hr, out.size());
            } else {
                ++s.patched;
                // The variant stays bound for the game's next draws with the same shader, so the
                // ray-tracing capture sees it: it gets the original's alpha classification.
                SceneGeometry_OnPixelShader(e.variant[slot], e.code.data(), e.code.size());
            }
        } else if (s.refused++ < 8) {
            Log("[GBuffer] shader left alone: %s", info.refused ? info.refused : "?");
        }
        if (slot == baseSlot || e.kind == raw::dxbc::ShaderKind::Unknown) {
            e.kind = info.kind;
            e.materialClass = info.materialClass;
        }
    };

    // Always inspect/compile the base slot first so e.kind and e.materialClass are known cleanly.
    compileSlot(baseSlot, 0);

    // Promote generic Lighting+Lit (base==1) draws to SkinnedActor (14) or Foliage/SoftLit (5),
    // AND promote skinned fur/creature SoftLit (base==5) draws (horses/wolves/bears with BLENDINDICES+BLENDWEIGHT+80-bone CB10)
    // to SkinnedActor (14) so furred animals NEVER fall into foliage shading!
    const bool canPromote = (hint != DrawClassHint::None) &&
                            e.kind == raw::dxbc::ShaderKind::Lighting &&
                            (e.materialClass == raw::dxbc::material::Lit ||
                             (hint == DrawClassHint::SkinnedActor && e.materialClass == raw::dxbc::material::SoftLit));
    const uint8_t overrideClass = canPromote
        ? (hint == DrawClassHint::SkinnedActor ? raw::dxbc::material::SkinnedActor : raw::dxbc::material::SoftLit)
        : raw::dxbc::material::None;
    const int k = canPromote ? (baseSlot + static_cast<int>(hint) * 2) : baseSlot;
    if (canPromote) {
        compileSlot(k, overrideClass);
    }

    *kind = e.kind;
    if (matClass) *matClass = canPromote ? overrideClass : e.materialClass;
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
    const bool writesMaterial = (mode == 0 || mode == 4);
    for (UINT k = kAlbedoTarget; k <= kEmissiveTarget; ++k) {
        // The material targets follow the colour target's blend. A shader that has no material leaves them alone.
        d.RenderTarget[k] = d.RenderTarget[0];
        if (!writesMaterial) d.RenderTarget[k].RenderTargetWriteMask = 0;
    }
    // A non-depth-writing blended draw (a decal) keeps the class of the surface under it;
    // mode 4 (depth-writing alpha-blended foliage/sapling cards) keeps Spec.z write enabled so base==5u lands cleanly.
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
    if (s.span == Span::None) {
        s.lastDrawSkinnedActor = false;
        s.lastDrawHint = DrawClassHint::None;
        if (s.lastDrawWater) {
            s.lastDrawWater = false;
            raw::tx::SetDrawWaterHint(real, false);
        }
        if (s.lastDrawSkipSubdiv) {
            s.lastDrawSkipSubdiv = false;
            raw::tx::SetDrawSkipSubdivHint(real, false);
        }
        return;
    }
    // Check per-draw actor/foliage hint every draw even when PS/blend did not change: Skyrim shares generic
    // BSLightingShader (Lit=1) across static rocks, skinned creatures/NPC clothing, and alpha-tested pine saplings.
    bool depthBlendFoliage = false;
    const DrawClassHint hint = ClassifyDrawHint(real, s.span, s.gamePs, &depthBlendFoliage);
    if (!s.dirty && hint == s.lastDrawHint) {
        if (s.lastDrawWater) raw::tx::SetDrawWaterHint(real, true);
        if (s.lastDrawSkipSubdiv) raw::tx::SetDrawSkipSubdivHint(real, true);
        return;
    }
    s.dirty = false;
    s.lastDrawHint = hint;
    s.lastDrawSkinnedActor = (hint == DrawClassHint::SkinnedActor);
    raw::dxbc::ShaderKind kind{};
    uint8_t matClass = 0;
    ID3D11PixelShader* v = s.gamePs
        ? VariantFor(s, s.gamePs, s.span == Span::FirstPerson, &kind, &matClass, hint)
        : nullptr;
    if (matClass == raw::dxbc::material::SkinnedActor) ++s.skinnedActorDraws;
    if (hint == DrawClassHint::Foliage && matClass == raw::dxbc::material::SoftLit) ++s.foliagePromotedDraws;
    const bool material = kind == raw::dxbc::ShaderKind::Lighting || kind == raw::dxbc::ShaderKind::DistantLod;
    const bool isWater = (s.span == Span::Water) || (kind == raw::dxbc::ShaderKind::Water);
    // Structurally exclude Landscape terrain, DistantLod quadtree chunks, skinned actors, and alpha-tested foliage
    // from TxSubdiv so cell edges stay watertight, actor silhouettes never split, and sapling cards stay clean.
    const bool skipSubdiv = !isWater && (kind == raw::dxbc::ShaderKind::DistantLod ||
                                         matClass == raw::dxbc::material::Landscape ||
                                         matClass == raw::dxbc::material::DistantLod ||
                                         matClass == raw::dxbc::material::SkinnedActor ||
                                         matClass == raw::dxbc::material::SoftLit ||
                                         hint != DrawClassHint::None);
    s.lastDrawWater = isWater;
    s.lastDrawSkipSubdiv = skipSubdiv;
    raw::tx::SetDrawWaterHint(real, isWater);
    raw::tx::SetDrawSkipSubdivHint(real, skipSubdiv);
    const int blendMode = !v ? 2
        : (kind == raw::dxbc::ShaderKind::Water ? 3
            : (material ? (depthBlendFoliage ? 4 : 0) : 1));
    ID3D11BlendState* blend = BlendVariant(s, s.gameBlend, blendMode);
    // Preserve depth/stencil state 100% untouched (never call OMSetDepthStencilState); restore original PS on refused draws.
    if (v) real->PSSetShader(v, nullptr, 0);
    else if (s.gamePs && s.substituted) real->PSSetShader(s.gamePs, nullptr, 0);
    if (blend) real->OMSetBlendState(blend, s.factor, s.sampleMask);
    s.substituted = true;
    ++s.swaps;
    ++s.kindDraws[static_cast<int>(s.span)][v ? static_cast<int>(kind) : 0];
}

bool GBuffer_CurrentDrawIsCullableProp()
{
    State& s = S();
    if (s.span != Span::Opaque || !s.gamePs || s.lastDrawSkinnedActor) return false;
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.shaders.find(s.gamePs);
    if (it == s.shaders.end()) return false;
    const PsEntry& e = it->second;
    // Structurally exclude Landscape terrain, DistantLod quadtree chunks, Water, Effects, and Actor shaders at the DXBC level.
    return e.kind == raw::dxbc::ShaderKind::Lighting &&
           e.materialClass != raw::dxbc::material::Landscape &&
           e.materialClass != raw::dxbc::material::DistantLod &&
           e.materialClass != raw::dxbc::material::Water &&
           e.materialClass != raw::dxbc::material::Skin &&
           e.materialClass != raw::dxbc::material::Hair &&
           e.materialClass != raw::dxbc::material::Eye &&
           e.materialClass != raw::dxbc::material::SkinnedActor;
}

} // namespace SB::Proxy
