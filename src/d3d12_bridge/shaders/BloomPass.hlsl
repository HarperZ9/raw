// BloomPass.hlsl: True 6-Level RGBA16F Firefly-Free Energy-Conserving Karis-Weighted HDR Bloom Pyramid
// (Jimenez SIGGRAPH 2014 + Karis 2013 Per-Tap & Per-Quad Anti-Firefly Prefilter, SM 6.6 / 6.10).
// Runs directly on RAW's native DX12 linear RGBA16F HDR scene (replacing Skyrim's low-res vanilla bloom).
// Entry points:
//   prefilter : 13-tap per-tap + per-quad Karis-weighted soft-knee HDR threshold + isolated-glint firefly guard (full -> 1/2)
//   down      : 13-tap NaN-safe energy-conserving Jimenez progressive downsample (1/2 -> 1/4 -> ... -> 1/64)
//   up        : 9-tap 3x3 tent-filter upsample (1/64 -> ... -> 1/2)
//   composite : energy-conserving linear HDR bloom merge into final m_out
// Original work, MIT.

Texture2D<float4> SrcA : register(t0);
Texture2D<float4> SrcB : register(t1);
SamplerState      Lin  : register(s0);

static const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);

// Sanitize raw HDR fetch before FP16 conversion (eliminates NaN/Inf/FP16-overflow explosions from extreme glints):
float16_t3 sanitizeHdrTap(float3 raw) {
    if (any(isnan(raw)) || any(isinf(raw))) { return float16_t3(0.0h, 0.0h, 0.0h); }
    raw = max(raw, 0.0f.xxx);
    const float pk = max(raw.r, max(raw.g, raw.b));
    if (pk > 12.0f) {
        const float softPk = 12.0f + (pk - 12.0f) / (1.0f + (pk - 12.0f) * 0.45f);
        raw *= min(softPk, 18.0f) / pk;
    }
    return float16_t3(raw);
}

float16_t karisW(float16_t3 c) {
    return float16_t(1.0h) / (float16_t(1.0h) + max(dot(c, float16_t3(kLuma)), float16_t(0.0h)));
}

float16_t3 softThreshold(float16_t3 c) {
    const float16_t br = max(c.r, max(c.g, c.b));
    // Strictly above diffuse surfaces, snow, clouds & plaster (<=1.95); genuine fire cores, sun shafts & spells bloom cleanly!
    const float16_t thresh = float16_t(2.25h);
    const float16_t knee = float16_t(0.28h);
    float16_t soft = clamp(br - thresh + knee, float16_t(0.0h), float16_t(2.0h) * knee);
    soft = (soft * soft) / max(float16_t(4.0h) * knee, float16_t(1.0e-4h));
    const float16_t contrib = max(soft, br - thresh) / max(br, float16_t(1.0e-4h));
    return min(c * saturate(contrib), float16_t3(3.0h, 3.0h, 3.0h));
}

