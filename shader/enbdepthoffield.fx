//----------------------------------------------------------------------------------------------//
//                     ENB of the Elders - Depth of Field                                        //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Physical DOF: autofocus, CoC computation, disc bokeh gather, composite.                     //
//  Based on AMON ENB / LonelyKitsuune ADOF system.                                            //
//                                                                                              //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=== ENB EXTERNAL VARIABLES ===//

float4  Timer;
float4  ScreenSize;
float   AdaptiveQuality;
float4  Weather;
float4  TimeOfDay1;
float4  TimeOfDay2;
float   ENightDayFactor;
float   EInteriorFactor;
float   FieldOfView;
float4  DofParameters;     // z = ApertureTime*elapsed, w = FocusingTime*elapsed
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== TEXTURES ===//

Texture2D   TextureColor;           // Scene color (current pass input)
Texture2D   TextureOriginal;        // Original unmodified scene
Texture2D   TextureDepth;           // Depth buffer
Texture2D   TextureFocus;           // Focus texture (previous frame focus)
Texture2D   TextureCurrent;         // Current focus computation
Texture2D   TexturePrevious;        // Previous frame focus
Texture2D   TextureAperture;        // Aperture from previous frame
Texture2D   RenderTargetRGBA32;     // 32-bit RT (CoC storage)
Texture2D   RenderTargetRGBA64F;    // 64-bit RT (far bokeh)
Texture2D   RenderTargetR16F;       // 16-bit single channel (near CoC)


//=== SAMPLERS ===//

SamplerState smpPoint
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState smpLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


//=== CONSTANTS ===//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float  DELTA = 1e-6;
static const float  FPS_HAND_CUTOFF = 0.001;


//=== UI PARAMETERS ===//

// --- Focus ---

int ui_FocusMode
<
    string UIName = "DOF | Focus Mode (1=Auto 2=Mouse 3=Manual)";
    string UIWidget = "Spinner";
    int UIMin = 1;
    int UIMax = 3;
> = {1};

float2 ui_AutofocusCenter
<
    string UIName = "DOF | Autofocus Center XY";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5, 0.5};

float ui_AutofocusRadius
<
    string UIName = "DOF | Autofocus Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.05};

float ui_ManualFocusDepth
<
    string UIName = "DOF | Manual Focus Depth";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.0001;
> = {0.05};

// --- Blur ---

float ui_NearBlurCurve
<
    string UIName = "DOF | Near Blur Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 10.0;
    float UIStep = 0.01;
> = {1.0};

float ui_FarBlurCurve
<
    string UIName = "DOF | Far Blur Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 10.0;
    float UIStep = 0.01;
> = {1.4};

bool ui_RemoveFPSHands
<
    string UIName = "DOF | Remove FPS Weapon Blur";
> = {true};

// --- Bokeh Shape ---

float ui_BokehRadius
<
    string UIName = "DOF | Bokeh Max Radius (px)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 100.0;
    float UIStep = 0.5;
> = {15.0};

int ui_BokehQuality
<
    string UIName = "DOF | Bokeh Quality (rings)";
    string UIWidget = "Spinner";
    int UIMin = 2;
    int UIMax = 12;
> = {5};

// --- Post Processing ---

float ui_SmoothAmount
<
    string UIName = "DOF | Smoothing Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 10.0;
    float UIStep = 0.1;
> = {4.0};


//=== HELPER FUNCTIONS ===//

// Raw depth: [0, 1] range directly from depth buffer. Non-linear (reverse-Z hyperbolic)
// but well-distributed for DOF CoC computation where relative differences matter.
float GetRawDepth(float2 uv)
{
    return TextureDepth.SampleLevel(smpPoint, uv, 0).x;
}

// Linear depth: [0, 1] linearized over full z-range (znear=1, zfar=3000)
// Self-normalizing CoC formula scales with focus distance, so linear depth works well.
float GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}


//=== VERTEX SHADERS ===//

// VS input struct — ENB vertex buffer uses POSITION semantic (not SV_POSITION)
struct VS_INPUT
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}

// Focus readback VS — scales quad to 1/16th size for minification
struct VS_FOCUS_OUT
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VS_FOCUS_OUT VS_ReadFocus(VS_INPUT IN)
{
    VS_FOCUS_OUT OUT;
    OUT.pos = float4(IN.pos.xy * 0.0625 + float2(-0.9375, 0.9375), 0.5, 1.0);
    OUT.texcoord = IN.txcoord;
    return OUT;
}

