//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                CRT / VHS Display Simulation Addon v3.0                                       //
//                ENB of the Elders — Zain Dana Harper                                          //
//                                                                                              //
//  Comprehensive analog display emulation with two independent pipelines:                      //
//                                                                                              //
//  CRT Pipeline:                                                                               //
//    Screen curvature → Convergence errors → NTSC color → Scanlines (Gaussian beam) →         //
//    Anti-aliased phosphor mask → Phosphor bloom → CRT halation → Corner shadow                //
//                                                                                              //
//  VHS Pipeline:                                                                               //
//    YIQ conversion → Chroma bandwidth limiting → Head switching → Tracking errors →           //
//    Tape noise/dropouts → Scanline jitter → Generation loss                                   //
//                                                                                              //
//  Combined mode: VHS → CRT (VHS tape played on a CRT television)                             //
//                                                                                              //
//  Host: enbeffectpostpass.fx (LDR, R10G10B10A2)                                              //
//  Uses: smpLinear, smpPoint, CineFXVSOutput, VS_CineFX, Timer, ScreenSize, PixelSize        //
//  Available textures: TextureColor, TextureOriginal, RenderTarget128                          //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef EFFECT_CRTSHADER_FXH
#define EFFECT_CRTSHADER_FXH


//=============================================================================//
//                         UI PARAMETERS                                       //
//=============================================================================//

// --- CRT Display ---

bool UICRT_Enable
<
    string UIName = "CRT | Enable CRT Simulation";
> = {false};

float UICRT_Curvature
<
    string UIName = "CRT | Screen Curvature";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.005;
> = {0.05};

float UICRT_Overscan
<
    string UIName = "CRT | Overscan";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.05; float UIStep = 0.001;
> = {0.01};

float UICRT_Brightness
<
    string UIName = "CRT | Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.01;
> = {1.1};

float UICRT_Contrast
<
    string UIName = "CRT | Contrast";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.01;
> = {1.05};

float UICRT_Saturation
<
    string UIName = "CRT | Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {1.15};

float UICRT_ScanIntensity
<
    string UIName = "CRT | Scanline Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UICRT_ScanWidth
<
    string UIName = "CRT | Scanline Width";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 4.0; float UIStep = 0.1;
> = {2.0};

float UICRT_BeamWidth
<
    string UIName = "CRT | Beam Width Modulation (bright=wider)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.50};

int UICRT_MaskType
<
    string UIName = "CRT | Mask Type (0=Grille 1=Slot 2=Shadow)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 2;
> = {0};

float UICRT_MaskIntensity
<
    string UIName = "CRT | Mask Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.40};

float UICRT_MaskScale
<
    string UIName = "CRT | Mask Scale (dot pitch)";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;
> = {1.0};

float UICRT_Bloom
<
    string UIName = "CRT | Phosphor Bloom";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

float UICRT_Convergence
<
    string UIName = "CRT | Convergence Error (px)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.05;
> = {0.0};

float UICRT_Halation
<
    string UIName = "CRT | Halation (inner glass scatter)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.10};

float UICRT_CornerRadius
<
    string UIName = "CRT | Corner Shadow";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.15; float UIStep = 0.001;
> = {0.03};

float UICRT_SignalNoise
<
    string UIName = "CRT | Signal Noise";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.0};

int UICRT_ColorProfile
<
    string UIName = "CRT | Color Profile (0=sRGB 1=P22 2=Trinitron 3=Cool)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 3;
> = {0};


// --- VHS Tape ---

bool UIVHS_Enable
<
    string UIName = "VHS | Enable VHS Simulation";
> = {false};

float UIVHS_ChromaBlur
<
    string UIName = "VHS | Chroma Blur (bandwidth limit)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.60};

float UIVHS_ChromaSmear
<
    string UIName = "VHS | Chroma Smear (rightward bleed)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UIVHS_TrackingNoise
<
    string UIName = "VHS | Tracking Noise";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.10};

float UIVHS_HeadSwitch
<
    string UIName = "VHS | Head Switching (bottom noise)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.30};

