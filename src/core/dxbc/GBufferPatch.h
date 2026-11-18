#pragma once
// GBufferPatch: adds the surface outputs the game does not write to one of the
// game's own compiled pixel shaders.
//
// The patched shader computes exactly what the original computes and writes
// four more render targets:
//
//   SV_Target4  albedo        rgb: the base colour before lighting, times the vertex colour where the
//                             shader multiplies the lit result by one; a: the draw's alpha
//   SV_Target5  specular      r: the normal map's specular mask; g: gloss, log2(shininess) / 13;
//                             b: material class / 255; a: the draw's alpha
//   SV_Target6  emissive      rgb: emitted light in linear units; a: the draw's alpha
//   SV_Target7  layer         r: SV_Position.z; gb: the view normal in the game's two-channel
//                             encoding, or (the draw's alpha, -1) when the shader has no normal;
//                             a: 1 where the draw's alpha is at least 1/255
//
// How: every write to an original output goes to a new temporary instead, taps
// copy intermediate values where the shader holds them, and before each ret
// the temporaries are written to the outputs. No instruction of the original
// is removed or reordered.
//
// The shader kind comes from reflection of the bytecode alone: constant buffer
// sizes, bound texture slots and instruction patterns (docs/FRAME_BUFFERS.md).
//
// Original work, MIT.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace raw::dxbc
{

constexpr uint32_t kAlbedoTarget = 4, kSpecularTarget = 5, kEmissiveTarget = 6, kLayerTarget = 7;
constexpr float kLayerAlpha = 1.0f / 255.0f;   // a draw enters the layer where its alpha reaches this

enum class ShaderKind : uint8_t
{
    Unknown,     // not a pixel shader RAW can patch
    Lighting,    // the game's lit surface shader family
    DistantLod,  // lit distant objects: one texture, no material constants
    Effect,      // particles, mist, decals of the effect family
    Water,       // a water surface
    Generic,     // anything else: gets the layer output only
};

// Material classes, written to SV_Target5.b. They name what the shader binds.
namespace material
{
constexpr uint8_t None = 0, Lit = 1, EnvironmentMapped = 2, GlowMapped = 3, Landscape = 4, SoftLit = 5,
                  Parallax = 6, DistantLod = 7, Effect = 8, Water = 9;
constexpr uint8_t FirstPersonFlag = 128;   // added for draws of the first-person model
}

struct PatchInfo
{
    ShaderKind  kind = ShaderKind::Unknown;
    uint8_t     materialClass = material::None;
    bool        albedo = false;         // base colour found
    bool        albedoInPlace = false;  // read where lighting begins (covers blended layers and tints)
    bool        specularMask = false;   // the normal map's alpha is sampled
    bool        gloss = false;          // a shininess exponent was found
    bool        emissive = false;       // an emissive term was found
    bool        vertexColour = false;   // the albedo carries the vertex colour the lit result is multiplied by
    bool        normal = false;         // the layer carries a normal
    bool        depth = false;          // the layer carries SV_Position.z
    const char* refused = nullptr;      // why no patch was made
};

struct PatchOptions
{
    uint8_t classFlags = 0;             // or-ed into the material class (material::FirstPersonFlag)
};

// Reflection only: what kind of shader this is and what a patch would find.
PatchInfo Inspect(const void* bytecode, size_t size);

// The patched container in *out. False (and info->refused set) when the shader
// is left alone; the caller then keeps the original.
bool PatchForGBuffer(const void* bytecode, size_t size, const PatchOptions& options, std::vector<uint8_t>* out,
                     PatchInfo* info);

} // namespace raw::dxbc
