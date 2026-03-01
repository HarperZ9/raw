//=============================================================================
//  enbdepthoffield.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal DOF shader: RT-targeted techniques output zero (no blur),
//  screen techniques pass TextureColor unchanged.
//  If the game renders normally, the ENB pipeline is working.
//
//  SkyrimBridge v3.0.0 — Phase 0 Pipeline Diagnostic
//=============================================================================

// ── ENB Built-in Variables ─────────────────────────────────────────────────

float4 ScreenSize;
float4 Timer;
float  ENightDayFactor;
float  EInteriorFactor;
float4 FocusInfo;              // ENB focus data
float4 DOFParameters;          // ENB DOF settings

Texture2D TextureColor;
Texture2D TextureDepth;
Texture2D TextureOriginal;
Texture2D TextureFocus;

// ENB render targets
Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTargetR16F;

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
// Must match the exact names and count ENB expects (10 techniques).

// Tech 0: Read Focus → RenderTargetR16F (zero = no focus data)
technique11 DOF <string UIName="DOF - Passthrough Diagnostic"; string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 1: Focus Transition → screen (passthrough)
technique11 DOF1
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 2: Draw CoC → RenderTargetRGBA64 (zero = no blur)
technique11 DOF2 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 3: Downsample Near → RenderTargetR16F (zero)
technique11 DOF3 <string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 4: Upsample CoC → RenderTargetRGBA32 (zero)
technique11 DOF4 <string RenderTarget="RenderTargetRGBA32";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 5: Far Bokeh → RenderTargetRGBA64F (zero)
technique11 DOF5 <string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 6: Near Bokeh → screen (passthrough)
technique11 DOF6
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 7: Combine → screen (passthrough)
technique11 DOF7
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 8: Gaussian Vertical → screen (passthrough)
technique11 DOF8
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 9: Gaussian Horizontal + CA → screen (passthrough)
technique11 DOF9
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
