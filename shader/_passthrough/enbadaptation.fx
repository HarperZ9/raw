//=============================================================================
//  enbadaptation.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal adaptation shader that maintains stable exposure.
//  If the game renders normally, the ENB pipeline is working.
//  Outputs a slowly-adapting average luminance (no actual histogram/tonemapping).
//
//  SkyrimBridge v3.0.0 — Phase 0 Pipeline Diagnostic
//=============================================================================

// ── ENB Built-in Variables ─────────────────────────────────────────────────
// These are injected by ENB automatically — do NOT declare as cbuffer.

float4 AdaptationParameters;   // ENB adaptation settings
Texture2D TextureCurrent;      // Current frame (256x256 → 16x16 depending on technique)
Texture2D TexturePrevious;     // Previous frame adaptation result (1x1)

SamplerState Sampler0
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};


// ── Vertex Shader ──────────────────────────────────────────────────────────

void VS_Quad(inout float4 pos : SV_POSITION, inout float4 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


// ── Pixel Shaders ──────────────────────────────────────────────────────────

// Tech 0: Downsample — reads 256x256, outputs to 16x16 RT
// Just pass through the center sample (average luminance approximation)
float4 PS_Downsample(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureCurrent.Sample(Sampler0, txcoord.xy).rgb;
    // Simple luminance: Rec.709
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    return float4(lum, lum, lum, 1.0);
}

// Tech 1: Draw — reads 16x16 TextureCurrent + 1x1 TexturePrevious, outputs 1x1
// Temporal smoothing between current and previous adaptation
float4 PS_Adapt(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    // Average the current 16x16 input (just sample center for simplicity)
    float current = TextureCurrent.Sample(Sampler0, float2(0.5, 0.5)).x;
    float previous = TexturePrevious.Sample(Sampler0, float2(0.5, 0.5)).x;

    // Clamp to safe range
    current  = clamp(current,  0.001, 10.0);
    previous = clamp(previous, 0.001, 10.0);

    // Slow temporal adaptation (5% blend per frame)
    float adapted = lerp(previous, current, 0.05);

    return float4(adapted, adapted, adapted, 1.0);
}


// ── Techniques ─────────────────────────────────────────────────────────────
// ENB adaptation requires exactly this two-technique structure.

technique11 Downsample
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
              SetPixelShader (CompileShader(ps_5_0, PS_Downsample())); }
}

technique11 Draw <string UIName="Adaptation - Passthrough Diagnostic";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
              SetPixelShader (CompileShader(ps_5_0, PS_Adapt())); }
}