// DOF VS with focus data
struct VS_DOF_OUT
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float  focus    : TEXCOORD1;
};

VS_DOF_OUT VS_DoF(VS_INPUT IN)
{
    VS_DOF_OUT OUT;
    OUT.pos = float4(IN.pos.xy, 0.5, 1.0);
    OUT.texcoord = IN.txcoord;
    // Read focus from TextureFocus (populated by Focus technique)
    OUT.focus = TextureFocus.Load(int3(0, 0, 0)).x;
    return OUT;
}


//=== PIXEL SHADERS ===//

// Pass 0: Read focus — weighted depth sampling
float4 PS_ReadFocus(VS_FOCUS_OUT IN) : SV_Target
{
    if (ui_FocusMode == 3) return ui_ManualFocusDepth;

    float2 center = (ui_FocusMode == 1) ? ui_AutofocusCenter : tempInfo2.zw;
    float radius = ui_AutofocusRadius;

    float focusSum = 0.0;
    float weightSum = DELTA;

    // 10x10 grid sampling
    [loop]
    for (float x = 0.0; x < 10.0; x++)
    {
        [loop]
        for (float y = 0.0; y < 10.0; y++)
        {
            float2 offset = (float2(x, y) + 0.5) * 0.2 - 1.0;
            offset *= radius;
            float2 sampleUV = center + offset;

            float w = saturate(1.2 * exp2(dot(offset, offset) * -4.0));
            float d = GetLinearDepth(sampleUV);

            // Bias toward nearer objects (inverse depth weighting)
            w /= (d + DELTA);

            // FPS hand rejection (near objects have very small linear depth)
            if (ui_RemoveFPSHands && d < FPS_HAND_CUTOFF)
                w = 0.0;

            focusSum += d * w;
            weightSum += w;
        }
    }

    float focus = focusSum / weightSum;
    return (weightSum > DELTA * 2.0) ? focus : -1.0;
}

// Pass 1: Temporal focus smoothing (writes to tiny quad — must NOT overwrite scene)
float4 PS_Focus(VS_FOCUS_OUT IN) : SV_Target
{
    float prevFocus = TexturePrevious.Load(int3(0, 0, 0)).x;
    float currFocus = TextureCurrent.Load(int3(0, 0, 0)).x;
    float speed = DofParameters.w;

    // Freeze when all samples blocked
    speed *= (currFocus > -DELTA);

    return (ui_FocusMode == 3) ? currFocus : lerp(prevFocus, currFocus, speed);
}

// Pass 2: CoC computation → RenderTargetRGBA32
// Self-normalizing CoC: divides by focus distance so blur scales naturally
float4 PS_DrawCoC(VS_DOF_OUT IN) : SV_Target
{
    float focus = IN.focus;
    float depth = GetLinearDepth(IN.texcoord);

    float depthDiff = depth - focus;
    float2 coc = 0.0;

    if (depthDiff > 0.0)
    {
        // Far blur: self-normalizing (scales with focus distance)
        coc.x = saturate(depthDiff * ui_FarBlurCurve / max(focus, DELTA));
    }
    else
    {
        // Near blur: self-normalizing (scales with focus distance)
        coc.y = saturate(-depthDiff * ui_NearBlurCurve / max(focus, DELTA));
    }

    // FPS hand rejection
    if (ui_RemoveFPSHands && depth < FPS_HAND_CUTOFF)
        coc = 0.0;

    // x=far, y=near, z=smoothstepped far, w=smoothstepped near
    float4 sep = saturate(float4(coc.x, coc.y, coc.x, coc.y));
    sep.zw = sep.zw * sep.zw * (3.0 - 2.0 * sep.zw);

    return sep;
}

// Pass 3: Near CoC downsample + blur → R16F
float4 PS_NearCoCBlur(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float nearCoC = 0.0;
    float totalWeight = 0.0;

    // Horizontal + vertical 7-tap blur
    [unroll]
    for (int i = -3; i <= 3; i++)
    {
        float2 offset = float2(PixelSize.x * float(i) * 2.0, 0.0);
        float sample0 = RenderTargetRGBA32.Sample(smpLinear, txcoord + offset).y;
        float w = exp(-abs(float(i)) * 0.5);
        nearCoC += sample0 * w;
        totalWeight += w;
    }

    return nearCoC / totalWeight;
}