float UIVHS_TapeNoise
<
    string UIName = "VHS | Tape Noise (dropouts)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.05};

float UIVHS_Snow
<
    string UIName = "VHS | Static / Snow";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.03};

float UIVHS_Jitter
<
    string UIName = "VHS | Scanline Jitter";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

int UIVHS_Quality
<
    string UIName = "VHS | Quality (0=SP 1=LP 2=EP)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 2;
> = {0};

int UIVHS_Generation
<
    string UIName = "VHS | Copy Generation (0=original)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 5;
> = {0};


//=============================================================================//
//                         COLOR SPACE HELPERS                                 //
//=============================================================================//

float3 CRT_RGBtoYIQ(float3 rgb)
{
    return float3(
        dot(rgb, float3(0.2989, 0.5870, 0.1140)),
        dot(rgb, float3(0.5959, -0.2744, -0.3216)),
        dot(rgb, float3(0.2115, -0.5229, 0.3114))
    );
}

float3 CRT_YIQtoRGB(float3 yiq)
{
    return float3(
        dot(yiq, float3(1.0, 0.9563, 0.6210)),
        dot(yiq, float3(1.0, -0.2721, -0.6474)),
        dot(yiq, float3(1.0, -1.1070, 1.7046))
    );
}

// Fast hash for noise generation (uses host CFX_Hash if available)
float CRT_Hash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(443.8975, 397.2973, 491.1871));
    p3 += dot(p3, p3.yzx + 19.19);
    return frac((p3.x + p3.y) * p3.z);
}


//=============================================================================//
//                         CRT PHOSPHOR MASKS                                  //
//=============================================================================//
//  Anti-aliased versions using saturate + linear ramp instead of hard if/else.
//  Luminance-adaptive fade prevents masks from killing peak brightness.

float3 CRT_ApertureGrille(float2 pixelPos, float intensity, float luma)
{
    float phase = frac(pixelPos.x / 3.0) * 3.0;
    float3 mask;
    mask.r = saturate(1.0 - abs(phase - 0.5) * 2.0);
    mask.g = saturate(1.0 - abs(phase - 1.5) * 2.0);
    mask.b = saturate(1.0 - abs(phase - 2.5) * 2.0);
    // Brightness-adaptive fade: mask weakens on bright pixels
    float adaptedIntensity = intensity * (1.0 - luma * 0.5);
    return lerp(1.0 - adaptedIntensity, 1.0, mask);
}

float3 CRT_SlotMask(float2 pixelPos, float intensity, float luma)
{
    float rowOff = floor(fmod(pixelPos.y, 2.0)) * 1.5;
    float phase = frac((pixelPos.x + rowOff) / 3.0) * 3.0;
    float3 mask;
    mask.r = saturate(1.0 - abs(phase - 0.5) * 2.0);
    mask.g = saturate(1.0 - abs(phase - 1.5) * 2.0);
    mask.b = saturate(1.0 - abs(phase - 2.5) * 2.0);
    // Slot vertical gap
    float slotV = frac(pixelPos.y / 2.0);
    float slotGap = smoothstep(0.4, 0.5, abs(slotV - 0.5));
    mask *= lerp(1.0, 0.7, slotGap);
    float adaptedIntensity = intensity * (1.0 - luma * 0.5);
    return lerp(1.0 - adaptedIntensity * 0.8, 1.0, mask);
}

float3 CRT_ShadowMask(float2 pixelPos, float intensity, float luma)
{
    float2 hex = pixelPos * float2(1.0, 0.866);
    float rowOff = floor(fmod(hex.y, 2.0)) * 0.5;
    float phase = frac((hex.x + rowOff) / 3.0) * 3.0;
    // Circular phosphor dots
    float dist = length(frac(hex) - 0.5);
    float dotMask = smoothstep(0.45, 0.3, dist);
    float3 mask;
    mask.r = (phase < 1.0) ? dotMask : 0.0;
    mask.g = (phase >= 1.0 && phase < 2.0) ? dotMask : 0.0;
    mask.b = (phase >= 2.0) ? dotMask : 0.0;
    float adaptedIntensity = intensity * (1.0 - luma * 0.5);
    return lerp(1.0 - adaptedIntensity * 0.6, 1.0, mask);
}


