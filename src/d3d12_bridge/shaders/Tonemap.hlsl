// Tonemap.hlsl: RAW's tone map, in place of the game's (src/d3d11_proxy/Tonemap.cpp). It takes the game's tone map's
// bindings as they are: t1 the HDR scene, t0 the bloom, t2 the eye-adapted luminance (x the adapted average, y the
// game's target), pixel buffer 2 the imagespace values (bloom limit, white, curve choice, saturation, brightness,
// contrast, tint) and per-frame buffer 12 (the output gamma, the dynamic resolution scale). RAW's own values come in
// buffer 5. The build embeds this file (TonemapPs.h); with [HotReload] on, a copy in ShaderDir replaces it live.
// Compiled as ps_5_0 by d3dcompiler_47. Original work, MIT.
cbuffer PerFrame : register(b12) { float4 F[45]; };
cbuffer Image : register(b2) { float4 P[5]; };
cbuffer Raw : register(b5) { float4 RA; float4 RB; float4 RC; float4 RX; };   // RA: mode, bias (stops), min lum, max lum;
// RB: curve, key, white, view; RC: adapt, reference average, ToneX0, ToneX1; RX: ToneX2..5
Texture2D<float4> Bloom : register(t0);
Texture2D<float4> Scene : register(t1);
Texture2D<float4> Lum   : register(t2);
SamplerState S0 : register(s0);
SamplerState S1 : register(s1);
SamplerState S2 : register(s2);
struct In { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
static const float3 W = float3(0.2125f, 0.7154f, 0.0721f);

float3 filmic(float3 x) { return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)); }

// Curve 2: an extended Reinhard per channel, x (1 + x / W^2) / (1 + x), white at W (ToneX1, default 4). Its slope at
// black is 1, as the game's own curve's is, so shadows keep their level; the fitted ACES curve's slope there is about
// 0.2, and it crushed a third of a Riverwood dawn frame to black (rt8 matrix: 47 % of pixels at 4 or less against the
// game's 6 %). ToneX2 lifts the shoulder's saturation back towards the input's hue (0 none, 1 full).
float3 reinhardW(float3 x) {
    const float w = RC.w > 0.0f ? RC.w : 4.0f;
    const float3 m = x * (1.0f + x / (w * w)) / (1.0f + x);
    const float lx = dot(x, W), lm = dot(m, W);
    const float3 hue = x * (lm / max(lx, 1.0e-5f));   // the luminance's curve, the input's chroma
    return saturate(lerp(m, hue, saturate(RX.x)));
}

float gameCurve(float l) {   // the game's own: a Hejl-style curve or a Reinhard with a white point
    if (P[2].z > 0.5f) {
        const float x = max(l - 0.004f, 0.0f);
        return pow((x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f), 2.2f) * P[2].y;
    }
    return l * (1.0f + l * P[2].y) / (1.0f + l);
}

