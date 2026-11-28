#pragma once
// RawTerrainGen.h: Multi-scale procedural geological terrain synthesis engine (`raw::worldgen`).
// Standalone C++20/23 clean-room MIT framework with zero Skyrim or graphics API header dependencies.
// Features:
//   - Tectonic ridge / warped quintic-gradient + Worley continental plate synthesis
//   - River / lake hydrography valley carving & alpine basin clamping
//   - Hydraulic droplet erosion + thermal talus slope relaxation (boundary-consistent halo)
//   - 3D SDF cliff overhang, glacial sea-arch & ice-cave carving with analytical gradients
//   - Exact analytical normals, mean curvature, flow/erosion/snow/wetness splat maps
//   - Deterministic chunk streaming at (chunkX, chunkY, lodLevel 0..3) with watertight skirtless LOD seams
//   - C2 quintic seamless border blending beyond Tamriel's exterior void (|x|>185000 or |y|>145000)
// Original work, MIT.

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }

inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(Vec2 a) { return std::sqrt(Dot(a, a)); }
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline Vec2 Normalize(Vec2 a) {
    const float l = Length(a);
    return l > 1.0e-6f ? Vec2{a.x / l, a.y / l} : Vec2{1.0f, 0.0f};
}
inline Vec3 Normalize(Vec3 a) {
    const float l = Length(a);
    return l > 1.0e-6f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0f, 0.0f, 1.0f};
}
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Smoothstep(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
inline float QuinticSmoothstep(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Neighbor coarser LOD bitmask for watertight skirtless edge stitching.
enum EdgeStitchMask : uint8_t {
    StitchNone = 0u,
    StitchNegX = 1u << 0,
    StitchPosX = 1u << 1,
    StitchNegY = 1u << 2,
    StitchPosY = 1u << 3,
};

struct WorleyResult {
    float    f1 = 0.0f;       // Distance to closest feature point
    float    f2 = 0.0f;       // Distance to second-closest feature point
    float    edgeDist = 0.0f; // f2 - f1 (0 on Voronoi cell borders)
    uint32_t cellId = 0u;     // Deterministic hash of closest cell
    Vec2     cellCenter{};    // Position of closest feature point in lattice space
};

struct TectonicSample {
    float continent01 = 0.5f; // Continental shelf vs ocean basin [0..1]
    float ridge01     = 0.0f; // Tectonic mountain range uplift [0..1]
    float fault01     = 0.0f; // Plate boundary volcanic/rift shear [0..1]
    float baseHeightU = 0.0f; // Combined macro geological elevation (world units)
};

struct HydrographySample {
    float riverMask01  = 0.0f; // 1 in active river channel
    float riverDepthU  = 0.0f; // Carved channel depth (world units >= 0)
    float lakeMask01   = 0.0f; // 1 in alpine meltwater / caldera lake basin
    float waterLevelZ  = 0.0f; // Local water surface elevation
    float wetness01    = 0.0f; // Bank/delta soil saturation [0..1]
    Vec2  flowDir{1.0f, 0.0f}; // Downstream unit flow vector
};

struct OverhangSdfResult {
    float sdfU = 0.0f;         // Signed distance: <0 inside carved void/air, >0 solid rock/ice (surface at 0)
    Vec3  normal{0, 0, 1};     // Analytical outward surface normal
    float archMask01 = 0.0f;   // 1 near carved sea-arch / bridge span
    float caveMask01 = 0.0f;   // 1 inside carved subsurface ice/basalt cave
};

// Configuration for seamless blending beyond Tamriel's exterior borders.
// Inside |x| <= 185,000 && |y| <= 145,000, border blend is strictly 0.0 so Skyrim's existing
// exterior terrain is untouched; outside, it C2-ramps across blendApronU into the procedural frontier.
struct TamrielBorderConfig {
    bool  enabled          = true;
    float halfExtentX      = 185000.0f;
    float halfExtentY      = 145000.0f;
    float blendApronU      = 16384.0f;
    float tamrielEdgeBaseZ = -1200.0f;
};

struct TerrainConfig {
    uint32_t            seed                   = 202607u;
    float               chunkSizeU             = 4096.0f; // 1 Skyrim exterior cell
    float               seaLevelZ              = -1400.0f;
    float               snowLineZ              = 2100.0f;
    float               heightScaleU           = 5200.0f;
    bool                enableHydraulicErosion = true;
    uint32_t            hydraulicDroplets      = 240u;
    uint32_t            thermalIterations      = 6u;
    float               talusAngleTan          = 0.7813f; // tan(38 deg) physical scree repose angle
    TamrielBorderConfig border{};
};

// Callback hook so RawBiomeWorld can shape geological elevation without circular headers.
using ElevationShaperFn = float (*)(float worldX, float worldY, const TectonicSample& tec,
                                    const HydrographySample& hydro, uint32_t seed, void* user);

struct TerrainSample {
    float heightZ       = 0.0f;
    Vec3  normal{0.0f, 0.0f, 1.0f};
    float meanCurvature = 0.0f; // >0 convex ridge/crest, <0 concave valley/crevice
    float slope01       = 0.0f; // 0 flat, 1 vertical cliff
    float flowAccum01   = 0.0f;
    float erosionWear01 = 0.0f;
    float snowMask01    = 0.0f;
    float wetness01     = 0.0f;
    float riverDepthU   = 0.0f;
    float borderBlend01 = 1.0f;
};

struct TerrainChunkData {
    int32_t  chunkX            = 0;
    int32_t  chunkY            = 0;
    uint32_t lodLevel          = 0u; // 0=64x64, 1=32x32, 2=16x16, 3=8x8 quads
    uint32_t gridN             = 64u;
    uint32_t stride            = 65u;
    uint8_t  stitchMask        = 0u;
    float    chunkSizeU        = 4096.0f;
    float    originX           = 0.0f;
    float    originY           = 0.0f;
    float    minZ              = 0.0f;
    float    maxZ              = 0.0f;

    // Per-vertex arrays of length stride * stride (row-major: iy * stride + ix)
    std::vector<float>   heights;
    std::vector<Vec3>    normals;
    std::vector<float>   curvatures;
    std::vector<float>   flow;
    std::vector<float>   erosion;
    std::vector<float>   snow;
    std::vector<float>   wetness;
    std::vector<float>   riverDepth;
    std::vector<float>   borderBlend;
    std::vector<Vec4>    splat0;       // x=Rock, y=Snow, z=Ice, w=Basalt
    std::vector<Vec4>    splat1;       // x=Magma, y=Moss, z=PebbleSand, w=Crystal
    std::vector<float>   emissiveMask; // [0..1] volcanic magma / bioluminescent crystal intensity
    std::vector<uint8_t> primaryBiome; // BiomeId index (0..5)

    // Bilinearly samples chunk height & normal at local coordinates u,v in [0..1].
    float SampleHeightBilinear(float u, float v) const;
    Vec3  SampleNormalBilinear(float u, float v) const;
};

// Deterministic integer hash & analytical-derivative noise kernels.
uint32_t HashU32(uint32_t x);
uint32_t Hash2I(int32_t x, int32_t y, uint32_t seed);
uint32_t Hash3I(int32_t x, int32_t y, int32_t z, uint32_t seed);
float    HashTo01(uint32_t h);

// Returns (value in [0..1], dVal/dx, dVal/dy).
Vec3 ValueNoise2DGrad(float x, float y, uint32_t seed);
// Returns (value in [0..1], dVal/dx, dVal/dy, dVal/dz).
Vec4 ValueNoise3DGrad(float x, float y, float z, uint32_t seed);

float Fbm2D(float x, float y, uint32_t seed, int octaves = 5, float lacunarity = 2.02f, float gain = 0.5f);
Vec3  Fbm2DGrad(float x, float y, uint32_t seed, int octaves = 5, float lacunarity = 2.02f, float gain = 0.5f);
float RidgedMultifractal2D(float x, float y, uint32_t seed, int octaves = 5, float lacunarity = 2.08f, float gain = 0.52f);
WorleyResult Worley2D(float x, float y, uint32_t seed);
Vec2 DomainWarp2D(float x, float y, uint32_t seed, float strength, float freq);

// Macro geological & border evaluation.
float EvaluateTamrielBorderBlend(float worldX, float worldY, const TamrielBorderConfig& cfg);
TectonicSample EvaluateTectonicPlate(float worldX, float worldY, const TerrainConfig& cfg);
HydrographySample EvaluateHydrography(float worldX, float worldY, const TectonicSample& tec, const TerrainConfig& cfg);

// Continuous point evaluation (exact analytical derivatives + curvature; matches chunk boundaries bit-for-bit).
TerrainSample SampleTerrainPoint(float worldX, float worldY, const TerrainConfig& cfg,
                                 ElevationShaperFn shaper = nullptr, void* shaperUser = nullptr);

// 3D SDF cliff overhang, glacial sea-arch & subsurface cave carving.
OverhangSdfResult EvaluateOverhangSdf(Vec3 worldPos, float terrainHeightAtXY, float slope01,
                                      uint8_t primaryBiome, uint32_t seed);

// Generates a complete deterministic terrain chunk at (chunkX, chunkY, lodLevel in 0..3)
// with halo-consistent hydraulic droplet erosion, thermal talus relaxation, analytical normals,
// curvature, splat weights, and watertight skirtless LOD edge stitching.
bool GenerateTerrainChunk(int32_t chunkX, int32_t chunkY, uint32_t lodLevel, const TerrainConfig& cfg,
                          uint8_t neighborCoarserMask, TerrainChunkData* outChunk,
                          ElevationShaperFn shaper = nullptr, void* shaperUser = nullptr);

// Explicitly applies skirtless LOD edge stitching to an existing chunk (linear midpoint snap on coarser edges).
void ApplySkirtlessLodStitch(TerrainChunkData& chunk, uint8_t neighborCoarserMask);

}  // namespace raw::worldgen
