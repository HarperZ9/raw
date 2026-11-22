// DofPass.hlsl: True Multi-Pass Near/Far Physical Depth of Field (SM 6.9, Jimenez / Gilcher Cinema Bokeh).
//   dof_coc    : 1/2-res signed Circle-of-Confusion prefilter (CoC < 0 near-field, CoC > 0 far-field)
//   dof_gather : 1/2-res 32-tap Heptagonal-Aperture Cat's-Eye Bokeh gather separating Near-Field bleed over
//                sharp backgrounds from anti-bleed Far-Field bokeh + luminous HDR specular orb expansion
//   dof_comp   : full-res sharp + near/far bokeh composite (runs before 6-level Jimenez HDR Bloom)
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> SrcA  : register(t0);
Texture2D<float>  Depth : register(t1);
Texture2D<float4> SrcB  : register(t2);
SamplerState      Lin   : register(s0);

float signedCoC(float curZ, float focusZ, float maxDist) {
    if (maxDist <= 1.0f || focusZ > maxDist) { return 0.0f; }
    const float engage = saturate((maxDist - focusZ) / max(maxDist * 0.45f, 120.0f));
    if (curZ < focusZ * 0.78f) {
        // Near-field (negative CoC): foreground object closer than focal plane.
        return -saturate((focusZ * 0.78f - curZ) / max(focusZ * 0.55f, 35.0f)) * engage;
    }
    if (curZ > focusZ * 1.32f) {
        // Far-field (positive CoC): background behind focal plane.
        return saturate((curZ - focusZ * 1.32f) / max(focusZ * 4.0f, 320.0f)) * engage;
    }
    return 0.0f;
}

float4 dof_coc(float4 at : SV_Position) : SV_Target0 {
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 dMax = int2(dims.xy) - 1;
    const int2 ip = clamp(int2(fullPx), int2(0, 0), dMax);
    const float focusD = Depth.Load(int3(int2(dims.xy * 0.5f), 0));
    const float focusZ = focusD < 1.0f ? viewZOf(focusD) : 10000.0f;
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

    const float maxRad = clamp(dofParams.y, 1.5f, 24.0f) * 0.5f;   // half-res radius in pixels
    const float2 uvNorm = (px + 0.5f) * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.45f);
    const float2 radAxis = length(uvNorm) > 1.0e-4f ? normalize(uvNorm) : float2(1.0f, 0.0f);
    const float rot0 = ign(px) * 6.2831853f;
    const float kBlade = 6.2831853f / 7.0f;

    float3 farAcc = center.a > 0.01f ? center.rgb : 0.0f.xxx;
    float  farW   = center.a > 0.01f ? 1.0f : 0.0f;
    float3 nearAcc = 0.0f.xxx;
    float  nearW   = 0.0f;
    float  nearCov = 0.0f;

    [loop] for (int i = 1; i <= 32; i++) {
        const float u = sqrt(float(i) / 32.0f);
        const float a = float(i) * 2.39996323f + rot0;
        const float sec = abs(fmod(a, kBlade) - kBlade * 0.5f);
        const float bladeR = clamp(0.90096887f / max(cos(sec), 0.72f), 0.85f, 1.12f);
        float2 off = float2(cos(a), sin(a)) * (u * maxRad * bladeR);
        off -= radAxis * (dot(off, radAxis) * cornerMag * 0.32f);
        const float4 tap = SrcA.SampleLevel(Lin, saturate((px + 0.5f + off) * invDim), 0.0f);
        const float tapLum = dot(tap.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float orb = 1.0f + saturate(tapLum - 0.65f) * dofParams.z;
        if (tap.a < -0.02f) {
            // Near-field tap bleeds softly OVER sharp backgrounds (true foreground silhouette bokeh).
            const float nRad = -tap.a;
            const float nw = saturate((nRad - u * 0.85f) * 4.0f) * orb;
            nearAcc += tap.rgb * nw;
            nearW += nw;
            nearCov += saturate((nRad - u * 0.85f) * 4.0f);
        } else if (tap.a > 0.02f && center.a > 0.01f) {
            // Far-field tap gathers cleanly without pulling in-focus foreground subjects.
            const float fw = saturate((tap.a - u * 0.80f) * 3.5f) * orb;
            farAcc += tap.rgb * fw;
            farW += fw;
        }
    }
    const float3 farCol = farW > 1.0e-4f ? (farAcc / farW) : center.rgb;
    const float3 nearCol = nearW > 1.0e-4f ? (nearAcc / nearW) : center.rgb;
    const float nearAlpha = saturate(nearCov / 10.0f);
    const float3 outRgb = lerp(farCol, nearCol, nearAlpha);
    return float4(outRgb, max(nearAlpha, saturate(center.a)));
}

float4 dof_comp(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    const float4 sharp = SrcA.Load(ip);
    if (dofParams.x <= 1.0f) { return sharp; }
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float4 bokeh = SrcB.SampleLevel(Lin, uv, 0.0f);
    const float focusD = Depth.Load(int3(int2(dims.xy * 0.5f), 0));
    const float focusZ = focusD < 1.0f ? viewZOf(focusD) : 10000.0f;
    const float d = Depth.Load(ip);
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    const float coc = abs(signedCoC(curZ, focusZ, dofParams.x));
    const float blend = saturate(max(coc, bokeh.a * 0.85f));
    return float4(lerp(sharp.rgb, bokeh.rgb, smoothstep(0.02f, 0.90f, blend)), sharp.a);
}
