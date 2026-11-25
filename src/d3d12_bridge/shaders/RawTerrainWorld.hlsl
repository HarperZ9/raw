// RawTerrainWorld.hlsl: SM 6.6 / 6.10 Multi-Biome Procedural Frontier World Renderer (`raw::worldgen`).
// Renders 64v/126t GPU-culled AS+MS terrain chunks & hero props across 6 distinct showcase biomes:
//   0. Glacial Fjord Spires          (IOR 1.31 non-pulsing Snell parallax ice via evalFrostGlacier3D)
//   1. Geothermal Volcanic Caldera   (1000K..3400K Planck blackbody magma via evalHeatMagma3D)
//   2. Old-Growth Conifer Highlands  (mossy tri-planar granite cliffs, boreal canopy wrap SSS)
//   3. Obsidian Basalt Canyons       (hexagonal obsidian-basalt bevels, wet rapids)
//   4. Bioluminescent Crystal Tundra (Cauchy RGB dispersion quartz + Airy thin-film via RawMaterials3D.hlsli)
//   5. Alpine Meltwater Deltas       (wet cobblestone shoals, braided glacial river banks)
// Fuses seamlessly with LightingCommon.hlsli atmosphere, aerial perspective & dynamic environment cube.
// Original work, MIT.

#include "LightingCommon.hlsli"
#include "RawProceduralNoise.hlsli"
#include "RawMaterials3D.hlsli"

// Matches raw::worldgen::MeshletDesc (48 bytes)
struct TerrainMeshletDesc {
    uint   vertexOffset;
    uint   triangleOffset;
    uint   vertexCount;
    uint   triangleCount;
    float4 boundSphere;   // xyz absolute world center, w radius
    float4 normalCone;    // xyz axis, w cutoff cos(halfAngle + pi/2)
};

// Matches raw::worldgen::TerrainVertex (80 bytes)
struct TerrainVertex {
    float3 pos;           // absolute world position (Z-up)
    float3 normal;        // unit analytical world normal
    float2 uv;
    float4 splat0;        // x=RockGranite, y=SnowPack, z=GlacialIce, w=BasaltColumn
    float4 splat1;        // x=MagmaFissure, y=MossMeadow, z=RiverPebbleSand, w=ArcaneCrystal
    float4 meta;          // x=curvature, y=wetness, z=emissiveMask, w=primaryBiome
};

cbuffer TerrainDrawCB : register(b1) {
    float4 frustumPlanes[6];       // camera-relative world frustum planes
    float4 chunkOriginAndScale;    // xyz world offset (0 for pre-baked world verts), w tri-planar detail scale
    uint   meshletCount;
    float  lodMorphFactor;
    float  magmaFlowPhase;         // slow volcanic convection phase (0 for static)
    float  fogHeightFalloff;       // aerial perspective vertical scale (default 0.00028)
};

StructuredBuffer<TerrainMeshletDesc> TerrainMeshlets : register(t0);
StructuredBuffer<TerrainVertex>      TerrainVertices : register(t1);
ByteAddressBuffer                    TerrainIndices  : register(t2);

struct TerrainPayload {
    uint meshletIndices[32];
};

struct TerrainOutVert {
    float4 clipPos     : SV_Position;
    float3 relWorldPos : TEXCOORD0;   // camera-relative world position (float32 precision safe at |x|>200,000!)
    float3 absWorldPos : TEXCOORD1;   // absolute world position for world-locked tri-planar strata
    float3 worldNormal : TEXCOORD2;
    float2 uv          : TEXCOORD3;
    float4 splat0      : TEXCOORD4;
    float4 splat1      : TEXCOORD5;
    float4 meta        : TEXCOORD6;   // x=curvature, y=wetness, z=emissiveMask, w=primaryBiome
};

groupshared TerrainPayload s_terrainPayload;

bool terrainSphereVisible(float3 relCenter, float radius) {
    [unroll] for (uint i = 0u; i < 6u; ++i) {
        if (dot(frustumPlanes[i].xyz, relCenter) + frustumPlanes[i].w < -radius) {
            return false;
        }
    }
    return true;
}

float4 projectCameraRelative(float3 relP) {
    const float4 q = float4(relP, 1.0f);
    return float4(dot(worldToClip[0], q), dot(worldToClip[1], q),
                  dot(worldToClip[2], q), dot(worldToClip[3], q));
}

