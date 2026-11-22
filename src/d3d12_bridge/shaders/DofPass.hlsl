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

// Wide 9-point foreground-rejecting cinema metering zone (takes upper-quartile depth across the central 12% of the
// frame so panning across a pillar, twig, or doorway edge never snaps or flickers DoF to the crosshair pixel).
float sampleFocusZoneZ() {
    const int2 c = int2(dims.xy * 0.5f);
    const int2 dMax = int2(dims.xy) - 1;
    const int sx = max(int(dims.x * 0.06f), 24);
    const int sy = max(int(dims.y * 0.06f), 16);
    const int2 o[9] = {
        int2(0, 0), int2(sx, 0), int2(-sx, 0), int2(0, sy), int2(0, -sy),
        int2(sx, sy), int2(-sx, sy), int2(sx, -sy), int2(-sx, -sy)
    };
    float zVals[9];
    [unroll] for (int k = 0; k < 9; k++) {
        const float dk = Depth.Load(int3(clamp(c + o[k], int2(0, 0), dMax), 0));
        zVals[k] = dk < 1.0f ? viewZOf(dk) : 10000.0f;
    }
    // Partial insertion sort to select the 6th-deepest (upper-quartile) sample, rejecting thin foreground blockers.
    [unroll] for (int i = 1; i < 9; i++) {
        const float key = zVals[i];
        int j = i - 1;
        [unroll] for (int s = 0; s < 8; s++) {
            if (j >= 0 && zVals[j] > key) { zVals[j + 1] = zVals[j]; j--; }
        }
        zVals[j + 1] = key;
    }
    return (zVals[5] + zVals[6]) * 0.5f;
}

// Exact Physical Thin-Lens Optical Circle of Confusion: CoC proportional to (1/focusZ - 1/curZ).
// Generous cinema focal plane (dead=0.28, focalScale=0.82) guarantees the targeted subject (e.g. NPCs, workbenches,
// foreground riverbanks) stays 100% razor-sharp while distant backgrounds & extreme foreground melt into cinema bokeh.
float signedCoC(float curZ, float focusZ, float maxDist) {
    if (maxDist <= 1.0f || focusZ > maxDist) { return 0.0f; }
    const float engage = smoothstep(maxDist, maxDist * 0.45f, focusZ);
    const float s1 = max(focusZ, 42.0f);
    const float s2 = max(curZ, 16.0f);
    const float diopter = (1.0f / s1 - 1.0f / s2);
    const float rawCoc = diopter * (s1 * 0.82f);
    const float dead = 0.28f;
    if (abs(rawCoc) <= dead) { return 0.0f; }
    const float shaped = sign(rawCoc) * saturate((abs(rawCoc) - dead) / (1.0f - dead));
    return shaped * engage;
}