//=============================================================================//
//                         CRT COLOR PROFILES                                  //
//=============================================================================//
//  Simulate different phosphor chromaticities and color temperatures.

float3 CRT_ApplyColorProfile(float3 color, int profile)
{
    // 0 = sRGB (passthrough), 1 = P22 (warm), 2 = Trinitron (vivid), 3 = Cool White
    if (profile == 1)
    {
        // P22 phosphors: warm, slightly green-shifted
        color *= float3(1.05, 1.02, 0.90);
    }
    else if (profile == 2)
    {
        // Trinitron: vivid, slightly boosted red and green
        color *= float3(1.08, 1.04, 0.95);
        // Slight saturation boost
        float luma = dot(color, K_LUM);
        color = lerp(luma, color, 1.15);
    }
    else if (profile == 3)
    {
        // Cool white (D93, Japanese TVs): blue-shifted
        color *= float3(0.92, 0.97, 1.12);
    }
    return color;
}


//=============================================================================//
//                         CRT CORE FUNCTIONS                                  //
//=============================================================================//

// Barrel distortion with separate X/Y for cylindrical (Trinitron) option
float2 CRT_Curvature(float2 uv, float curvature)
{
    float2 c = uv * 2.0 - 1.0;
    float r2 = dot(c, c);
    c *= 1.0 + r2 * curvature;
    return c * 0.5 + 0.5;
}

// Convergence errors: RGB channel spatial misregistration, worse at edges
float3 CRT_Convergence(float2 uv, float amount)
{
    float2 center = uv - 0.5;
    float edgeFactor = dot(center, center) * 4.0;
    float2 offset = amount * PixelSize * edgeFactor;
    // Red shifts up-left, blue shifts down-right (common CRT misalignment)
    float r = TextureColor.SampleLevel(smpLinear, uv + offset * float2(-0.7, -0.5), 0).r;
    float g = TextureColor.SampleLevel(smpLinear, uv, 0).g;
    float b = TextureColor.SampleLevel(smpLinear, uv + offset * float2(0.5, 0.7), 0).b;
    return float3(r, g, b);
}

// Generalized Gaussian scanline beam profile
// Bright pixels produce wider, more plateau-like beams
float CRT_ScanlineBeam(float dist, float luma, float beamMod)
{
    // Sigma: narrow for dark, wide for bright
    float sigma = lerp(0.35, 0.7, luma * beamMod);
    // Shape: peaked for dark, plateau for bright
    float shape = lerp(2.0, 6.0, luma * beamMod);
    float x = abs(dist) / max(sigma, 0.001);
    return exp(-0.5 * pow(x, shape));
}

// 8-tap cross+diagonal phosphor bloom
float3 CRT_PhosphorBloom(float2 uv, float radius)
{
    float r = radius * PixelSize.x;
    float rv = radius * PixelSize.y;
    float dr = r * 0.7071;
    float drv = rv * 0.7071;

    float3 bloom = 0.0;
    // Cardinal taps (weight 1.0)
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2( r, 0), 0).rgb;
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2(-r, 0), 0).rgb;
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2(0,  rv), 0).rgb;
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2(0, -rv), 0).rgb;
    // Diagonal taps (weight 0.7)
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2( dr,  drv), 0).rgb * 0.7;
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2(-dr,  drv), 0).rgb * 0.7;
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2( dr, -drv), 0).rgb * 0.7;
    bloom += TextureColor.SampleLevel(smpLinear, uv + float2(-dr, -drv), 0).rgb * 0.7;
    bloom /= 6.8; // 4 * 1.0 + 4 * 0.7

    return bloom;
}

