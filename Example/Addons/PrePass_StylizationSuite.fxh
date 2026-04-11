//=============================================================================
//  PrePass_StylizationSuite.fxh — Non-Photorealistic Rendering Suite v1.0
//
//  5 artistic rendering modes for enbeffectprepass.fx:
//    Mode 0: Off (passthrough)
//    Mode 1: Anisotropic Kuwahara (oil painting / painterly)
//    Mode 2: Watercolor (edge-aware blur + pigment + paper texture)
//    Mode 3: Ink Wash / Sumi-e (tonal bands + brush stroke edges)
//    Mode 4: Cross-Hatching (screen-space engraving patterns)
//    Mode 5: Posterization (quantized color + edge outline)
//
//  Placed in prepass so stylized scene receives natural DOF, bloom, and
//  tonemapping — painterly effects with physically-correct lighting on top.
//
//  All modes disabled by default (Mode 0 = Off).
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef PREPASS_STYLIZATION_SUITE_FXH
#define PREPASS_STYLIZATION_SUITE_FXH

#define _STYLIZATION_SUITE_
#define STYLE_LOADED 1


//=== CONSTANTS ===//

static const float3 SLUM = float3(0.2126, 0.7152, 0.0722);
static const float SPI  = 3.14159265;


//=== UI PARAMETERS ===//

// --- Global ---

int ui_StyleMode
<
    string UIName = "STYLE | Mode (0=Off 1=Kuwahara 2=Watercolor 3=Ink 4=Hatch 5=Poster)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 5;
> = {0};

float ui_StyleIntensity
<
    string UIName = "STYLE | Blend Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {1.0};

bool ui_StyleDepthFade
<
    string UIName = "STYLE | Depth Fade (reduce at distance)";
> = {false};

float ui_StyleDepthStart
<
    string UIName = "STYLE | Depth Fade Start";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.0};

float ui_StyleDepthEnd
<
    string UIName = "STYLE | Depth Fade End";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- 01 Kuwahara ---

float ui_KuwaRadius
<
    string UIName = "STYLE 01 KUW | Brush Radius (px)";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 8.0; float UIStep = 0.5;
> = {3.0};

float ui_KuwaSharpness
<
    string UIName = "STYLE 01 KUW | Sector Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 32.0; float UIStep = 0.5;
> = {8.0};

float ui_KuwaEdge
<
    string UIName = "STYLE 01 KUW | Edge Darkening";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

// --- 02 Watercolor ---

float ui_WCBlur
<
    string UIName = "STYLE 02 WC | Pigment Diffusion";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;
> = {2.0};

float ui_WCEdge
<
    string UIName = "STYLE 02 WC | Edge Darkening";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_WCPaper
<
    string UIName = "STYLE 02 WC | Paper Texture";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_WCGranulation
<
    string UIName = "STYLE 02 WC | Pigment Granulation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.2};

// --- 03 Ink Wash ---

int ui_InkLevels
<
    string UIName = "STYLE 03 INK | Tonal Levels (2-8)";
    string UIWidget = "Spinner";
    int UIMin = 2; int UIMax = 8;
> = {5};

float ui_InkEdge
<
    string UIName = "STYLE 03 INK | Stroke Width";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {1.0};

float ui_InkThreshold
<
    string UIName = "STYLE 03 INK | Edge Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 0.5; float UIStep = 0.01;
> = {0.1};

float3 ui_InkTint
<
    string UIName = "STYLE 03 INK | Wash Tint";
    string UIWidget = "Color";
> = {0.15, 0.12, 0.1};

float ui_InkPaper
<
    string UIName = "STYLE 03 INK | Paper Tone";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 1.0; float UIStep = 0.01;
> = {0.95};

// --- 04 Cross-Hatching ---

float ui_HatchDensity
<
    string UIName = "STYLE 04 HATCH | Line Density";
    string UIWidget = "Spinner";
    float UIMin = 20.0; float UIMax = 200.0; float UIStep = 5.0;
> = {80.0};

float ui_HatchThickness
<
    string UIName = "STYLE 04 HATCH | Line Thickness";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 0.8; float UIStep = 0.01;
> = {0.4};

int ui_HatchLayers
<
    string UIName = "STYLE 04 HATCH | Max Layers (1-4)";
    string UIWidget = "Spinner";
    int UIMin = 1; int UIMax = 4;
> = {3};

float3 ui_HatchInk
<
    string UIName = "STYLE 04 HATCH | Ink Color";
    string UIWidget = "Color";
