//=============================================================================
//  enbsunsprite.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal sunsprite shader: single technique with one pass that returns
//  TextureColor unchanged (no sun disc, no flares, no corona).
//  If the game renders normally, the ENB pipeline is working.
//
//  SkyrimBridge v3.0.0 — Phase 0 Pipeline Diagnostic
//=============================================================================

// ── ENB Built-in Variables ─────────────────────────────────────────────────

float4 ScreenSize;
float4 Timer;
float  ENightDayFactor;
float  EInteriorFactor;

float4 LightParameters;        // ENB sun position data
float4 LightScreenPos;         // Sun screen coordinates

Texture2D TextureColor;
Texture2D TextureDepth;

SamplerState Sampler0
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};


// ── Vertex Shader ──────────────────────────────────────────────────────────

void VS_Basic(inout float4 pos : SV_POSITION, inout float4 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


// ── Pixel Shader ───────────────────────────────────────────────────────────

float4 PS_Passthrough(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler0, txcoord.xy);
}


// ── Technique ──────────────────────────────────────────────────────────────
// Original has 21 passes for various sprite elements.
// Passthrough: single pass, no sprites.

technique11 KitsuuneSunspriteGD <string UIName="Sunsprite - Passthrough Diagnostic";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