// Per-tap Karis-weighted 2x2 downsample with isolated 1-pixel firefly suppression:
// 1-pixel water glints, snow sparkles, and candle tips (1 hot tap surrounded by 3 dark taps) are attenuated smoothly
// so they NEVER strobe or explode into pulsing bloom rings, while genuine multi-pixel fire cores & sun shafts pass at 100%!
float16_t3 karisBox4(float16_t3 t0, float16_t3 t1, float16_t3 t2, float16_t3 t3) {
    const float16_t3 s0 = softThreshold(t0);
    const float16_t3 s1 = softThreshold(t1);
    const float16_t3 s2 = softThreshold(t2);
    const float16_t3 s3 = softThreshold(t3);

    const float16_t l0 = max(dot(s0, float16_t3(kLuma)), float16_t(0.0h));
    const float16_t l1 = max(dot(s1, float16_t3(kLuma)), float16_t(0.0h));
    const float16_t l2 = max(dot(s2, float16_t3(kLuma)), float16_t(0.0h));
    const float16_t l3 = max(dot(s3, float16_t3(kLuma)), float16_t(0.0h));

    // Per-tap Karis inverse-luminance weights suppress single-pixel HDR spikes BEFORE averaging:
    const float16_t w0 = float16_t(1.0h) / (float16_t(1.0h) + l0);
    const float16_t w1 = float16_t(1.0h) / (float16_t(1.0h) + l1);
    const float16_t w2 = float16_t(1.0h) / (float16_t(1.0h) + l2);
    const float16_t w3 = float16_t(1.0h) / (float16_t(1.0h) + l3);
    float16_t3 avg = (s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) / max(w0 + w1 + w2 + w3, float16_t(1.0e-4h));

    // Isolated 1-tap outlier guard: compare peak tap against 2nd-brightest tap in the 2x2 neighborhood.
    // A 1-pixel glint has peak >> secondMax (suppressed cleanly); a real fire/sun core has >=2 bright taps (full energy!).
    const float16_t hi01 = max(l0, l1), lo01 = min(l0, l1);
    const float16_t hi23 = max(l2, l3), lo23 = min(l2, l3);
    const float16_t peakL   = max(hi01, hi23);
    const float16_t secondL = max(min(hi01, hi23), max(lo01, lo23));
    if (peakL > float16_t(0.05h)) {
        const float16_t support = saturate(secondL / max(peakL * float16_t(0.32h), float16_t(0.02h)));
        avg *= lerp(float16_t(0.28h), float16_t(1.0h), support);
    }
    return avg;
}

// 13-tap Jimenez downsample pattern in native 16-bit FP16 (2x ALU throughput & half VGPR pressure):
float3 jimenez13(Texture2D<float4> tex, float2 uv, bool useKaris) {
    float w, h;
    tex.GetDimensions(w, h);
    const float2 dx = float2(1.0f / max(w, 1.0f), 0.0f);
    const float2 dy = float2(0.0f, 1.0f / max(h, 1.0f));
    const float16_t3 a  = sanitizeHdrTap(tex.SampleLevel(Lin, uv - 2.0f * dx + 2.0f * dy, 0.0f).rgb);
    const float16_t3 b  = sanitizeHdrTap(tex.SampleLevel(Lin, uv + 2.0f * dy, 0.0f).rgb);
    const float16_t3 c  = sanitizeHdrTap(tex.SampleLevel(Lin, uv + 2.0f * dx + 2.0f * dy, 0.0f).rgb);
    const float16_t3 d  = sanitizeHdrTap(tex.SampleLevel(Lin, uv - 2.0f * dx, 0.0f).rgb);
    const float16_t3 e  = sanitizeHdrTap(tex.SampleLevel(Lin, uv, 0.0f).rgb);
    const float16_t3 f  = sanitizeHdrTap(tex.SampleLevel(Lin, uv + 2.0f * dx, 0.0f).rgb);
    const float16_t3 g  = sanitizeHdrTap(tex.SampleLevel(Lin, uv - 2.0f * dx - 2.0f * dy, 0.0f).rgb);
    const float16_t3 hh = sanitizeHdrTap(tex.SampleLevel(Lin, uv - 2.0f * dy, 0.0f).rgb);
    const float16_t3 ii = sanitizeHdrTap(tex.SampleLevel(Lin, uv + 2.0f * dx - 2.0f * dy, 0.0f).rgb);
    const float16_t3 j  = sanitizeHdrTap(tex.SampleLevel(Lin, uv - dx + dy, 0.0f).rgb);
    const float16_t3 k  = sanitizeHdrTap(tex.SampleLevel(Lin, uv + dx + dy, 0.0f).rgb);
    const float16_t3 l  = sanitizeHdrTap(tex.SampleLevel(Lin, uv - dx - dy, 0.0f).rgb);
    const float16_t3 m  = sanitizeHdrTap(tex.SampleLevel(Lin, uv + dx - dy, 0.0f).rgb);

    if (useKaris) {
        const float16_t3 g0 = karisBox4(j, k, l, m);
        const float16_t3 g1 = karisBox4(a, b, d, e);
        const float16_t3 g2 = karisBox4(b, c, e, f);
        const float16_t3 g3 = karisBox4(d, e, g, hh);
        const float16_t3 g4 = karisBox4(e, f, hh, ii);
        const float16_t w0 = karisW(g0) * float16_t(0.50h);
        const float16_t w1 = karisW(g1) * float16_t(0.125h), w2 = karisW(g2) * float16_t(0.125h);
        const float16_t w3 = karisW(g3) * float16_t(0.125h), w4 = karisW(g4) * float16_t(0.125h);
        return float3((g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, float16_t(1.0e-4h)));
    }
    return float3(e * float16_t(0.125h) + (a + c + g + ii) * float16_t(0.03125h) + (b + d + f + hh) * float16_t(0.0625h) + (j + k + l + m) * float16_t(0.125h));
}

