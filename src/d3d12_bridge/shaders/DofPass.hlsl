// DofPass.hlsl: Physical 35mm Full-Frame Thin-Lens Cinema Depth of Field (SM 6.9 — Silent Horizons / ADOF Parity).
//   dof_coc    : 1/2-res exact physical 35mm Full-Frame Thin-Lens CoC (meters, 50mm prime, 9-point median autofocus)
//   dof_gather : 1/2-res 91-tap 5-ring concentric aperture disc gather (zero noise/zero stipple, Jimenez/McGuire
//                near/far separation, HDR specular bokeh orb weighting, cat's-eye corner squeeze & LoCA)
//   dof_comp   : full-res seamless sub-pixel CoC transition composite (runs before 12-pass Jimenez HDR Bloom)
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> SrcA  : register(t0);
Texture2D<float>  Depth : register(t1);
Texture2D<float4> SrcB  : register(t2);
SamplerState      Lin   : register(s0);

// 13-point center-weighted portrait/subject autofocus metering zone (±1.2% inner + ±3.2% outer ring):
// Takes the near-biased 30th-percentile depth so NPC faces & foreground subjects hold lock smoothly even when
// the crosshair grazes their shoulder or hair (prevents Orgnar blurring out in 11.16.46.11.mp4!).
float sampleFocusZoneZ() {
    const int2 c = int2(dims.xy * 0.5f);
    const int2 dMax = int2(dims.xy) - 1;
    const int sx1 = max(int(dims.x * 0.012f), 6);
    const int sy1 = max(int(dims.y * 0.012f), 5);
    const int sx2 = max(int(dims.x * 0.032f), 16);
    const int sy2 = max(int(dims.y * 0.032f), 12);
    const int2 o[13] = {
        int2(0, 0),
        int2(sx1, 0), int2(-sx1, 0), int2(0, sy1), int2(0, -sy1),
        int2(sx1, sy1), int2(-sx1, sy1), int2(sx1, -sy1), int2(-sx1, -sy1),
        int2(sx2, 0), int2(-sx2, 0), int2(0, sy2), int2(0, -sy2)
    };
    float zVals[13];
    [unroll] for (int k = 0; k < 13; k++) {
        const float dk = Depth.Load(int3(clamp(c + o[k], int2(0, 0), dMax), 0));
        const float zk = dk < 1.0f ? viewZOf(dk) : 12000.0f;
        zVals[k] = zk < 42.0f ? 12000.0f : zk;
    }
    [unroll] for (int i = 1; i < 13; i++) {
        const float key = zVals[i];
        int j = i - 1;
        [unroll] for (int s = 0; s < 12; s++) {
            if (j >= 0 && zVals[j] > key) { zVals[j + 1] = zVals[j]; j--; }
        }
        zVals[j + 1] = key;
    }
    // Smooth blend between 30th-percentile (subject lock) and median (zVals[6]) prevents instant background hijack:
    return lerp(zVals[3], zVals[6], 0.35f);
}

// Exact Physical 35mm Full-Frame Thin-Lens CoC Equation (70 Skyrim units = 1 meter, 50mm cinema prime):
//   CoC_sensor = ((d - f) / d) * (F^2 / (N * (f - F)))
// Returns signed normalized CoC in [-1..+1] (negative = near-field foreground, positive = far-field background),
// smoothly stepping through every intermediate depth plane with zero hard clipping and natural hyperfocal infinity!
float signedCoC(float curZ, float focusZ, float maxDist) {
    if (maxDist <= 1.0f) { return 0.0f; }
    const float dM = max(curZ, 24.0f) / 70.0f;
    const float fM = clamp(focusZ, 42.0f, 14000.0f) / 70.0f;
    // 50mm lens (F = 0.050m); aperture f-number N smoothly controlled by dofMaxDist (420u -> f/2.0, 200u -> f/4.0, 800u -> f/1.2)
    const float F = 0.050f;
    const float N = clamp(840.0f / max(maxDist, 120.0f), 1.1f, 8.0f);
    // Physical CoC diameter on 35mm sensor (36mm wide), converted to full-res pixels:
    const float cocSensorM = ((dM - fM) / max(dM, 0.25f)) * ((F * F) / (N * max(fM - F, 0.20f)));
    const float cocPx = (cocSensorM / 0.036f) * dims.x;
    const float maxRadFull = max(clamp(dofParams.y, 1.5f, 24.0f) * 2.0f, 4.0f);
    // Sub-pixel dead-zone (< 0.45 px) keeps the exact focal plane razor-sharp with zero softness:
    const float absPx = abs(cocPx);
    if (absPx <= 0.45f) { return 0.0f; }
    const float norm = saturate((absPx - 0.45f) / maxRadFull);
    return cocPx < 0.0f ? -norm : norm;
}