> = {0.1, 0.08, 0.06};

float3 ui_HatchPaper
<
    string UIName = "STYLE 04 HATCH | Paper Color";
    string UIWidget = "Color";
> = {0.95, 0.92, 0.85};

// --- 05 Posterization ---

int ui_PosterLevels
<
    string UIName = "STYLE 05 POST | Color Levels (2-16)";
    string UIWidget = "Spinner";
    int UIMin = 2; int UIMax = 16;
> = {6};

float ui_PosterSoft
<
    string UIName = "STYLE 05 POST | Edge Softness";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

bool ui_PosterOutline
<
    string UIName = "STYLE 05 POST | Edge Outline";
> = {true};

float ui_PosterOutlineStr
<
    string UIName = "STYLE 05 POST | Outline Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {1.0};


//=== NOISE HELPERS ===//

float StyleHash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float StyleNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = StyleHash(i);
    float b = StyleHash(i + float2(1, 0));
    float c = StyleHash(i + float2(0, 1));
    float d = StyleHash(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float StyleFBM(float2 p)
{
    return StyleNoise(p) * 0.667 + StyleNoise(p * 2.03) * 0.333;
}


//=== EDGE DETECTION ===//

// Sobel returning gradient magnitude + orientation angle
void StyleSobel(float2 uv, out float mag, out float angle)
{
    float tl = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-PixelSize.x, -PixelSize.y), 0).rgb, SLUM);
    float tc = dot(TextureColor.SampleLevel(smpLinear, uv + float2(           0, -PixelSize.y), 0).rgb, SLUM);
    float tr = dot(TextureColor.SampleLevel(smpLinear, uv + float2( PixelSize.x, -PixelSize.y), 0).rgb, SLUM);
    float ml = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-PixelSize.x,            0), 0).rgb, SLUM);
    float mr = dot(TextureColor.SampleLevel(smpLinear, uv + float2( PixelSize.x,            0), 0).rgb, SLUM);
    float bl = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-PixelSize.x,  PixelSize.y), 0).rgb, SLUM);
    float bc = dot(TextureColor.SampleLevel(smpLinear, uv + float2(           0,  PixelSize.y), 0).rgb, SLUM);
    float br = dot(TextureColor.SampleLevel(smpLinear, uv + float2( PixelSize.x,  PixelSize.y), 0).rgb, SLUM);

    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);

    mag = sqrt(gx * gx + gy * gy);

    // Structure tensor eigenanalysis → smooth orientation
    float Sxx = gx * gx;
    float Syy = gy * gy;
    float Sxy = gx * gy;
    angle = 0.5 * atan2(2.0 * Sxy, Sxx - Syy);
}


//=== MODE 1: ANISOTROPIC KUWAHARA ===//
//
// Structure-tensor-guided sector filter. 8 sectors oriented by local gradient.
// Each sector: 3 ring samples along sector center direction.
// Sectors blended by inverse variance (lowest variance = smoothest = preferred).
// Produces oil-painting-like brush strokes aligned to image features.

float3 Style_Kuwahara(float2 uv, float3 original)
{
    float edgeMag, orient;
    StyleSobel(uv, edgeMag, orient);

    float radius = ui_KuwaRadius;
    float sharpQ = ui_KuwaSharpness;
    float sectorArc = SPI * 0.25; // 2π/8 = π/4

    float3 result = 0.0;
    float totalW = 0.0;

    [unroll]
    for (int sec = 0; sec < 8; sec++)
    {
        float sAngle = orient + (float)sec * sectorArc;

        float3 sum = original;
        float3 sumSq = original * original;
        float count = 1.0;

        // 3 radial ring samples per sector
        [unroll]
        for (int ring = 1; ring <= 3; ring++)
        {
            float r = (float)ring / 3.0 * radius;
            float2 off = float2(cos(sAngle), sin(sAngle)) * r * PixelSize;
            float3 c = TextureColor.SampleLevel(smpLinear, uv + off, 0).rgb;
            sum += c;
            sumSq += c * c;
            count += 1.0;
        }

        float3 mean = sum / count;
        float3 var = sumSq / count - mean * mean;
        float v = dot(max(var, 0.0), float3(1.0, 1.0, 1.0));

        // Lower variance = smoother region = higher weight
        float w = 1.0 / (1.0 + v * sharpQ);
        result += mean * w;
        totalW += w;
    }

    result /= max(totalW, 0.001);

    // Edge darkening: darken along detected edges for painterly outline
    float edgeDark = saturate(edgeMag * 4.0) * ui_KuwaEdge;
    result *= (1.0 - edgeDark);

    return result;
}


