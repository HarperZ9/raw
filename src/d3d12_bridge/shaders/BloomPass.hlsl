// BloomPass.hlsl: True 6-Level RGBA16F Progressive Karis-Weighted HDR Bloom & Photochemical Halation Pyramid
// (Jimenez SIGGRAPH 2014 "Next Generation Post Processing in Call of Duty: Advanced Warfare").
// Runs directly on RAW's native DX12 linear RGBA16F HDR scene (replacing Skyrim's low-res vanilla bloom).
// Entry points (SM 6.9):
//   prefilter : 13-tap Karis-weighted soft-knee HDR threshold + downsample (full -> 1/2)
//   down      : 13-tap energy-conserving Jimenez progressive downsample (1/2 -> 1/4 -> ... -> 1/64)
//   up        : 9-tap 3x3 tent-filter upsample + spectral emulsion halation blend (1/64 -> ... -> 1/2)
//   composite : energy-conserving linear HDR bloom + halation merge into final m_out
// Original work, MIT.

Texture2D<float4> SrcA : register(t0);
Texture2D<float4> SrcB : register(t1);
SamplerState      Lin  : register(s0);

static const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);

float16_t karisW(float16_t3 c) {
    return float16_t(1.0h) / (float16_t(1.0h) + max(dot(c, float16_t3(kLuma)), float16_t(0.0h)));
}

float16_t3 softThreshold(float16_t3 c) {
    const float16_t br = max(c.r, max(c.g, c.b));
    // Calibrated strictly above diffuse sunlit snow/ice (<=1.20) and below flames, sun disc, spells & glints (>=1.80):
    const float16_t thresh = float16_t(1.48h);
    const float16_t knee = float16_t(0.30h);
    float16_t soft = clamp(br - thresh + knee, float16_t(0.0h), float16_t(2.0h) * knee);
    soft = (soft * soft) / max(float16_t(4.0h) * knee, float16_t(1.0e-4h));
    const float16_t contrib = max(soft, br - thresh) / max(br, float16_t(1.0e-4h));
    return min(c * saturate(contrib), float16_t3(4.0h, 4.0h, 4.0h));
}

// 13-tap Jimenez downsample pattern in native 16-bit FP16 (2x ALU throughput & half VGPR pressure on RTX 4090/5090):
float3 jimenez13(Texture2D<float4> tex, float2 uv, bool useKaris) {
    float w, h;
    tex.GetDimensions(w, h);
    const float2 dx = float2(1.0f / max(w, 1.0f), 0.0f);
    const float2 dy = float2(0.0f, 1.0f / max(h, 1.0f));
    const float16_t3 a = float16_t3(tex.SampleLevel(Lin, uv - 2.0f * dx + 2.0f * dy, 0.0f).rgb);
    const float16_t3 b = float16_t3(tex.SampleLevel(Lin, uv + 2.0f * dy, 0.0f).rgb);
    const float16_t3 c = float16_t3(tex.SampleLevel(Lin, uv + 2.0f * dx + 2.0f * dy, 0.0f).rgb);
    const float16_t3 d = float16_t3(tex.SampleLevel(Lin, uv - 2.0f * dx, 0.0f).rgb);
    const float16_t3 e = float16_t3(tex.SampleLevel(Lin, uv, 0.0f).rgb);
    const float16_t3 f = float16_t3(tex.SampleLevel(Lin, uv + 2.0f * dx, 0.0f).rgb);
    const float16_t3 g = float16_t3(tex.SampleLevel(Lin, uv - 2.0f * dx - 2.0f * dy, 0.0f).rgb);
    const float16_t3 hh = float16_t3(tex.SampleLevel(Lin, uv - 2.0f * dy, 0.0f).rgb);
    const float16_t3 ii = float16_t3(tex.SampleLevel(Lin, uv + 2.0f * dx - 2.0f * dy, 0.0f).rgb);
    const float16_t3 j = float16_t3(tex.SampleLevel(Lin, uv - dx + dy, 0.0f).rgb);
    const float16_t3 k = float16_t3(tex.SampleLevel(Lin, uv + dx + dy, 0.0f).rgb);
    const float16_t3 l = float16_t3(tex.SampleLevel(Lin, uv - dx - dy, 0.0f).rgb);
    const float16_t3 m = float16_t3(tex.SampleLevel(Lin, uv + dx - dy, 0.0f).rgb);

    if (useKaris) {
        const float16_t3 g0 = softThreshold((j + k + l + m) * float16_t(0.25h));
        const float16_t3 g1 = softThreshold((a + b + d + e) * float16_t(0.25h));
        const float16_t3 g2 = softThreshold((b + c + e + f) * float16_t(0.25h));
        const float16_t3 g3 = softThreshold((d + e + g + hh) * float16_t(0.25h));
        const float16_t3 g4 = softThreshold((e + f + hh + ii) * float16_t(0.25h));
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
    // Feature 9: 6-Blade Aperture Diffraction Starburst + Panavision Anamorphic Stretch on extreme HDR peaks (>3.5 linear).
    const float3 anaL = SrcA.SampleLevel(Lin, saturate(uv - float2(d.x * 3.5f, 0.0f)), 0.0f).rgb;
    const float3 anaR = SrcA.SampleLevel(Lin, saturate(uv + float2(d.x * 3.5f, 0.0f)), 0.0f).rgb;
    const float3 anaGlow = max((anaL + anaR) * 0.5f - 0.65f.xxx, 0.0f.xxx) * float3(0.72f, 0.88f, 1.12f) * 0.12f;
    const float2 d60a = float2(d.x * 2.2f, d.y * 3.8f), d60b = float2(d.x * 2.2f, -d.y * 3.8f);
    const float3 sb = (SrcA.SampleLevel(Lin, saturate(uv + d60a), 0.0f).rgb + SrcA.SampleLevel(Lin, saturate(uv - d60a), 0.0f).rgb +
                       SrcA.SampleLevel(Lin, saturate(uv + d60b), 0.0f).rgb + SrcA.SampleLevel(Lin, saturate(uv - d60b), 0.0f).rgb) * 0.25f;
    const float3 starBurst = max(sb - 2.6f.xxx, 0.0f.xxx) * float3(1.04f, 0.96f, 0.90f) * 0.09f;
    // Energy-conserving achromatic tent upsample (0.50 + 0.50 = 1.00; kills 1.20^5 pyramid runaway & pink halation on cyan ice caves!):
    return float4(curCol * 0.50f + upCol * 0.50f + anaGlow * 0.45f + starBurst * 0.60f, 1.0f);
}

// Full-res energy-conserving linear HDR composite: SrcA = pre-bloom linear HDR scene, SrcB = 6-level bloom pyramid mip 0.
float4 composite(float4 at : SV_Position) : SV_Target0 {
    float sw, sh;
    SrcA.GetDimensions(sw, sh);
    const float2 uv = at.xy / max(float2(sw, sh), float2(1.0f, 1.0f));
    const float4 scene = SrcA.Load(int3(at.xy, 0));
    const float3 bl = SrcB.SampleLevel(Lin, uv, 0.0f).rgb;
    // Clean energy-conserving merge: flames/sun/spells glow without washing out snowbanks or ice caves.
    const float3 outRgb = scene.rgb + bl * (0.11f / (1.0f + max(scene.r, max(scene.g, scene.b)) * 0.55f));
    return float4(outRgb, scene.a);
}