float4 dof_coc(float4 at : SV_Position) : SV_Target0 {
    // Live 3D World-Map & UI Depth Isolation: exact UI::Singleton menuPaused (timeEnv.w) + aerial world-map altitude.
    if (timeEnv.w > 0.5f || camPosW.z > 18000.0f) {
        const float2 fullPx0 = (floor(at.xy) + 0.5f) * 2.0f;
        return float4(SrcA.SampleLevel(Lin, fullPx0 * dims.zw, 0.0f).rgb, 0.0f);
    }
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 dMax = int2(dims.xy) - 1;
    const int2 ip = clamp(int2(fullPx), int2(0, 0), dMax);
    const float focusZ = sampleFocusZoneZ();
    // 2x2 conservative depth downsample: keeps nearest foreground CoC intact for near-field feathering
    // while using max-depth for background to prevent foreground edges from leaking into far bokeh!
    const float d00 = Depth.Load(int3(ip, 0));
    const float d10 = Depth.Load(int3(min(ip + int2(1, 0), dMax), 0));
    const float d01 = Depth.Load(int3(min(ip + int2(0, 1), dMax), 0));
    const float d11 = Depth.Load(int3(min(ip + int2(1, 1), dMax), 0));
    const float dMin = min(min(d00, d10), min(d01, d11));
    const float dMaxV = max(max(d00, d10), max(d01, d11));
    const float zNear = dMin < 1.0f ? viewZOf(dMin) : 100000.0f;
    const float zFar  = dMaxV < 1.0f ? viewZOf(dMaxV) : 100000.0f;
    const float cocNear = zNear < 45.0f ? 0.0f : signedCoC(zNear, focusZ, dofParams.x);
    const float cocFar  = zFar < 45.0f ? 0.0f : signedCoC(zFar, focusZ, dofParams.x);
    const float coc = (cocNear < -0.02f) ? cocNear : cocFar;
    const float3 col = SrcA.SampleLevel(Lin, fullPx * dims.zw, 0.0f).rgb;
    return float4(col, coc);
}

