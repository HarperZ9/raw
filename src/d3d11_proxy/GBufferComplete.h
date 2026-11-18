#pragma once
// GBufferComplete: the surface data the game does not write, written by the
// game's own shaders into targets RAW owns (docs/FRAME_BUFFERS.md).
//
// For four kinds of draw span RAW adds render targets to the game's list and
// swaps each pixel shader for its patched form (src/core/dxbc/GBufferPatch.h):
//
//   opaque set        slots 4, 5, 6: albedo, specular, emissive
//   first person      slot 2: the game's own normal target; slots 4, 5, 6 as above
//   blended draws     slot 7: the layer (depth, normal, coverage of the nearest blended surface)
//   water surface     slot 7: the layer
//
// Blend states are swapped for variants with per-target blending, so a blended
// draw treats the new targets by the same rule as its colour, and the layer
// keeps a surface only where the draw shows: alpha of at least 1/255, or a blend
// that does not weigh by alpha.
//
//   [GBuffer]
//   Enabled=1
//
// Original work, MIT.
#include <d3d11.h>
#include <cstddef>

namespace SB::Proxy
{

inline bool g_gbufferActive = false;   // [GBuffer] Enabled=1

void GBuffer_LoadConfig();
void GBuffer_OnPixelShaderCreated(ID3D11PixelShader* ps, const void* bytecode, size_t size);

// The game binds targets. Returns true when RAW adds its own: out receives the
// list to bind (8 entries) and outCount its length.
bool GBuffer_OnTargets(ID3D11DeviceContext* real, UINT n, ID3D11RenderTargetView* const* rtvs,
                       ID3D11DepthStencilView* dsv, ID3D11RenderTargetView** out, UINT* outCount);
void GBuffer_OnPixelShader(ID3D11PixelShader* ps);
void GBuffer_OnBlendState(ID3D11BlendState* state, const float factor[4], UINT sampleMask);
// Before every draw: binds the patched shader and the blend variant when a span is open.
void GBuffer_BeforeDraw(ID3D11DeviceContext* real);
void GBuffer_OnPresent();

struct GBufferTextures
{
    ID3D11Texture2D* albedo = nullptr;     // R8G8B8A8_UNORM
    ID3D11Texture2D* specular = nullptr;   // R8G8B8A8_UNORM
    ID3D11Texture2D* emissive = nullptr;   // R11G11B10_FLOAT
    ID3D11Texture2D* layer = nullptr;      // R32G32B32A32_FLOAT
};
GBufferTextures GBuffer_Textures();

} // namespace SB::Proxy
