// SPDX-FileCopyrightText: 2026 Zain Dana Harper
// SPDX-License-Identifier: MIT
//
// Wave 91 (OPT-052 / INV-06): DirectX Shader Model 6.9 / 6.10 HLSL Shader-Struct Spec
// for RAW DX12 Inline Neural Texture Compression (BuildLang-NTC610).
// Exact 4K PBR archetype footprint: 10.6675 MiB (5.9995x vs 64 MiB BC7; 170.68 MiB for 16 world classes).
// Pure struct/constant spec (zero unbound global resource declarations).

#ifndef RAW_NTC610_SHADER_SPEC_HLSLI
#define RAW_NTC610_SHADER_SPEC_HLSLI

static const uint kRawNtc610StatusOk             = 0u;
static const uint kRawNtc610StatusCorruptScale   = 1u;
static const uint kRawNtc610StatusOobMipOrUv     = 2u;
static const uint kRawNtc610StatusUnnormNormal   = 3u;
static const uint kRawNtc610MaxMipDefault        = 11u;
static const float kRawNtc610MaxValidQScale      = 0.05f;
static const uint kRawNtc610MlpBytesPerArchetype = 880u;

// Exact 256-byte constant buffer per material archetype (matches C++23 static_assert(sizeof == 256)).
struct RawNtc610MaterialCB
{
    uint   archetypeId;      // 0..15 (AssetClass14 / 16-class RawTextureForge index)
    uint   g0Res;            // 1024 (1/4 of 4096 -> 8.000 MiB INT8 8-ch)
    uint   g1Res;            // 512  (1/8 of 4096 -> 2.667 MiB INT8 8-ch mip pyramid)
    uint   maxMip;           // 11   (4096x4096 mip chain)

    float  qScaleG0;         // Fail-closed valid bound: (0.0, 0.05] (default 1.0/127.0)
    float  qScaleG1;         // Fail-closed valid bound: (0.0, 0.05] (default 1.0/127.0)
    float  demodEpsilon;     // Radiance demodulation floor (1e-4)
    uint   mlpWeightOffset;  // Byte offset into structured FP16 MLP buffer (880 B/archetype)

    float4 pad[14];          // Reserved for SM 6.10 Variable GSM tile metadata (256B aligned)
};

// Decoded 8-channel PBR output + reconstructed unit tangent normal + fail-closed status.
struct RawNtc610PbrSample
{
    float3 albedo;           // Linear sRGB [0, 1]
    float3 normalTS;         // Unit tangent-space normal (|length - 1| <= 1e-4)
    float  roughness;        // [0.04, 1.0]
    float  metalness;        // [0.0, 1.0]
    float  height;           // [0.0, 1.0]
    uint   status;           // 0=OK, 1=ERR_CORRUPT_SCALE, 2=ERR_OOB_MIP, 3=ERR_UNNORM_NORMAL
};

#endif // RAW_NTC610_SHADER_SPEC_HLSLI