// Corner shadow with smooth beam falloff
float CRT_CornerMask(float2 uv, float radius)
{
    float2 edge = smoothstep(0.0, radius, uv) * smoothstep(0.0, radius, 1.0 - uv);
    return pow(edge.x * edge.y, 0.3);
}


//=============================================================================//
//                         VHS CORE FUNCTIONS                                  //
//=============================================================================//

// Chroma bandwidth limiting — horizontal Gaussian blur on I/Q channels
float3 VHS_ChromaBlur(float2 uv, float blurAmount, float genScale)
{
    float3 yiq = CRT_RGBtoYIQ(TextureColor.SampleLevel(smpPoint, uv, 0).rgb);

    float radius = (2.0 + blurAmount * 4.0) * genScale;
    float2 chromaAccum = 0.0;
    float totalW = 0.0;

    [unroll]
    for (int i = -8; i <= 8; i++)
    {
        float w = exp(-0.5 * (float)(i * i) / max(radius * radius, 0.01));
        float2 sUV = uv + float2((float)i * PixelSize.x * 2.0, 0);
        float3 s = CRT_RGBtoYIQ(TextureColor.SampleLevel(smpPoint, sUV, 0).rgb);
        chromaAccum += s.yz * w;
        totalW += w;
    }
    yiq.yz = chromaAccum / totalW;
    return saturate(CRT_YIQtoRGB(yiq));
}

// Rightward chroma smear (tape head scan direction causes one-sided delay)
float3 VHS_ChromaSmear(float3 color, float2 uv, float smearAmount)
{
    float3 yiq = CRT_RGBtoYIQ(color);
    float2 chromaSmeared = 0.0;
    float totalW = 0.0;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float t = (float)i / 8.0;
        float w = exp(-t * 3.0);
        float2 sUV = uv + float2(t * smearAmount * PixelSize.x * 8.0, 0);
        float3 s = CRT_RGBtoYIQ(TextureColor.SampleLevel(smpPoint, sUV, 0).rgb);
        chromaSmeared += s.yz * w;
        totalW += w;
    }
    yiq.yz = chromaSmeared / totalW;
    return saturate(CRT_YIQtoRGB(yiq));
}

// Head switching noise at bottom of frame
float3 VHS_HeadSwitch(float3 color, float2 uv, float time, float intensity)
{
    float switchZone = smoothstep(0.92, 0.98, uv.y);
    if (switchZone < 0.001) return color;

    // Horizontal displacement jitter
    float lineID = floor(uv.y * 480.0);
    float jitter = (frac(sin(dot(float2(lineID, time * 100.0),
                   float2(12.9898, 78.233))) * 43758.5453) - 0.5) * 2.0;
    float2 displaced = uv + float2(jitter * 0.05 * switchZone * intensity, 0);

    // Noise injection
    float noise = frac(sin(dot(uv * 500.0 + time, float2(17.3, 41.7))) * 43758.5453);

    float3 result = TextureColor.SampleLevel(smpLinear, saturate(displaced), 0).rgb;
    result = lerp(result, float3(noise, noise, noise), switchZone * intensity * 0.5);
    return lerp(color, result, switchZone);
}

// Tracking errors — horizontal displacement bands
float2 VHS_Tracking(float2 uv, float time, float severity)
{
    // Random horizontal displacement bands
    float band = step(0.98, frac(sin(floor(uv.y * 50.0 + time * 3.0) * 17.13) * 43758.5));
    float displacement = sin(time * 7.0 + uv.y * 100.0) * severity * band * 0.03;
    return float2(uv.x + displacement, uv.y);
}