float4 dof_coc(float4 at : SV_Position) : SV_Target0 {
    // Live 3D World-Map & UI Depth Isolation: bypass DoF on the high-altitude aerial world map (camPosW.z > 18000u).
    if (camPosW.z > 18000.0f) {
        const float2 fullPx0 = (floor(at.xy) + 0.5f) * 2.0f;
        return float4(SrcA.SampleLevel(Lin, fullPx0 * dims.zw, 0.0f).rgb, 0.0f);
    }
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 dMax = int2(dims.xy) - 1;
    const int2 ip = clamp(int2(fullPx), int2(0, 0), dMax);
    const float focusZ = sampleFocusZoneZ();
    const float d = Depth.Load(int3(ip, 0));
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    const float coc = signedCoC(curZ, focusZ, dofParams.x);
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

    const float maxRad = clamp(dofParams.y, 1.5f, 26.0f) * 0.55f;   // half-res optical bokeh radius
    const float2 uvNorm = (px + 0.5f) * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.52f);
    const float2 radAxis = length(uvNorm) > 1.0e-4f ? normalize(uvNorm) : float2(1.0f, 0.0f);
    const float rot0 = ign(px) * 6.2831853f;
    const float kBlade = 6.2831853f / 9.0f;   // 9-blade Cooke/Zeiss cinema iris

    float3 farAcc = center.a > 0.01f ? center.rgb : 0.0f.xxx;
    float  farW   = center.a > 0.01f ? 1.0f : 0.0f;
    float3 nearAcc = 0.0f.xxx;
    float  nearW   = 0.0f;
    float  nearCov = 0.0f;

    // 48-tap concentric Vogel-disc cinema bokeh gather with subtle 1.18x anamorphic oval,
    // aperture-rim specular orb brightening, and physical Longitudinal Chromatic Aberration (LoCA).
    [loop] for (int i = 1; i <= 48; i++) {
        const float u = sqrt((float(i) - 0.5f) / 48.0f);
        const float a = float(i) * 2.39996323f + rot0;
        const float sec = abs(fmod(a, kBlade) - kBlade * 0.5f);
        const float bladeR = clamp(0.9396926f / max(cos(sec), 0.78f), 0.90f, 1.08f);
        // Subtle 1.18x vertical anamorphic cinema-lens bokeh oval + mechanical cat's-eye corner squeeze.
        float2 off = float2(cos(a) * 0.88f, sin(a) * 1.14f) * (u * maxRad * bladeR);
        off -= radAxis * (dot(off, radAxis) * cornerMag * 0.38f);
        const float4 tap = SrcA.SampleLevel(Lin, saturate((px + 0.5f + off) * invDim), 0.0f);
        const float tapLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        // Cinema glass spherical-aberration rim brightening on HDR highlights (creamy luminous bokeh discs).
        const float rimRing = 0.86f + 0.42f * (u * u);
        const float orb = (1.0f + pow(saturate(tapLum - 0.55f), 1.35f) * (dofParams.z * 1.35f)) * rimRing;
        // Physical Longitudinal Chromatic Aberration (LoCA): warm magenta near rim, cool emerald-cyan far rim.
        const float3 locaNear = lerp(1.0f.xxx, float3(1.06f, 0.94f, 1.04f), u * u * 0.55f);
        const float3 locaFar  = lerp(1.0f.xxx, float3(0.94f, 1.05f, 1.02f), u * u * 0.55f);
        if (tap.a < -0.02f) {
            const float nRad = -tap.a;
            const float nw = saturate((nRad - u * 0.82f) * 4.5f) * orb;
            nearAcc += tap.rgb * locaNear * nw;
            nearW += nw;
            nearCov += saturate((nRad - u * 0.82f) * 4.5f);
        } else if (tap.a > 0.02f && center.a > 0.01f) {
            const float fw = saturate((tap.a - u * 0.78f) * 4.0f) * orb;
            farAcc += tap.rgb * locaFar * fw;
            farW += fw;
        }
    }
    const float3 farCol = farW > 1.0e-4f ? (farAcc / farW) : center.rgb;
    const float3 nearCol = nearW > 1.0e-4f ? (nearAcc / nearW) : center.rgb;
    const float nearAlpha = saturate(nearCov / 14.0f);
    const float3 outCol = lerp(farCol, nearCol, nearAlpha);
    return float4(outCol, max(nearAlpha, saturate(center.a)));
}

float4 dof_comp(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float4 sharp = SrcA.Load(int3(ip, 0));
    if (dofParams.x <= 1.0f) { return sharp; }
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float focusZ = sampleFocusZoneZ();
    const float d = Depth.Load(int3(ip, 0));
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    const float coc = abs(signedCoC(curZ, focusZ, dofParams.x));
    const float4 bokeh = SrcB.SampleLevel(Lin, uv, 0.0f);
    // Protect in-focus subjects (coc == 0) from background bokeh bleed; only blend where pixel is genuinely out of focus.
    const float blend = coc > 0.01f ? saturate(max(coc, bokeh.a * 0.75f)) : 0.0f;
    return float4(lerp(sharp.rgb, bokeh.rgb, smoothstep(0.04f, 0.92f, blend)), sharp.a);
}
