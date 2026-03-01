//=============================================================================
//  enblens.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal lens shader: all 14 techniques pass TextureColor unchanged.
//  RT-targeted techniques output zero (neutral lens = no effect).
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

// ENB render targets (must be declared for RT-targeted techniques)
Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTarget256;
Texture2D RenderTarget512;

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

// Pass through TextureColor unchanged (for screen-writing techniques)
float4 PS_Passthrough(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler0, txcoord.xy);
}

// Output zero (for RT-targeted techniques: no lens flares, no rain, no anam)
float4 PS_Blank(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return float4(0.0, 0.0, 0.0, 0.0);
}


// ── Techniques ─────────────────────────────────────────────────────────────
// Must match the exact names and count ENB expects (14 techniques).

// Tech 0: Anamorphic prepass → RGBA64F (zero)
technique11 KitsuuneMasterLens <string UIName = "Master Lens"; string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 1: Anamorphic main → RT256 (zero)
technique11 KitsuuneMasterLens1 <string RenderTarget="RenderTarget256";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 2: Reflections → RT512 (zero)
technique11 KitsuuneMasterLens2 <string RenderTarget="RenderTarget512";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 3: Ghost accumulation → RGBA64F (zero)
technique11 KitsuuneMasterLens3 <string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 4: Starburst clear → screen (passthrough)
technique11 KitsuuneMasterLens4
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 5: Starburst init → screen (passthrough)
technique11 KitsuuneMasterLens5
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 6: Starburst main → screen (passthrough)
technique11 KitsuuneMasterLens6
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 7: Starburst postpass → screen (passthrough)
technique11 KitsuuneMasterLens7
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 8: Dirt and post-pass → screen (passthrough)
technique11 KitsuuneMasterLens8
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 9: Rain droplets → RGBA32 (zero, no rain geometry)
technique11 KitsuuneMasterLens9 <string RenderTarget="RenderTargetRGBA32";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 10: Rain blur → RGBA64F (zero)
technique11 KitsuuneMasterLens10 <string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 11: Halation prepass → RT512 (zero)
technique11 KitsuuneMasterLens11 <string RenderTarget="RenderTarget512";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 12: Lens Optics composite → screen (passthrough)
technique11 KitsuuneMasterLens12
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 13: Film Grain + Sensor Response → screen (passthrough, final)
technique11 KitsuuneMasterLens13 <string UIName = "Lens - Passthrough Diagnostic";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