// Tape noise — dropout lines, static snow
float3 VHS_TapeNoise(float3 color, float2 uv, float time, float dropoutRate, float snowAmount)
{
    // Dropout lines: random horizontal white/black streaks
    float lineHash = frac(sin(floor(uv.y * 480.0) * 43.17 + floor(time * 30.0) * 17.31) * 43758.5);
    float isDropout = step(1.0 - dropoutRate, lineHash);
    float dropoutBright = step(0.5, frac(lineHash * 7.13));
    float dropoutWidth = frac(lineHash * 3.71) * 0.3 + 0.05;
    float dropoutX = frac(lineHash * 13.37);
    float inDropout = isDropout * step(dropoutX, uv.x) * step(uv.x, dropoutX + dropoutWidth);

    color = lerp(color, float3(dropoutBright, dropoutBright, dropoutBright), inDropout * 0.8);

    // Snow/static overlay
    if (snowAmount > 0.001)
    {
        float snow = frac(sin(dot(uv * ScreenSize.xy + time * 1000.0,
                      float2(12.9898, 78.233))) * 43758.5453);
        color = lerp(color, float3(snow, snow, snow), snowAmount * 0.15);
    }

    return color;
}

// Scanline horizontal jitter (wobble)
float2 VHS_Jitter(float2 uv, float time, float amount)
{
    float lineID = floor(uv.y * 480.0);
    float jit = sin(lineID * 0.7 + time * 50.0) * amount * PixelSize.x * 2.0;
    // Add random component
    jit += (CRT_Hash(float2(lineID, floor(time * 60.0))) - 0.5) * amount * PixelSize.x * 3.0;
    return float2(uv.x + jit, uv.y);
}


//=============================================================================//
//                         MAIN PIXEL SHADER                                   //
//=============================================================================//