// ============================================================================
// Stage 1: Amplification Shader (32 meshlets/wave -> frustum + cone cull)
// ============================================================================
[numthreads(32, 1, 1)]
void as_terrain_main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID) {
    bool visible = false;
    if (dtid < meshletCount) {
        const TerrainMeshletDesc m = TerrainMeshlets[dtid];
        const float3 absC = m.boundSphere.xyz + chunkOriginAndScale.xyz;
        const float3 relC = absC - camPosW.xyz;
        if (terrainSphereVisible(relC, m.boundSphere.w)) {
            const float3 toEye = normalize(eyeAt() - relC);
            if (dot(m.normalCone.xyz, toEye) >= m.normalCone.w) {
                visible = true;
            }
        }
    }
    const uint outSlot = WavePrefixCountBits(visible);
    if (visible) {
        s_terrainPayload.meshletIndices[outSlot] = dtid;
    }
    const uint totalVisible = WaveActiveCountBits(visible);
    DispatchMesh(totalVisible, 1u, 1u, s_terrainPayload);
}

// ============================================================================
// Stage 2: Mesh Shader (up to 64 vertices / 126 triangles per meshlet)
// ============================================================================
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void ms_terrain_main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID,
                     in payload TerrainPayload pl,
                     out vertices TerrainOutVert outVerts[64],
                     out indices uint3 outTris[126]) {
    const TerrainMeshletDesc m = TerrainMeshlets[pl.meshletIndices[gid]];
    const uint vCount = min(m.vertexCount, 64u);
    const uint tCount = min(m.triangleCount, 126u);
    SetMeshOutputCounts(vCount, tCount);

    if (gtid < vCount) {
        const TerrainVertex tv = TerrainVertices[m.vertexOffset + gtid];
        const float3 absP = tv.pos + chunkOriginAndScale.xyz;
        const float3 relP = absP - camPosW.xyz;

        TerrainOutVert ov;
        ov.clipPos     = projectCameraRelative(relP);
        ov.relWorldPos = relP;
        ov.absWorldPos = absP;
        ov.worldNormal = normalize(tv.normal);
        ov.uv          = tv.uv;
        ov.splat0      = tv.splat0;
        ov.splat1      = tv.splat1;
        ov.meta        = tv.meta;
        outVerts[gtid] = ov;
    }
    if (gtid < tCount) {
        const uint byteBase = (m.triangleOffset + gtid * 3u) * 4u;
        outTris[gtid] = uint3(
            TerrainIndices.Load(byteBase),
            TerrainIndices.Load(byteBase + 4u),
            TerrainIndices.Load(byteBase + 8u));
    }
}

// ============================================================================
// Optional Vertex Shader Fallback (non-mesh-shader or shadow prepass path)
// ============================================================================
TerrainOutVert vs_terrain_main(uint vid : SV_VertexID) {
    const TerrainVertex tv = TerrainVertices[vid];
    const float3 absP = tv.pos + chunkOriginAndScale.xyz;
    const float3 relP = absP - camPosW.xyz;

    TerrainOutVert ov;
    ov.clipPos     = projectCameraRelative(relP);
    ov.relWorldPos = relP;
    ov.absWorldPos = absP;
    ov.worldNormal = normalize(tv.normal);
    ov.uv          = tv.uv;
    ov.splat0      = tv.splat0;
    ov.splat1      = tv.splat1;
    ov.meta        = tv.meta;
    return ov;
}

// ============================================================================
// Tri-Planar Geological Strata & Detail Relief (Zero UV Cliff Stretching!)
// ============================================================================
struct TriPlanarStrata {
    float  bandVal;
    float  grainVal;
    float3 normalPerturb;
};

