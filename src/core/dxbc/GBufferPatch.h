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
//   SV_Target6  emissive      rgb: emitted light in linear units (soft-lit and skin: the soft-lighting
//                             map; environment-mapped: the cube map's colour times its mask; any other shader
//                             that adds the character light: -(that light) - 1/1024); a: the draw's alpha
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
                  Parallax = 6, DistantLod = 7, Effect = 8, Water = 9, Skin = 10, Hair = 11, Eye = 12,
                  TruePBR = 13, SkinnedActor = 14;
constexpr uint8_t FirstPersonFlag = 128;   // added for draws of the first-person model

// Resolves final SV_Target5.b class byte. A non-zero classOverride promotes generic Lighting+Lit (base==1)
// draws (e.g., skinned creatures/NPC clothing -> SkinnedActor=14, alpha-tested pine saplings/shrubs -> SoftLit=5)
// while strictly preserving specialized classes (Skin=10, Hair=11, Eye=12, SoftLit=5, Landscape=4, EnvMap=2, etc.).
constexpr uint8_t ResolveMaterialClass(uint8_t analysedClass, uint8_t classOverride, uint8_t classFlags) noexcept
{
    const uint8_t base = (classOverride != None && analysedClass == Lit) ? classOverride : analysedClass;
    return static_cast<uint8_t>((base & 127u) | (classFlags & FirstPersonFlag));
}

constexpr float EncodeMaterialClassUnorm(uint8_t classByte) noexcept
{
    return static_cast<float>(classByte) / 255.0f;
}