float4 PS_CRTDisplay(CineFXVSOutput IN) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    // Early-out if both disabled
    if (!UICRT_Enable && !UIVHS_Enable)
        return float4(color, 1.0);

    float2 uv = IN.texcoord;
    float time = Timer.x;

    // Quality scaling for VHS generation loss
    float genScale = 1.0 + (float)UIVHS_Generation * 0.4;
    float qualityMul = 1.0 + (float)UIVHS_Quality * 0.3;


    //=================================================================
    //  VHS PIPELINE (applied before CRT if both enabled)
    //=================================================================

    [branch] if (UIVHS_Enable)
    {
        // Tracking errors (UV displacement)
        if (UIVHS_TrackingNoise > 0.01)
        {
            uv = VHS_Tracking(uv, time, UIVHS_TrackingNoise * qualityMul);
        }

        // Scanline jitter
        if (UIVHS_Jitter > 0.01)
        {
            uv = VHS_Jitter(uv, time, UIVHS_Jitter * qualityMul * 0.5);
        }

        uv = saturate(uv);

        // Chroma bandwidth limiting (YIQ horizontal blur)
        if (UIVHS_ChromaBlur > 0.01)
        {
            color = VHS_ChromaBlur(uv, UIVHS_ChromaBlur * qualityMul, genScale);
        }
        else
        {
            color = TextureColor.SampleLevel(smpLinear, uv, 0).rgb;
        }

        // Chroma smear (rightward bleed)
        if (UIVHS_ChromaSmear > 0.01)
        {
            color = VHS_ChromaSmear(color, uv, UIVHS_ChromaSmear * qualityMul);
        }

        // Head switching noise
        if (UIVHS_HeadSwitch > 0.01)
        {
            color = VHS_HeadSwitch(color, uv, time, UIVHS_HeadSwitch);
        }

        // Tape noise / dropouts
        if (UIVHS_TapeNoise > 0.001 || UIVHS_Snow > 0.001)
        {
            color = VHS_TapeNoise(color, uv, time,
                                  UIVHS_TapeNoise * genScale,
                                  UIVHS_Snow * genScale);
        }

        // Generation loss: contrast reduction + slight blur
        if (UIVHS_Generation > 0)
        {
            float lossFactor = (float)UIVHS_Generation * 0.08;
            // Contrast reduction
            color = lerp(color, 0.5, lossFactor);
            // Slight desaturation per generation
            float luma = dot(color, K_LUM);
            color = lerp(color, luma, lossFactor * 0.5);
        }

        color = saturate(color);
    }


    //=================================================================
    //  CRT PIPELINE
    //=================================================================

    [branch] if (UICRT_Enable)
    {
        // ---- SCREEN CURVATURE ---- //
        if (UICRT_Curvature > 0.001)
        {
            uv = CRT_Curvature(uv, UICRT_Curvature);
        }

        // ---- OVERSCAN ---- //
        uv = (uv - 0.5) * (1.0 + UICRT_Overscan * 2.0) + 0.5;

        // Out-of-bounds check
        float2 edgeDist = uv * (1.0 - uv);
        if (min(edgeDist.x, edgeDist.y) < 0.0)
            return float4(0.0, 0.0, 0.0, 1.0);

        // Re-sample at distorted UV
        // ---- CONVERGENCE ERRORS ---- //
        if (UICRT_Convergence > 0.01)
        {
            color = CRT_Convergence(uv, UICRT_Convergence);
        }
        else
        {
            color = TextureColor.SampleLevel(smpLinear, uv, 0).rgb;
        }

        // ---- COLOR PROFILE ---- //
        if (UICRT_ColorProfile > 0)
        {
            color = CRT_ApplyColorProfile(color, UICRT_ColorProfile);
        }

        // ---- BRIGHTNESS / CONTRAST / SATURATION ---- //
        color *= UICRT_Brightness;
        color = (color - 0.5) * UICRT_Contrast + 0.5;
        color = max(color, 0.0);

        float luma = dot(color, K_LUM);
        color = lerp(luma, color, UICRT_Saturation);
        color = max(color, 0.0);
        luma = dot(color, K_LUM);

        // ---- SCANLINES (Generalized Gaussian beam) ---- //
        if (UICRT_ScanIntensity > 0.01)
        {
            float screenY = uv.y / PixelSize.y;
            float scanPhase = frac(screenY / UICRT_ScanWidth);
            float scanDist = scanPhase - 0.5; // distance from beam center

            float beamProfile = CRT_ScanlineBeam(scanDist, luma, UICRT_BeamWidth);
            float scanMask = lerp(1.0, beamProfile, UICRT_ScanIntensity);

            color *= scanMask;
        }

        // ---- PHOSPHOR MASK (anti-aliased) ---- //
        if (UICRT_MaskIntensity > 0.01)
        {
            float2 pixelPos = uv / PixelSize / UICRT_MaskScale;
            float3 maskRGB;

            if (UICRT_MaskType == 0)
                maskRGB = CRT_ApertureGrille(pixelPos, UICRT_MaskIntensity, luma);
            else if (UICRT_MaskType == 1)
                maskRGB = CRT_SlotMask(pixelPos, UICRT_MaskIntensity, luma);
            else
                maskRGB = CRT_ShadowMask(pixelPos, UICRT_MaskIntensity, luma);

            color *= maskRGB;
        }

        // ---- PHOSPHOR BLOOM (8-tap) ---- //
        if (UICRT_Bloom > 0.01)
        {
            float3 bloom = CRT_PhosphorBloom(uv, 1.5);
            color = lerp(color, max(color, bloom), UICRT_Bloom);
        }

        // ---- HALATION (inner glass scatter from bloom mip) ---- //
        if (UICRT_Halation > 0.01)
        {
            float3 halation = RenderTarget128.SampleLevel(smpLinear, uv, 0).rgb;
            // Halation is warm-shifted (phosphor re-excitation bias)
            halation *= float3(1.15, 1.0, 0.85);
            color += halation * UICRT_Halation * 0.3;
        }

        // ---- SIGNAL NOISE ---- //
        if (UICRT_SignalNoise > 0.01)
        {
            float noiseVal = CRT_Hash(uv * ScreenSize.xy + time * 1000.0) - 0.5;
            color += noiseVal * UICRT_SignalNoise * 0.15;
        }

        // ---- CORNER SHADOW ---- //
        if (UICRT_CornerRadius > 0.001)
        {
            color *= CRT_CornerMask(uv, UICRT_CornerRadius);
        }

        color = saturate(color);
    }

    return float4(color, 1.0);
}


#endif // EFFECT_CRTSHADER_FXH
