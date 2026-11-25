// RawPostCinematicSim.hlsl: Clean-Room SM 6.6 / 6.10 GPU Compute Cinematic Post-Process Solver
// (Tile-Max Velocity Reduction + 3x3 Neighbor-Max Dilation + 12-Tap Directional Motion Blur
//  + Depth-Aware Physical Circle-of-Confusion Hexagonal Bokeh DoF with Foreground Bleed Guard
//  + Natural cos^4 Filmic Optical Vignette).
//
// Depth Convention: Standard-Z (0 = nearZ, 1 = farZ, sky >= 0.99998).
//
// Entry points:
//   1. cs_post_tile_max           ([numthreads(16, 16, 1)]): 16x16 per-tile max velocity reduction
//   2. cs_post_neighbor_max       ([numthreads(8, 8, 1)]):   3x3 tile neighborhood max dilation
//   3. cs_post_cinematic_composite([numthreads(8, 8, 1)]):   Hexagonal Bokeh DoF + 12-tap Motion Blur + Vignette
//
// Original clean-room implementation, MIT License.

cbuffer PostCinematicCB : register(b0)
{
    uint4  gDims;         // x = width, y = height, z = tileW, w = tileH
    float4 gInvDims;      // x = 1/w, y = 1/h, z = nearZ, w = farZ
    float4 gDofLens;      // x = focusDistanceM, y = focalLengthM, z = apertureFStop, w = cocScale
    float4 gDofMotion;    // x = dofStrength, y = maxBokehRadiusPx, z = motionBlurScale, w = maxMotionRadiusPx
    float4 gVignetteJit;  // x = vignetteStrength, y = frameJitter, z = hasVelocityTex (0/1), w = 0
};

Texture2D<float4>   SceneColor  : register(t0);
Texture2D<float>    SceneDepth  : register(t1);
Texture2D<float2>   VelocityTex : register(t2);
Texture2D<float2>   TileVelTex  : register(t3);

RWTexture2D<float2> TileMaxOut  : register(u0); // Used by cs_post_tile_max & cs_post_neighbor_max
RWTexture2D<float4> ColorOut    : register(u0); // Used by cs_post_cinematic_composite

SamplerState gLinearClamp : register(s0);

static const float kSkyDepthThreshold = 0.99998f;
static const float kSensorHeightM     = 0.024f; // 35mm full-frame vertical aperture (24mm)

float3 Sanitize3(float3 v)
{
    return float3(
        isfinite(v.x) ? clamp(v.x, 0.0f, 65000.0f) : 0.0f,
        isfinite(v.y) ? clamp(v.y, 0.0f, 65000.0f) : 0.0f,
        isfinite(v.z) ? clamp(v.z, 0.0f, 65000.0f) : 0.0f);
}

float2 SanitizeVel(float2 v, float scale, float maxRadPx, float2 invDims)
{
    if (!all(isfinite(v)) || scale <= 0.0f || maxRadPx <= 0.0f) {
        return float2(0.0f, 0.0f);
    }
    float2 vs = v * scale;
    float2 vPx = vs / max(invDims, float2(1.0e-6f, 1.0e-6f));
    float  lenPx = length(vPx);
    if (lenPx > maxRadPx && lenPx > 1.0e-5f) {
        vPx *= (maxRadPx / lenPx);
        vs = vPx * invDims;
    }
    return vs;
}

float LinearizeStdZ(float rawZ)
{
    const float nearZ = max(gInvDims.z, 1.0e-4f);
    const float farZ  = max(gInvDims.w, nearZ + 1.0f);
    if (!isfinite(rawZ) || rawZ >= kSkyDepthThreshold) {
        return farZ;
    }
    const float z = clamp(rawZ, 0.0f, kSkyDepthThreshold);
    const float denom = max(farZ - z * (farZ - nearZ), 1.0e-5f);
    return clamp((nearZ * farZ) / denom, nearZ, farZ);
}

