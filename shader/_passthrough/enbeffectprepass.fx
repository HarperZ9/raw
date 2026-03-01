//=============================================================================
//  enbeffectprepass.fx — PASSTHROUGH DIAGNOSTIC
//
//  Minimal prepass shader: all 12 techniques pass TextureColor unchanged.
//  RT-targeted techniques output zero (neutral AO/GI/CS = no effect).
//  If the game renders normally, the ENB pipeline is working.
//
//  SkyrimBridge v3.0.0 — Phase 0 Pipeline Diagnostic
//=============================================================================

// ── ENB Built-in Variables ─────────────────────────────────────────────────

float4 ScreenSize;             // .x = width, .y = 1/width, .z = aspect, .w = 1
float4 Timer;                  // .x = engine time
float  ENightDayFactor;        // 0 = night, 1 = day
float  EInteriorFactor;        // 0 = exterior, 1 = interior

Texture2D TextureColor;        // Current color buffer
Texture2D TextureDepth;        // Depth buffer (reversed-Z, 0=far, 1=near)
Texture2D TextureOriginal;     // Pre-effect color (same as TextureColor at prepass start)
Texture2D TextureNormals;      // Screen-space normals

// ENB render targets (must be declared for RT-targeted techniques)
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTargetR16F;

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

// Pass through TextureColor unchanged (for screen-writing techniques)
float4 PS_Passthrough(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler0, txcoord.xy);
}

// Output zero (for RT-targeted techniques: no AO, no GI, no contact shadows)
float4 PS_Blank(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return float4(0.0, 0.0, 0.0, 0.0);
}


// ── Techniques ─────────────────────────────────────────────────────────────
// Must match the exact names and count ENB expects (12 techniques).

// Tech 0: SSS Horizontal → screen (passthrough)
technique11 KitsuunePrePass
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 1: SSS Vertical → screen (passthrough)
technique11 KitsuunePrePass1
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 2: SSS Composite → screen (passthrough)
technique11 KitsuunePrePass2
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 3: GTAO → RenderTargetRGBA64 (clear + zero)
technique11 KitsuunePrePass3 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 4: SSGI → RenderTargetRGBA64F (clear + zero)
technique11 KitsuunePrePass4 <string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 5: Contact Shadows → RenderTargetR16F (clear + zero)
technique11 KitsuunePrePass5 <string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
    pass p1 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Blank())); }
}

// Tech 6: Effects Composite → screen (passthrough)
technique11 KitsuunePrePass6
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 7: God Rays + Fog → screen (passthrough)
technique11 KitsuunePrePass7
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 8: Watercolor → screen (passthrough)
technique11 KitsuunePrePass8
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

// Tech 9-11: Unused slots → screen (passthrough)
technique11 KitsuunePrePass9
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

technique11 KitsuunePrePass10
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}

technique11 KitsuunePrePass11
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Passthrough())); }
}
