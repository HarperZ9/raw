//=============================================================================
//  enbunderwater.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal underwater shader: all 6 techniques pass TextureColor unchanged.
//  RT-targeted techniques output zero.
//  If the game renders normally underwater, the ENB pipeline is working.
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

// ENB render targets
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetR16F;

SamplerState Sampler0
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};


// ── Vertex Shader ──────────────────────────────────────────────────────────

void VS_Draw(inout float4 pos : SV_POSITION, inout float4 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


// ── Pixel Shaders ──────────────────────────────────────────────────────────

float4 PS_Passthrough(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler0, txcoord.xy);
}

float4 PS_Blank(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return float4(0.0, 0.0, 0.0, 0.0);
}


// ── Techniques ─────────────────────────────────────────────────────────────
// Must match the exact names from the original (6 techniques).

// Tech 0: Wave Distortion → RenderTargetRGBA64 (zero = no distortion)
technique11 Underwater <string UIName="Underwater - Passthrough Diagnostic"; string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 1: Blur Mask → RenderTargetR16F (zero)
technique11 Underwater1 <string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 2: Horizontal Blur → screen (passthrough)
technique11 Underwater2
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 3: Vertical Blur → screen (passthrough)
technique11 Underwater3
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 4: Volumetric Composite → screen (passthrough)
technique11 Underwater4
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 5: Lens Distortion → screen (passthrough)
technique11 Underwater5
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
