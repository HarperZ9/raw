// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Grass subsurface scattering approximation.
// Reads scene color + HiZ depth (reversed-Z) + material mask from
// MaterialClassifier. For pixels classified as vegetation, computes a
// fake translucency term based on the alignment between the view
// direction and the negated (light + normal*wrap) vector, then applies
// multi-light contribution from ClusteredLighting and blends the
// result additively onto the backbuffer UAV.
//
// Reference: Barré-Brisebois & Bouchard, "Approximating Translucency
// for a Fast, Cheap and Convincing Subsurface Scattering Look",
// GDC 2011.

struct LightData
{
    float4 positionAndRadius;
    float4 colorAndIntensity;
    float4 directionAndAngle;
    uint   flags;
    uint3  pad;
};
struct ClusterInfo
{
    uint offset;
    uint count;
    uint2 pad;
};

cbuffer GrassLightingCB : register(b0)
{
    float3 sunDirection;      float  ambientBoost;
    float3 sunColor;          float  subsurfaceStrength;
    float3 ambientColor;      float  multiLightIntensity;
    float  windSway;
    float  gameTime;
    float  nearZ;
    float  farZ;
    uint   screenWidth;
    uint   screenHeight;
    uint   lightCount;
    uint   pad0;
    float4x4 invViewProj;
}

static const uint MAT_VEGETATION = 6;  // SceneMaterial::Vegetation
static const uint MAT_TERRAIN    = 5;  // SceneMaterial::Terrain

Texture2D<float4>           tBackbuffer     : register(t0);
Texture2D<uint>             tMaterialID     : register(t1);
Texture2D<float4>           tNormals        : register(t2);
Texture2D<float>            tDepth          : register(t3);
StructuredBuffer<LightData> tLights         : register(t4);
StructuredBuffer<ClusterInfo> tClusters     : register(t5);
Buffer<uint>                tLightIndices   : register(t6);
Texture2D<float>            LinearDepth     : register(t31);
RWTexture2D<float4>         uBackbuffer     : register(u0);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Reconstruct world position from depth + pixel coord via inverse VP.
float3 ReconstructWorldPos(uint2 pixelCoord, float depth)
{
    float2 uv = (float2(pixelCoord) + 0.5) / float2(screenWidth, screenHeight);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// Linearize reversed-Z depth to view-space distance.
float LinearizeDepth(float z)
{
    return nearZ * farZ / (nearZ + z * (farZ - nearZ));
}

// Barré-Brisebois/Bouchard translucency approximation.
// viewDir  : normalised direction from surface to camera
// lightDir : normalised direction TO the light source
// normal   : surface normal
// wrapFactor: normal distortion amount (0.5 typical for grass)
float ComputeTranslucency(float3 viewDir, float3 lightDir, float3 normal, float wrapFactor)
{
    float3 halfThick = -(lightDir + normal * wrapFactor);
    halfThick = normalize(halfThick);
    float vdotH = saturate(dot(viewDir, halfThick));
    return pow(vdotH, 4.0);
}

// Cluster index for a pixel (16x16 tiles, log-depth 32 slices).
uint3 GetClusterIndex(uint2 pixelCoord, float linearZ)
{
    uint tileX = pixelCoord.x / 16;
    uint tileY = pixelCoord.y / 16;
    uint tilesX = (screenWidth  + 15) / 16;
    uint tilesY = (screenHeight + 15) / 16;

    float logNear = log2(max(nearZ, 1.0));
    float logFar  = log2(farZ);
    float logZ    = log2(max(linearZ, 1.0));
    uint  slice   = (uint)clamp((logZ - logNear) / (logFar - logNear) * 32.0, 0.0, 31.0);

    return uint3(tileX, tileY, slice);
}

uint FlattenClusterIndex(uint3 ci)
{
    uint tilesX = (screenWidth  + 15) / 16;
    uint tilesY = (screenHeight + 15) / 16;
    return ci.z * tilesX * tilesY + ci.y * tilesX + ci.x;
}

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

    // Depth — HiZ is reversed-Z (near=1, far=0).
    float rawDepth = tDepth.Load(int3(DTid.xy, 0));

    // Sky check (reversed-Z: sky ~ 0).
    if (rawDepth < 0.0001)
        return;

    // First-person geometry skip.
    float linearZ = LinearDepth.Load(int3(DTid.xy, 0));
    if (linearZ < 16.0)
        return;

    // Reconstruct world position and derive view direction.
    float3 worldPos = ReconstructWorldPos(DTid.xy, rawDepth);
    float3 cameraPos = mul(float4(0, 0, 0, 1), invViewProj).xyz /
                        mul(float4(0, 0, 0, 1), invViewProj).w;
    float3 viewDir = normalize(cameraPos - worldPos);

    // Decode G-buffer normal (octahedron or packed — assume xyz in [0,1] mapped to [-1,1]).
    float4 rawNormal = tNormals.Load(int3(DTid.xy, 0));
    float3 normal = normalize(rawNormal.xyz * 2.0 - 1.0);

    // Read existing scene color.
    float4 sceneColor = uBackbuffer[DTid.xy];

    // ── Sun translucency ────────────────────────────────────────────────
    float sunTranslucency = ComputeTranslucency(viewDir, sunDirection, normal, 0.5);
    float3 sunContrib = sunColor * sunTranslucency * subsurfaceStrength;

    // ── Ambient fill ────────────────────────────────────────────────────
    // Hemisphere-based ambient: lerp upper/lower by normal.z.
    float hemiBlend = normal.z * 0.5 + 0.5;
    float3 ambientContrib = ambientColor * ambientBoost * hemiBlend;

    // ── Multi-light translucency via ClusteredLighting ──────────────────
    float3 multiLightContrib = float3(0, 0, 0);
    if (lightCount > 0)
    {
        uint3 ci = GetClusterIndex(DTid.xy, linearZ);
        uint  flatIdx = FlattenClusterIndex(ci);
        ClusterInfo cluster = tClusters[flatIdx];

        for (uint i = 0; i < cluster.count && i < 32; ++i)
        {
            uint lightIdx = tLightIndices[cluster.offset + i];
            LightData light = tLights[lightIdx];

            float3 lightPos    = light.positionAndRadius.xyz;
            float  lightRadius = light.positionAndRadius.w;
            float3 lightColor  = light.colorAndIntensity.xyz;
            float  lightPower  = light.colorAndIntensity.w;

            float3 toLight = lightPos - worldPos;
            float  dist    = length(toLight);
            if (dist > lightRadius) continue;

            float3 lightDir = toLight / max(dist, 0.001);
            float  atten    = saturate(1.0 - dist / lightRadius);
            atten *= atten; // Quadratic falloff.

            float trans = ComputeTranslucency(viewDir, lightDir, normal, 0.5);
            multiLightContrib += lightColor * lightPower * trans * atten;
        }
        multiLightContrib *= multiLightIntensity;
    }

    // ── Composite ───────────────────────────────────────────────────────
    float3 totalAdd = sunContrib + ambientContrib + multiLightContrib;

    // Modulate by existing scene luminance to avoid over-brightening dark grass.
    float sceneLuma = dot(sceneColor.rgb, float3(0.299, 0.587, 0.114));
    totalAdd *= saturate(sceneLuma * 2.0);

    uBackbuffer[DTid.xy] = float4(sceneColor.rgb + totalAdd, sceneColor.a);
}
