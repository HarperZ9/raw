// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Separable bilateral Burley diffusion-profile blur for skin SSS.
// Dispatched twice per frame: once with direction=(1,0) for horizontal,
// once with direction=(0,1) for vertical.
//
// Only blurs pixels classified as skin (MAT_SKIN=1) or foliage
// (MAT_FOLIAGE=4) by MaterialClassifier. Non-SSS pixels pass through.
//
// Bilateral weight uses depth similarity to prevent bleed across
// silhouette edges:
//   weight = BurleyProfile(offset) * exp(-depthDiff^2 / (2*thickness^2))
//
// Reference: Burley, "Extending the Disney BRDF to a BSDF with
// Integrated Subsurface Scattering", SIGGRAPH 2015.
// Jimenez et al., "Separable Subsurface Scattering", CGF 2015.

cbuffer SSSBlurCB : register(b0)
{
    float2 direction;       // (1,0) for horizontal, (0,1) for vertical
    float  sssRadius;       // Screen-space blur radius
    float  sssStrength;     // Overall SSS intensity
    float  translucency;    // Back-lighting translucency factor
    float3 skinWidth;       // Per-channel Burley widths for skin (R, G, B)
    float  foliageWidth;    // Foliage SSS width (single channel, broader)
    float  nearZ;
    float  farZ;
    float  pad0;
    uint   screenWidth;
    uint   screenHeight;
    uint   frameIndex;
    uint   pad1;
}

static const uint MAT_SKIN    = 1;
static const uint MAT_FOLIAGE = 4;

Texture2D<float4> tInput      : register(t0);
Texture2D<uint>   tMaterialID : register(t1);
Texture2D<float>  tDepth      : register(t2);
Texture2D<float4> tNormals    : register(t3);
Texture2D<float>  LinearDepth : register(t31);
RWTexture2D<float4> uOutput   : register(u0);
SamplerState sPointClamp : register(s0);

// ---------------------------------------------------------------------------
// Burley diffusion profile (normalised Gaussian approximation)
// ---------------------------------------------------------------------------

// Burley's diffusion profile R(r) ~ A * exp(-r/d) + B * exp(-r/(3d))
// where d is the diffusion width.  We use the sum-of-Gaussians
// approximation from Jimenez et al. for separability.
//
// For a single channel with width 'w':
//   weight(offset) = 0.233 * G(offset, 0.0484*w) +
//                    0.100 * G(offset, 0.187*w)  +
//                    0.118 * G(offset, 0.567*w)  +
//                    0.113 * G(offset, 1.99*w)   +
//                    0.358 * G(offset, 7.41*w)
//
// We use a simplified 3-Gaussian kernel for efficiency.

float BurleyWeight(float offset, float width)
{
    float o2 = offset * offset;
    float w1 = width * 0.15;
    float w2 = width * 0.55;
    float w3 = width * 2.0;

    float g1 = exp(-o2 / (2.0 * w1 * w1 + 0.0001));
    float g2 = exp(-o2 / (2.0 * w2 * w2 + 0.0001));
    float g3 = exp(-o2 / (2.0 * w3 * w3 + 0.0001));

    return 0.40 * g1 + 0.35 * g2 + 0.25 * g3;
}

// Number of blur taps on each side of center.
static const int KERNEL_RADIUS = 11;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
    {
        return;
    }

    // Read center pixel material.
    uint matID = tMaterialID.Load(int3(DTid.xy, 0));
    bool isSkin    = (matID == MAT_SKIN);
    bool isFoliage = (matID == MAT_FOLIAGE);

    // Pass through non-SSS pixels unchanged.
    if (!isSkin && !isFoliage)
    {
        uOutput[DTid.xy] = tInput.Load(int3(DTid.xy, 0));
        return;
    }

    // Depth — HiZ reversed-Z.
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.0001)
    {
        uOutput[DTid.xy] = tInput.Load(int3(DTid.xy, 0));
        return;
    }

    // First-person geometry skip.
    float centerLinearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (centerLinearZ < 16.0)
    {
        uOutput[DTid.xy] = tInput.Load(int3(DTid.xy, 0));
        return;
    }

    // Per-channel Burley widths.
    float3 widths;
    if (isSkin)
        widths = skinWidth;
    else
        widths = float3(foliageWidth, foliageWidth, foliageWidth);

    // Scale blur radius by depth (closer = more pixels, further = fewer).
    // This approximates a world-space-constant blur width.
    float depthScale = saturate(100.0 / centerLinearZ);
    float effectiveRadius = sssRadius * depthScale;

    // Center pixel color.
    float4 centerColor = tInput.Load(int3(DTid.xy, 0));

    // Bilateral blur kernel.
    float3 colorAccum = float3(0, 0, 0);
    float3 weightAccum = float3(0, 0, 0);

    // Thickness parameter for bilateral depth rejection.
    // Larger = more permissive bleed across depth edges.
    float thickness = max(centerLinearZ * 0.02, 2.0);

    for (int i = -KERNEL_RADIUS; i <= KERNEL_RADIUS; ++i)
    {
        float offset = float(i);
        int2 sampleCoord = int2(DTid.xy) + int2(direction * offset);

        // Bounds check.
        if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
            (uint)sampleCoord.x >= screenWidth || (uint)sampleCoord.y >= screenHeight)
            continue;

        // Only blur within same material class.
        uint sampleMat = tMaterialID.Load(int3(sampleCoord, 0));
        if (sampleMat != matID)
        {
            // Weight center pixel's color for foreign material samples.
            float3 bw;
            bw.r = BurleyWeight(offset, widths.r * effectiveRadius);
            bw.g = BurleyWeight(offset, widths.g * effectiveRadius);
            bw.b = BurleyWeight(offset, widths.b * effectiveRadius);
            colorAccum += centerColor.rgb * bw;
            weightAccum += bw;
            continue;
        }

        float4 sampleColor = tInput.Load(int3(sampleCoord, 0));

        // Bilateral depth weight: reject samples with large depth difference.
        float sampleLinearZ = LinearDepth.Load(int3(sampleCoord, 0));
        float depthDiff = abs(centerLinearZ - sampleLinearZ);
        float depthWeight = exp(-(depthDiff * depthDiff) / (2.0 * thickness * thickness));

        // Burley profile weight per channel.
        float3 bw;
        bw.r = BurleyWeight(offset, widths.r * effectiveRadius);
        bw.g = BurleyWeight(offset, widths.g * effectiveRadius);
        bw.b = BurleyWeight(offset, widths.b * effectiveRadius);

        float3 w = bw * depthWeight;
        colorAccum += sampleColor.rgb * w;
        weightAccum += w;
    }

    // Normalise.
    float3 blurred;
    blurred.r = (weightAccum.r > 0.0001) ? (colorAccum.r / weightAccum.r) : centerColor.r;
    blurred.g = (weightAccum.g > 0.0001) ? (colorAccum.g / weightAccum.g) : centerColor.g;
    blurred.b = (weightAccum.b > 0.0001) ? (colorAccum.b / weightAccum.b) : centerColor.b;

    // Blend between original and blurred by sssStrength.
    float3 finalColor = lerp(centerColor.rgb, blurred, sssStrength);

    uOutput[DTid.xy] = float4(finalColor, centerColor.a);
}
