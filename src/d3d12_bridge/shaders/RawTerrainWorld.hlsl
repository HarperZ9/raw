// RawTerrainWorld.hlsl: SM 6.6 / 6.10 Multi-Biome Procedural Frontier World Renderer (`raw::worldgen`).
// Renders 64v/126t GPU-culled AS+MS terrain chunks & 3D procedural hero props/conifers across 6 biomes:
//   0. Glacial Fjord Spires          (IOR 1.31 non-pulsing Snell parallax ice, zero Voronoi honeycomb)
//   1. Geothermal Volcanic Caldera   (1000K..3400K Planck blackbody magma via evalHeatMagma3D)
//   2. Old-Growth Conifer Highlands  (mossy tri-planar granite cliffs, deep boreal spruce & gnarled bark)
//   3. Obsidian Basalt Canyons       (hexagonal obsidian-basalt columns, wet rapids)
//   4. Bioluminescent Crystal Tundra (Cauchy RGB dispersion quartz + Airy thin-film via RawMaterials3D.hlsli)
//   5. Alpine Meltwater Deltas       (wet cobblestone shoals, braided glacial river banks)
// Depth-tested in hardware (D32_FLOAT) AND occluded against Skyrim's standard-Z SceneDepth (0=near, 1=far).
// Fuses seamlessly with LightingCommon.hlsli atmosphere, aerial perspective & dynamic environment cube.
// Original work, MIT.

#define ATLAS_SLOT t4
#include "LightingCommon.hlsli"
#include "RawProceduralNoise.hlsli"
#include "RawMaterials3D.hlsli"
#include "RawFloraWind.hlsli"
#include "RawSeasonsThermals.hlsli"

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
    float4 meta;          // x=curvature, y=wetness, z=emissiveMask, w=primaryBiome (10=Bark, 11=ConiferNeedles)
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
Texture2D<float>                     SceneDepth      : register(t3);
// Wave 47: GPU Procedural PBR Texture2DArray Atlas (8 RawTextureForge materials x 8 mips, 128x128):
//   Slice 0 = NordicGraniteStrata, 1 = GlacialStratifiedIce, 2 = ForgedBrushedSteel, 3 = BorealPineBark,
//   4 = HexagonalBasalt, 5 = VolcanicMagmaCrust, 6 = AlpineMossMeadow, 7 = BraidedRiverPebbles.
Texture2DArray<float4>               g_pbrAlbedo     : register(t5);
Texture2DArray<float4>               g_pbrNormal     : register(t6);
Texture2DArray<float4>               g_pbrOrmh       : register(t7);
SamplerState                         g_pbrSampler    : register(s0);

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
    float4 meta        : TEXCOORD6;   // x=curvature, y=wetness, z=emissiveMask, w=primaryBiome/propMatTag
};

groupshared uint s_terrainVisCount;
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

// Applies 4-tier Euler-Bernoulli cantilever wind sway & winter snow-load sag to baked 3D flora vertices.
void applyTerrainFloraWind(inout float3 absP, inout float3 worldN, TerrainVertex tv) {
    if (tv.meta.w < 9.5f) {
        return;
    }
    const bool isBark = (tv.meta.w < 10.5f);
    const float hFrac = saturate(1.0f - tv.uv.y);
    const float bendFlex = isBark ? (hFrac * 0.32f) : saturate(0.24f + 0.76f * hFrac);
    if (bendFlex <= 1.0e-4f) {
        return;
    }
    const float3 rootPos = absP - float3(0.0f, 0.0f, max(hFrac * 340.0f, 4.0f));
    const float phaseHash = frac(dot(floor(absP * 0.03f), float3(0.371f, 0.719f, 0.193f)));
    const float speciesId = (tv.meta.w >= 11.5f) ? 2.0f : 0.0f; // 0=evergreen conifer, 2=deciduous
    const float2 windXY = clamp(cloudWind.xy * 0.0045f, -2.5f.xx, 2.5f.xx);
    const float simTime = (physWorld[0].x > 1.0e-4f) ? physWorld[0].x : camPosW.w;

    FloraSeasonalParams fSeason;
    fSeason.chlorophyll01       = phenology0.y;
    fSeason.autumnCarotenoid01  = phenology0.z;
    fSeason.autumnAnthocyanin01 = phenology0.w;
    fSeason.leafAbscission01    = phenology1.x;
    fSeason.canopySnowLoad01    = saturate(phenology1.y / 18.0f);

    const FloraWindResult wr = evalFloraSeasonalWind(
        absP, rootPos, bendFlex, phaseHash, speciesId, windXY, simTime, worldN, fSeason);
    absP += wr.displacement;
    worldN = wr.bentNormal;
}

