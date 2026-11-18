#pragma once
// State shared by GBufferComplete.cpp (spans and targets) and
// GBufferVariants.cpp (patched shaders and blend variants). Original work, MIT.
#include "GBufferComplete.h"
#include "../core/dxbc/GBufferPatch.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace SB::Proxy::gbuffer
{

enum class Span { None, Opaque, FirstPerson, Blended, Water };

struct Target
{
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

struct PsEntry
{
    std::vector<uint8_t> code;
    ID3D11PixelShader* variant[2] = {};      // [1]: with the first-person flag
    bool tried[2] = {};
    raw::dxbc::ShaderKind kind = raw::dxbc::ShaderKind::Unknown;
};

struct State
{
    bool loaded = false;
    ID3D11Device* device = nullptr;
    Target albedo, specular, emissive, layer;
    UINT width = 0, height = 0;

    // The game's targets, found at the opaque set and compared by address.
    ID3D11Resource *sceneHdr = nullptr, *motion = nullptr, *normal = nullptr, *mainDepth = nullptr;
    ID3D11RenderTargetView* normalRtv = nullptr;   // holds a reference
    bool opaqueSeen = false;                        // this frame

    Span span = Span::None;
    bool dirty = false, substituted = false;
    ID3D11PixelShader* gamePs = nullptr;
    ID3D11BlendState* gameBlend = nullptr;
    float factor[4] = {1, 1, 1, 1};
    UINT sampleMask = 0xFFFFFFFF;

    std::mutex mutex;                               // shader creation can come from another thread
    std::unordered_map<ID3D11PixelShader*, PsEntry> shaders;
    std::map<std::pair<ID3D11BlendState*, int>, ID3D11BlendState*> blends;

    // Counters, logged every 600 frames.
    uint64_t frame = 0;
    uint32_t spans[5] = {}, patched = 0, refused = 0, createFailed = 0, swaps = 0;
    uint32_t kindDraws[5][6] = {};                  // per span, draws by shader kind (0: not patched)
};

State& S();

// The patched form of the game's bound shader for the open span, or null.
// kind receives the shader's kind.
ID3D11PixelShader* VariantFor(State& s, ID3D11PixelShader* game, bool firstPerson, raw::dxbc::ShaderKind* kind);
// mode 0: the shader writes the material targets; 1: it writes the layer only; 2: it writes none of RAW's targets.
ID3D11BlendState* BlendVariant(State& s, ID3D11BlendState* game, int mode);

} // namespace SB::Proxy::gbuffer
