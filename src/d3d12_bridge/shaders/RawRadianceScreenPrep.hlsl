// RawRadianceScreenPrep.hlsl: Live Screen-Space Radiance Cascades GI Prep & Depth-Aware Bilateral Composite (SM 6.6 / 6.10).
//
// 2 Compute Entry Points ([numthreads(8, 8, 1)]):
//   1. cs_rc_screen_prep  : Downsamples full-res HDR ColorTex (t0) + standard-Z SceneDepth (t1) into a 256x256
//                           RGBA16F Emissive+Alpha field (u0). Extracts bright emitters above luma > 1.35 (soft-capped
//                           <= 6.0 for energy conservation) and foreground depth-discontinuity wall/pillar occluders.
//   2. cs_rc_screen_apply : Depth-aware bilateral upsample of resolved 256x256 IrradianceTex (t0) using SceneDepth (t1),
//                           adding subtle energy-conserving diffuse GI bounce onto HDR ColorOut (u0) with strict sky
//                           gating (depth >= 0.99998 untouched) and luminance-shoulder clamping against self-amplification.
//
// Original clean-room implementation, MIT License.

cbuffer ScreenPrepCB : register(b0)
{
    uint4  dims;            // x = fullWidth, y = fullHeight, z = atlasRes (256), w = flags
    float4 invDims;         // x = 1/fullW, y = 1/fullH, z = 1/256, w = giStrength
    float4 extractParams;   // x = emissiveThresh (1.35), y = emissiveCap (6.0), z = depthEdgeScale (45.0), w = skyDepthThresh (0.99998)
    float4 bilateralParams; // x = bilateralDepthSigma (0.015), y = lumaShoulder (2.5), z = maxBounceLuma (1.25), w = unused
};

Texture2D<float4>   ColorOrIrrad : register(t0); // prep: full-res HDR ColorTex | apply: 256x256 IrradianceTex
Texture2D<float>    SceneDepth   : register(t1); // standard-Z hardware depth (0 = near, 1 = far, sky >= 0.99998)
RWTexture2D<float4> OutTex       : register(u0); // prep: 256x256 EmissiveAlpha | apply: full-res HDR ColorOut (in-place)

SamplerState LinClamp : register(s0);

float rec709Luma(float3 c)
{
    return dot(max(c, float3(0.0f, 0.0f, 0.0f)), float3(0.2126f, 0.7152f, 0.0722f));
}

// ----------------------------------------------------------------------------
// Pass 1: cs_rc_screen_prep (32 x 32 x 1 groups -> 256 x 256 EmissiveAlpha)
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void cs_rc_screen_prep(uint3 tid : SV_DispatchThreadID)
{
    const uint atlasRes = max(dims.z, 1u);
    if (tid.x >= atlasRes || tid.y >= atlasRes) {
        return;
    }

    const float invAtlas = invDims.z;
    const float2 uvC = (float2(tid.xy) + 0.5f) * invAtlas;
    const float thresh = max(extractParams.x, 0.05f);
    const float cap = max(extractParams.y, 0.5f);
    const float skyThresh = extractParams.w;

    // 2x2 super-sampled extraction so narrow candles/torches/embers never drop out during downsampling
    const float2 subOff = float2(0.25f * invAtlas, 0.25f * invAtlas);
    static const float2 kSub[4] = {
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f), float2( 1.0f,  1.0f)
    };

    float3 emitSum = float3(0.0f, 0.0f, 0.0f);
    float  emitLumaMax = 0.0f;
    float  dCenterMin = 1.0f;
    float  dCenterAvg = 0.0f;
    float  nonSkyCount = 0.0f;

    [unroll] for (int i = 0; i < 4; ++i) {
        const float2 uv = saturate(uvC + kSub[i] * subOff);
        const float d = SceneDepth.SampleLevel(LinClamp, uv, 0.0f);
        dCenterMin = min(dCenterMin, d);
        dCenterAvg += d;
        if (d < skyThresh) {
            nonSkyCount += 1.0f;
            const float3 col = max(ColorOrIrrad.SampleLevel(LinClamp, uv, 0.0f).rgb, float3(0.0f, 0.0f, 0.0f));
            const float lum = rec709Luma(col);
            const float excess = max(0.0f, lum - thresh);
            if (excess > 1.0e-4f) {
                // Smooth energy-conserving soft-cap <= emissiveCap (default 6.0)
                const float cappedLum = cap * excess / (excess + cap * 0.55f);
                const float3 eCol = col * (cappedLum / max(lum, 1.0e-4f));
                emitSum += eCol;
                emitLumaMax = max(emitLumaMax, cappedLum);
            }
        }
    }

    const float3 emitRGB = emitSum * 0.25f;
    dCenterAvg *= 0.25f;

    // Foreground wall/pillar occluder detection via multi-radius depth context comparison (standard-Z: smaller = closer)
    float occluderAlpha = 0.0f;
    if (nonSkyCount > 0.5f) {
        const float rNear = 2.0f * invAtlas;
        const float rWide = 10.0f * invAtlas;
        const float dL1 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2(-rNear, 0.0f)), 0.0f);
        const float dR1 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2( rNear, 0.0f)), 0.0f);
        const float dD1 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2(0.0f, -rNear)), 0.0f);
        const float dU1 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2(0.0f,  rNear)), 0.0f);

        const float dL2 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2(-rWide, 0.0f)), 0.0f);
        const float dR2 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2( rWide, 0.0f)), 0.0f);
        const float dD2 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2(0.0f, -rWide)), 0.0f);
        const float dU2 = SceneDepth.SampleLevel(LinClamp, saturate(uvC + float2(0.0f,  rWide)), 0.0f);

        // Max surrounding depth (capped below sky so sky borders don't turn entire terrain into opaque walls)
        const float bgMax1 = max(max(min(dL1, 0.995f), min(dR1, 0.995f)), max(min(dD1, 0.995f), min(dU1, 0.995f)));
        const float bgMax2 = max(max(min(dL2, 0.995f), min(dR2, 0.995f)), max(min(dD2, 0.995f), min(dU2, 0.995f)));
        const float bgRef = max(bgMax1, bgMax2);

        // Foreground structure significantly closer than background casts soft penumbra shadows
        const float relJump = max(0.0f, bgRef - dCenterMin) / max(1.0f - dCenterMin, 0.04f);
        const float fgWall = smoothstep(0.06f, 0.24f, relJump);

        // Sharp depth-discontinuity silhouette ridge
        const float edgeGrad = max(abs(dR1 - dL1), abs(dU1 - dD1)) * extractParams.z;
        const float edgeOcc = smoothstep(0.18f, 0.75f, edgeGrad) * smoothstep(0.02f, 0.12f, bgMax1 - dCenterAvg);

        occluderAlpha = max(fgWall, edgeOcc);

        // Active bright emitters carry solid emission opacity so Radiance Cascades integrates exact surface radiance
        const float emitAlpha = smoothstep(0.05f, 0.55f, emitLumaMax);
        occluderAlpha = max(occluderAlpha, emitAlpha);
    }

    OutTex[tid.xy] = float4(emitRGB, saturate(occluderAlpha));
}