// Pass 4: Combine CoC with near bleed → overwrite RenderTargetRGBA32
float4 PS_CombineCoC(VS_DOF_OUT IN) : SV_Target
{
    float2 txcoord = IN.texcoord;

    // Read the blurred near CoC from R16F (written by pass 3)
    float nearBleed = RenderTargetR16F.Sample(smpLinear, txcoord).x;

    // Focus from VS (reads TextureFocus)
    float focus = IN.focus;
    float depth = GetLinearDepth(txcoord);

    float depthDiff = depth - focus;
    float2 coc = 0.0;

    if (depthDiff > 0.0)
        coc.x = saturate(depthDiff * ui_FarBlurCurve / max(focus, DELTA));
    else
        coc.y = saturate(-depthDiff * ui_NearBlurCurve / max(focus, DELTA));

    if (ui_RemoveFPSHands && depth < FPS_HAND_CUTOFF)
        coc = 0.0;

    // x=far CoC, y=near CoC, z=near bleed (blurred), w=near bleed smoothstepped
    float blurNear = max(nearBleed, coc.y);
    return float4(coc.x, coc.y, blurNear, blurNear * blurNear * (3.0 - 2.0 * blurNear));
}

// Pass 5: Far bokeh gather (disc blur) → RenderTargetRGBA64F
float4 PS_FarBokeh(VS_DOF_OUT IN) : SV_Target
{
    float3 centerColor = TextureOriginal.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float4 cocData = RenderTargetRGBA32.Sample(smpPoint, IN.texcoord);
    float coc = cocData.x; // far CoC

    float radiusPx = coc * ui_BokehRadius;
    if (radiusPx < 0.5) return float4(centerColor, coc);

    // Disc bokeh gather with concentric rings
    float2 radius = float2(radiusPx * PixelSize.x, radiusPx * PixelSize.y);
    int rings = (int)lerp(2.0, float(ui_BokehQuality), saturate(coc));

    float3 bokehSum = centerColor;
    float wSum = 1.0;

    [loop]
    for (int ring = 1; ring <= rings; ring++)
    {
        int samples = ring * 6; // 6 samples per ring
        float ringRadius = float(ring) / float(rings);
        float2 rr = radius * ringRadius;

        [loop]
        for (int s = 0; s < samples; s++)
        {
            float angle = float(s) * 6.283185 / float(samples);
            float2 dir;
            sincos(angle, dir.y, dir.x);

            float2 sUV = IN.texcoord + dir * rr;
            float3 sCol = TextureOriginal.SampleLevel(smpLinear, sUV, 0).rgb;

            // Leak prevention: check sample's far CoC
            float sCoc = RenderTargetRGBA32.SampleLevel(smpPoint, sUV, 0).x;
            float sW = saturate(sCoc * 10.0); // Only include if sample is also out-of-focus far

            // Karis tone weight for anti-firefly
            sW *= 1.0 / (max(max(sCol.r, sCol.g), sCol.b) + 1.0);

            bokehSum += sCol * sW;
            wSum += sW;
        }
    }

    return float4(bokehSum / wSum, coc);
}

// Pass 6: Near bokeh gather (same disc pattern, uses near CoC)
float4 PS_NearBokeh(VS_DOF_OUT IN) : SV_Target
{
    float3 centerColor = TextureOriginal.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float4 cocData = RenderTargetRGBA32.Sample(smpPoint, IN.texcoord);
    float coc = cocData.z; // near bleed CoC

    float radiusPx = coc * ui_BokehRadius;
    if (radiusPx < 0.5) return float4(centerColor, coc);

    float2 radius = float2(radiusPx * PixelSize.x, radiusPx * PixelSize.y);
    int rings = (int)lerp(2.0, float(ui_BokehQuality), saturate(coc));

    float3 bokehSum = centerColor;
    float wSum = 1.0;

    [loop]
    for (int ring = 1; ring <= rings; ring++)
    {
        int samples = ring * 6;
        float ringRadius = float(ring) / float(rings);
        float2 rr = radius * ringRadius;

        [loop]
        for (int s = 0; s < samples; s++)
        {
            float angle = float(s) * 6.283185 / float(samples);
            float2 dir;
            sincos(angle, dir.y, dir.x);

            float2 sUV = IN.texcoord + dir * rr;
            float3 sCol = TextureOriginal.SampleLevel(smpLinear, sUV, 0).rgb;

            float sW = 1.0 / (max(max(sCol.r, sCol.g), sCol.b) + 1.0);
            bokehSum += sCol * sW;
            wSum += sW;
        }
    }

    return float4(bokehSum / wSum, coc);
}

