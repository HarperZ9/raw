// RawAgxDisplay.hlsl: Fullscreen Triangle AgX Filmic Tonemap + IEC sRGB + TPDF IGN Dither Display Pass.
// Transforms RGBA16F linear HDR scene color (with sky backdrop for alpha==0) into R8G8B8A8_UNORM swapchain output.
// Entry points: vs_agx_display (vs_6_6 / vs_6_10), ps_agx_display (ps_6_6 / ps_6_10).
// Original clean-room implementation, MIT License.

cbuffer AgxDisplayCB : register(b0)
{
    float4 skyZenithExposure; // rgb: linear HDR zenith sky, w: exposure multiplier
    float4 skyHorizonFrame;   // rgb: linear HDR horizon sky, w: frame index for TPDF IGN dither
    float4 sunTintAndGlow;    // rgb: subtle horizon sun scatter tint, w: sun glow gain
    float4 _pad0;
};

Texture2D<float4> HdrScene    : register(t0);
SamplerState      LinearClamp : register(s0);

struct VsOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VsOut vs_agx_display(uint vid : SV_VertexID)
{
    VsOut o;
    const float2 p = float2((vid == 1u) ? 3.0f : -1.0f,
                            (vid == 2u) ? -3.0f : 1.0f);
    o.pos = float4(p, 0.0f, 1.0f);
    o.uv  = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return o;
}

float ignHash(float2 px, float frame)
{
    const float2 p = px + 5.588238f * frac(frame * 0.015625f) * 64.0f;
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float3 agxDefaultContrastApprox(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return + 15.5f     * x4 * x2
           - 40.14f    * x4 * x
           + 31.96f    * x4
           - 6.868f    * x2 * x
           + 0.4298f   * x2
           + 0.1191f   * x
           - 0.00232f;
}

float3 linearToSrgb(float3 c)
{
    const float3 lo = c * 12.92f;
    const float3 hi = 1.055f * pow(max(c, 1.0e-6f), 1.0f / 2.4f) - 0.055f;
    return saturate(lerp(lo, hi, step(0.0031308f, c)));
}

float3 agxFilmicTonemap(float3 col)
{
    // Exact Troy Sobotka AgX inset matrix (linear sRGB -> AgX working primaries)
    static const float3x3 kAgxInset = float3x3(
        0.842479062253094f,  0.0784335999999992f, 0.0792237451477643f,
        0.0423282422610123f, 0.878468636469772f,  0.0791661274605434f,
        0.0423756549057051f, 0.0784336000000000f, 0.879142973793104f
    );
    static const float3x3 kAgxOutset = float3x3(
         1.19687900512017f,   -0.0528968517574562f, -0.0529716355144438f,
        -0.0980208811401368f,  1.15190312990417f,   -0.0980434501171241f,
        -0.0990297440797205f, -0.0989611768448433f,  1.15107367264116f
    );
    static const float kMinEv = -12.47393f;
    static const float kMaxEv =  4.026069f;

    float3 v = mul(kAgxInset, max(col, 0.0f));
    v = clamp(log2(max(v, 1.0e-10f)), kMinEv, kMaxEv);
    v = (v - kMinEv) / (kMaxEv - kMinEv);
    v = saturate(agxDefaultContrastApprox(v));
    v = saturate(mul(kAgxOutset, v));
    // Display-referred AgX curve -> linear -> exact IEC 61966-2-1 sRGB
    return linearToSrgb(pow(v, 2.2f));
}

float4 ps_agx_display(VsOut inp) : SV_Target0
{
    const float4 hdr = HdrScene.SampleLevel(LinearClamp, inp.uv, 0.0f);
    float3 sceneRgb = hdr.rgb;
    const float luma = dot(sceneRgb, float3(0.2126f, 0.7152f, 0.0722f));

    // Synthesize physical sky backdrop cleanly on untouched sky pixels (alpha == 0 && zero radiance).
    if (hdr.a < 0.01f && luma < 0.001f) {
        const float skyT = saturate(inp.uv.y * 1.15f);
        const float horGlow = pow(saturate(1.0f - abs(inp.uv.y - 0.62f) * 2.4f), 3.0f);
        sceneRgb = lerp(skyZenithExposure.rgb, skyHorizonFrame.rgb, smoothstep(0.0f, 1.0f, skyT))
                 + sunTintAndGlow.rgb * (horGlow * sunTintAndGlow.w);
    }

    const float exposure = max(skyZenithExposure.w, 0.05f);
    float3 srgb = agxFilmicTonemap(sceneRgb * exposure);

    // Triangular-PDF Interleaved Gradient Noise zero-banding 8-bit output dither.
    const float n1 = ignHash(inp.pos.xy, skyHorizonFrame.w);
    const float n2 = ignHash(inp.pos.xy + float2(13.7f, 29.3f), skyHorizonFrame.w + 17.0f);
    const float tpdf = (n1 + n2 - 1.0f) * (0.75f / 255.0f);
    srgb = saturate(srgb + tpdf);

    return float4(srgb, 1.0f);
}