float4 dof_gather(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 dim = max(float2(w, h), float2(1.0f, 1.0f));
    const float2 invDim = 1.0f / dim;
    const float2 px = floor(at.xy);
    const float4 center = SrcA.Load(int3(px, 0));
    if (WaveActiveAllTrue(dofParams.x <= 1.0f)) { return float4(center.rgb, 0.0f); }

    // 1/2-res cinema aperture bokeh radius (1.40x optical expansion breaks up the flat small-radius Gaussian look!):
    const float maxRad = clamp(dofParams.y, 2.0f, 24.0f) * 1.40f;
    const float centerCoc = center.a;
    const float centerAbs = abs(centerCoc);
    const float2 uvNorm = (px + 0.5f) * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.42f);
    const float2 radAxis = length(uvNorm) > 1.0e-4f ? normalize(uvNorm) : float2(1.0f, 0.0f);
    const float kBlade = 6.2831853f / 7.0f;   // 7-blade classic cinema iris (Silent Horizons / Cooke signature)

    // Adaptive gather radius for far-field: packs all 5 concentric rings tightly inside centerCoc * maxRad
    // so intermediate depth planes have 100% continuous, zero-stipple, zero-noise optical bokeh!
    const float farGatherRad = max(centerCoc > 0.0f ? centerCoc * maxRad : 0.0f, 0.0f);
    const float nearGatherRad = maxRad;

    float3 farAcc = centerCoc > 0.005f ? center.rgb : 0.0f.xxx;
    float  farW   = centerCoc > 0.005f ? 1.0f : 0.0f;
    float3 nearAcc = centerCoc < -0.005f ? center.rgb : 0.0f.xxx;
    float  nearW   = centerCoc < -0.005f ? 1.0f : 0.0f;
    float  nearCov = centerCoc < -0.005f ? 1.0f : 0.0f;

    // 91-tap 5-Ring Concentric Aperture Disc Gather (6 + 12 + 18 + 24 + 30 = 90 ring taps + 1 center = 91 taps):
    // Zero per-pixel random noise -> completely eliminates stipple, shimmer, and Vogel swirl arcs!
    const float kInvRings = 1.0f / 5.0f;
    [loop] for (int ring = 1; ring <= 5; ring++) {
        const float rFrac = float(ring) * kInvRings;
        const int nTaps = ring * 6;
        const float dAng = 6.2831853f / float(nTaps);
        const float angOff = float(ring) * 0.5235988f;   // 30-deg alternating ring phase for perfect hexagonal packing
        [loop] for (int t = 0; t < nTaps; t++) {
            const float a = float(t) * dAng + angOff;
            const float sec = abs(fmod(a, kBlade) - kBlade * 0.5f);
            const float bladeR = clamp(0.9009689f / max(cos(sec), 0.76f), 0.92f, 1.08f);
            float2 unitOff = float2(cos(a), sin(a)) * (rFrac * bladeR);
            // Subtle mechanical cat's-eye corner vignette squeeze on outer bokeh discs:
            unitOff -= radAxis * (dot(unitOff, radAxis) * cornerMag * 0.28f);

            // 1) Far-field gather (dense 90-tap packing scaled to exact farGatherRad -> silky smooth step-down!):
            if (farGatherRad > 0.35f) {
                const float4 tapF = SrcA.SampleLevel(Lin, saturate((px + 0.5f + unitOff * farGatherRad) * invDim), 0.0f);
                if (tapF.a > 0.005f) {
                    // Exact disc-overlap & foreground-occlusion test (prevents sharp foreground leaking into far bokeh):
                    const float tapCocF = tapF.a;
                    const float bgOcc = smoothstep(centerCoc * 0.45f - 0.02f, centerCoc * 0.85f + 0.02f, tapCocF);
                    const float discW = smoothstep(rFrac * centerCoc - 0.06f, rFrac * centerCoc + 0.06f, tapCocF);
                    const float lumF = dot(tapF.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                    // Zeiss/Cooke cinema-prime spherical-aberration bokeh ring profile (0.58 + 0.85*r^3) +
                    // strong quadratic HDR bokeh orb expansion (breaks up flat out-of-focus mush into luminous cinema discs!):
                    const float saRingF = 0.58f + 0.85f * (rFrac * rFrac * rFrac);
                    const float orbF = (1.0f + pow(saturate(lumF - 0.30f), 2.0f) * (dofParams.z * 3.8f)) * saRingF;
                    const float3 locaFar = lerp(1.0f.xxx, float3(0.95f, 1.04f, 1.02f), rFrac * rFrac * 0.42f);
                    const float fw = bgOcc * discW * orbF;
                    farAcc += tapF.rgb * locaFar * fw;
                    farW += fw;
                }
            }

            // 2) Near-field gather (probes full nearGatherRad so close out-of-focus foreground feathers over sharp bg!):
            const float4 tapN = SrcA.SampleLevel(Lin, saturate((px + 0.5f + unitOff * nearGatherRad) * invDim), 0.0f);
            if (tapN.a < -0.015f) {
                const float nRad = -tapN.a;
                const float covN = smoothstep(rFrac - 0.16f, rFrac + 0.10f, nRad);
                if (covN > 0.001f) {
                    const float lumN = dot(tapN.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                    const float saRingN = 0.58f + 0.85f * (rFrac * rFrac * rFrac);
                    const float orbN = (1.0f + pow(saturate(lumN - 0.30f), 2.0f) * (dofParams.z * 3.2f)) * saRingN;
                    const float3 locaNear = lerp(1.0f.xxx, float3(1.05f, 0.95f, 1.04f), rFrac * rFrac * 0.42f);
                    const float nw = covN * orbN;
                    nearAcc += tapN.rgb * locaNear * nw;
                    nearW += nw;
                    nearCov += covN;
                }
            }
        }
    }

    const float3 farCol = farW > 1.0e-4f ? (farAcc / farW) : center.rgb;
    const float3 nearCol = nearW > 1.0e-4f ? (nearAcc / nearW) : center.rgb;
    const float nearAlpha = saturate(nearCov / 26.0f);
    const float3 outCol = lerp(farCol, nearCol, nearAlpha);
    return float4(outCol, nearAlpha > 0.02f ? -nearAlpha : saturate(centerCoc));
}

float4 dof_comp(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float4 sharp = SrcA.Load(int3(ip, 0));
    if (dofParams.x <= 1.0f) { return sharp; }
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float focusZ = sampleFocusZoneZ();
    const float d = Depth.Load(int3(ip, 0));
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    // Keep 1st-person hands/weapons/torches (< 45u) razor-sharp:
    if (curZ < 45.0f) { return sharp; }
    const float coc = signedCoC(curZ, focusZ, dofParams.x);
    const float absCoc = abs(coc);
    // Smooth 4-tap bilinear tent upsample of 1/2-res 91-tap concentric bokeh buffer:
    const float2 bOff = dims.zw * 0.75f;
    const float4 bokeh = (SrcB.SampleLevel(Lin, uv + float2( bOff.x,  bOff.y), 0.0f)
                        + SrcB.SampleLevel(Lin, uv + float2(-bOff.x,  bOff.y), 0.0f)
                        + SrcB.SampleLevel(Lin, uv + float2( bOff.x, -bOff.y), 0.0f)
                        + SrcB.SampleLevel(Lin, uv + float2(-bOff.x, -bOff.y), 0.0f)) * 0.25f;
    // Seamless physical transition from 0 px (razor sharp) -> 2 px (100% bokeh) with near-field silhouette feathering:
    const float maxRadFull = max(clamp(dofParams.y, 1.5f, 24.0f) * 2.0f, 4.0f);
    const float cocPx = absCoc * maxRadFull;
    const float farBlend = coc > 0.0f ? smoothstep(0.0f, 2.2f, cocPx) : 0.0f;
    const float nearSelf = coc < 0.0f ? smoothstep(0.0f, 2.2f, cocPx) : 0.0f;
    const float nearBleed = bokeh.a < 0.0f ? smoothstep(0.02f, 0.85f, -bokeh.a) : 0.0f;
    const float blend = max(farBlend, max(nearSelf, nearBleed));
    return float4(lerp(sharp.rgb, bokeh.rgb, blend), sharp.a);
}
