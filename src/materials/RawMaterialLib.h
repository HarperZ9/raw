#pragma once
// RawMaterialLib.h: Standalone C++20/23 Physically-Measured Material Registry & Optical Evaluators.
// Zero Skyrim / engine / platform dependencies — reusable across RAW, Playground, low-water, and general gamedev.
// Covers all 8 core material families with complex IOR (eta + i*k at Rec.709 RGB 615/540/465 nm),
// Cauchy dispersion, Beer-Lambert volumetric absorption, Planck blackbody emission (1000K-3400K+),
// Belcour-Barla Airy thin-film interference, clearcoat, SSS mean-free-path, 64-byte GPU SRV records, and JSON export.
// Original work, MIT.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace raw::materials {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Canonical Rec.709 RGB hero wavelengths in nanometers (matches RawMaterials3D.hlsli).
inline constexpr float kLambdaRedNm   = 615.0f;
inline constexpr float kLambdaGreenNm = 540.0f;
inline constexpr float kLambdaBlueNm  = 465.0f;

// Canonical material family IDs (matches RAW_MAT_FAMILY_* in RawMaterials3D.hlsli).
enum class MaterialFamily : uint8_t {
    MagicArcane            = 0,
    FrostGlacial           = 1,
    HeatMagma              = 2,
    ForgedMetal            = 3,
    RefractiveGlassGem     = 4,
    StoneBasaltMasonry     = 5,
    OrganicWoodBarkLeather = 6,
    SnowBotanical          = 7,
    Count                  = 8
};

// Canonical built-in physical material identifiers across all 8 core families.
enum class MaterialId : uint16_t {
    // 1. Magic / Arcane
    AetherialPlasma         = 0,
    SoulGemResonance        = 1,
    EnchantedRuneIridescent = 2,

    // 2. Frost & Glacial Ice
    GlacialIce              = 10,
    HoarfrostCrystal        = 11,
    PackedRimeIce           = 12,

    // 3. Heat, Magma & Forge Embers
    MoltenMagma             = 20,
    BasaltCoolingCrust      = 21,
    IncandescentForgeIron   = 22,
    SmolderingEmbers        = 23,

    // 4. Forged Metals (measured complex IOR n + ik)
    IronForged              = 30,
    NordicSteel             = 31,
    SilverPolished          = 32,
    GoldMeasured            = 33,
    DwemerBrass             = 34,
    EbonyVolcanicMetal      = 35,
    GlassSteelAlloy         = 36,
    DaedricObsidianMetal    = 37,

    // 5. True Refractive Glass, Amber & Gemstones
    CrownGlass              = 40,
    QuartzAmber             = 41,
    SapphireCrystal         = 42,
    RubyCrystal             = 43,
    DiamondCut              = 44,

    // 6. Stone, Basalt & Masonry
    ColumnarBasalt          = 50,
    NordicAshlarStone       = 51,
    MossyGranite            = 52,

    // 7. Organic Wood, Bark & Leather
    NordicPineBark          = 60,
    OiledOakTimber          = 61,
    TannedLeather           = 62,

    // 8. Snow, Slush & Botanical Conifers
    FreshAlpineSnow         = 70,
    ThawedSlush             = 71,
    NordicSpruceFoliage     = 72,

    CustomBase              = 1000
};

// Full CPU-side physically-measured material definition.
struct PhysicalMaterial {
    MaterialId     id                 = MaterialId::NordicAshlarStone;
    MaterialFamily family             = MaterialFamily::StoneBasaltMasonry;
    std::string    name;
    std::string    description;

    Vec3  albedo{0.5f, 0.5f, 0.5f};        // Linear Rec.709 base reflectance or transmission tint [0, 1]
    Vec3  etaRGB{1.5f, 1.5f, 1.5f};        // Real IOR at (615, 540, 465) nm
    Vec3  kRGB{0.0f, 0.0f, 0.0f};          // Conductor extinction k at (615, 540, 465) nm
    Vec3  absorbSigma{0.0f, 0.0f, 0.0f};   // Beer-Lambert volumetric absorption sigma_a [1/m]

    float baseIor540          = 1.50f;     // Dielectric base IOR at 540 nm (ignored if metallic >= 0.5)
    float cauchyB             = 0.0f;      // Cauchy dispersion B in um^2 (n(lam) = A + B / lam_um^2)
    float roughness           = 0.50f;     // Linear perceptual GGX roughness [0.02, 0.98]
    float anisotropy          = 0.0f;      // Brushed/fiber anisotropy in [-1, 1]
    float metallic            = 0.0f;      // Conductor weight [0, 1]
    float sssMeanFreePathM    = 0.0f;      // Subsurface scattering mean-free-path in meters [0, 0.5]
    float blackbodyTempK      = 0.0f;      // Planck blackbody emission temp in K (0 = off, 1000..3400+)
    float clearcoatWeight     = 0.0f;      // Optical clearcoat layer weight [0, 1]
    float clearcoatRoughness  = 0.08f;     // Optical clearcoat GGX roughness [0.02, 0.35]
    float thinFilmNm          = 0.0f;      // Airy thin-film optical path difference in nm (0 = off)
    float porosity            = 0.0f;      // Stone/organic pore darkening factor [0, 1]
    float edgeWear            = 0.0f;      // Metal/masonry bevel edge-wear factor [0, 1]
    float patina              = 0.0f;      // Metal/stone weathering/oxidation/lichen factor [0, 1]
};

