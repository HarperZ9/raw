// Physical thin-lens depth-of-field -- Pass 5: Final composite
// Reference: Potmesil & Chakravarty 1981, Jimenez 2014 (compositing)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer CompositeCB : register(b0)
{
    float2 ScreenDims;
    float2 HalfDims;
    float  CAStrength;       // Longitudinal chromatic aberration
    float  AnamorphicRatio;  // Horizontal stretch on bokeh
    float  MaxBokehRadius;
    uint   FocusPeaking;     // 1 = show focus peaking overlay
    float  FocusPeakThreshold;  // CoC threshold for "in focus"
    float3 pad0;
}

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy (sharp)
Texture2D<float4> FarField   : register(t1);  // Half-res far bokeh
Texture2D<float4> NearField  : register(t2);  // Half-res near bokeh (a=weight)
Texture2D<float2> CoCMap     : register(t3);  // Full-res CoC (.x=signed, .y=abs)
SamplerState PointSamp  : register(s0);
SamplerState LinearSamp : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

// Triangular dither to break banding (Gjoel 2016)
float TriangularDither(float2 pos)
{
    float noise = frac(sin(dot(pos, float2(12.9898, 78.233))) * 43758.5453);
    // Triangular PDF: remap [0,1) uniform to [-0.5, 0.5) triangular
    noise = noise * 2.0 - 1.0;
    return sign(noise) * (1.0 - sqrt(1.0 - abs(noise))) * 0.5;
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // Read sharp scene color
    float4 scene = SceneColor.Sample(PointSamp, uv);

    // Read per-pixel CoC
    float2 cocData = CoCMap.Sample(PointSamp, uv);
    float  signedCoC = cocData.x;
    float  absCoC    = cocData.y;

    // Compute half-res UV for sampling bokeh textures
    float2 halfUV = uv;  // same UV space, different texture dimensions

    // ── Far-field blend ─────────────────────────────────────────────────
    // Blend from sharp scene to far bokeh based on far CoC magnitude.
    float farCoC = max(signedCoC, 0.0);
    float farBlendFactor = saturate(farCoC / max(MaxBokehRadius, 1.0));

    // Smooth the blend factor to avoid hard transitions
    farBlendFactor = smoothstep(0.0, 1.0, farBlendFactor);

    float4 farColor = FarField.Sample(LinearSamp, halfUV);
    float3 output = lerp(scene.rgb, farColor.rgb, farBlendFactor);

    // ── Near-field blend ────────────────────────────────────────────────
    // Near field overlaps everything -- it bleeds over focused and far regions.
    float4 nearColor = NearField.Sample(LinearSamp, halfUV);
    float  nearAlpha = nearColor.a;

    // Boost near alpha slightly so near-field objects fully cover the scene
    nearAlpha = saturate(nearAlpha * 1.5);

    output = lerp(output, nearColor.rgb, nearAlpha);

    // ── Longitudinal chromatic aberration ───────────────────────────────
    // Shift red and blue channels radially based on CoC magnitude.
    // Longitudinal CA causes color fringing along the optical axis.
    if (CAStrength > 0.001)
    {
        float2 center = float2(0.5, 0.5);
        float2 fromCenter = uv - center;
        float  caOffset = absCoC * CAStrength * 0.001; // subtle

        float2 uvR = uv + fromCenter * caOffset;
        float2 uvB = uv - fromCenter * caOffset;

        // Re-sample only R and B channels with offset
        float4 farR = FarField.Sample(LinearSamp, uvR);
        float4 farB = FarField.Sample(LinearSamp, uvB);

        // Apply CA only to blurred regions
        float caBlend = saturate(absCoC / max(MaxBokehRadius, 1.0));
        output.r = lerp(output.r, lerp(scene.r, farR.r, farBlendFactor), caBlend);
        output.b = lerp(output.b, lerp(scene.b, farB.b, farBlendFactor), caBlend);
    }

    // ── Focus peaking debug overlay ─────────────────────────────────────
    if (FocusPeaking > 0)
    {
        // Detect "in focus" region: CoC below threshold
        if (absCoC < FocusPeakThreshold)
        {
            // Edge detection on CoC map for peaking outline
            float2 texelSize = 1.0 / ScreenDims;
            float cocL = CoCMap.Sample(PointSamp, uv + float2(-texelSize.x, 0)).y;
            float cocR = CoCMap.Sample(PointSamp, uv + float2( texelSize.x, 0)).y;
            float cocU = CoCMap.Sample(PointSamp, uv + float2(0, -texelSize.y)).y;
            float cocD = CoCMap.Sample(PointSamp, uv + float2(0,  texelSize.y)).y;

            float edge = abs(cocR - cocL) + abs(cocD - cocU);
            if (edge > 0.5)
            {
                // Red peaking overlay
                output = lerp(output, float3(1, 0, 0), 0.7);
            }
        }
    }

    // ── Triangular dither to break banding ──────────────────────────────
    float dither = TriangularDither(input.position.xy) / 255.0;
    output += dither;

    return float4(output, scene.a);
}
