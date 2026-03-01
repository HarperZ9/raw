//=============================================================================
//  enbeffectpostpass.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal postpass shader: all 9 techniques pass TextureColor unchanged.
//  RT-targeted techniques output zero (neutral).
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

// ENB render targets
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;

SamplerState Sampler0
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Sampler1
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


// ── Vertex Shader ──────────────────────────────────────────────────────────

void VS_Basic(inout float4 pos : SV_POSITION, inout float4 txcoord : TEXCOORD0)
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
// Must match the exact names and count ENB expects (9 techniques).

// Tech 0: SMAA Edge Detection → RenderTargetRGBA64F (clear + zero)
technique11 KitsuunePostPass <string UIName="Post Processing - Passthrough Diagnostic"; string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 1: SMAA Blend Weights → RenderTargetRGBA64 (zero)
technique11 KitsuunePostPass1 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 2: SMAA Neighborhood Blend → screen (passthrough)
technique11 KitsuunePostPass2
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 3: FXAA → screen (passthrough)
technique11 KitsuunePostPass3
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 4: KiSharp → screen (passthrough)
technique11 KitsuunePostPass4
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 5: Blur Suite → screen (passthrough; original conditionally compiled)
technique11 KitsuunePostPass5
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 6: Lens Distortion → screen (passthrough)
technique11 KitsuunePostPass6
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 7: Color Grading → screen (passthrough)
technique11 KitsuunePostPass7
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 8: Final (Vignette + Grain + Borders) → screen (passthrough)
technique11 KitsuunePostPass8
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