// Signed physical thin-lens Circle of Confusion in pixels:
//   < 0 : near foreground in front of focus plane
//   = 0 : sharp at focusDistanceM
//   > 0 : far background behind focus plane
float ComputeSignedCocPx(float linearZ)
{
    const float dofStrength = gDofMotion.x;
    const float maxRadPx    = gDofMotion.y;
    if (dofStrength <= 1.0e-4f || maxRadPx <= 0.25f) {
        return 0.0f;
    }
    const float S = max(gDofLens.x, 0.1f);
    const float f = clamp(gDofLens.y, 0.008f, 0.400f);
    const float N = max(gDofLens.z, 0.7f);
    const float cocScale = max(gDofLens.w, 0.0f);

    const float apertureM  = f / N;
    const float focusDenom = max(S - f, 0.05f);
    const float cocSensor  = apertureM * (f / focusDenom) * ((linearZ - S) / max(linearZ, 0.05f));
    const float cocPx      = (cocSensor / kSensorHeightM) * (float)gDims.y * cocScale * dofStrength;
    return clamp(cocPx, -maxRadPx, maxRadPx);
}

float InterleavedGradientNoise(float2 px, float frameJitter)
{
    const float2 p = px + float2(frameJitter * 5.588238f, frameJitter * 7.139127f);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float SoftDepthCompare(float zA, float zB)
{
    // Returns ~1 when zA is in front of or equal to zB, ~0 when zA is significantly behind zB.
    const float zScale = max(0.08f * min(zA, zB), 0.15f);
    return saturate(1.0f - (zA - zB) / zScale);
}

float ConeWeight(float distPx, float velLenPx)
{
    return saturate(1.0f - distPx / max(velLenPx, 0.5f));
}

float CylinderWeight(float distPx, float velLenPx)
{
    return 1.0f - smoothstep(0.95f * velLenPx, 1.05f * velLenPx, distPx);
}

// ============================================================================
// Pass 1: 16x16 Tile-Max Velocity Reduction
// ============================================================================
groupshared float2 s_maxVel[256];

[numthreads(16, 16, 1)]
void cs_post_tile_max(
    uint3 dtid       : SV_DispatchThreadID,
    uint3 groupID    : SV_GroupID,
    uint  groupIndex : SV_GroupIndex)
{
    float2 v = float2(0.0f, 0.0f);
    if (gVignetteJit.z > 0.5f && dtid.x < gDims.x && dtid.y < gDims.y) {
        const float2 rawV = VelocityTex.Load(int3(dtid.xy, 0));
        v = SanitizeVel(rawV, gDofMotion.z, gDofMotion.w, gInvDims.xy);
    }

    s_maxVel[groupIndex] = v;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (groupIndex < s) {
            const float2 a = s_maxVel[groupIndex];
            const float2 b = s_maxVel[groupIndex + s];
            if (dot(b, b) > dot(a, a)) {
                s_maxVel[groupIndex] = b;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0u && groupID.x < gDims.z && groupID.y < gDims.w) {
        TileMaxOut[groupID.xy] = s_maxVel[0];
    }
}

// ============================================================================
// Pass 2: 3x3 Neighbor-Max Velocity Dilation
// ============================================================================
[numthreads(8, 8, 1)]
void cs_post_neighbor_max(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDims.z || dtid.y >= gDims.w) return;

    float2 bestV  = float2(0.0f, 0.0f);
    float  bestL2 = -1.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            const int2 tc = clamp(int2(dtid.xy) + int2(dx, dy), int2(0, 0), int2(gDims.zw) - 1);
            const float2 cand = TileVelTex.Load(int3(tc, 0));
            const float  l2   = all(isfinite(cand)) ? dot(cand, cand) : 0.0f;
            if (l2 > bestL2) {
                bestL2 = l2;
                bestV  = all(isfinite(cand)) ? cand : float2(0.0f, 0.0f);
            }
        }
    }

    TileMaxOut[dtid.xy] = bestV;
}

// ============================================================================
// Pass 3: Hexagonal Bokeh DoF (Foreground Bleed Guard) + 12-Tap Motion Blur + Vignette
// ============================================================================