TriPlanarStrata evalTriPlanarStrata(float3 absPos, float3 nUnit, float scale) {
    const float s = max(scale, 0.001f);
    // Sharp tri-planar axis blend weights
    float3 w = pow(abs(nUnit), 4.0f);
    w /= max(w.x + w.y + w.z, 1.0e-5f);

    // Sedimentary horizontal banding along Z + warped bedding planes on X/Y cliffs
    const float4 nXY = rawNoise3Grad(float3(absPos.xy * (0.0042f * s), absPos.z * (0.014f * s)));
    const float4 nXZ = rawNoise3Grad(float3(absPos.x * (0.0050f * s), absPos.z * (0.022f * s), absPos.y * (0.0025f * s)));
    const float4 nYZ = rawNoise3Grad(float3(absPos.y * (0.0050f * s), absPos.z * (0.022f * s), absPos.x * (0.0025f * s)));
    const float4 fine = rawNoise3Grad(absPos * (0.028f * s) + 11.3f);

    const float strataBand = 0.5f + 0.5f * sin((absPos.z * 0.032f * s) + nXY.x * 3.2f);
    const float triVal = nYZ.x * w.x + nXZ.x * w.y + nXY.x * w.z;

    TriPlanarStrata res;
    res.bandVal = saturate(strataBand * 0.55f + triVal * 0.45f);
    res.grainVal = fine.x;
    res.normalPerturb = (nYZ.yzw * w.x + nXZ.yzw * w.y + nXY.yzw * w.z) * 0.24f + fine.yzw * 0.14f;
    return res;
}