// Pass 7: Final composite
float4 PS_Combine(VS_DOF_OUT IN) : SV_Target
{
    float3 original = TextureOriginal.SampleLevel(smpPoint, IN.texcoord, 0).rgb;
    float4 farBokeh = RenderTargetRGBA64F.Sample(smpLinear, IN.texcoord);
    float3 nearBokeh = TextureColor.Sample(smpLinear, IN.texcoord).rgb;
    float4 cocData = RenderTargetRGBA32.Sample(smpPoint, IN.texcoord);

    float farCoC = cocData.x;
    float nearCoC = cocData.w; // smoothstepped near bleed

    // Far blend: smooth transition from in-focus to far bokeh
    float farBlend = smoothstep(0.05, 0.3, farCoC);
    farBlend = sqrt(farBlend);

    // Near blend: smooth transition from in-focus to near bokeh
    float nearBlend = saturate(sqrt(nearCoC) * 1.5);

    // Layered composite: original → far → near
    float3 result = lerp(original, farBokeh.rgb, farBlend);
    result = lerp(result, nearBokeh, nearBlend);

    // Alpha encodes max blur for post-blur
    float alpha = saturate(max(nearCoC, farCoC) * 4.0);

    return float4(result, alpha);
}

// Pass 8-9: Gaussian post-blur (V then H)
float4 PS_GaussBlur(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0,
                    uniform float2 blurDir) : SV_Target
{
    float4 center = TextureColor.Sample(smpPoint, txcoord);

    if (ui_SmoothAmount < 0.01) return center;

    float cocAlpha = center.a;
    float blurSize = ui_SmoothAmount * cocAlpha;

    float3 result = center.rgb;
    float totalWeight = 1.0;

    [unroll]
    for (int i = 1; i <= 4; i++)
    {
        float2 offset = blurDir * PixelSize * float(i) * blurSize;
        float w = exp(-float(i * i) * 0.5);

        float4 s1 = TextureColor.Sample(smpLinear, txcoord + offset);
        float4 s2 = TextureColor.Sample(smpLinear, txcoord - offset);

        result += s1.rgb * w + s2.rgb * w;
        totalWeight += w * 2.0;
    }

    return float4(result / totalWeight, cocAlpha);
}


//=== TECHNIQUES ===//
// ENB requires these EXACT technique names for DOF:
//   ReadFocus (→ TextureCurrent), Focus (→ TextureFocus), then DOF* for processing
// UIName must be on the DOF technique (3rd), NOT on ReadFocus or Focus

// Technique 1: Focus readback → TextureCurrent (1x1 R32F)
technique11 ReadFocus
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_ReadFocus()));
        SetPixelShader(CompileShader(ps_5_0, PS_ReadFocus()));
    }
}

// Technique 2: Temporal focus smoothing → TextureFocus (1x1 R32F)
technique11 Focus
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_ReadFocus()));
        SetPixelShader(CompileShader(ps_5_0, PS_Focus()));
    }
}

// Technique 3: CoC computation → RenderTargetRGBA32
technique11 DOF <string UIName = "EotE: Depth of Field";  string RenderTarget = "RenderTargetRGBA32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_DrawCoC()));
    }
}

// Technique 4: Near CoC blur → RenderTargetR16F
technique11 DOF1 <string RenderTarget = "RenderTargetR16F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_NearCoCBlur()));
    }
}

// Technique 5: Combine CoC → RenderTargetRGBA32
technique11 DOF2 <string RenderTarget = "RenderTargetRGBA32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_CombineCoC()));
    }
}

// Technique 6: Far bokeh → RenderTargetRGBA64F
technique11 DOF3 <string RenderTarget = "RenderTargetRGBA64F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_FarBokeh()));
    }
}

// Technique 7: Near bokeh (writes to default TextureColor)
technique11 DOF4
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_NearBokeh()));
    }
}

// Technique 8: Composite
technique11 DOF5
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_Combine()));
    }
}

// Technique 9: Vertical Gaussian post-blur
technique11 DOF6
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_GaussBlur(float2(0.0, 1.0))));
    }
}

// Technique 10: Horizontal Gaussian post-blur
technique11 DOF7
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_GaussBlur(float2(1.0, 0.0))));
    }
}