// Deterministic 18-tap 2-ring hexagonal cinema-aperture kernel (6 inner @ r=0.5, 12 outer @ r=1.0)
static const float2 kHexOffsets[18] = {
    // Inner hex ring (r = 0.5)
    float2( 0.500000f,  0.000000f),
    float2( 0.250000f,  0.433013f),
    float2(-0.250000f,  0.433013f),
    float2(-0.500000f,  0.000000f),
    float2(-0.250000f, -0.433013f),
    float2( 0.250000f, -0.433013f),
    // Outer hex ring (r = 1.0 vertices + mid-edges along hexagon boundary)
    float2( 1.000000f,  0.000000f),
    float2( 0.750000f,  0.433013f),
    float2( 0.500000f,  0.866025f),
    float2( 0.000000f,  0.866025f),
    float2(-0.500000f,  0.866025f),
    float2(-0.750000f,  0.433013f),
    float2(-1.000000f,  0.000000f),
    float2(-0.750000f, -0.433013f),
    float2(-0.500000f, -0.866025f),
    float2( 0.000000f, -0.866025f),
    float2( 0.500000f, -0.866025f),
    float2( 0.750000f, -0.433013f)
};

[numthreads(8, 8, 1)]
void cs_post_cinematic_composite(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDims.x || dtid.y >= gDims.y) return;

    const int2   px       = int2(dtid.xy);
    const float2 invDims  = gInvDims.xy;
    const float2 uv       = (float2(dtid.xy) + 0.5f) * invDims;
    const float3 centerC  = Sanitize3(SceneColor.Load(int3(px, 0)).rgb);
    const float  rawZ     = SceneDepth.Load(int3(px, 0));
    const float  centerZ  = LinearizeStdZ(rawZ);
    const float  cocPx    = ComputeSignedCocPx(centerZ);
    const float  absCocPx = abs(cocPx);

    // ------------------------------------------------------------------------
    // Stage 1: Depth-Aware Physical CoC Hexagonal Bokeh DoF (Foreground Bleed Guard)
    // ------------------------------------------------------------------------
    float3 dofColor = centerC;
    if (absCocPx >= 0.35f) {
        float3 accumC = centerC;
        float  accumW = 1.0f;

        // Depth threshold separating distinct foreground occluders from background plane
        const float depthGuardEps = max(0.06f * centerZ, 0.25f);

        [unroll]
        for (uint k = 0u; k < 18u; ++k) {
            const float2 tapOffPx = kHexOffsets[k] * absCocPx;
            const float  tapDist  = length(tapOffPx);
            const float2 tapUv    = clamp(uv + tapOffPx * invDims, invDims * 0.5f, 1.0f - invDims * 0.5f);
            const int2   tapPx    = clamp(int2(floor(tapUv * float2(gDims.xy))), int2(0, 0), int2(gDims.xy) - 1);

            const float  tapZ     = LinearizeStdZ(SceneDepth.Load(int3(tapPx, 0)));
            const float  tapCoc   = ComputeSignedCocPx(tapZ);
            const float  absTapC  = abs(tapCoc);

            float w = 1.0f;
            if (tapZ < centerZ - depthGuardEps) {
                // Tap is STRICTLY closer to camera than center pixel (foreground occluder!).
                // Foreground Bleed Guard: reject sharp foreground taps so they NEVER bleed onto
                // blurry background; only allow genuinely defocused foreground bokeh scatter.
                if (absTapC < 0.75f) {
                    w = 0.0f;
                } else {
                    w = smoothstep(tapDist - 0.5f, tapDist + 0.5f, absTapC);
                }
            } else if (tapZ > centerZ + depthGuardEps && cocPx < 0.0f) {
                // Center is blurred foreground and tap is far background: keep smooth foreground edge.
                w = 0.35f;
            } else {
                // Same depth plane or background-to-background: full bokeh weight.
                w = smoothstep(tapDist - 0.75f, tapDist + 0.5f, absCocPx);
            }

            if (w > 1.0e-4f) {
                // Point-load co-located with tapPx depth so hardware bilinear never leaks
                // sharp foreground silhouette color across depth discontinuities.
                const float3 tapC = Sanitize3(SceneColor.Load(int3(tapPx, 0)).rgb);
                accumC += tapC * w;
                accumW += w;
            }
        }
        dofColor = accumC / max(accumW, 1.0e-4f);
    }

    // ------------------------------------------------------------------------
    // Stage 2: 12-Tap Neighbor-Max Directional Motion Blur (McGuire 2012)
    // ------------------------------------------------------------------------
    float3 finalColor = dofColor;
    if (gVignetteJit.z > 0.5f && gDofMotion.z > 0.0f && gDofMotion.w > 0.25f) {
        const uint2  tileCoord = min(dtid.xy / 16u, gDims.zw - 1u);
        const float2 tileVel   = TileVelTex.Load(int3(tileCoord, 0));
        const float2 tileVelPx = all(isfinite(tileVel)) ? (tileVel / max(invDims, float2(1.0e-6f, 1.0e-6f))) : float2(0.0f, 0.0f);
        const float  tileLenPx = length(tileVelPx);

        if (tileLenPx >= 0.35f) {
            const float2 centerVel   = SanitizeVel(VelocityTex.Load(int3(px, 0)), gDofMotion.z, gDofMotion.w, invDims);
            const float  centerLenPx = length(centerVel / max(invDims, float2(1.0e-6f, 1.0e-6f)));

            const float  jitter   = (gVignetteJit.y > 0.0f) ? (InterleavedGradientNoise(float2(dtid.xy), gVignetteJit.y) - 0.5f) : 0.0f;
            float        weightS  = 1.0f / max(centerLenPx, 0.5f);
            float3       colorS   = dofColor * weightS;

            static const uint kMotionTaps = 12u;
            [unroll]
            for (uint i = 0u; i < kMotionTaps; ++i) {
                // Symmetric taps spanning [-1, +1] along neighbor-max velocity vector
                const float t     = (((float)i + 0.5f + jitter * 0.5f) / (float)kMotionTaps) * 2.0f - 1.0f;
                const float2 tapUv = clamp(uv + tileVel * t, invDims * 0.5f, 1.0f - invDims * 0.5f);
                const int2   tapPx = clamp(int2(floor(tapUv * float2(gDims.xy))), int2(0, 0), int2(gDims.xy) - 1);

                const float  tapZ     = LinearizeStdZ(SceneDepth.Load(int3(tapPx, 0)));
                const float2 tapVel   = SanitizeVel(VelocityTex.Load(int3(tapPx, 0)), gDofMotion.z, gDofMotion.w, invDims);
                const float  tapLenPx = length(tapVel / max(invDims, float2(1.0e-6f, 1.0e-6f)));
                const float  distPx   = abs(t) * tileLenPx;

                const float f = SoftDepthCompare(tapZ, centerZ);
                const float b = SoftDepthCompare(centerZ, tapZ);

                const float w =
                    f * ConeWeight(distPx, tapLenPx) +
                    b * ConeWeight(distPx, centerLenPx) +
                    CylinderWeight(distPx, tapLenPx) * CylinderWeight(distPx, centerLenPx) * 2.0f;

                if (w > 1.0e-4f) {
                    const float3 tapC = Sanitize3(SceneColor.SampleLevel(gLinearClamp, tapUv, 0.0f).rgb);
                    colorS  += tapC * w;
                    weightS += w;
                }
            }
            finalColor = colorS / max(weightS, 1.0e-4f);
        }
    }

    // ------------------------------------------------------------------------
    // Stage 3: Natural cos^4 Filmic Optical Vignette
    // ------------------------------------------------------------------------
    const float vigStrength = saturate(gVignetteJit.x);
    if (vigStrength > 1.0e-4f) {
        const float2 d   = (uv - 0.5f) * 2.0f;
        const float  r2  = dot(d, d);
        const float  vig = lerp(1.0f, pow(saturate(1.0f - 0.22f * r2), 1.6f), vigStrength);
        finalColor *= vig;
    }

    const float normCoc = (gDofMotion.y > 1.0e-4f) ? clamp(cocPx / gDofMotion.y, -1.0f, 1.0f) : 0.0f;
    ColorOut[dtid.xy] = float4(Sanitize3(finalColor), normCoc);
}