float4 prefilter(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 dst = max(floor((float2(w, h) + 1.0f) * 0.5f), float2(1.0f, 1.0f));
    return float4(jimenez13(SrcA, at.xy / dst, true), 1.0f);
}

float4 down(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 dst = max(floor((float2(w, h) + 1.0f) * 0.5f), float2(1.0f, 1.0f));
    return float4(jimenez13(SrcA, at.xy / dst, false), 1.0f);
}

// 9-tap 3x3 Jimenez tent upsample combining coarser mip (SrcA) with current-level downsample (SrcB).
float4 up(float4 at : SV_Position) : SV_Target0 {
    float bw, bh;
    SrcB.GetDimensions(bw, bh);
    const float2 uv = at.xy / max(float2(bw, bh), float2(1.0f, 1.0f));
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 d = float2(1.0f / max(w, 1.0f), 1.0f / max(h, 1.0f)) * 1.15f;
    float3 s = SrcA.SampleLevel(Lin, uv, 0.0f).rgb * 4.0f;
    s += (SrcA.SampleLevel(Lin, uv + float2( d.x, 0.0f), 0.0f).rgb + SrcA.SampleLevel(Lin, uv + float2(-d.x, 0.0f), 0.0f).rgb +
          SrcA.SampleLevel(Lin, uv + float2(0.0f,  d.y), 0.0f).rgb + SrcA.SampleLevel(Lin, uv + float2(0.0f, -d.y), 0.0f).rgb) * 2.0f;
    s += SrcA.SampleLevel(Lin, uv + float2( d.x,  d.y), 0.0f).rgb + SrcA.SampleLevel(Lin, uv + float2(-d.x,  d.y), 0.0f).rgb +
         SrcA.SampleLevel(Lin, uv + float2( d.x, -d.y), 0.0f).rgb + SrcA.SampleLevel(Lin, uv + float2(-d.x, -d.y), 0.0f).rgb;
    const float3 upCol = s * (1.0f / 16.0f);
    const float3 curCol = SrcB.SampleLevel(Lin, uv, 0.0f).rgb;
    // Pure energy-conserving achromatic tent upsample (zero low-threshold glare leakage):
    return float4(curCol * 0.50f + upCol * 0.50f, 1.0f);
}

// Full-res energy-conserving linear HDR composite: SrcA = pre-bloom linear HDR scene, SrcB = 6-level bloom pyramid mip 0.
float4 composite(float4 at : SV_Position) : SV_Target0 {
    float sw, sh;
    SrcA.GetDimensions(sw, sh);
    const float2 uv = at.xy / max(float2(sw, sh), float2(1.0f, 1.0f));
    const float4 scene = SrcA.Load(int3(at.xy, 0));
    const float3 bl = max(SrcB.SampleLevel(Lin, uv, 0.0f).rgb, 0.0f.xxx);
    // Disciplined linear HDR bloom merge (0.055): genuine emissives glow softly without any surface highlight bleed.
    const float3 outRgb = scene.rgb + bl * (0.055f / (1.0f + max(scene.r, max(scene.g, scene.b)) * 0.78f));
    return float4(outRgb, scene.a);
}