// ============================================================================
// Stage 3: Pixel Shader (6-Biome PBR Splatting + Curvature + Unified Fog)
// ============================================================================
float4 ps_terrain_main(TerrainOutVert inp) : SV_Target0 {
    const float3 nGeo = normalize(inp.worldNormal);
    const float3 toEye = eyeAt() - inp.relWorldPos;
    const float  viewDist = max(length(toEye), 1.0f);
    const float3 V = toEye / viewDist;
    const float3 L = astroSunDir();

    // Unified world-space atmosphere & lighting oracle from LightingCommon.hlsli
    float3 sunCol, skyZ, skyH, ambUp, ambDown;
    evalWorldAtmosphere(sunCol, skyZ, skyH, ambUp, ambDown);
    const float3 envCol = evalDynamicEnvCube(reflect(-V, nGeo), 0.35f);

    // Normalize incoming 8-channel splat weights cleanly
    float4 s0 = max(inp.splat0, 0.0f.xxxx); // Rock, Snow, Ice, Basalt
    float4 s1 = max(inp.splat1, 0.0f.xxxx); // Magma, Moss, PebbleSand, Crystal
    const float sSum = max(dot(s0, 1.0f.xxxx) + dot(s1, 1.0f.xxxx), 1.0e-5f);
    s0 /= sSum;
    s1 /= sSum;

    const float curvature = clamp(inp.meta.x, -1.0f, 1.0f);
    const float wetness   = saturate(inp.meta.y + weather.x * 0.45f);
    const float detailSc  = chunkOriginAndScale.w > 0.001f ? chunkOriginAndScale.w : 1.0f;

    // 1. Tri-planar geological strata & micro-normal relief
    const TriPlanarStrata strata = evalTriPlanarStrata(inp.absWorldPos, nGeo, detailSc);
    float3 N = normalize(nGeo + strata.normalPerturb * (1.0f - s0.y * 0.55f));

    // 2. Accumulate physically-based 8-channel material response
    float3 albedo = 0.0f.xxx;
    float  rough  = 0.0f;
    float3 F0     = 0.0f.xxx;
    float3 specAdd = 0.0f.xxx;
    float3 emissive = 0.0f.xxx;

    // Ch0: RockGranite (tri-planar sedimentary cliff strata + mineral grain)
    if (s0.x > 0.001f) {
        const float3 rockCol = lerp(float3(0.14f, 0.15f, 0.17f), float3(0.32f, 0.31f, 0.30f), strata.bandVal)
                             * (0.84f + 0.32f * strata.grainVal);
        albedo += rockCol * s0.x;
        rough  += lerp(0.82f, 0.64f, strata.grainVal) * s0.x;
        F0     += float3(0.042f, 0.042f, 0.044f) * s0.x;
    }

    // Ch1: SnowPack (wind-sculpted sastrugi + static crystalline glints, zero pulse!)
    if (s0.y > 0.001f) {
        const float3 snowCol = lerp(float3(0.66f, 0.74f, 0.84f), float3(0.84f, 0.88f, 0.94f), strata.grainVal);
        // World-locked static micro-flake sparkle (zero time dependence!)
        const float3 flakeCell = floor(inp.absWorldPos * 0.42f);
        const float3 fh = rawHash33(flakeCell);
        const float3 flakeN = normalize(N + (fh - 0.5f) * 1.05f);
        const float3 H = normalize(V + L);
        const float glint = (fh.x > 0.80f) ? pow(saturate(dot(flakeN, H)), 140.0f) * 0.70f : 0.0f;
        const float backSss = pow(saturate(dot(-V, L)), 3.0f) * 0.16f;
        albedo  += snowCol * s0.y;
        rough   += 0.56f * s0.y;
        F0      += float3(0.020f, 0.020f, 0.022f) * s0.y;
        specAdd += sunCol * (glint.xxx + float3(0.34f, 0.58f, 0.84f) * backSss) * s0.y;
    }

    // Ch2: GlacialIce (direct RawMaterials3D.hlsli evalFrostGlacier3D: IOR 1.31 Snell parallax, 100% static!)
    if (s0.z > 0.001f) {
        const RawMaterialEvalResult ice = evalFrostGlacier3D(
            inp.absWorldPos, nGeo, V, L, sunCol, float3(0.32f, 0.68f, 0.82f), 34.0f, s0.y * 0.45f);
        albedo  += ice.albedo * s0.z;
        rough   += ice.roughness * s0.z;
        F0      += ice.F0 * s0.z;
        N        = normalize(lerp(N, ice.normal, s0.z * 0.78f));
        specAdd += (ice.specularAdd + envCol * ice.F0 * 2.2f) * s0.z;
    }

    // Ch3: BasaltColumn (obsidian-basalt hexagonal prisms & dark volcanic shelves)
    if (s0.w > 0.001f) {
        const float4 wBas = rawWorley3D(float3(inp.absWorldPos.xy * 0.015f, inp.absWorldPos.z * 0.004f));
        const float edgeBevel = 1.0f - smoothstep(0.02f, 0.14f, wBas.y);
        const float3 basCol = lerp(float3(0.065f, 0.070f, 0.082f), float3(0.14f, 0.15f, 0.17f), edgeBevel * 0.65f + strata.grainVal * 0.35f);
        albedo += basCol * s0.w;
        rough  += lerp(0.34f, 0.52f, strata.grainVal) * s0.w;
        F0     += float3(0.052f, 0.054f, 0.058f) * s0.w;
    }

    // Ch4: MagmaFissure (direct RawMaterials3D.hlsli evalHeatMagma3D: 1000K..3400K Planck blackbody!)
    if (s1.x > 0.001f) {
        const float crustAmt = saturate(0.72f - inp.meta.z * 0.48f);
        const RawMaterialEvalResult magma = evalHeatMagma3D(
            inp.absWorldPos, nGeo, V, L, sunCol, 2450.0f, crustAmt, magmaFlowPhase);
        albedo   += magma.albedo * s1.x;
        rough    += magma.roughness * s1.x;
        F0       += magma.F0 * s1.x;
        N         = normalize(lerp(N, magma.normal, s1.x * 0.65f));
        specAdd  += magma.specularAdd * s1.x;
        emissive += magma.emissive * s1.x;
    }

    // Ch5: MossMeadow (old-growth conifer floor, lush boreal moss & wildflower tints)
    if (s1.y > 0.001f) {
        const float3 mossCol = lerp(float3(0.075f, 0.155f, 0.055f), float3(0.165f, 0.255f, 0.095f), strata.grainVal);
        const float backMoss = pow(saturate(dot(-V, L)), 2.8f) * 0.12f;
        albedo  += mossCol * s1.y;
        rough   += 0.84f * s1.y;
        F0      += float3(0.032f, 0.035f, 0.030f) * s1.y;
        specAdd += sunCol * float3(0.42f, 0.78f, 0.26f) * backMoss * s1.y;
    }

    // Ch6: RiverPebbleSand (alpine meltwater braided shoals & cobblestone banks)
    if (s1.z > 0.001f) {
        const float4 wCob = rawWorley3D(inp.absWorldPos * 0.048f);
        const float peb = smoothstep(0.03f, 0.24f, wCob.y);
        const float3 cobCol = lerp(float3(0.14f, 0.13f, 0.12f), float3(0.32f, 0.30f, 0.27f), peb * (0.65f + 0.35f * strata.grainVal));
        albedo += cobCol * s1.z;
        rough  += lerp(0.58f, 0.22f, wetness) * s1.z;
        F0     += float3(0.040f, 0.040f, 0.040f) * s1.z;
    }

    // Ch7: ArcaneCrystal (Cauchy RGB refractive quartz + Airy thin-film bioluminescent resonance, static!)
    if (s1.w > 0.001f) {
        const RawMaterialEvalResult gem = evalRefractiveGlassGem3D(
            inp.absWorldPos, nGeo, V, L, sunCol, envCol, 1.544f, 0.0085f,
            float3(0.38f, 0.24f, 0.06f), 18.0f, 0.065f);
        const RawMaterialEvalResult arc = evalArcaneMagic3D(
            inp.absWorldPos, nGeo, V, L, sunCol, float3(0.46f, 0.34f, 0.96f), 460.0f, 16.0f, 0.0f);
        albedo   += lerp(gem.albedo, arc.albedo, 0.45f) * s1.w;
        rough    += min(gem.roughness, arc.roughness) * s1.w;
        F0       += lerp(gem.F0, arc.F0, 0.50f) * s1.w;
        N         = normalize(lerp(N, gem.normal, s1.w * 0.80f));
        specAdd  += (gem.specularAdd * 0.65f + arc.specularAdd * 0.35f) * s1.w;
        emissive += arc.emissive * (0.55f + 0.45f * inp.meta.z) * s1.w;
    }

    // 3. Analytical Curvature & Wetness Physical Modulation
    // Convex ridges catch subtle mineral wear; concave valleys/crevices darken with micro-AO.
    const float curvAo = saturate(1.0f + curvature * 0.24f);
    const float ridgeHighlight = saturate(curvature) * 0.14f;
    albedo *= lerp(1.0f, 0.58f, wetness * (1.0f - s0.y * 0.8f));
    albedo *= lerp(0.76f, 1.08f, curvAo);
    rough   = clamp(lerp(rough, 0.11f, wetness * 0.72f), 0.05f, 0.96f);

    // 4. Direct Sun GGX + Hemisphere Ambient + Dynamic Environment Reflection
    const float nv = max(dot(N, V), 1.0e-4f);
    const float nl = saturate(dot(N, L));
    const float3 H = normalize(V + L);
    const float nh = saturate(dot(N, H));
    const float vh = saturate(dot(V, H));
    const float3 Fschlick = F0 + (1.0f - F0) * pow(1.0f - vh, 5.0f);
    const float ggx = rawGgxSpecTerm(nh, nv, nl, rough, dot(F0, 0.3333f.xxx));

    const float sh = cascadeShadow(inp.relWorldPos, inp.clipPos.z, true);
    const float3 diffuseDirect = albedo * (1.0f / PI) * sunCol * (nl * sh * (1.0f + ridgeHighlight));
    const float3 specDirect = sunCol * Fschlick * ggx * (sh * 0.32f) + specAdd;

    const float hemiW = saturate(N.z * 0.5f + 0.5f);
    const float3 hemiAmb = lerp(ambDown, ambUp, hemiW) * curvAo;
    const float3 Fenv = F0 + (max((1.0f - rough).xxx, F0) - F0) * pow(1.0f - nv, 5.0f);
    const float3 iblSpec = evalDynamicEnvCube(reflect(-V, N), rough) * Fenv * lerp(0.55f, 0.12f, rough);

    float3 hdrColor = diffuseDirect + albedo * hemiAmb + specDirect + iblSpec + emissive;

    // 5. Seamless Atmospheric Perspective & Height-Stratified Aerial Fog (fused with LightingCommon.hlsli)
    const float hFall = fogHeightFalloff > 1.0e-6f ? fogHeightFalloff : 0.00028f;
    const float baseDens = max(tune.x, 0.000035f);
    const float altFactor = exp(-max(inp.absWorldPos.z + 1200.0f, 0.0f) * hFall);
    const float optDepth = viewDist * baseDens * lerp(0.45f, 1.35f, altFactor);
    const float trans = exp(-optDepth);

    const float cosSun = dot(-V, L);
    const float miePhase = rawMultiScatterPhase(cosSun, clamp(tune.y, 0.25f, 0.78f), optDepth);
    const float viewElev = saturate(-V.z * 0.5f + 0.5f);
    const float3 fogCol = lerp(skyH, skyZ, saturate(viewElev * 0.72f))
                        + sunCol * (miePhase * saturate(astroSunElev() * 4.0f + 0.15f) * 0.65f);

    hdrColor = hdrColor * trans + fogCol * (1.0f - trans);
    return float4(max(hdrColor, 0.0f.xxx), 1.0f);
}