// ============================================================================
// Stage 1: Amplification Shader (32 meshlets/group, wave-size-independent cull)
// ============================================================================
[numthreads(32, 1, 1)]
void as_terrain_main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID) {
    if (gtid == 0u) {
        s_terrainVisCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    bool visible = false;
    if (dtid < meshletCount) {
        const TerrainMeshletDesc m = TerrainMeshlets[dtid];
        const float3 absC = m.boundSphere.xyz + chunkOriginAndScale.xyz;
        const float3 relC = absC - camPosW.xyz;
        if (terrainSphereVisible(relC, m.boundSphere.w)) {
            const float3 eyeDelta = eyeAt() - relC;
            const float eyeDist = length(eyeDelta);
            const float3 toEye = eyeDist > 1.0e-3f ? (eyeDelta / eyeDist) : float3(0.0f, 0.0f, 1.0f);
            // Conservative sphere-aware cone test: never falsely culls close meshlets or two-sided foliage.
            if (eyeDist < m.boundSphere.w * 2.5f || m.normalCone.w <= -0.92f ||
                dot(m.normalCone.xyz, toEye) >= m.normalCone.w - (m.boundSphere.w / max(eyeDist, 1.0f))) {
                visible = true;
            }
        }
    }
    if (visible) {
        uint outSlot = 0u;
        InterlockedAdd(s_terrainVisCount, 1u, outSlot);
        s_terrainPayload.meshletIndices[outSlot] = dtid;
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(s_terrainVisCount, 1u, 1u, s_terrainPayload);
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
        float3 absP = tv.pos + chunkOriginAndScale.xyz;
        float3 worldN = normalize(tv.normal);
        applyTerrainFloraWind(absP, worldN, tv);
        const float3 relP = absP - camPosW.xyz;

        TerrainOutVert ov;
        ov.clipPos     = projectCameraRelative(relP);
        ov.relWorldPos = relP;
        ov.absWorldPos = absP;
        ov.worldNormal = worldN;
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
// Vertex Shader Fallback (non-mesh-shader GPUs & WARP)
// ============================================================================
TerrainOutVert vs_terrain_main(uint vid : SV_VertexID) {
    const TerrainVertex tv = TerrainVertices[vid];
    float3 absP = tv.pos + chunkOriginAndScale.xyz;
    float3 worldN = normalize(tv.normal);
    applyTerrainFloraWind(absP, worldN, tv);
    const float3 relP = absP - camPosW.xyz;

    TerrainOutVert ov;
    ov.clipPos     = projectCameraRelative(relP);
    ov.relWorldPos = relP;
    ov.absWorldPos = absP;
    ov.worldNormal = worldN;
    ov.uv          = tv.uv;
    ov.splat0      = tv.splat0;
    ov.splat1      = tv.splat1;
    ov.meta        = tv.meta;
    return ov;
}

// ============================================================================
// Wave 47: Mip-Mapped Tri-Planar GPU Procedural PBR Atlas Sampling (t5..t7, s0)
// Uses precomputed world-space ddx/ddy derivatives via SampleGrad so anisotropic
// mipmap selection stays 100% exact inside divergent biome splat branches!
// ============================================================================
struct TriPlanarPbrSample {
    float3 albedoLin;
    float3 worldNormal;
    float  ao;
    float  roughness;
    float  metallic;
    float  height;
};

float3 rawSrgbToLinear(float3 s) {
    return s * (s * (s * 0.305306011f + 0.682171111f) + 0.012522878f);
}

TriPlanarPbrSample evalTriPlanarPbrAtlas(float3 absPos, float3 dPosX, float3 dPosY, float3 nUnit,
                                         float sliceIdx, float tileFreq) {
    float3 triW = pow(abs(nUnit), 4.0f);
    triW /= max(triW.x + triW.y + triW.z, 1.0e-5f);

    const float2 uvX = absPos.yz * tileFreq;
    const float2 uvY = absPos.xz * tileFreq;
    const float2 uvZ = absPos.xy * tileFreq;

    const float2 dXdx = dPosX.yz * tileFreq, dXdy = dPosY.yz * tileFreq;
    const float2 dYdx = dPosX.xz * tileFreq, dYdy = dPosY.xz * tileFreq;
    const float2 dZdx = dPosX.xy * tileFreq, dZdy = dPosY.xy * tileFreq;

    const float3 albX = rawSrgbToLinear(g_pbrAlbedo.SampleGrad(g_pbrSampler, float3(uvX, sliceIdx), dXdx, dXdy).rgb);
    const float3 albY = rawSrgbToLinear(g_pbrAlbedo.SampleGrad(g_pbrSampler, float3(uvY, sliceIdx), dYdx, dYdy).rgb);
    const float3 albZ = rawSrgbToLinear(g_pbrAlbedo.SampleGrad(g_pbrSampler, float3(uvZ, sliceIdx), dZdx, dZdy).rgb);

    const float3 tnX = g_pbrNormal.SampleGrad(g_pbrSampler, float3(uvX, sliceIdx), dXdx, dXdy).xyz * 2.0f - 1.0f;
    const float3 tnY = g_pbrNormal.SampleGrad(g_pbrSampler, float3(uvY, sliceIdx), dYdx, dYdy).xyz * 2.0f - 1.0f;
    const float3 tnZ = g_pbrNormal.SampleGrad(g_pbrSampler, float3(uvZ, sliceIdx), dZdx, dZdy).xyz * 2.0f - 1.0f;

    // ORMH layout uploaded by RawMeshHost: R=AO, G=Roughness, B=Metallic/Extra, A=Height
    const float4 ormX = g_pbrOrmh.SampleGrad(g_pbrSampler, float3(uvX, sliceIdx), dXdx, dXdy);
    const float4 ormY = g_pbrOrmh.SampleGrad(g_pbrSampler, float3(uvY, sliceIdx), dYdx, dYdy);
    const float4 ormZ = g_pbrOrmh.SampleGrad(g_pbrSampler, float3(uvZ, sliceIdx), dZdx, dZdy);
    const float4 orm  = ormX * triW.x + ormY * triW.y + ormZ * triW.z;

    // Whiteout tri-planar world normal blend (seam-free across all signed axis faces):
    const float3 wNx = float3(abs(tnX.z) * nUnit.x, tnX.x + nUnit.y, tnX.y + nUnit.z);
    const float3 wNy = float3(tnY.x + nUnit.x, abs(tnY.z) * nUnit.y, tnY.y + nUnit.z);
    const float3 wNz = float3(tnZ.x + nUnit.x, tnZ.y + nUnit.y, abs(tnZ.z) * nUnit.z);

    TriPlanarPbrSample res;
    res.albedoLin   = albX * triW.x + albY * triW.y + albZ * triW.z;
    res.worldNormal = normalize(wNx * triW.x + wNy * triW.y + wNz * triW.z);
    res.ao          = orm.r;
    res.roughness   = orm.g;
    res.metallic    = orm.b;
    res.height      = orm.a;
    return res;
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
    float3 w = pow(abs(nUnit), 4.0f);
    w /= max(w.x + w.y + w.z, 1.0e-5f);

    // Sedimentary horizontal banding along Z + warped bedding planes on X/Y cliffs (pure gradient noise, zero honeycomb)
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

// Clean stratified glacial ice without any Voronoi polygon / honeycomb edges (100% static, IOR 1.31 Snell parallax)
void evalCleanGlacialIce(float3 absPos, float3 nUnit, float3 V, float3 L, float3 sunCol, float3 envCol, float snowDust,
                         out float3 outAlb, out float outRough, out float3 outF0, out float3 outN, out float3 outSpec) {
    const float nv = max(dot(nUnit, V), 1.0e-4f);
    const float3 refrV = refract(-V, nUnit, 1.0f / 1.31f);
    const float3 rayDir = (dot(refrV, refrV) > 1.0e-4f) ? normalize(refrV) : -V;

    // 2-layer smooth domain-warped stratified ice bands (zero Worley honeycomb!)
    const float3 p1 = absPos + rayDir * 8.5f;
    const float3 p2 = absPos + rayDir * 24.0f;
    const float4 w1 = rawNoise3Grad(float3(p1.xy * 0.0065f, p1.z * 0.024f));
    const float4 w2 = rawNoise3Grad(float3(p2.xy * 0.011f + w1.yz * 1.4f, p2.z * 0.036f));
    const float4 fine = rawNoise3Grad(absPos * 0.075f);

    // Subtle organic internal shear veins (ridge abs(noise - 0.5), never regular polygons)
    const float vein = pow(saturate(1.0f - abs(w2.x - 0.5f) * 6.5f), 2.4f) * 0.42f;
    const float band = saturate(0.5f + 0.5f * sin(p1.z * 0.045f + w1.x * 3.8f));

    // Beer-Lambert glacial cobalt-turquoise absorption
    const float pathM = 0.45f / max(nv, 0.25f);
    const float3 transDeep = exp(-float3(0.42f, 0.085f, 0.022f) * pathM);
    const float3 deepIce = lerp(float3(0.055f, 0.205f, 0.345f), float3(0.165f, 0.445f, 0.585f), band * 0.7f + w2.x * 0.3f) * transDeep;
    const float3 iceBody = lerp(deepIce, float3(0.68f, 0.86f, 0.96f), vein);

    const float dust = saturate(snowDust);
    outAlb = saturate(lerp(iceBody, float3(0.76f, 0.84f, 0.92f), dust * 0.65f));
    outN = normalize(nUnit + w1.yzw * 0.12f + fine.yzw * 0.08f);
    outRough = lerp(lerp(0.08f, 0.26f, vein), 0.56f, dust);
    outF0 = 0.018f.xxx;

    const float3 H = normalize(V + L);
    const float Fdie = rawDielectricFresnel(saturate(dot(V, H)), 1.31f);
    const float backLit = pow(saturate(dot(-V, L)), 3.0f);
    const float3 sssGlow = sunCol * float3(0.14f, 0.46f, 0.62f) * (backLit * 0.35f);
    outSpec = envCol * (Fdie * 1.45f) + sssGlow;
}

// ============================================================================
// Stage 3: Pixel Shader (Scene-Depth Occlusion + 6-Biome PBR + Deep Boreal Conifers)
// ============================================================================
float4 ps_terrain_main(TerrainOutVert inp, bool isFrontFace : SV_IsFrontFace) : SV_Target0 {
    // 1. Exact Occlusion against Skyrim's Standard-Z Scene Depth (0 = near, 1 = far; sky >= 0.99998).
    // Tamriel mountains, architecture, trees, and actors ALWAYS occlude procedural frontier geometry,
    // while sky/empty void (sceneD >= 0.99998) NEVER occludes it.
    const int2 px = clamp(int2(inp.clipPos.xy), int2(0, 0), max(int2(dims.xy) - int2(1, 1), int2(0, 0)));
    const float sceneD = SceneDepth.Load(int3(px, 0));
    if (sceneD > 0.0f && sceneD < 0.99998f && sceneD < inp.clipPos.z) {
        discard;
    }

    const float3 nGeoRaw = normalize(inp.worldNormal);
    const float3 nGeo = isFrontFace ? nGeoRaw : -nGeoRaw;
    const float3 toEye = eyeAt() - inp.relWorldPos;
    const float  viewDist = max(length(toEye), 1.0f);
    const float3 V = toEye / viewDist;
    const float3 L = astroSunDir();

    // Unified world-space atmosphere & lighting oracle from LightingCommon.hlsli
    float3 sunCol, skyZ, skyH, ambUp, ambDown;
    evalWorldAtmosphere(sunCol, skyZ, skyH, ambUp, ambDown);
    // Wave 46 Unified Convective Cloud Shadowing: synchronize frontier mountains, glacial fjords,
    // volcanic calderas & biomechanical forests with Tamriel's live 3-layer wind-advected cloud field!
    const float3 sunColFog = sunCol;
    const float cloudSh = saturate(cloudShadowN(inp.absWorldPos));
    sunCol *= cloudSh;
    const float3 envCol = evalDynamicEnvCube(reflect(-V, nGeo), 0.35f);

    const float curvature = clamp(inp.meta.x, -1.0f, 1.0f);
    const float wetness   = saturate(inp.meta.y + weather.x * 0.45f);
    const float detailSc  = chunkOriginAndScale.w > 0.001f ? chunkOriginAndScale.w : 1.0f;
    const float matTag    = inp.meta.w;

    // Precompute screen-space world position derivatives OUTSIDE divergent branches so SampleGrad
    // selects exact hardware anisotropic mip levels across every biome splat and prop class!
    const float3 dPosX = ddx(inp.absWorldPos);
    const float3 dPosY = ddy(inp.absWorldPos);
    const float  pbrDistW = 1.0f - smoothstep(2600.0f, 11000.0f, viewDist);
    const float  pbrFreq  = 0.0068f * detailSc; // ~147 world units (~2.1m) per seamless tile repeat

    const TriPlanarStrata strata = evalTriPlanarStrata(inp.absWorldPos, nGeo, detailSc);
    float3 N = normalize(nGeo + strata.normalPerturb * 0.65f);

    float3 albedo = 0.0f.xxx;
    float  rough  = 0.72f;
    float3 F0     = 0.038f.xxx;
    float3 specAdd = 0.0f.xxx;
    float3 emissive = 0.0f.xxx;
    float  foliageMask = 0.0f;
    float  atlasAo = 1.0f;

    // Dedicated Procedural 3D Conifer Bark (meta.w ~ 10.0) & Deep Boreal Pine Needles (meta.w ~ 11.0)
    if (matTag > 9.5f && matTag < 10.5f) {
        // Gnarled furrowed Nordic spruce/cedar trunk & flared roots -> Slice 3 (BorealPineBark)
        const float4 barkF = rawNoise3Grad(float3(inp.absWorldPos.xy * 0.065f, inp.absWorldPos.z * 0.014f));
        const float4 barkD = rawNoise3Grad(inp.absWorldPos * 0.22f);
        const float furrow = smoothstep(0.22f, 0.78f, barkF.x * 0.65f + barkD.x * 0.35f);
        const float mossBase = saturate(inp.splat1.y * 0.75f + smoothstep(0.65f, 0.95f, inp.uv.y) * 0.25f);
        const float3 darkBark = float3(0.058f, 0.038f, 0.026f);
        const float3 warmBark = float3(0.145f, 0.095f, 0.062f);
        const float3 mossTint = float3(0.048f, 0.092f, 0.032f);
        albedo = lerp(lerp(darkBark, warmBark, furrow), mossTint, mossBase * 0.55f);
        rough  = lerp(0.92f, 0.78f, furrow);
        F0     = float3(0.032f, 0.030f, 0.028f);
        N      = normalize(nGeo + barkF.yzw * 0.32f + barkD.yzw * 0.16f);
        if (pbrDistW > 0.01f) {
            const TriPlanarPbrSample barkTex = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 3.0f, pbrFreq * 1.95f);
            albedo  = lerp(albedo, lerp(albedo * 0.55f + barkTex.albedoLin * 0.45f, barkTex.albedoLin, 0.38f), pbrDistW * 0.78f);
            rough   = lerp(rough, barkTex.roughness, pbrDistW * 0.55f);
            N       = normalize(lerp(N, barkTex.worldNormal, pbrDistW * 0.58f));
            atlasAo = lerp(1.0f, barkTex.ao, pbrDistW * 0.55f);
        }
    } else if (matTag >= 10.5f) {
        // Deep saturated boreal conifer pine needle clusters (Crimson Desert / redwood target: rich evergreen, NEVER pale white!)
        const float4 needleN = rawNoise3Grad(inp.absWorldPos * 0.14f);
        const float4 twigN   = rawNoise3Grad(inp.absWorldPos * 0.042f);
        const float  snowW   = saturate(inp.splat0.y * smoothstep(0.25f, 0.85f, nGeo.z));
        const float3 deepSpruce = float3(0.020f, 0.054f, 0.020f);
        const float3 richPine   = float3(0.056f, 0.122f, 0.038f);
        const float3 tipNeedle  = float3(0.088f, 0.155f, 0.048f);
        const float  tipMix     = saturate(1.0f - inp.uv.y) * 0.45f;
        float3 pineCol = lerp(lerp(deepSpruce, richPine, needleN.x), tipNeedle, tipMix * twigN.x);
        // Subtle inner-crown ambient occlusion (darker near trunk, slightly brighter on outer boughs)
        pineCol *= lerp(0.62f, 1.05f, saturate(1.0f - inp.uv.y * 0.7f));
        const float3 snowCol = float3(0.72f, 0.79f, 0.88f);
        albedo = lerp(pineCol, snowCol, snowW * 0.72f);
        rough  = lerp(0.82f, 0.60f, snowW);
        F0     = float3(0.028f, 0.032f, 0.026f);
        N      = normalize(nGeo + needleN.yzw * 0.36f + twigN.yzw * 0.18f);
        foliageMask = 1.0f - snowW * 0.8f;
        if (pbrDistW > 0.01f) {
            // Slice 6 (AlpineMossMeadow) adds tactile organic needle/foliage micro-roughness & AO without lightening evergreen hue!
            const TriPlanarPbrSample folTex = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 6.0f, pbrFreq * 2.4f);
            albedo  = lerp(albedo, albedo * lerp(0.76f, 1.24f, folTex.height), pbrDistW * 0.52f);
            rough   = lerp(rough, folTex.roughness, pbrDistW * 0.40f);
            N       = normalize(lerp(N, folTex.worldNormal, pbrDistW * 0.35f));
            atlasAo = lerp(1.0f, folTex.ao, pbrDistW * 0.45f);
        }

        // Live orbital seasonal phenology (conifer deep boreal evergreen + winter bough snow load & rime;
        // deciduous golden carotenoid -> crimson anthocyanin -> winter abscission):
        const bool isEvergreen = (matTag < 11.5f);
        const SeasonalFoliageEval sf = evalSeasonalFoliagePhenology(
            inp.absWorldPos, nGeo, albedo, phenology0, phenology1, seasonThermal0, seasonThermal1, isEvergreen);
        if (!isEvergreen && sf.alphaDiscard01 > 0.78f) {
            discard;
        }
        albedo *= sf.albedoTint;
        albedo  = lerp(albedo, float3(0.76f, 0.83f, 0.91f), sf.canopySnowCov01 * 0.72f);
        albedo  = lerp(albedo, float3(0.82f, 0.89f, 0.96f), sf.frostRim01 * 0.35f);
        rough   = lerp(rough, 0.56f, sf.canopySnowCov01 * 0.65f);
        foliageMask *= (1.0f - sf.canopySnowCov01 * 0.75f);
    } else {
        // Standard 8-channel multi-biome terrain, geological hero props & streamed ESM architecture
        float4 s0 = max(inp.splat0, 0.0f.xxxx); // Rock, Snow, Ice, Basalt
        float4 s1 = max(inp.splat1, 0.0f.xxxx); // Magma, Moss, PebbleSand, Crystal
        const float sSum = max(dot(s0, 1.0f.xxxx) + dot(s1, 1.0f.xxxx), 1.0e-5f);
        s0 /= sSum;
        s1 /= sSum;

        N = normalize(nGeo + strata.normalPerturb * (1.0f - s0.y * 0.55f));
        rough = 0.0f;
        F0 = 0.0f.xxx;
        float aoAcc = 0.0f;
        float3 atlasN = N;

        // Ch0: RockGranite (tri-planar sedimentary cliff strata + Slice 0 NordicGraniteStrata PBR micro-detail)
        if (s0.x > 0.001f) {
            float3 rockCol = lerp(float3(0.11f, 0.12f, 0.14f), float3(0.28f, 0.27f, 0.26f), strata.bandVal)
                           * (0.84f + 0.32f * strata.grainVal);
            float rockRough = lerp(0.84f, 0.66f, strata.grainVal);
            float rockAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex0 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 0.0f, pbrFreq);
                rockCol   = lerp(rockCol, lerp(rockCol, tex0.albedoLin, 0.58f), pbrDistW);
                rockRough = lerp(rockRough, tex0.roughness, pbrDistW * 0.65f);
                rockAo    = lerp(1.0f, tex0.ao, pbrDistW * 0.55f);
                atlasN    = normalize(lerp(atlasN, tex0.worldNormal, s0.x * pbrDistW * 0.52f));
            }
            albedo += rockCol * s0.x;
            rough  += rockRough * s0.x;
            F0     += float3(0.042f, 0.042f, 0.044f) * s0.x;
            aoAcc  += rockAo * s0.x;
        }

        // Ch1: SnowPack (wind-sculpted sastrugi + Slice 1 crystalline micro-grain + static sparkle)
        if (s0.y > 0.001f) {
            float3 snowCol = lerp(float3(0.64f, 0.72f, 0.82f), float3(0.82f, 0.86f, 0.92f), strata.grainVal);
            float snowRough = 0.58f;
            float snowAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex1 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 1.0f, pbrFreq * 1.45f);
                snowCol   *= lerp(1.0f, lerp(0.92f, 1.08f, tex1.height), pbrDistW * 0.50f);
                snowRough  = lerp(snowRough, lerp(0.46f, 0.68f, tex1.roughness), pbrDistW * 0.45f);
                snowAo     = lerp(1.0f, lerp(0.88f, 1.04f, tex1.ao), pbrDistW * 0.40f);
                atlasN     = normalize(lerp(atlasN, tex1.worldNormal, s0.y * pbrDistW * 0.28f));
            }
            const float3 flakeCell = floor(inp.absWorldPos * 0.42f);
            const float3 fh = rawHash33(flakeCell);
            const float3 flakeN = normalize(N + (fh - 0.5f) * 1.05f);
            const float3 H = normalize(V + L);
            const float glint = (fh.x > 0.80f) ? pow(saturate(dot(flakeN, H)), 140.0f) * 0.65f : 0.0f;
            const float backSss = pow(saturate(dot(-V, L)), 3.0f) * 0.14f;
            albedo  += snowCol * s0.y;
            rough   += snowRough * s0.y;
            F0      += float3(0.020f, 0.020f, 0.022f) * s0.y;
            specAdd += sunCol * (glint.xxx + float3(0.34f, 0.58f, 0.84f) * backSss) * s0.y;
            aoAcc   += snowAo * s0.y;
        }

        // Ch2: GlacialIce (clean stratified IOR 1.31 Snell parallax ice + Slice 1 GlacialStratifiedIce micro-glaze)
        if (s0.z > 0.001f) {
            float3 iceAlb, iceF0, iceN, iceSpec;
            float  iceRough;
            evalCleanGlacialIce(inp.absWorldPos, nGeo, V, L, sunCol, envCol, s0.y * 0.45f,
                                iceAlb, iceRough, iceF0, iceN, iceSpec);
            float iceAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample texIce = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 1.0f, pbrFreq * 1.15f);
                iceAlb   = lerp(iceAlb, lerp(iceAlb, texIce.albedoLin * 1.08f, 0.36f), pbrDistW);
                iceRough = lerp(iceRough, texIce.roughness, pbrDistW * 0.55f);
                iceN     = normalize(lerp(iceN, texIce.worldNormal, pbrDistW * 0.42f));
                iceAo    = lerp(1.0f, lerp(0.88f, 1.06f, texIce.ao), pbrDistW * 0.40f);
            }
            albedo  += iceAlb * s0.z;
            rough   += iceRough * s0.z;
            F0      += iceF0 * s0.z;
            atlasN   = normalize(lerp(atlasN, iceN, s0.z * 0.78f));
            specAdd += iceSpec * s0.z;
            aoAcc   += iceAo * s0.z;
        }

        // Ch3: BasaltColumn (obsidian-basalt fractured cliffs + Slice 4 HexagonalBasalt PBR micro-detail)
        if (s0.w > 0.001f) {
            const float4 bGrad = rawNoise3Grad(float3(inp.absWorldPos.xy * 0.016f, inp.absWorldPos.z * 0.004f));
            const float ridge = smoothstep(0.35f, 0.82f, bGrad.x);
            float3 basCol = lerp(float3(0.055f, 0.060f, 0.072f), float3(0.135f, 0.142f, 0.162f), ridge * 0.65f + strata.grainVal * 0.35f);
            float basRough = lerp(0.34f, 0.54f, strata.grainVal);
            float basAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex4 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 4.0f, pbrFreq * 1.10f);
                basCol   = lerp(basCol, lerp(basCol, tex4.albedoLin, 0.55f), pbrDistW);
                basRough = lerp(basRough, tex4.roughness, pbrDistW * 0.65f);
                basAo    = lerp(1.0f, tex4.ao, pbrDistW * 0.55f);
                atlasN   = normalize(lerp(atlasN, tex4.worldNormal, s0.w * pbrDistW * 0.55f));
            }
            albedo += basCol * s0.w;
            rough  += basRough * s0.w;
            F0     += float3(0.052f, 0.054f, 0.058f) * s0.w;
            aoAcc  += basAo * s0.w;
        }

        // Ch4: MagmaFissure (1000K..3400K Planck blackbody magma + Slice 5 VolcanicMagmaCrust vesicular crust)
        if (s1.x > 0.001f) {
            const float crustAmt = saturate(0.72f - inp.meta.z * 0.48f);
            const RawMaterialEvalResult magma = evalHeatMagma3D(
                inp.absWorldPos, nGeo, V, L, sunCol, 2450.0f, crustAmt, magmaFlowPhase);
            float3 magAlb = magma.albedo;
            float  magRough = magma.roughness;
            float3 magN = magma.normal;
            float  magAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex5 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 5.0f, pbrFreq * 1.25f);
                magAlb   = lerp(magAlb, lerp(magAlb, tex5.albedoLin, 0.45f), pbrDistW);
                magRough = lerp(magRough, tex5.roughness, pbrDistW * 0.55f);
                magN     = normalize(lerp(magN, tex5.worldNormal, pbrDistW * 0.45f));
                magAo    = lerp(1.0f, tex5.ao, pbrDistW * 0.50f);
            }
            albedo   += magAlb * s1.x;
            rough    += magRough * s1.x;
            F0       += magma.F0 * s1.x;
            atlasN    = normalize(lerp(atlasN, magN, s1.x * 0.65f));
            specAdd  += magma.specularAdd * s1.x;
            emissive += magma.emissive * s1.x;
            aoAcc    += magAo * s1.x;
        }

        // Ch5: MossMeadow (old-growth conifer floor + Slice 6 AlpineMossMeadow humus & lichen weave)
        if (s1.y > 0.001f) {
            float3 mossCol = lerp(float3(0.042f, 0.098f, 0.032f), float3(0.105f, 0.185f, 0.058f), strata.grainVal);
            float mossRough = 0.86f;
            float mossAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex6 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 6.0f, pbrFreq * 1.35f);
                mossCol   = lerp(mossCol, lerp(mossCol, tex6.albedoLin, 0.55f), pbrDistW);
                mossRough = lerp(mossRough, tex6.roughness, pbrDistW * 0.60f);
                mossAo    = lerp(1.0f, tex6.ao, pbrDistW * 0.52f);
                atlasN    = normalize(lerp(atlasN, tex6.worldNormal, s1.y * pbrDistW * 0.48f));
            }
            albedo  += mossCol * s1.y;
            rough   += mossRough * s1.y;
            F0      += float3(0.030f, 0.032f, 0.028f) * s1.y;
            aoAcc   += mossAo * s1.y;
        }

        // Ch6: RiverPebbleSand (alpine meltwater braided shoals + Slice 7 BraidedRiverPebbles cobble detail)
        if (s1.z > 0.001f) {
            const float4 pGrad = rawNoise3Grad(inp.absWorldPos * 0.055f);
            float3 cobCol = lerp(float3(0.12f, 0.11f, 0.10f), float3(0.28f, 0.26f, 0.23f), pGrad.x * 0.65f + strata.grainVal * 0.35f);
            float cobRough = lerp(0.58f, 0.22f, wetness);
            float cobAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex7 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 7.0f, pbrFreq * 1.55f);
                cobCol   = lerp(cobCol, lerp(cobCol, tex7.albedoLin, 0.62f), pbrDistW);
                cobRough = lerp(cobRough, lerp(tex7.roughness, 0.18f, wetness * 0.65f), pbrDistW * 0.65f);
                cobAo    = lerp(1.0f, tex7.ao, pbrDistW * 0.58f);
                atlasN   = normalize(lerp(atlasN, tex7.worldNormal, s1.z * pbrDistW * 0.58f));
            }
            albedo += cobCol * s1.z;
            rough  += cobRough * s1.z;
            F0     += float3(0.040f, 0.040f, 0.040f) * s1.z;
            aoAcc  += cobAo * s1.z;
        }

        // Ch7: ArcaneCrystal / Forged Metal Trim (Cauchy quartz + Slice 2 ForgedBrushedSteel anisotropic micro-sheen)
        if (s1.w > 0.001f) {
            const RawMaterialEvalResult gem = evalRefractiveGlassGem3D(
                inp.absWorldPos, nGeo, V, L, sunCol, envCol, 1.544f, 0.0085f,
                float3(0.38f, 0.24f, 0.06f), 18.0f, 0.065f);
            const RawMaterialEvalResult arc = evalArcaneMagic3D(
                inp.absWorldPos, nGeo, V, L, sunCol, float3(0.46f, 0.34f, 0.96f), 460.0f, 16.0f, 0.0f);
            float3 cryAlb = lerp(gem.albedo, arc.albedo, 0.45f);
            float  cryRough = min(gem.roughness, arc.roughness);
            float3 cryF0 = lerp(gem.F0, arc.F0, 0.50f);
            float3 cryN = gem.normal;
            float  cryAo = 1.0f;
            if (pbrDistW > 0.01f) {
                const TriPlanarPbrSample tex2 = evalTriPlanarPbrAtlas(inp.absWorldPos, dPosX, dPosY, nGeo, 2.0f, pbrFreq * 1.75f);
                cryAlb   = lerp(cryAlb, lerp(cryAlb, tex2.albedoLin, 0.32f), pbrDistW);
                cryRough = lerp(cryRough, tex2.roughness, pbrDistW * 0.42f);
                cryF0    = lerp(cryF0, lerp(cryF0, float3(0.56f, 0.58f, 0.62f), tex2.metallic * 0.45f), pbrDistW);
                cryN     = normalize(lerp(cryN, tex2.worldNormal, pbrDistW * 0.36f));
                cryAo    = lerp(1.0f, tex2.ao, pbrDistW * 0.40f);
            }
            albedo   += cryAlb * s1.w;
            rough    += cryRough * s1.w;
            F0       += cryF0 * s1.w;
            atlasN    = normalize(lerp(atlasN, cryN, s1.w * 0.80f));
            specAdd  += (gem.specularAdd * 0.65f + arc.specularAdd * 0.35f) * s1.w;
            emissive += arc.emissive * (0.55f + 0.45f * inp.meta.z) * s1.w;
            aoAcc    += cryAo * s1.w;
        }

        N = atlasN;
        atlasAo = clamp(aoAcc, 0.35f, 1.0f);
    }

    // 3. Analytical Curvature, Live Orbital Seasons/Thermals & Wetness Physical Modulation
    float effWetness = wetness;
    if (matTag < 9.5f) {
        // Accumulate active warm fire heat + volcanic magma self-heat for thermal snow/ice melt rings:
        float fireHeat01 = inp.splat1.x * 1.5f;
        const uint nFireL = min(flags.y, 16u);
        [loop] for (uint li = 0u; li < nFireL; ++li) {
            const float3 lc = lightColS[li].rgb;
            if (lc.r > lc.b * 1.35f && lc.r > 0.12f) {
                fireHeat01 = max(fireHeat01, evalFireThermalMeltMask(
                    inp.relWorldPos, lightPosR[li].xyz, lightPosR[li].w, saturate(lc.r * 1.25f)));
            }
        }
        const SurfaceThermalEval st = evalSeasonalSurfaceThermals(
            inp.absWorldPos, nGeo, L, cloudWind.xy, seasonThermal0, seasonThermal1,
            fireHeat01, wetness, weather.y, phenology1.w);
        if (st.snowCov01 > 0.005f) {
            albedo = lerp(albedo, st.snowAlbedo, st.snowCov01 * 0.88f);
            rough  = lerp(rough, st.snowRoughness, st.snowCov01 * 0.85f);
            F0     = lerp(F0, float3(0.020f, 0.020f, 0.022f), st.snowCov01 * 0.85f);
        }
        if (st.frostRime01 > 0.01f) {
            albedo = lerp(albedo, float3(0.76f, 0.85f, 0.94f), st.frostRime01 * 0.38f);
            rough  = lerp(rough, 0.42f, st.frostRime01 * 0.35f);
        }
        if (st.iceGlaze01 > 0.01f) {
            albedo = lerp(albedo, albedo * float3(0.68f, 0.78f, 0.88f), st.iceGlaze01 * 0.55f);
            rough  = lerp(rough, 0.09f, st.iceGlaze01 * 0.85f);
            F0     = lerp(F0, float3(0.020f, 0.022f, 0.024f), st.iceGlaze01 * 0.75f);
        }
        effWetness = saturate(max(effWetness, max(st.wetBoost01, st.slushRim01 * 0.85f)));
    }
    const float curvAo = saturate(1.0f + curvature * 0.24f) * lerp(0.88f, 1.06f, saturate(atlasAo));
    const float ridgeHighlight = saturate(curvature) * 0.12f;
    albedo *= lerp(1.0f, 0.62f, effWetness * (1.0f - inp.splat0.y * 0.8f) * (1.0f - foliageMask * 0.65f));
    albedo *= lerp(0.76f, 1.08f, curvAo);
    rough   = clamp(lerp(rough, 0.14f, effWetness * 0.65f * (1.0f - foliageMask * 0.7f)), 0.05f, 0.96f);

    // 4. Direct Sun GGX + Shadowed Foliage Rim Translucency + Hemisphere Ambient + Dynamic Environment Reflection
    const float nv = max(dot(N, V), 1.0e-4f);
    const float nl = saturate(dot(N, L));
    const float3 H = normalize(V + L);
    const float nh = saturate(dot(N, H));
    const float vh = saturate(dot(V, H));
    const float3 Fschlick = F0 + (1.0f - F0) * pow(1.0f - vh, 5.0f);
    const float ggx = rawGgxSpecTerm(nh, nv, nl, rough, dot(F0, 0.3333f.xxx));

    const float sh = cascadeShadow(inp.relWorldPos, inp.clipPos.z, true);
    const float3 diffuseDirect = albedo * (1.0f / PI) * sunCol * (nl * sh * (1.0f + ridgeHighlight));
    const float3 specDirect = sunCol * Fschlick * ggx * (sh * 0.28f * (1.0f - foliageMask * 0.65f)) + specAdd;
    // Subtle warm-golden chlorophyll backlit rim strictly gated by shadowed sun (never white bleach!)
    const float backFoliage = pow(saturate(dot(-V, L)), 3.2f) * foliageMask * (0.35f + 0.65f * saturate(dot(-N, L) * 0.5f + 0.5f));
    const float3 foliageRim = sunCol * float3(0.18f, 0.32f, 0.06f) * (backFoliage * sh * 0.22f);

    const float hemiW = saturate(N.z * 0.5f + 0.5f);
    const float3 hemiAmb = lerp(ambDown, ambUp, hemiW) * curvAo;
    const float3 Fenv = F0 + (max((1.0f - rough).xxx, F0) - F0) * pow(1.0f - nv, 5.0f);
    const float3 iblSpec = evalDynamicEnvCube(reflect(-V, N), rough) * Fenv * lerp(0.50f, 0.10f, rough);

    float3 hdrColor = diffuseDirect + albedo * hemiAmb + specDirect + foliageRim + iblSpec + emissive;

    // 5. Seamless Atmospheric Perspective & Height-Stratified Aerial Fog (fused with LightingCommon.hlsli)
    const float hFall = fogHeightFalloff > 1.0e-6f ? fogHeightFalloff : 0.00028f;
    const float baseDens = max(tune.x, 0.000032f);
    const float altFactor = exp(-max(inp.absWorldPos.z + 1200.0f, 0.0f) * hFall);
    const float optDepth = viewDist * baseDens * lerp(0.45f, 1.30f, altFactor);
    const float trans = exp(-optDepth);

    const float cosSun = dot(-V, L);
    const float miePhase = rawMultiScatterPhase(cosSun, clamp(tune.y, 0.25f, 0.78f), optDepth);
    const float viewElev = saturate(-V.z * 0.5f + 0.5f);
    const float3 fogCol = lerp(skyH, skyZ, saturate(viewElev * 0.72f))
                        + sunColFog * (miePhase * saturate(astroSunElev() * 4.0f + 0.15f) * 0.62f);

    hdrColor = hdrColor * trans + fogCol * (1.0f - trans);
    return float4(max(hdrColor, 0.0f.xxx), 1.0f);
}