constexpr uint8_t DecodeMaterialClassUnorm(float specZ) noexcept
{
    const float clamped = specZ < 0.0f ? 0.0f : (specZ > 1.0f ? 1.0f : specZ);
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

constexpr bool IsActorMaterialClass(uint8_t classByte) noexcept
{
    const uint8_t base = static_cast<uint8_t>(classByte & 127u);
    return base == Skin || base == Hair || base == Eye || base == SkinnedActor;
}

constexpr bool IsFoliageMaterialClass(uint8_t classByte) noexcept
{
    const uint8_t base = static_cast<uint8_t>(classByte & 127u);
    return base == SoftLit;
}

// Wave 88 Total Rebirth: 14-Class Live Asset Taxonomy + kRawTransformedBit (0.0% Vanilla Passthrough).
enum class AssetClass14 : uint8_t
{
    Unclassified           = 0,
    TerrainGround          = 1,
    CobbleRoadPath         = 2,
    RockCliffBoulder       = 3,
    MossLichen             = 4,
    TreeTrunkBark          = 5,
    TreeFoliageCanopy      = 6,
    GrassGroundcover       = 7,
    WaterSurface           = 8,
    GlacierIce             = 9,
    Snowpack               = 10,
    NordicStoneMasonry     = 11,
    TimberWood             = 12,
    ForgedMetalArmor       = 13,
    CharacterSkinHairCloth = 14,
};

constexpr uint8_t kAssetClass14Count = 14u;
constexpr uint8_t kAssetClass14Mask  = 0x0Fu;
constexpr uint8_t kRawTransformedBit = 0x10u;
static_assert((kAssetClass14Mask & kRawTransformedBit) == 0u, "AssetClass14 low nibble must not overlap kRawTransformedBit");
static_assert((FirstPersonFlag & kRawTransformedBit) == 0u, "FirstPersonFlag must not overlap kRawTransformedBit");

constexpr uint8_t EncodeAssetClass14(AssetClass14 cls, bool transformed) noexcept
{
    const uint8_t id = static_cast<uint8_t>(cls) & kAssetClass14Mask;
    return static_cast<uint8_t>(id | (transformed ? kRawTransformedBit : 0u));
}

constexpr AssetClass14 DecodeAssetClass14(uint8_t encoded) noexcept
{
    const uint8_t id = static_cast<uint8_t>(encoded & kAssetClass14Mask);
    return (id <= kAssetClass14Count) ? static_cast<AssetClass14>(id) : AssetClass14::Unclassified;
}

constexpr bool IsRawTransformed(uint8_t encoded) noexcept
{
    return (encoded & kRawTransformedBit) != 0u;
}

constexpr const char* GetAssetClass14Name(AssetClass14 cls) noexcept
{
    switch (cls) {
        case AssetClass14::TerrainGround:          return "TerrainGround";
        case AssetClass14::CobbleRoadPath:         return "CobbleRoadPath";
        case AssetClass14::RockCliffBoulder:       return "RockCliffBoulder";
        case AssetClass14::MossLichen:             return "MossLichen";
        case AssetClass14::TreeTrunkBark:          return "TreeTrunkBark";
        case AssetClass14::TreeFoliageCanopy:      return "TreeFoliageCanopy";
        case AssetClass14::GrassGroundcover:       return "GrassGroundcover";
        case AssetClass14::WaterSurface:           return "WaterSurface";
        case AssetClass14::GlacierIce:             return "GlacierIce";
        case AssetClass14::Snowpack:               return "Snowpack";
        case AssetClass14::NordicStoneMasonry:     return "NordicStoneMasonry";
        case AssetClass14::TimberWood:             return "TimberWood";
        case AssetClass14::ForgedMetalArmor:       return "ForgedMetalArmor";
        case AssetClass14::CharacterSkinHairCloth: return "CharacterSkinHairCloth";
        default:                                   return "Unclassified";
    }
}

struct DrawClassifyInput
{
    uint8_t  materialBase  = Lit;
    uint32_t primitiveCount = 0u;
    uint32_t vbStride      = 0u;
    uint64_t srvSignature  = 0u;
    bool     hasSkinning   = false;
    bool     hasAlphaTest  = false;
    bool     isExterior    = true;
    bool     isSpecularMetal = false;
    float    avgAlbedoR    = 0.35f;
    float    avgAlbedoG    = 0.35f;
    float    avgAlbedoB    = 0.35f;
    float    upNormalZ     = 0.50f;
};

// Deterministic 14-class live draw-call classifier: never returns Unclassified for any valid world/actor draw.
constexpr AssetClass14 ClassifyLiveDraw14(const DrawClassifyInput& in) noexcept
{
    const uint8_t base = static_cast<uint8_t>(in.materialBase & 127u);
    if (in.hasSkinning || base == Skin || base == Hair || base == Eye || base == SkinnedActor) {
        return AssetClass14::CharacterSkinHairCloth;
    }
    if (base == Water) {
        return AssetClass14::WaterSurface;
    }
    if (base == EnvironmentMapped || in.isSpecularMetal) {
        if (in.avgAlbedoB > in.avgAlbedoR * 1.12f && in.avgAlbedoB > 0.32f) {
            return AssetClass14::GlacierIce;
        }
        return AssetClass14::ForgedMetalArmor;
    }
    if (base == Parallax) {
        if (in.avgAlbedoB > in.avgAlbedoR * 1.08f && in.avgAlbedoB > 0.30f) {
            return AssetClass14::GlacierIce;
        }
        return (in.upNormalZ > 0.65f) ? AssetClass14::CobbleRoadPath : AssetClass14::NordicStoneMasonry;
    }
    if (base == SoftLit || in.hasAlphaTest) {
        if (in.upNormalZ > 0.60f && in.primitiveCount <= 4096u) {
            return AssetClass14::GrassGroundcover;
        }
        return AssetClass14::TreeFoliageCanopy;
    }
    if (base == Landscape) {
        const float luma = in.avgAlbedoR * 0.2126f + in.avgAlbedoG * 0.7152f + in.avgAlbedoB * 0.0722f;
        const float chroma = (in.avgAlbedoR > in.avgAlbedoB ? in.avgAlbedoR - in.avgAlbedoB : in.avgAlbedoB - in.avgAlbedoR);
        if (luma > 0.68f && chroma < 0.08f) {
            return AssetClass14::Snowpack;
        }
        if (in.avgAlbedoG > in.avgAlbedoR * 1.08f && in.avgAlbedoG > in.avgAlbedoB * 1.12f) {
            return (in.upNormalZ > 0.72f) ? AssetClass14::MossLichen : AssetClass14::GrassGroundcover;
        }
        if (chroma < 0.055f && in.upNormalZ > 0.78f && luma > 0.20f && luma < 0.56f) {
            return AssetClass14::CobbleRoadPath;
        }
        return AssetClass14::TerrainGround;
    }
    // Generic Lit / TruePBR / GlowMapped / DistantLod / Effect fallback: classify every single draw into 1..14!
    const float luma = in.avgAlbedoR * 0.2126f + in.avgAlbedoG * 0.7152f + in.avgAlbedoB * 0.0722f;
    if (luma > 0.72f && in.avgAlbedoB >= in.avgAlbedoR * 0.96f && in.upNormalZ > 0.50f) {
        return AssetClass14::Snowpack;
    }
    if (in.avgAlbedoG > in.avgAlbedoR * 1.06f && in.avgAlbedoG > in.avgAlbedoB * 1.10f) {
        return (in.upNormalZ > 0.35f) ? AssetClass14::MossLichen : AssetClass14::TreeFoliageCanopy;
    }
    if (in.avgAlbedoR > in.avgAlbedoB * 1.18f && in.avgAlbedoR >= in.avgAlbedoG * 0.96f) {
        if (in.isExterior && in.upNormalZ < 0.45f && luma < 0.32f) {
            return AssetClass14::TreeTrunkBark;
        }
        return AssetClass14::TimberWood;
    }
    if (!in.isExterior || (in.upNormalZ < 0.55f && in.vbStride >= 36u)) {
        return AssetClass14::NordicStoneMasonry;
    }
    return AssetClass14::RockCliffBoulder;
}
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
    bool        softLight = false;      // soft-lit: the slot 12 sample goes to the emissive target in place of emission
    bool        charLight = false;      // the actors' fixed-direction fill light (slot 11 scale) goes to the emissive target
    bool        envMap = false;         // environment-mapped: the cube's colour times its mask goes to the emissive target
    bool        parallaxHeight = false; // parallax: the height map (t3 or t8, _p.dds) goes to SV_Target5.g
    bool        rmaosMap = false;       // TruePBR / PGPatcher: t5 (_rmaos.dds -> R=Roughness, G=Metallic, B=AO) goes to SV_Target6.rgb
    bool        hairTint = false;       // the albedo is multiplied by the hair's tint, lerp(1, tint, vertex colour)
    bool        vertexColour = false;   // the albedo carries the vertex colour the lit result is multiplied by
    bool        normal = false;         // the layer carries a normal
    bool        depth = false;          // the layer carries SV_Position.z
    const char* refused = nullptr;      // why no patch was made
};

struct PatchOptions
{
    uint8_t classFlags = 0;             // or-ed into the material class (material::FirstPersonFlag)
    uint8_t classOverride = 0;          // non-zero promotes Lighting+Lit (1) draws (e.g. material::SkinnedActor = 14)
    bool    alphaDiscard = false;       // emit discard_z (alpha < 0.35) on foliage/LOD color passes for safe D32_FLOAT
};

// Reflection only: what kind of shader this is and what a patch would find.
PatchInfo Inspect(const void* bytecode, size_t size);

// The patched container in *out. False (and info->refused set) when the shader
// is left alone; the caller then keeps the original.
bool PatchForGBuffer(const void* bytecode, size_t size, const PatchOptions& options, std::vector<uint8_t>* out,
                     PatchInfo* info);

} // namespace raw::dxbc
