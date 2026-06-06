// Indirect specular composition — SSR + cubemap fallback
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Blends SSR near-field reflections with dynamic cubemap far-field
// reflections based on SSR confidence and surface roughness. Applies
// Schlick Fresnel approximation for physically-correct energy
// conservation at grazing angles.
//
// SSR input format: .rgb = reflected color, .a = confidence [0..1]
// Cubemap: mipmapped TextureCube, roughness selects mip level
// Material ID bit 0: reflective surface flag (from MaterialClassifier)
//
// Depth is expected in reversed-Z (near=1, far=0).

cbuffer IndirectSpecularCB : register(b0)
{
    float3   CameraPos;
    float    Intensity;
    float4x4 InvViewProj;
    float4x4 ViewMatrix;
    float    NearZ;
    float    FarZ;
    uint     ScreenWidth;
    uint     ScreenHeight;
    float    CubemapFallback;
    float    FresnelBias;
    float    RoughnessThreshold;
    uint     FrameIndex;
}

Texture2D<float4>   tSSR        : register(t0);  // SSR output (.rgb=color, .a=confidence)
TextureCube<float4> tCubemap    : register(t1);  // Dynamic cubemap with mips
Texture2D<float>    tDepth      : register(t2);  // Depth buffer (reversed-Z)
Texture2D<float4>   tNormals    : register(t3);  // G-buffer normals (.xyz = world normal, .w = roughness)
Texture2D<uint>     tMaterialID : register(t4);  // Material classification
SamplerState sLinear : register(s0);
RWTexture2D<float4> uOutput : register(u0);

// Schlick Fresnel approximation: F(theta) = F0 + (1 - F0) * (1 - cos(theta))^5
float3 SchlickFresnel(float3 F0, float cosTheta)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (1.0 - F0) * (t2 * t2 * t);  // t^5
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenWidth || DTid.y >= ScreenHeight)
        return;

    int2   pixelCoord = int2(DTid.xy);
    float2 uv = (float2(pixelCoord) + 0.5) / float2(ScreenWidth, ScreenHeight);

    // Load depth (reversed-Z: near=1, far=0)
    float depth = tDepth.Load(int3(pixelCoord, 0));

    // Skip sky pixels
    if (depth < 0.0001)
    {
        uOutput[DTid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Load normal + roughness from G-buffer
    float4 normalData = tNormals.Load(int3(pixelCoord, 0));
    float3 worldNormal = normalize(normalData.xyz * 2.0 - 1.0);
    float  roughness = normalData.w;

    // Skip very rough surfaces (diffuse-only)
    if (roughness > RoughnessThreshold)
    {
        uOutput[DTid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Reconstruct world position from depth
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos  = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    // View direction (from surface to camera)
    float3 viewDir = normalize(CameraPos - worldPos.xyz);

    // Reflection direction
    float3 reflDir = reflect(-viewDir, worldNormal);

    // Fresnel: F0 approximation for dielectrics ~ 0.04, metals higher
    // Use material ID to distinguish if available
    uint matID = tMaterialID.Load(int3(pixelCoord, 0));
    float3 F0 = float3(0.04, 0.04, 0.04);  // dielectric default
    if (matID & 1u)
        F0 = float3(0.6, 0.6, 0.6);  // metallic approximation

    float NdotV = max(dot(worldNormal, viewDir), 0.0);
    float3 fresnel = SchlickFresnel(F0 + FresnelBias, NdotV);

    // ── SSR contribution ─────────────────────────────────────────────
    float4 ssrData = tSSR.Load(int3(pixelCoord, 0));
    float3 ssrColor      = ssrData.rgb;
    float  ssrConfidence = ssrData.a;

    // ── Cubemap contribution ─────────────────────────────────────────
    // Roughness-based mip selection: rough surfaces -> blurry reflection
    // Typical cubemap has ~7 mip levels for 128x128
    float maxMip = 6.0;
    float cubemapMip = roughness * maxMip;
    float3 cubemapColor = tCubemap.SampleLevel(sLinear, reflDir, cubemapMip).rgb;

    // ── Blend SSR + cubemap ──────────────────────────────────────────
    // Where SSR has high confidence, prefer SSR. Where low, use cubemap.
    // Smooth blend to avoid hard transitions.
    float ssrWeight     = ssrConfidence;
    float cubemapWeight = (1.0 - ssrConfidence) * CubemapFallback;

    float3 reflection = ssrColor * ssrWeight + cubemapColor * cubemapWeight;

    // Apply Fresnel and intensity
    float3 result = reflection * fresnel * Intensity;

    // Roughness-based attenuation: smoother surfaces get stronger reflections
    float roughnessAtten = 1.0 - roughness * roughness;
    result *= roughnessAtten;

    // Output: RGB = indirect specular, A = total reflection weight (for compositing)
    float totalWeight = saturate(ssrWeight + cubemapWeight);
    uOutput[DTid.xy] = float4(result, totalWeight);
}