// GPU-packable 64-byte structured buffer record (matches RawGpuMaterialRecord in RawMaterials3D.hlsli).
struct alignas(16) GpuMaterialRecord {
    float    albedo[3];
    float    roughness;
    float    etaRGB[3];
    float    anisotropy;
    float    kOrAbsorbRGB[3];
    float    metallic;
    float    blackbodyTempK;
    float    thinFilmNm;
    float    clearcoatWeight;
    uint32_t packedMeta;  // bits 0..7: family, 8..15: id_lo, 16..23: coatRough_u8, 24..31: sssMfp_u8 (0..0.5m)
};
static_assert(sizeof(GpuMaterialRecord) == 64, "GpuMaterialRecord must be exactly 64 bytes for GPU SRV alignment");
static_assert(alignof(GpuMaterialRecord) == 16, "GpuMaterialRecord must be 16-byte aligned");

// ============================================================================
// Exact Clean-Room Physical Optical Evaluators (CPU counterparts to HLSL)
// ============================================================================

// Cauchy dispersion: n(lambda) = A + B / lambda_um^2, anchored so n(540 nm) == baseIor540.
float EvaluateCauchyIor(float baseIor540, float cauchyB, float lambdaNm);

// Evaluates Cauchy refractive index across Rec.709 RGB hero wavelengths (615, 540, 465 nm).
Vec3 EvaluateCauchyRGB(float baseIor540, float cauchyB);

// Exact unpolarized dielectric Fresnel reflectance in [0, 1] (returns 1.0 past TIR critical angle).
float EvaluateDielectricFresnel(float cosThetaI, float ior);

// Exact complex-IOR conductor Fresnel F(cosTheta, eta + i*k) in [0, 1]^3.
Vec3 EvaluateConductorFresnelRGB(float cosThetaI, Vec3 eta, Vec3 k);

// Exact Planck blackbody chromaticity RGB in [0, 1]^3 and optional monotonic HDR radiance scale.
Vec3 EvaluatePlanckBlackbodyRGB(float tempK, float* outHdrRadiance = nullptr);

// Exact Beer-Lambert volumetric transmittance T(d) = exp(-sigma_a * d) in (0, 1]^3.
Vec3 EvaluateBeerLambert(Vec3 absorbSigma, float distanceMeters);

// Belcour-Barla Airy thin-film spectral interference factor in [0, 1]^3.
Vec3 EvaluateThinFilmAiryRGB(float cosTheta, float opdNm);

// Computes physical normal-incidence reflectance F0 (conductor complex Fresnel or dielectric ((n-1)/(n+1))^2).
Vec3 ComputeNormalIncidenceF0(const PhysicalMaterial& m);

// Validates physical soundness (finite values, energy conservation F0 <= 1, IOR >= 1, cauchyB >= 0, bounds).
bool ValidateMaterialPhysics(const PhysicalMaterial& m, std::string* outReason = nullptr);

// Packs a PhysicalMaterial into a 64-byte GPU-ready GpuMaterialRecord and provides lossless unpack helpers.
GpuMaterialRecord PackGpuMaterialRecord(const PhysicalMaterial& m);
MaterialFamily    UnpackGpuMaterialFamily(const GpuMaterialRecord& rec);
uint16_t          UnpackGpuMaterialIdLo(const GpuMaterialRecord& rec);
float             UnpackGpuClearcoatRoughness(const GpuMaterialRecord& rec);
float             UnpackGpuSssMfpMeters(const GpuMaterialRecord& rec);

std::string_view FamilyName(MaterialFamily family);
std::string      ExportMaterialToJson(const PhysicalMaterial& m);

// ============================================================================
// Reusable Physical Material Registry
// ============================================================================
class MaterialRegistry {
public:
    MaterialRegistry();

    void PopulateCanonicalDefaults();
    bool RegisterMaterial(PhysicalMaterial mat, std::string* outErr = nullptr);

    const PhysicalMaterial*        FindById(MaterialId id) const;
    const PhysicalMaterial*        FindByName(std::string_view name) const;
    std::vector<const PhysicalMaterial*> ListByFamily(MaterialFamily family) const;
    const std::vector<PhysicalMaterial>& All() const { return materials_; }

    // Builds a contiguous 64-byte-strided GPU buffer ready for StructuredBuffer SRV upload.
    std::vector<GpuMaterialRecord> BuildGpuBuffer() const;

    // Exports the entire registry as clean, standalone JSON for Playground, low-water, or external engines.
    std::string ExportAllToJson() const;

private:
    std::vector<PhysicalMaterial> materials_;
};

// Singleton canonical registry initialized with all 8 physically-measured families.
const MaterialRegistry& DefaultMaterialRegistry();

}  // namespace raw::materials
