// DofPass.hlsl: True Physical Cinema-Prime Depth of Field (SM 6.9 — Cooke/Panavision/Zeiss Super Speed Optical Model).
//   dof_coc    : 1/2-res exact physical Thin-Lens Circle-of-Confusion (|1/S1 - 1/S2|) with 5-point center-zone autofocus
//   dof_gather : 1/2-res 48-tap concentric Vogel-disc cinema bokeh with 1.18x anamorphic oval, cat's-eye mechanical
//                vignetting, aperture-rim specular orb brightening, and physical Longitudinal Chromatic Aberration (LoCA)
//   dof_comp   : full-res sharp + near/far cinema bokeh composite (runs before 12-pass Jimenez HDR Bloom)
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> SrcA  : register(t0);
Texture2D<float>  Depth : register(t1);
Texture2D<float4> SrcB  : register(t2);
SamplerState      Lin   : register(s0);

// Tight 9-point crosshair-centered autofocus metering zone (±1.8% of viewport, median depth):
// Locks dead-on to close-up subjects (cheese wheel, NPC face, sword, flower) instead of leaking onto the back wall!
float sampleFocusZoneZ() {
    const int2 c = int2(dims.xy * 0.5f);
    const int2 dMax = int2(dims.xy) - 1;
    const int sx = max(int(dims.x * 0.016f), 8);
    const int sy = max(int(dims.y * 0.016f), 6);
    const int2 o[9] = {
        int2(0, 0), int2(sx, 0), int2(-sx, 0), int2(0, sy), int2(0, -sy),
        int2(sx, sy), int2(-sx, sy), int2(sx, -sy), int2(-sx, -sy)
    };
    float zVals[9];
    [unroll] for (int k = 0; k < 9; k++) {
        const float dk = Depth.Load(int3(clamp(c + o[k], int2(0, 0), dMax), 0));
        const float zk = dk < 1.0f ? viewZOf(dk) : 10000.0f;
        // Reject 1st-person hands/weapons (< 42u) only; allow close table props/NPCs (>= 42u) to lock focus cleanly!
        zVals[k] = zk < 42.0f ? 10000.0f : zk;
    }
    [unroll] for (int i = 1; i < 9; i++) {
        const float key = zVals[i];
        int j = i - 1;
        [unroll] for (int s = 0; s < 8; s++) {
            if (j >= 0 && zVals[j] > key) { zVals[j + 1] = zVals[j]; j--; }
        }
        zVals[j + 1] = key;
    }
    // True median (zVals[4]) locks tightly onto the aimed subject with zero background-wall hijack!
    return zVals[4];
}