//=== MODE 2: WATERCOLOR ===//
//
// Bilateral blur (pigment diffusion) + edge darkening (pigment pooling) +
// paper texture (procedural) + pigment granulation (noise in valleys).

float3 Style_Watercolor(float2 uv, float3 original)
{
    float centerL = dot(original, SLUM);

    // Bilateral blur: 5x5 weighted by luminance similarity
    float3 blur = 0.0;
    float tw = 0.0;

    [unroll]
    for (int y = -2; y <= 2; y++)
    {
        [unroll]
        for (int x = -2; x <= 2; x++)
        {
            float2 off = float2(x, y) * PixelSize * ui_WCBlur;
            float3 s = TextureColor.SampleLevel(smpLinear, uv + off, 0).rgb;
            float sL = dot(s, SLUM);

            float sw = exp(-0.5 * (float)(x * x + y * y) / 4.0);
            float lw = exp(-abs(sL - centerL) * 8.0);
            float w = sw * lw;

            blur += s * w;
            tw += w;
        }
    }
    blur /= max(tw, 0.001);

    float3 color = blur;

    // Edge darkening (pigment pools at boundaries)
    float edgeMag, edgeAngle;
    StyleSobel(uv, edgeMag, edgeAngle);
    float edgeDark = saturate(edgeMag * 6.0) * ui_WCEdge;
    color *= (1.0 - edgeDark);

    // Paper texture: procedural noise grain
    float2 paperUV = uv * float2(ScreenSize.x, ScreenSize.x * ScreenSize.w) * 0.5;
    float paper = StyleFBM(paperUV);
    float paperMask = lerp(1.0, 0.85 + paper * 0.3, ui_WCPaper);
    // Paper shows more in light areas (pigment thins out)
    float lightArea = saturate(centerL * 2.0 - 0.5);
    color *= lerp(1.0, paperMask, lightArea);

    // Pigment granulation: noise settles in paper texture valleys
    float grain = StyleNoise(paperUV * 3.0) * 2.0 - 1.0;
    float grainMask = (1.0 - paper) * ui_WCGranulation;
    color += color * grain * grainMask * 0.3;

    return max(color, 0.0);
}


//=== MODE 3: INK WASH / SUMI-E ===//
//
// Luminance quantization into tonal bands with soft transitions,
// Sobel edge detection for brush stroke outlines,
// directional noise for ink texture, paper tone bleed-through.

float3 Style_InkWash(float2 uv, float3 original)
{
    float luma = dot(original, SLUM);
    float levels = (float)ui_InkLevels;

    // Quantize luminance to N bands with soft transitions
    float quantized = floor(luma * levels + 0.5) / levels;
    // Soft blend between bands
    float blend = frac(luma * levels + 0.5);
    float soft = smoothstep(0.2, 0.8, blend);
    float lower = floor(luma * levels) / levels;
    float upper = lower + 1.0 / levels;
    float inkLuma = lerp(lower, upper, soft);

    // Edge detection for brush stroke outlines
    float edgeMag, edgeAngle;
    StyleSobel(uv, edgeMag, edgeAngle);
    float edge = saturate(edgeMag / max(ui_InkThreshold, 0.01)) * ui_InkEdge;

    // Ink texture: directional noise for brush-like quality
    float2 inkUV = uv * float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);
    float inkNoise = StyleNoise(inkUV * 0.3 + float2(edgeAngle, edgeAngle) * 2.0);
    float inkTexture = lerp(1.0, 0.7 + inkNoise * 0.6, 0.3);

    // Compose: paper base tinted by wash, darkened by ink density
    float3 paper = ui_InkPaper;
    float3 ink = ui_InkTint;

    // Ink density increases with darkness
    float inkDensity = 1.0 - inkLuma;
    float3 color = lerp(paper, ink, inkDensity * inkTexture);

    // Apply edge strokes (always dark ink)
    color = lerp(color, ink * 0.5, edge);

    return color;
}


//=== MODE 4: CROSS-HATCHING ===//
//
// Screen-space hatching patterns modulated by luminance.
// Darker areas get more hatching layers at different angles.
// Uses surface normals (when available) to orient hatching.

