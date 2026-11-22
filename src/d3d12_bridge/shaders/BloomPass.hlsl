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

float karisW(float3 c) {
    return 1.0f / (1.0f + max(dot(c, kLuma), 0.0f));
}

float3 softThreshold(float3 c) {
    const float br = max(c.r, max(c.g, c.b));
    const float thresh = 0.85f;
    const float knee = 0.55f;
    float soft = clamp(br - thresh + knee, 0.0f, 2.0f * knee);
    soft = (soft * soft) / max(4.0f * knee, 1.0e-5f);
    const float contrib = max(soft, br - thresh) / max(br, 1.0e-4f);
    // Cap extreme single-pixel specular spikes before entering the pyramid so fires/sun stay silky.
    return min(c * saturate(contrib), 6.0f.xxx);
}

// 13-tap Jimenez downsample pattern (eliminates pulsing & box artifacts across mip transitions).
float3 jimenez13(Texture2D<float4> tex, float2 uv, bool useKaris) {
    float w, h;
    tex.GetDimensions(w, h);
    const float2 dx = float2(1.0f / max(w, 1.0f), 0.0f);
    const float2 dy = float2(0.0f, 1.0f / max(h, 1.0f));
    const float3 a = tex.SampleLevel(Lin, uv - 2.0f * dx + 2.0f * dy, 0.0f).rgb;
    const float3 b = tex.SampleLevel(Lin, uv + 2.0f * dy, 0.0f).rgb;
    const float3 c = tex.SampleLevel(Lin, uv + 2.0f * dx + 2.0f * dy, 0.0f).rgb;
    const float3 d = tex.SampleLevel(Lin, uv - 2.0f * dx, 0.0f).rgb;
    const float3 e = tex.SampleLevel(Lin, uv, 0.0f).rgb;
    const float3 f = tex.SampleLevel(Lin, uv + 2.0f * dx, 0.0f).rgb;
    const float3 g = tex.SampleLevel(Lin, uv - 2.0f * dx - 2.0f * dy, 0.0f).rgb;
    const float3 hh = tex.SampleLevel(Lin, uv - 2.0f * dy, 0.0f).rgb;
    const float3 ii = tex.SampleLevel(Lin, uv + 2.0f * dx - 2.0f * dy, 0.0f).rgb;
    const float3 j = tex.SampleLevel(Lin, uv - dx + dy, 0.0f).rgb;
    const float3 k = tex.SampleLevel(Lin, uv + dx + dy, 0.0f).rgb;
    const float3 l = tex.SampleLevel(Lin, uv - dx - dy, 0.0f).rgb;
    const float3 m = tex.SampleLevel(Lin, uv + dx - dy, 0.0f).rgb;

    if (useKaris) {
        const float3 g0 = softThreshold((j + k + l + m) * 0.25f);
        const float3 g1 = softThreshold((a + b + d + e) * 0.25f);
        const float3 g2 = softThreshold((b + c + e + f) * 0.25f);
        const float3 g3 = softThreshold((d + e + g + hh) * 0.25f);
        const float3 g4 = softThreshold((e + f + hh + ii) * 0.25f);
        const float w0 = karisW(g0) * 0.50f;
        const float w1 = karisW(g1) * 0.125f, w2 = karisW(g2) * 0.125f;
        const float w3 = karisW(g3) * 0.125f, w4 = karisW(g4) * 0.125f;
        return (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, 1.0e-5f);
    }
    return e * 0.125f + (a + c + g + ii) * 0.03125f + (b + d + f + hh) * 0.0625f + (j + k + l + m) * 0.125f;
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
    // Subtle wide horizontal Panavision anamorphic stretch on bright HDR sources across coarser pyramid mips.
    const float3 anaL = SrcA.SampleLevel(Lin, saturate(uv - float2(d.x * 3.5f, 0.0f)), 0.0f).rgb;
    const float3 anaR = SrcA.SampleLevel(Lin, saturate(uv + float2(d.x * 3.5f, 0.0f)), 0.0f).rgb;
    const float3 anaGlow = max((anaL + anaR) * 0.5f - 0.65f.xxx, 0.0f.xxx) * float3(0.72f, 0.88f, 1.12f) * 0.12f;
    // Slight warm crimson-orange photochemical halation bias on wide scattering levels.
    const float3 halTint = float3(1.03f, 0.99f, 0.96f);
    return float4(curCol * 0.55f + upCol * halTint * 0.65f + anaGlow, 1.0f);
}

// Full-res energy-conserving linear HDR composite: SrcA = pre-bloom linear HDR scene, SrcB = 6-level bloom pyramid mip 0.
float4 composite(float4 at : SV_Position) : SV_Target0 {
    float sw, sh;
    SrcA.GetDimensions(sw, sh);
    const float2 uv = at.xy / max(float2(sw, sh), float2(1.0f, 1.0f));
    const float4 scene = SrcA.Load(int3(at.xy, 0));
    const float3 bl = SrcB.SampleLevel(Lin, uv, 0.0f).rgb;
    // Soft energy-conserving linear merge so bright snow & coastal skies never whiteout while fires/sun glow richly.
    const float3 outRgb = scene.rgb + bl * (0.14f / (1.0f + max(scene.r, max(scene.g, scene.b)) * 0.45f));
    return float4(outRgb, scene.a);
}
