//=============================================================================
//  enbbloom.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal bloom shader: passes TextureColor unchanged (no bloom applied).
//  If the game renders normally, the ENB pipeline is working.
//
//  SkyrimBridge v3.0.0 — Phase 0 Pipeline Diagnostic
//=============================================================================

// ── ENB Built-in Variables ─────────────────────────────────────────────────

float4 ScreenSize;
float4 Timer;
float  ENightDayFactor;
float  EInteriorFactor;

Texture2D TextureColor;
Texture2D TextureDepth;
Texture2D TextureOriginal;

SamplerState Sampler0
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};


// ── Vertex Shader ──────────────────────────────────────────────────────────

void VS_Bloom(inout float4 pos : SV_POSITION, inout float4 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


// ── Pixel Shader ───────────────────────────────────────────────────────────

float4 PS_Bloom(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler0, txcoord.xy);
}


// ── Technique ──────────────────────────────────────────────────────────────

technique11 Draw <string UIName = "Bloom - Passthrough Diagnostic";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Bloom()));
              SetPixelShader (CompileShader(ps_5_0, PS_Bloom())); }
}