float Style_HatchPattern(float2 uv, float angle, float density, float thickness)
{
    float c = cos(angle);
    float s = sin(angle);
    float2 rotUV = float2(
        uv.x * c - uv.y * s,
        uv.x * s + uv.y * c
    );
    float hatchLine = abs(frac(rotUV.x * density) - 0.5) * 2.0;
    return smoothstep(thickness, thickness + 0.15, hatchLine);
}

float3 Style_CrossHatch(float2 uv, float3 original)
{
    float luma = dot(original, SLUM);

    // Screen-space coordinate for consistent pattern
    float2 screenUV = uv * float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);
    float density = ui_HatchDensity / 100.0;
    float thick = ui_HatchThickness;

    // Hatching layers at different angles for increasing darkness
    // Layer 1: 45 degrees (light shadow)
    // Layer 2: -45 degrees (medium shadow, cross pattern)
    // Layer 3: 30 degrees (dark shadow)
    // Layer 4: -60 degrees (deepest shadow)
    float hatch = 1.0;
    int maxLayers = ui_HatchLayers;

    // Layer 1: activates below 0.75 luminance
    if (maxLayers >= 1 && luma < 0.75)
    {
        float mask = saturate((0.75 - luma) * 4.0);
        float h = Style_HatchPattern(screenUV, 0.785, density, thick);
        hatch *= lerp(1.0, h, mask);
    }

    // Layer 2: activates below 0.5
    if (maxLayers >= 2 && luma < 0.50)
    {
        float mask = saturate((0.50 - luma) * 4.0);
        float h = Style_HatchPattern(screenUV, -0.785, density * 1.1, thick);
        hatch *= lerp(1.0, h, mask);
    }

    // Layer 3: activates below 0.3
    if (maxLayers >= 3 && luma < 0.30)
    {
        float mask = saturate((0.30 - luma) * 5.0);
        float h = Style_HatchPattern(screenUV, 0.524, density * 0.9, thick * 0.9);
        hatch *= lerp(1.0, h, mask);
    }

    // Layer 4: activates below 0.15
    if (maxLayers >= 4 && luma < 0.15)
    {
        float mask = saturate((0.15 - luma) * 8.0);
        float h = Style_HatchPattern(screenUV, -1.047, density * 1.2, thick * 0.8);
        hatch *= lerp(1.0, h, mask);
    }

    return lerp(ui_HatchInk, ui_HatchPaper, hatch);
}


//=== MODE 5: POSTERIZATION ===//
//
// Color quantization with soft-edge transitions + optional Sobel outline.

float3 Style_Posterize(float2 uv, float3 original)
{
    float levels = (float)ui_PosterLevels;
    float softness = ui_PosterSoft;

    // Quantize each channel
    float3 quantized = floor(original * levels + 0.5) / levels;

    // Soft transitions: blend between quantized and original near boundaries
    float3 frac_part = frac(original * levels + 0.5);
    float3 soft_mask = smoothstep(0.5 - softness * 0.5, 0.5 + softness * 0.5, frac_part);
    float3 lower = floor(original * levels) / levels;
    float3 upper = lower + 1.0 / levels;
    float3 color = lerp(lower, upper, soft_mask);

    // Edge outline overlay
    if (ui_PosterOutline)
    {
        float edgeMag, edgeAngle;
        StyleSobel(uv, edgeMag, edgeAngle);
        float outline = saturate(edgeMag * 10.0) * ui_PosterOutlineStr;
        color *= (1.0 - outline);
    }

    return color;
}


//=== PIXEL SHADER ===//

float4 PS_Stylize(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 original = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    if (ui_StyleMode == 0) return float4(original, 1.0);

    float3 result = original;

    [branch] if (ui_StyleMode == 1) result = Style_Kuwahara(txcoord, original);
    [branch] if (ui_StyleMode == 2) result = Style_Watercolor(txcoord, original);
    [branch] if (ui_StyleMode == 3) result = Style_InkWash(txcoord, original);
    [branch] if (ui_StyleMode == 4) result = Style_CrossHatch(txcoord, original);
    [branch] if (ui_StyleMode == 5) result = Style_Posterize(txcoord, original);

    // Depth fade: reduce stylization at distance
    float intensity = ui_StyleIntensity;
    if (ui_StyleDepthFade)
    {
        float depth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;
        float depthFade = 1.0 - saturate((depth - ui_StyleDepthStart) /
                          max(ui_StyleDepthEnd - ui_StyleDepthStart, 0.001));
        intensity *= depthFade;
    }

    return float4(lerp(original, result, intensity), 1.0);
}


#endif // PREPASS_STYLIZATION_SUITE_FXH