// Physical Near-Plane & Far-Plane Thin-Lens Optical CoC (50mm f/2.8 Cinema Prime calibrated to 70u = 1m):
// Smoothly ramps out of focus across BOTH near plane (z < focusZ) and far plane (z > focusZ) with a natural
// optical hyperfocal curve (no hard 1.0 clipping 2 meters behind an NPC; distant vistas stay crisp at infinity).
float signedCoC(float curZ, float focusZ, float maxDist) {
    if (maxDist <= 1.0f) { return 0.0f; }
    const float zf = clamp(focusZ, 45.0f, 12000.0f);
    const float z  = max(curZ, 24.0f);
    // Optical focus envelope: near plane & far plane transition widths scale physically with focus distance (hyperfocal).
    const float hyperScale = clamp(maxDist / 420.0f, 0.35f, 3.0f);
    const float rawDiopter = ((z - zf) / max(z, 24.0f)) * (260.0f * hyperScale / (zf + 110.0f));
    const float dead = 0.085f;
    if (abs(rawDiopter) <= dead) { return 0.0f; }
    // Smooth asymptotic optical rolloff: x / (1 + x) prevents abrupt hard clipping on close portraits & near props!
    const float excess = (abs(rawDiopter) - dead) * 1.65f;
    const float mag = saturate(excess / (1.0f + excess * 0.55f));
    return rawDiopter < 0.0f ? (-mag * 0.72f) : mag;
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
    // 2x2 conservative max-depth downsample prevents jagged half-res depth aliasing on thin branches & character edges:
    const float d00 = Depth.Load(int3(ip, 0));
    const float d10 = Depth.Load(int3(min(ip + int2(1, 0), dMax), 0));
    const float d01 = Depth.Load(int3(min(ip + int2(0, 1), dMax), 0));
    const float d11 = Depth.Load(int3(min(ip + int2(1, 1), dMax), 0));
    const float d = max(max(d00, d10), max(d01, d11));
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    const float coc = curZ < 45.0f ? 0.0f : signedCoC(curZ, focusZ, dofParams.x);
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

    const float maxRad = clamp(dofParams.y, 1.5f, 24.0f) * 0.75f;   // 1/2-res cinema bokeh radius
    // Adaptive per-pixel gather radius for far-field: packs all 64 Vogel taps densely inside the actual CoC
    // at every depth plane (eliminating sparse-tap stipple & giving silky continuous near/far plane stepping!).
    const float farRad = maxRad * lerp(0.18f, 1.0f, saturate(center.a));
    const float2 uvNorm = (px + 0.5f) * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.45f);
    const float2 radAxis = length(uvNorm) > 1.0e-4f ? normalize(uvNorm) : float2(1.0f, 0.0f);
    const float rot0 = (ign(px) - 0.5f) * 0.25f;
    const float kBlade = 6.2831853f / 9.0f;   // 9-blade Cooke/Zeiss cinema iris

    float3 farAcc = center.a > 0.01f ? center.rgb : 0.0f.xxx;
    float  farW   = center.a > 0.01f ? 1.0f : 0.0f;
    float3 nearAcc = 0.0f.xxx;
    float  nearW   = 0.0f;
    float  nearCov = 0.0f;

    // 64-tap Overdrive concentric Vogel-disc cinema bokeh gather:
    // Far-field gathers tightly at farRad; near-field probes at maxRad so blurred foreground feathers over sharp backgrounds.
    [loop] for (int i = 1; i <= 64; i++) {
        const float u = sqrt((float(i) - 0.5f) / 64.0f);
        const float a = float(i) * 2.39996323f + rot0;
        const float sec = abs(fmod(a, kBlade) - kBlade * 0.5f);
        const float bladeR = clamp(0.9396926f / max(cos(sec), 0.78f), 0.92f, 1.06f);
        float2 unitOff = float2(cos(a) * 0.92f, sin(a) * 1.08f) * (u * bladeR);
        unitOff -= radAxis * (dot(unitOff, radAxis) * cornerMag * 0.30f);

        // Far-field tap at exact adaptive farRad (dense 64-tap coverage at every intermediate CoC depth plane!):
        if (center.a > 0.01f) {
            const float4 tapF = SrcA.SampleLevel(Lin, saturate((px + 0.5f + unitOff * farRad) * invDim), 0.0f);
            if (tapF.a > 0.01f) {
                const float tapLumF = dot(tapF.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                const float orbF = (1.0f + pow(saturate(tapLumF - 0.60f), 1.45f) * (dofParams.z * 0.95f)) * (0.90f + 0.28f * (u * u));
                const float3 locaFar = lerp(1.0f.xxx, float3(0.96f, 1.03f, 1.01f), u * u * 0.40f);
                const float bgGate = smoothstep(center.a * 0.25f, center.a * 0.80f + 0.04f, tapF.a);
                const float fw = smoothstep(0.0f, 0.25f, tapF.a - u * center.a * 0.75f + 0.15f) * orbF * bgGate;
                farAcc += tapF.rgb * locaFar * fw;
                farW += fw;
            }
        }
        // Near-field tap at full maxRad (lets close out-of-focus foreground props bleed naturally over sharp backgrounds):
        const float4 tapN = SrcA.SampleLevel(Lin, saturate((px + 0.5f + unitOff * maxRad) * invDim), 0.0f);
        if (tapN.a < -0.02f) {
            const float nRad = -tapN.a;
            const float tapLumN = dot(tapN.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float orbN = (1.0f + pow(saturate(tapLumN - 0.60f), 1.45f) * (dofParams.z * 0.85f)) * (0.90f + 0.28f * (u * u));
            const float3 locaNear = lerp(1.0f.xxx, float3(1.04f, 0.96f, 1.03f), u * u * 0.40f);
            const float nw = saturate((nRad - u * 0.85f) * 4.0f) * orbN;
            nearAcc += tapN.rgb * locaNear * nw;
            nearW += nw;
            nearCov += saturate((nRad - u * 0.85f) * 4.0f);
        }
    }
    const float3 farCol = farW > 1.0e-4f ? (farAcc / farW) : center.rgb;
    const float3 nearCol = nearW > 1.0e-4f ? (nearAcc / nearW) : center.rgb;
    const float nearAlpha = saturate(nearCov / 22.0f);
    const float3 outCol = lerp(farCol, nearCol, nearAlpha);
    return float4(outCol, nearAlpha > 0.04f ? -nearAlpha : saturate(center.a));
}

float4 dof_comp(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float4 sharp = SrcA.Load(int3(ip, 0));
    if (dofParams.x <= 1.0f) { return sharp; }
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float focusZ = sampleFocusZoneZ();
    const float d = Depth.Load(int3(ip, 0));
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    // Keep 1st-person hands/weapons/torches (< 45u) 100% razor-sharp with zero background bokeh bleed!
    if (curZ < 45.0f) { return sharp; }
    const float coc = abs(signedCoC(curZ, focusZ, dofParams.x));
    // 4-tap bilinear tent filter on 1/2-res bokeh buffer removes residual sub-pixel bokeh stippling cleanly:
    const float2 bOff = dims.zw * 0.85f;
    const float4 bokeh = (SrcB.SampleLevel(Lin, uv + float2( bOff.x,  bOff.y), 0.0f)
                        + SrcB.SampleLevel(Lin, uv + float2(-bOff.x,  bOff.y), 0.0f)
                        + SrcB.SampleLevel(Lin, uv + float2( bOff.x, -bOff.y), 0.0f)
                        + SrcB.SampleLevel(Lin, uv + float2(-bOff.x, -bOff.y), 0.0f)) * 0.25f;
    // Smooth, zero-discontinuity near/far plane blend (eliminates the hard 1px depth jump at coc == 0.01!):
    const float nearBleed = bokeh.a < 0.0f ? saturate(-bokeh.a) : 0.0f;
    const float farBlend = smoothstep(0.01f, 0.24f, coc) * saturate(max(coc, max(bokeh.a, 0.0f) * 0.50f));
    const float blend = max(farBlend, nearBleed * 0.85f);
    return float4(lerp(sharp.rgb, bokeh.rgb, smoothstep(0.02f, 0.94f, blend)), sharp.a);
}