// 9-tap FXAA 3.11 sub-pixel edge anti-aliasing followed by gentle local-clamped CAS micro-contrast.
// Eliminates stair-stepped aliasing on thin grass blades, pine needles, and armor edges without vaseline blur.
float3 fxaaAndCas(float2 suv) {
    float w, h;
    Scene.GetDimensions(w, h);
    const float2 px = 1.0f / max(float2(w, h), float2(1.0f, 1.0f));
    const float3 c  = Scene.Sample(S1, suv).rgb;
    const float3 n  = Scene.Sample(S1, suv + float2( 0.0f, -px.y)).rgb;
    const float3 s  = Scene.Sample(S1, suv + float2( 0.0f,  px.y)).rgb;
    const float3 e  = Scene.Sample(S1, suv + float2( px.x,  0.0f)).rgb;
    const float3 ww = Scene.Sample(S1, suv + float2(-px.x,  0.0f)).rgb;
    const float3 nw = Scene.Sample(S1, suv + float2(-px.x, -px.y)).rgb;
    const float3 ne = Scene.Sample(S1, suv + float2( px.x, -px.y)).rgb;
    const float3 sw = Scene.Sample(S1, suv + float2(-px.x,  px.y)).rgb;
    const float3 se = Scene.Sample(S1, suv + float2( px.x,  px.y)).rgb;

    // Perceptual luma in Reinhard-compressed space so HDR highlights don't dominate edge detection.
    const float lc  = dot(c  / (1.0f + c ), W);
    const float ln  = dot(n  / (1.0f + n ), W);
    const float ls  = dot(s  / (1.0f + s ), W);
    const float le  = dot(e  / (1.0f + e ), W);
    const float lw  = dot(ww / (1.0f + ww), W);
    const float lnw = dot(nw / (1.0f + nw), W);
    const float lne = dot(ne / (1.0f + ne), W);
    const float lsw = dot(sw / (1.0f + sw), W);
    const float lse = dot(se / (1.0f + se), W);

    const float lMin = min(lc, min(min(ln, ls), min(le, lw)));
    const float lMax = max(lc, max(max(ln, ls), max(le, lw)));
    const float lRange = lMax - lMin;

    float3 aa = c;
    if (lRange >= max(0.035f, lMax * 0.12f)) {
        const float edgeH = abs(lnw + lne - 2.0f * ln) + 2.0f * abs(lw + le - 2.0f * lc) + abs(lsw + lse - 2.0f * ls);
        const float edgeV = abs(lnw + lsw - 2.0f * lw) + 2.0f * abs(ln + ls - 2.0f * lc) + abs(lne + lse - 2.0f * le);
        const bool isHoriz = edgeH >= edgeV;
        const float l1 = isHoriz ? ln : lw;
        const float l2 = isHoriz ? ls : le;
        const float g1 = abs(l1 - lc), g2 = abs(l2 - lc);
        const float stepDir = (g1 >= g2 ? -1.0f : 1.0f) * (isHoriz ? px.y : px.x);
        const float subAvg = (2.0f * (ln + ls + le + lw) + lnw + lne + lsw + lse) / 12.0f;
        const float subAmt = smoothstep(0.0f, 1.0f, saturate(abs(subAvg - lc) / max(lRange, 1.0e-4f)));
        const float blend = subAmt * subAmt * 0.65f;
        const float2 aaUv = isHoriz ? float2(suv.x, suv.y + stepDir * blend) : float2(suv.x + stepDir * blend, suv.y);
        aa = Scene.Sample(S1, aaUv).rgb;
    }

    // Gentle local-clamped CAS sharpening on the anti-aliased result.
    const float3 mn = min(aa, min(min(n, s), min(e, ww)));
    const float3 mx = max(aa, max(max(n, s), max(e, ww)));
    const float3 amp = saturate(min(mn, max(2.0f.xxx - mx, 0.0f.xxx)) / max(mx, 1.0e-4f.xxx));
    const float3 wCAS = sqrt(amp) * (-0.115f);
    const float3 outC = (aa + (n + s + e + ww) * wCAS) / max(1.0f.xxx + 4.0f * wCAS, 0.30f.xxx);
    return clamp(outC, mn * 0.90f, mx * 1.10f);
}

