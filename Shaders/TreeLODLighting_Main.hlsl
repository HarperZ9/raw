// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Tree LOD lighting correction.
// Distant tree LODs receive flat lighting from the engine because their
// geometry is simplified billboard quads.  This pass identifies vegetation
// pixels at LOD distances (deep linear depth), reconstructs approximate
// normals from depth gradients, and applies directional + ambient fill to
// reduce the flat "cardboard" appearance.
//
// Writes corrected color additively onto the backbuffer UAV.

cbuffer TreeLODCB : register(b0)
{
    float3 sunDirection;       float  ambientMatchStrength;
    float3 sunColor;           float  directionalStrength;
    float3 ambientColorUpper;  float  colorMatchBlend;
    float3 ambientColorLower;  float  nearZ;
    float  farZ;
    uint   screenWidth;
    uint   screenHeight;
    uint   pad0;
    float4x4 invViewProj;
}

static const uint MAT_VEGETATION = 6;

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<uint>     tMaterialID : register(t1);
Texture2D<float>    tDepth      : register(t2);
Texture2D<float4>   tAtmosLUT   : register(t3);  // Atmosphere transmittance LUT
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> uBackbuffer : register(u0);
SamplerState SamplerState_Linear : register(s0);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Linearize reversed-Z depth to view-space distance.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Reconstruct world position from pixel coord + reversed-Z depth.
float3 ReconstructWorldPos(uint2 pixelCoord, float depth)
{
    float2 uv = (float2(pixelCoord) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// Reconstruct a coarse normal from depth gradients (screen-space).
float3 ReconstructNormal(uint2 pixelCoord)
{
    float dC = LinearDepth.Load(int3(pixelCoord, 0));
    float dR = LinearDepth.Load(int3(pixelCoord + uint2(1, 0), 0));
    float dD = LinearDepth.Load(int3(pixelCoord + uint2(0, 1), 0));

    float3 posC = ReconstructWorldPos(pixelCoord, tDepth.Load(int3(pixelCoord, 0)));
    float3 posR = ReconstructWorldPos(pixelCoord + uint2(1, 0),
                                       tDepth.Load(int3(pixelCoord + uint2(1, 0), 0)));
    float3 posD = ReconstructWorldPos(pixelCoord + uint2(0, 1),
                                       tDepth.Load(int3(pixelCoord + uint2(0, 1), 0)));

    float3 ddx = posR - posC;
    float3 ddy = posD - posC;
    return normalize(cross(ddy, ddx));
}

// LOD distance threshold — only apply correction beyond this depth (game units).
// Typical LOD transition for trees is ~2000-4000 units.
static const float LOD_DEPTH_START = 1500.0;
static const float LOD_DEPTH_FULL  = 4000.0;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    // Material check — only process vegetation pixels.
    uint matID = tMaterialID.Load(int3(DTid.xy, 0));
    if (matID != MAT_VEGETATION)
        return;

    // Depth — HiZ reversed-Z (near=1, far=0).
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.0001)
        return;

    // First-person geometry skip.
    float linearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (linearZ < 16.0)
        return;

    // LOD distance fade — only apply correction to distant vegetation.
    float lodFade = saturate((linearZ - LOD_DEPTH_START) / (LOD_DEPTH_FULL - LOD_DEPTH_START));
    if (lodFade < 0.01)
        return;

    // Read existing scene color.
    float4 sceneColor = uBackbuffer[DTid.xy];

    // Reconstruct coarse normal from depth derivatives.
    float3 normal = ReconstructNormal(DTid.xy);

    // ── Directional lighting correction ─────────────────────────────────
    // sunDirection is the direction TO the sun.
    float NdotL = saturate(dot(normal, sunDirection));

    // Shadow-side ambient fill: stronger where the surface faces away from sun.
    float shadowSide = 1.0 - NdotL;
    float3 ambientFill = ambientMatchStrength * shadowSide;

    // Hemisphere ambient — blend upper/lower by vertical component of normal.
    float hemiBlend = normal.z * 0.5 + 0.5;
    float3 hemiAmbient = lerp(ambientColorLower, ambientColorUpper, hemiBlend);

    // ── Sun directional boost ───────────────────────────────────────────
    // LOD trees are often too dark on the lit side. Boost direct lighting.
    float3 directBoost = sunColor * NdotL * directionalStrength;

    // ── Atmosphere transmittance (optional) ─────────────────────────────
    // Sample atmosphere LUT based on view-space depth for distance fade.
    float2 atmosUV = float2(saturate(linearZ / farZ), 0.5);
    float3 atmosTrans = tAtmosLUT.SampleLevel(SamplerState_Linear, atmosUV, 0).rgb;
    // If LUT is unavailable (all zeros), default to white.
    if (dot(atmosTrans, float3(1, 1, 1)) < 0.01)
        atmosTrans = float3(1, 1, 1);

    // ── Compose correction ──────────────────────────────────────────────
    // Color match: blend original luminance toward expected tree color.
    float sceneLuma = dot(sceneColor.rgb, float3(0.299, 0.587, 0.114));

    float3 correction = (hemiAmbient * ambientFill + directBoost) * atmosTrans;
    correction *= lodFade;  // Smooth fade at LOD transition boundary.

    // Modulate by scene luminance to prevent over-brightening very dark pixels.
    correction *= saturate(sceneLuma * 3.0);

    // Color match blend: lerp scene toward a more natural tree tone to fix
    // the washed-out billboard appearance.
    float3 targetGreen = float3(0.08, 0.12, 0.05);
    float3 colorMatched = lerp(sceneColor.rgb, sceneColor.rgb * (targetGreen / max(sceneLuma, 0.01)),
                                colorMatchBlend * lodFade * 0.1);

    float3 finalColor = colorMatched + correction;
    uBackbuffer[DTid.xy] = float4(finalColor, sceneColor.a);
}
