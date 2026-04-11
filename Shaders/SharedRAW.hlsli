//=============================================================================
// SharedRAW.hlsli — Common utility functions for all RAW shaders
//
// Include this instead of duplicating LinearizeDepth, UVToView,
// ReconstructNormal, etc. in every shader file.
//
// Requires: NearZ, FarZ, ProjMatrix defined in the including shader's CB.
//=============================================================================

#ifndef SHARED_RAW_HLSLI
#define SHARED_RAW_HLSLI

// ─── Constants ──────────────────────────────────────────────────────
static const float PI     = 3.14159265358979323846;
static const float TWO_PI = 6.28318530717958647692;
static const float HALF_PI= 1.57079632679489661923;
static const float INV_PI = 0.31830988618379067154;

// ─── Depth ──────────────────────────────────────────────────────────

// Linearize reversed-Z depth to view-space Z distance
// reversed-Z: z=1 at near, z=0 at far
// Returns: NearZ at near plane, FarZ at far plane
float LinearizeDepth(float z_rev, float N, float F)
{
    return N * F / (N + z_rev * (F - N));
}

// Check if a reversed-Z depth value is sky (far plane)
bool IsSky(float rawDepth)
{
    return rawDepth < 0.0001;
}

// ─── View-Space Reconstruction ──────────────────────────────────────

// Convert UV + linear view-Z to view-space position using projection matrix
// Requires ProjMatrix in the including shader's CB
#ifdef HAS_PROJ_MATRIX
float3 UVToView(float2 uv, float viewZ, float4x4 proj)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return float3(ndc.x * viewZ / proj[0][0],
                  ndc.y * viewZ / proj[1][1],
                  viewZ);
}

// Reconstruct geometric normal from depth gradients (4-neighbor min-difference)
// Picks the closest neighbor per axis to avoid silhouette artifacts
float3 ReconstructNormal(Texture2D<float> depthTex, uint2 coord, float centerZ,
                         uint2 screenDims, float nearZ, float farZ, float4x4 proj)
{
    float2 ts = 1.0 / float2(screenDims);
    float2 uvC = (float2(coord) + 0.5) * ts;

    float zL = LinearizeDepth(depthTex.Load(int3(max((int)coord.x - 1, 0),     coord.y, 0)), nearZ, farZ);
    float zR = LinearizeDepth(depthTex.Load(int3(min(coord.x + 1, screenDims.x - 1), coord.y, 0)), nearZ, farZ);
    float zU = LinearizeDepth(depthTex.Load(int3(coord.x, max((int)coord.y - 1, 0),     0)), nearZ, farZ);
    float zD = LinearizeDepth(depthTex.Load(int3(coord.x, min(coord.y + 1, screenDims.y - 1), 0)), nearZ, farZ);

    float3 pC = UVToView(uvC, centerZ, proj);

    bool useR = abs(zR - centerZ) < abs(centerZ - zL);
    bool useU = abs(zU - centerZ) < abs(zD - centerZ);

    float3 ddx = useR
        ? UVToView(uvC + float2(ts.x, 0), zR, proj) - pC
        : pC - UVToView(uvC - float2(ts.x, 0), zL, proj);
    float3 ddy = useU
        ? UVToView(uvC + float2(0, -ts.y), zU, proj) - pC
        : pC - UVToView(uvC + float2(0, ts.y), zD, proj);

    return normalize(cross(ddy, ddx));
}
#endif // HAS_PROJ_MATRIX

// ─── Color Space ────────────────────────────────────────────────────

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 RGBToYCoCg(float3 c)
{
    return float3(
        dot(c, float3(0.25, 0.50, 0.25)),
        dot(c, float3(0.50, 0.00, -0.50)),
        dot(c, float3(-0.25, 0.50, -0.25)));
}

float3 YCoCgToRGB(float3 c)
{
    return float3(c.x + c.y - c.z,
                  c.x + c.z,
                  c.x - c.y - c.z);
}

// ─── Multi-Bounce AO (Jimenez 2016) ────────────────────────────────
// Prevents AO from over-darkening colored/bright surfaces by accounting
// for light inter-reflection. Brighter albedos get less AO darkening.
float3 MultiBounceAO(float3 albedo, float ao)
{
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}

// ─── Specular AO (Lagarde, Frostbite PBR 3.0) ──────────────────────
float SpecularAOLagarde(float NdotV, float ao, float roughness)
{
    return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// ─── Sampling ───────────────────────────────────────────────────────

// R2 quasi-random sequence (plastic constant) for blue-noise-like sampling
float2 R2Sequence(uint index)
{
    return frac(float2(index * 0.7548776662, index * 0.5698402910));
}

// ─── Safety ─────────────────────────────────────────────────────────

float SafeFloat(float x, float fallback)
{
    return (isnan(x) || isinf(x)) ? fallback : x;
}

float3 SafeFloat3(float3 x, float3 fallback)
{
    return any(isnan(x)) || any(isinf(x)) ? fallback : x;
}

float4 SafeFloat4(float4 x, float4 fallback)
{
    return any(isnan(x)) || any(isinf(x)) ? fallback : x;
}

#endif // SHARED_RAW_HLSLI