float4 main(In i) : SV_Target {
    const float2 suv = min(max(i.uv * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
    float3 hdr = fxaaAndCas(suv);
    // Logarithmic highlight roll-off on extreme HDR peaks (>1.35): tames Skyrim's post-opaque sun-glare billboard
    // into a crisp natural solar disk while leaving all scene midtones (<1.35) untouched.
    const float hdrPeak = max(hdr.r, max(hdr.g, hdr.b));
    if (hdrPeak > 1.35f) {
        const float comp = 1.35f + log2(1.0f + (hdrPeak - 1.35f) * 0.55f) * 0.42f;
        hdr *= comp / hdrPeak;
    }
    const float3 bloom = P[0].x > 0.5f ? Bloom.Sample(S0, suv).rgb : Bloom.Sample(S0, i.uv).rgb;
    const float2 lum = Lum.Sample(S2, i.uv).xy;
    const float avg = max(lum.x, 1.0e-5f);
    // Mode 1: a camera's exposure brings the metered average to the key. Adapt (RC.x) is the share of a change of
    // average the exposure follows: 1 is a camera's full compensation, below 1 a dark room stays darker than a bright
    // street, as the eye's partial adaptation leaves it. The reference average (RC.y) is where both agree.
    // The eye follows less of a change into the dark than into the light: below the reference the share is
    // ToneX0 (RC.z; 0 takes Adapt's). The metered average is the game's adapted one (Lum.x), which the game's own
    // luminance chain moves over time towards the frame's, so the exposure adapts as the eye does, at the game's rate.
    const float metered = clamp(avg, RA.z, RA.w);
    const float share = metered < RC.y && RC.z > 0.0f ? RC.z : RC.x;
    const float camera = RB.y / RC.y * pow(max(metered / RC.y, 1.0e-4f), -share);
    // RX.w: the interior factor the proxy sets per frame (ToneX5 in a cell with no sun cascades, else 1).
    const float exposure = (RA.x < 0.5f ? lum.y / avg : camera * (RX.w > 0.0f ? RX.w : 1.0f)) * exp2(RA.y);
    float3 mapped;
    float pivot = lum.x;
    if (RB.x < 0.5f) {
        const float l = max(dot(hdr, W), 1.0e-5f);
        mapped = hdr * (gameCurve(l * exposure) / l);
    } else if (RB.x > 2.5f) {
        // Curve 3: the game's own curve on luminance, l (1 + l / W^2) / (1 + l), with the white point raised to at
        // least ToneX1 (the game's clips about 5 % of a clear midday frame), then a contrast about the key (below). The hue stays the input's, as in the game.
        const float l = max(dot(hdr, W), 1.0e-5f) * exposure * RB.z;
        const float w2 = max(1.0f / max(P[2].y, 1.0e-4f), RC.w * RC.w);
        // Contrast in two halves about the key: above it the luminance is raised to ToneX3 (more contrast in the
        // light), below it to ToneX4 (below 1 lifts the shadows). One power for both crushed the darks: at 1.25 a
        // quarter of the inn went to 4 of 255 or less while midday only then reached the game's detail (rt8 panel 3).
        const float cu = RX.y > 0.0f ? RX.y : 1.0f, cl = RX.z > 0.0f ? RX.z : cu;
        // Below the key the power runs from ToneX4 at black to ToneX3 at the key, so the deep shadows lift while the
        // middle tones keep the contrast (a night frame lost a tenth of its detail with ToneX4 alone below the key).
        // Above the key the power eases back to 1 by four times the key, so bright snow and sky roll into the
        // shoulder instead of clipping (the vista at midday clipped 44 % of its pixels at a constant 1.55).
        const float cHi = lerp(cu, 1.0f, saturate(log2(max(l / RB.y, 1.0f)) * 0.5f));
        const float lc = RB.y * pow(l / RB.y, l > RB.y ? cHi : lerp(cl, cu, saturate(l / RB.y)));
        const float lOut = saturate(lc * (1.0f + lc / w2) / (1.0f + lc));
        float3 col = hdr * exposure * RB.z * (lOut / l);
        // AgX-style highlight shoulder desaturation: prevents neon orange/blue clipping in bright skies and fire.
        const float hiSat = saturate((lOut - 0.55f) / 0.45f);
        col = lerp(col, lOut.xxx, hiSat * hiSat * 0.42f);
        mapped = saturate(col);
        if (RA.x > 0.5f) { pivot = RB.y * (1.0f + RB.y / w2) / (1.0f + RB.y); }
    } else {
        const bool rh = RB.x > 1.5f;
        mapped = rh ? reinhardW(hdr * exposure * RB.z) : filmic(hdr * exposure * RB.z);
        if (RA.x > 0.5f) { pivot = dot(rh ? reinhardW((RB.y * exp2(RA.y) * RB.z).xxx) : filmic((RB.y * exp2(RA.y) * RB.z).xxx), W); }   // the grade turns about mid grey
    }
    // Cinema highlight-only bloom + subtle warm optical halation: kills Skyrim's muddy midtone and sun-glare wash.
    const float bloomLum = dot(bloom, W);
    const float kneeB = saturate((bloomLum - 0.55f) / 0.55f);
    const float3 cleanBloom = bloom * (kneeB * kneeB * 0.14f);
    const float3 halation = float3(1.0f, 0.42f, 0.18f) * saturate((bloomLum - 1.05f) * 0.30f) * 0.10f;
    mapped = saturate(mapped + cleanBloom + halation);
    if (RB.w > 1.5f) { return float4(lum.x * 0.25f, lum.y * 0.25f, exposure * 0.25f, 1.0f); }   // view 2: adapted average, the game's target, exposure (over 4, no gamma)
    if (RB.w > 0.5f) { return float4(exposure.xxx * 0.25f, 1.0f); }   // view 1: the exposure (4 = white, no gamma)
    // The game's grade, lane for lane as the game computes it (alpha included; its anti-aliasing reads it).
    const float g = dot(mapped, W);
    float4 c = float4(mapped, 1.0f);
    c = P[3].x * (c - g) + g;
    c = P[4].w * (g * P[4] - c) + c;
    c = P[3].z * (P[3].w * c - pivot) + pivot;
    c.rgb = pow(saturate(c.rgb), F[42].x);
    // Elder-inspired filmic finish: subtle optical vignette + terminal triangular dithering (eliminates 8-bit sky/fog banding).
    const float2 vc = i.uv - 0.5f;
    c.rgb *= lerp(1.0f, saturate(1.0f - dot(vc, vc) * 0.22f), 0.35f);
    const float2 dp = floor(i.pos.xy);
    const float h1 = frac(sin(dot(dp, float2(12.9898f, 78.233f))) * 43758.5453f);
    const float h2 = frac(sin(dot(dp + float2(17.0f, 53.0f), float2(12.9898f, 78.233f))) * 43758.5453f);
    c.rgb = saturate(c.rgb + ((h1 - h2) / 255.0f).xxx);
    return c;
}
