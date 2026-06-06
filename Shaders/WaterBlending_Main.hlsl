// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Water edge blending.
// Softens hard water-terrain edges by comparing water depth with
// neighbouring non-water depth. At boundaries where the depth
// difference is small, alpha-blends the water pixel with the
// underlying terrain color to create a smooth shoreline transition.
// Also applies a simple depth-based fog tint for underwater depth.

cbuffer WaterBlendCB : register(b0)
{
    float  edgeBlendWidth;
    float  causticIntensity;
    float  causticScale;
    float  depthFogStrength;
    float3 waterColor;      float  waterAlpha;
    float3 sunDirection;    float  gameTime;
    float3 sunColor;        float  nearZ;
    float  farZ;
    uint   screenWidth;
    uint   screenHeight;
    uint   pad0;
    float4 waterPlane;      // nx, ny, nz, d
    float4x4 invViewProj;
}

static const uint MAT_WATER = 255;  // Water has a special material ID

Texture2D<float4>   tBackbuffer : register(t0);
Texture2D<uint>     tMaterialID : register(t1);
Texture2D<float>    tDepth      : register(t2);
Texture2D<float>    tNoise      : register(t3);
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> uBackbuffer : register(u0);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Linearize reversed-Z depth to view-space distance.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Simple procedural caustic pattern using two overlapping noise octaves.
float CausticPattern(float2 worldXY, float time)
{
    float2 uv1 = worldXY * causticScale + float2(time * 0.03, time * 0.02);
    float2 uv2 = worldXY * causticScale * 1.3 + float2(-time * 0.02, time * 0.04);

    // Wrap to [0,1] for noise texture sampling.
    uv1 = frac(uv1);
    uv2 = frac(uv2);

    // tNoise is 128x128 R8_UNORM procedural noise.
    float n1 = tNoise.Load(int3(uint2(uv1 * 128.0) % 128, 0));
    float n2 = tNoise.Load(int3(uint2(uv2 * 128.0) % 128, 0));

    // Combine two octaves with sharpening.
    float pattern = saturate((n1 + n2) - 0.6);
    return pattern * pattern * causticIntensity;
}

// Search radius for finding nearest non-water depth (in pixels).
static const int EDGE_SEARCH_RADIUS = 3;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= screenWidth || DTid.y >= screenHeight)
        return;

    // Depth — HiZ reversed-Z.
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.0001)
        return;

    // First-person geometry skip.
    float linearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (linearZ < 16.0)
        return;

    // Material check — we process pixels that ARE water.
    uint matID = tMaterialID.Load(int3(DTid.xy, 0));
    bool isWater = (matID == MAT_WATER);

    if (!isWater)
    {
        // For non-water pixels adjacent to water, check if we should receive
        // a caustic projection.  Find nearest water pixel in the search radius.
        bool nearWater = false;
        for (int dy = -EDGE_SEARCH_RADIUS; dy <= EDGE_SEARCH_RADIUS && !nearWater; ++dy)
        {
            for (int dx = -EDGE_SEARCH_RADIUS; dx <= EDGE_SEARCH_RADIUS && !nearWater; ++dx)
            {
                int2 sampleCoord = int2(DTid.xy) + int2(dx, dy);
                if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
                    (uint)sampleCoord.x >= screenWidth || (uint)sampleCoord.y >= screenHeight)
                    continue;
                uint neighborMat = tMaterialID.Load(int3(sampleCoord, 0));
                if (neighborMat == MAT_WATER)
                    nearWater = true;
            }
        }

        if (!nearWater)
            return;

        // Apply subtle caustic pattern to terrain near water edges.
        float2 uv = (float2(DTid.xy) + 0.5) / float2(screenWidth, screenHeight);
        float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float4 clipPos = float4(ndc, rawDepth, 1.0);
        float4 worldPos = mul(clipPos, invViewProj);
        worldPos.xyz /= worldPos.w;

        float caustic = CausticPattern(worldPos.xy, gameTime);
        float4 sceneColor = uBackbuffer[DTid.xy];
        float3 causticColor = sunColor * caustic * 0.3;
        uBackbuffer[DTid.xy] = float4(sceneColor.rgb + causticColor, sceneColor.a);
        return;
    }

    // ── Water pixel processing ──────────────────────────────────────────

    float4 sceneColor = uBackbuffer[DTid.xy];

    // Find the nearest non-water depth for edge detection.
    float nearestTerrainDepth = 0.0;  // reversed-Z: 0 = far
    bool foundTerrain = false;

    for (int dy = -EDGE_SEARCH_RADIUS; dy <= EDGE_SEARCH_RADIUS; ++dy)
    {
        for (int dx = -EDGE_SEARCH_RADIUS; dx <= EDGE_SEARCH_RADIUS; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            int2 sampleCoord = int2(DTid.xy) + int2(dx, dy);
            if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
                (uint)sampleCoord.x >= screenWidth || (uint)sampleCoord.y >= screenHeight)
                continue;

            uint neighborMat = tMaterialID.Load(int3(sampleCoord, 0));
            if (neighborMat != MAT_WATER)
            {
                float neighborDepth = tDepth.Load(int3(sampleCoord, 0));
                if (neighborDepth > nearestTerrainDepth)  // reversed-Z: larger = closer
                {
                    nearestTerrainDepth = neighborDepth;
                    foundTerrain = true;
                }
            }
        }
    }

    // ── Edge blend ──────────────────────────────────────────────────────
    float edgeAlpha = waterAlpha;
    if (foundTerrain)
    {
        float waterLinear   = LinearizeDepth(rawDepth);
        float terrainLinear = LinearizeDepth(nearestTerrainDepth);
        float depthDiff     = abs(waterLinear - terrainLinear);

        // Smooth blend at edges: alpha goes from 0 (fully transparent) to
        // waterAlpha over the edgeBlendWidth distance.
        float edgeFade = saturate(depthDiff / max(edgeBlendWidth, 0.01));
        edgeAlpha = waterAlpha * edgeFade;
    }

    // ── Depth fog ───────────────────────────────────────────────────────
    // Tint water toward waterColor based on depth below the water plane.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    worldPos.xyz /= worldPos.w;

    float waterDepthBelow = max(waterPlane.w - worldPos.z, 0.0);
    float fogFactor = 1.0 - exp(-waterDepthBelow * depthFogStrength * 0.01);
    fogFactor = saturate(fogFactor);

    // ── Caustic on water surface ────────────────────────────────────────
    float caustic = CausticPattern(worldPos.xy, gameTime);

    // ── Composite ───────────────────────────────────────────────────────
    float3 waterTint = lerp(sceneColor.rgb, waterColor, fogFactor);
    waterTint += sunColor * caustic;

    float3 blended = lerp(sceneColor.rgb, waterTint, edgeAlpha);
    uBackbuffer[DTid.xy] = float4(blended, sceneColor.a);
}