// ----------------------------------------------------------------------------
// Pass 2: cs_rc_screen_apply (full-res depth-aware bilateral GI composite)
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void cs_rc_screen_apply(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= dims.x || tid.y >= dims.y) {
        return;
    }

    const int3 ip = int3(int(tid.x), int(tid.y), 0);
    const float dCenter = SceneDepth.Load(ip);
    const float skyThresh = extractParams.w;

    // Strict sky gate: sky pixels (depth >= 0.99998) are 100% untouched
    if (dCenter >= skyThresh) {
        return;
    }

    const float giStrength = max(invDims.w, 0.0f);
    if (giStrength <= 1.0e-5f) {
        return;
    }

    const float2 uv = (float2(tid.xy) + 0.5f) * invDims.xy;
    const float atlasResF = float(max(dims.z, 1u));
    const float invAtlas = invDims.z;

    // Depth-aware 2x2 bilateral upsample from 256x256 IrradianceTex (zero foreground halo bleeding!)
    const float2 atlasCoord = uv * atlasResF - 0.5f;
    const float2 baseIdx = floor(atlasCoord);
    const float2 fracW = frac(atlasCoord);
    const float depthSigma = max(bilateralParams.x * max(1.0f - dCenter, 0.04f), 1.0e-4f);

    float3 giSum = float3(0.0f, 0.0f, 0.0f);
    float  wSum  = 0.0f;

    [unroll] for (int dy = 0; dy <= 1; ++dy) {
        [unroll] for (int dx = 0; dx <= 1; ++dx) {
            const float wx = (dx == 0) ? (1.0f - fracW.x) : fracW.x;
            const float wy = (dy == 0) ? (1.0f - fracW.y) : fracW.y;
            const float2 tapUv = saturate((baseIdx + float2(float(dx) + 0.5f, float(dy) + 0.5f)) * invAtlas);
            const float dTap = SceneDepth.SampleLevel(LinClamp, tapUv, 0.0f);
            if (dTap >= skyThresh) {
                continue;
            }
            const float wDepth = exp(-abs(dCenter - dTap) / depthSigma);
            const float w = wx * wy * wDepth;
            if (w > 1.0e-5f) {
                const float3 irr = max(ColorOrIrrad.SampleLevel(LinClamp, tapUv, 0.0f).rgb, float3(0.0f, 0.0f, 0.0f));
                giSum += irr * w;
                wSum += w;
            }
        }
    }

    const float3 giIrrad = (wSum > 1.0e-4f)
        ? (giSum / wSum)
        : max(ColorOrIrrad.SampleLevel(LinClamp, uv, 0.0f).rgb, float3(0.0f, 0.0f, 0.0f));

    const float4 cur = OutTex[tid.xy];
    const float curLuma = rec709Luma(cur.rgb);

    // Luminance-shoulder self-amplification guard: bright flames/torches never glow brighter onto themselves
    const float shoulder = max(bilateralParams.y, 0.5f);
    const float lumaRatio = curLuma / shoulder;
    const float selfGuard = 1.0f / (1.0f + lumaRatio * lumaRatio * lumaRatio);

    float3 bounce = giIrrad * (giStrength * selfGuard);
    const float maxBounce = max(bilateralParams.z, 0.1f);
    const float bounceLum = rec709Luma(bounce);
    if (bounceLum > 1.0e-5f) {
        bounce *= maxBounce / (bounceLum + maxBounce);
    }

    OutTex[tid.xy] = float4(cur.rgb + bounce, cur.a);
}
