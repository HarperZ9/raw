//=============================================================================
//  PrePass_PhotoStudio.fxh — Screenshot Composition & Exposure Tools
//
//  1-pass addon for enbeffectprepass.fx providing in-game photography aids:
//    1. Composition Guides — Rule of Thirds, Golden Ratio, Diagonal, Center
//    2. Focus Peaking — Laplacian depth-edge detection with color overlay
//    3. Zebra Stripes — Animated diagonal stripes on overexposed pixels
//    4. Luminance Histogram — Approximate 32-bin bar chart overlay
//    5. Preview Tints — B&W, Sepia, High Contrast previews
//
//  All features independently toggled. Default: everything off.
//  HDR-safe (prepass runs in float16). No SkyrimBridge dependency.
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef _PREPASS_PHOTOSTUDIO_
#define _PREPASS_PHOTOSTUDIO_

#define PHOTO_LOADED 1


//=== UI PARAMETERS ===//

// --- Master ---

int ui_Photo_Sep0
<
    string UIName = "===== PHOTO STUDIO =====";
    int UIMin = 0; int UIMax = 0;
> = {0};

bool ui_PhotoEnable
<
    string UIName = "PHOTO | Master Enable";
> = {false};

// --- Composition Guides ---

int ui_PhotoGuide
<
    string UIName = "PHOTO | Guide (0=Off 1=Thirds 2=Golden 3=Diagonal 4=Center)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 4;
> = {0};

float3 ui_PhotoGuideColor
<
    string UIName = "PHOTO | Guide Line Color";
    string UIWidget = "Color";
> = {1.0, 1.0, 1.0};

float ui_PhotoGuideOpacity
<
    string UIName = "PHOTO | Guide Opacity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_PhotoGuideWidth
<
    string UIName = "PHOTO | Guide Line Width (px)";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;
> = {1.2};

// --- Focus Peaking ---

bool ui_FocusPeakEnable
<
    string UIName = "PHOTO | Focus Peaking Enable";
> = {false};

float3 ui_FocusPeakColor
<
    string UIName = "PHOTO | Focus Peaking Color";
    string UIWidget = "Color";
> = {1.0, 0.1, 0.1};

float ui_FocusPeakThreshold
<
    string UIName = "PHOTO | Focus Peaking Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.001; float UIMax = 0.1; float UIStep = 0.001;
> = {0.015};

float ui_FocusPeakRange
<
    string UIName = "PHOTO | Focus Peaking Depth Range";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- Zebra Stripes ---

bool ui_ZebraEnable
<
    string UIName = "PHOTO | Zebra Stripes Enable";
> = {false};

float ui_ZebraThreshold
<
    string UIName = "PHOTO | Zebra Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 1.0; float UIStep = 0.01;
> = {0.9};

// --- Histogram ---

bool ui_HistogramEnable
<
    string UIName = "PHOTO | Histogram Enable";
> = {false};

float ui_HistogramSize
<
    string UIName = "PHOTO | Histogram Size";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 0.4; float UIStep = 0.01;
> = {0.2};

float ui_HistogramOpacity
<
    string UIName = "PHOTO | Histogram Opacity";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 1.0; float UIStep = 0.01;
> = {0.7};

// --- Preview Tint ---

int ui_PhotoTint
<
    string UIName = "PHOTO | Tint (0=Off 1=B&W 2=Sepia 3=HiContrast)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 3;
> = {0};


//=== CONSTANTS ===//

static const float3 PHOTO_LUM = float3(0.2126, 0.7152, 0.0722);
static const float  PHOTO_PHI = 0.6180339887;  // 1 / golden ratio

// Histogram config
#define HIST_BINS     32
#define HIST_SAMPLES  16   // NxN grid of scene samples (256 total)


//=== LINE DRAWING ===//

// Anti-aliased line from distance field (pixel-space distance)
float PhotoDrawLine(float pixelDist, float widthPx)
{
    float halfW = widthPx * 0.5;
    return 1.0 - saturate((pixelDist - halfW) / 1.5);
}

// Distance from point to line segment (in UV space)
float PhotoDistToSegment(float2 p, float2 a, float2 b)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float t = saturate(dot(pa, ba) / dot(ba, ba));
    return length(p - (a + t * ba));
}


//=== COMPOSITION GUIDES ===//

// Convert UV-space distance to approximate pixel distance
float PhotoUVToPixelH(float uvDist)
{
    return uvDist / PixelSize.x;
}

float PhotoUVToPixelV(float uvDist)
{
    return uvDist / PixelSize.y;
}

// Rule of thirds: 2H + 2V lines + 4 intersection dots
float Photo_RuleOfThirds(float2 uv, float lineW)
{
    float mask = 0.0;

    // Vertical lines at 1/3 and 2/3
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelH(abs(uv.x - 1.0 / 3.0)), lineW));
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelH(abs(uv.x - 2.0 / 3.0)), lineW));

    // Horizontal lines at 1/3 and 2/3
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelV(abs(uv.y - 1.0 / 3.0)), lineW));
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelV(abs(uv.y - 2.0 / 3.0)), lineW));

    // Power points (intersection circles)
    float dotRadius = lineW * 2.5;
    float2 pts[4] = {
        float2(1.0/3.0, 1.0/3.0), float2(2.0/3.0, 1.0/3.0),
        float2(1.0/3.0, 2.0/3.0), float2(2.0/3.0, 2.0/3.0)
    };

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 diff = (uv - pts[i]) / PixelSize;
        float dist = length(diff);
        mask = max(mask, PhotoDrawLine(dist, dotRadius));
    }

    return mask;
}

// Golden ratio (Phi grid): lines at ~0.382 and ~0.618
float Photo_GoldenRatio(float2 uv, float lineW)
{
    float mask = 0.0;

    mask = max(mask, PhotoDrawLine(PhotoUVToPixelH(abs(uv.x - PHOTO_PHI)), lineW));
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelH(abs(uv.x - (1.0 - PHOTO_PHI))), lineW));
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelV(abs(uv.y - PHOTO_PHI)), lineW));
    mask = max(mask, PhotoDrawLine(PhotoUVToPixelV(abs(uv.y - (1.0 - PHOTO_PHI))), lineW));

    return mask;
}

// Diagonal method: baroque + sinister diagonals with reciprocals
float Photo_Diagonal(float2 uv, float lineW)
{
    float mask = 0.0;

    // Main diagonals (full screen corners)
    float d1 = PhotoDistToSegment(uv, float2(0, 0), float2(1, 1));
    float d2 = PhotoDistToSegment(uv, float2(0, 1), float2(1, 0));

    // Convert to approximate pixel distance using average of H/V
    float avgPixel = 0.5 * (1.0 / PixelSize.x + 1.0 / PixelSize.y);
    mask = max(mask, PhotoDrawLine(d1 * avgPixel, lineW));
    mask = max(mask, PhotoDrawLine(d2 * avgPixel, lineW));

    // Reciprocal diagonals (shorter, from corners perpendicular to main)
    float d3 = PhotoDistToSegment(uv, float2(0, 0), float2(0.5, 1.0));
    float d4 = PhotoDistToSegment(uv, float2(1, 0), float2(0.5, 1.0));
    float d5 = PhotoDistToSegment(uv, float2(0, 1), float2(0.5, 0.0));
    float d6 = PhotoDistToSegment(uv, float2(1, 1), float2(0.5, 0.0));

    mask = max(mask, PhotoDrawLine(d3 * avgPixel, lineW * 0.6));
    mask = max(mask, PhotoDrawLine(d4 * avgPixel, lineW * 0.6));
    mask = max(mask, PhotoDrawLine(d5 * avgPixel, lineW * 0.6));
    mask = max(mask, PhotoDrawLine(d6 * avgPixel, lineW * 0.6));

    return mask;
}

// Center cross with action-safe (95%) and title-safe (90%) rectangles
float Photo_CenterCross(float2 uv, float lineW)
{
    float mask = 0.0;
    float crossExtent = 0.04;  // 4% of screen

    // Center crosshair — horizontal arm
    float hDist = PhotoUVToPixelV(abs(uv.y - 0.5));
    float vDist = PhotoUVToPixelH(abs(uv.x - 0.5));

    if (abs(uv.x - 0.5) < crossExtent)
        mask = max(mask, PhotoDrawLine(vDist, lineW) * saturate(1.0 - hDist / (crossExtent / PixelSize.y)));

    // Center crosshair — vertical arm
    if (abs(uv.y - 0.5) < crossExtent)
        mask = max(mask, PhotoDrawLine(hDist, lineW) * saturate(1.0 - vDist / (crossExtent / PixelSize.x)));

    // Center dot
    float centerDist = length((uv - 0.5) / PixelSize);
    mask = max(mask, PhotoDrawLine(centerDist, lineW * 2.0));

    // Action safe (95% — 2.5% margin)
    float safeA = 0.025;
    float aL = PhotoUVToPixelH(abs(uv.x - safeA));
    float aR = PhotoUVToPixelH(abs(uv.x - (1.0 - safeA)));
    float aT = PhotoUVToPixelV(abs(uv.y - safeA));
    float aB = PhotoUVToPixelV(abs(uv.y - (1.0 - safeA)));

    // Draw safe zone borders only when inside the safe margin band
    if (uv.y > safeA && uv.y < (1.0 - safeA))
    {
        mask = max(mask, PhotoDrawLine(aL, lineW * 0.5));
        mask = max(mask, PhotoDrawLine(aR, lineW * 0.5));
    }
    if (uv.x > safeA && uv.x < (1.0 - safeA))
    {
        mask = max(mask, PhotoDrawLine(aT, lineW * 0.5));
        mask = max(mask, PhotoDrawLine(aB, lineW * 0.5));
    }

    // Title safe (90% — 5% margin), thinner lines
    float safeT = 0.05;
    float tL = PhotoUVToPixelH(abs(uv.x - safeT));
    float tR = PhotoUVToPixelH(abs(uv.x - (1.0 - safeT)));
    float tT = PhotoUVToPixelV(abs(uv.y - safeT));
    float tB = PhotoUVToPixelV(abs(uv.y - (1.0 - safeT)));

    if (uv.y > safeT && uv.y < (1.0 - safeT))
    {
        mask = max(mask, PhotoDrawLine(tL, lineW * 0.7));
        mask = max(mask, PhotoDrawLine(tR, lineW * 0.7));
    }
    if (uv.x > safeT && uv.x < (1.0 - safeT))
    {
        mask = max(mask, PhotoDrawLine(tT, lineW * 0.7));
        mask = max(mask, PhotoDrawLine(tB, lineW * 0.7));
    }

    return mask;
}


//=== FOCUS PEAKING ===//
//
// Laplacian edge detection on the depth buffer.
// Highlights in-focus edges (strong depth discontinuities within a
// configurable depth range) with a colored overlay.

float Photo_FocusPeaking(float2 uv)
{
    float cD = GetLinearDepth(uv);

    // Skip sky
    if (cD > 0.99) return 0.0;

    // 3x3 Laplacian kernel on depth
    float dL = GetLinearDepth(uv + float2(-PixelSize.x,  0));
    float dR = GetLinearDepth(uv + float2( PixelSize.x,  0));
    float dU = GetLinearDepth(uv + float2( 0, -PixelSize.y));
    float dD = GetLinearDepth(uv + float2( 0,  PixelSize.y));

    // Laplacian: sum of neighbors minus 4x center
    float laplacian = abs(dL + dR + dU + dD - 4.0 * cD);

    // Normalize by depth to make threshold depth-independent
    float normalizedEdge = laplacian / max(cD, 0.001);

    // Threshold: only highlight edges stronger than the user threshold
    float edgeMask = saturate((normalizedEdge - ui_FocusPeakThreshold) /
                    max(ui_FocusPeakThreshold, 0.001));

    // Depth range masking: only show peaking within the focus range
    // Focus plane is approximated as center-screen depth
    float focusDepth = GetLinearDepth(float2(0.5, 0.5));
    float depthDiff = abs(cD - focusDepth);
    float rangeMask = 1.0 - saturate(depthDiff / max(ui_FocusPeakRange, 0.001));

    return edgeMask * rangeMask;
}


//=== ZEBRA STRIPES ===//
//
// Animated diagonal stripes overlaid on pixels exceeding the luminance
// threshold. Classic videography tool for exposure monitoring.

float Photo_ZebraStripes(float2 uv, float3 color)
{
    float luma = dot(color, PHOTO_LUM);

    if (luma < ui_ZebraThreshold) return 0.0;

    // Screen-space pixel coordinate for stripe pattern
    float2 screenPos = uv / PixelSize;

    // Animated diagonal stripe: 45-degree lines scrolling over time
    float stripe = frac((screenPos.x + screenPos.y) * 0.05 + Timer.x * 2.0);

    // Square wave: 50% duty cycle
    float pattern = step(0.5, stripe);

    // Intensity ramps with how far over threshold
    float overExposure = saturate((luma - ui_ZebraThreshold) /
                         max(1.0 - ui_ZebraThreshold, 0.001));

    return pattern * lerp(0.4, 0.8, overExposure);
}


//=== LUMINANCE HISTOGRAM ===//
//
// Approximate histogram built in the pixel shader by sampling the scene
// at a reduced grid. Not compute-shader accurate, but sufficient for a
// visual exposure reference. Each pixel in the histogram overlay region
// checks which bin it falls into and compares against the sampled count.

float Photo_Histogram(float2 uv, out float3 histColor)
{
    histColor = float3(0.8, 0.8, 0.8);

    // Histogram placement: bottom-left corner
    float hSize = ui_HistogramSize;
    float margin = 0.02;

    float2 hOrigin = float2(margin, 1.0 - margin - hSize);
    float2 hEnd    = float2(margin + hSize * 1.5, 1.0 - margin);

    // Check if pixel is within histogram region
    if (uv.x < hOrigin.x || uv.x > hEnd.x ||
        uv.y < hOrigin.y || uv.y > hEnd.y)
        return 0.0;

    // Normalized position within histogram [0,1]
    float2 hUV = (uv - hOrigin) / (hEnd - hOrigin);

    // Build histogram: sample scene in a grid
    float bins[HIST_BINS];

    [unroll]
    for (int b = 0; b < HIST_BINS; b++)
        bins[b] = 0.0;

    float totalSamples = 0.0;

    [loop]
    for (int sy = 0; sy < HIST_SAMPLES; sy++)
    {
        [loop]
        for (int sx = 0; sx < HIST_SAMPLES; sx++)
        {
            float2 sampleUV = float2(
                (float(sx) + 0.5) / float(HIST_SAMPLES),
                (float(sy) + 0.5) / float(HIST_SAMPLES)
            );

            float3 sampleColor = TextureColor.SampleLevel(smpLinear, sampleUV, 0).rgb;
            float sampleLuma = saturate(dot(sampleColor, PHOTO_LUM));

            int bin = clamp(int(sampleLuma * float(HIST_BINS)), 0, HIST_BINS - 1);
            bins[bin] += 1.0;
            totalSamples += 1.0;
        }
    }

    // Find max bin for normalization
    float maxBin = 1.0;

    [unroll]
    for (int m = 0; m < HIST_BINS; m++)
        maxBin = max(maxBin, bins[m]);

    // Determine which bin this pixel column maps to
    int currentBin = clamp(int(hUV.x * float(HIST_BINS)), 0, HIST_BINS - 1);
    float binHeight = bins[currentBin] / maxBin;

    // Y axis: 0 = bottom, 1 = top — flip since UV.y increases downward
    float barY = 1.0 - hUV.y;

    if (barY > binHeight)
    {
        // Background: dark translucent
        histColor = float3(0.05, 0.05, 0.05);
        return 0.5;
    }

    // Color the bar by luminance zone
    float binLuma = (float(currentBin) + 0.5) / float(HIST_BINS);

    if (binLuma < 0.15)
        histColor = float3(0.2, 0.3, 0.8);   // Shadows: blue tint
    else if (binLuma > 0.85)
        histColor = float3(0.9, 0.3, 0.2);   // Highlights: red tint
    else
        histColor = float3(0.7, 0.7, 0.7);   // Midtones: neutral

    return 1.0;
}


//=== PREVIEW TINTS ===//

float3 Photo_ApplyTint(float3 color, int mode)
{
    float luma = dot(color, PHOTO_LUM);

    if (mode == 1)
    {
        // B&W Preview: full desaturation preserving luminance
        return float3(luma, luma, luma);
    }

    if (mode == 2)
    {
        // Sepia: desaturate then apply warm tint
        float3 sepia = float3(
            luma * 1.05,
            luma * 0.88,
            luma * 0.68
        );
        return sepia;
    }

    if (mode == 3)
    {
        // High Contrast Preview: S-curve on luminance, partial desat
        // Attempt to boost the contrast curve to preview dynamic range
        float contrast = saturate(luma);
        // Smooth S-curve: hermite interpolation centered at 0.5
        contrast = contrast * contrast * (3.0 - 2.0 * contrast);
        // Apply a second pass for stronger curve
        contrast = contrast * contrast * (3.0 - 2.0 * contrast);

        // Blend: keep some original color, boost contrast
        float3 result = lerp(float3(contrast, contrast, contrast), color, 0.3);
        // Re-apply the contrast-adjusted luminance
        float resultLuma = dot(result, PHOTO_LUM);
        if (resultLuma > 0.001)
            result *= contrast / resultLuma;
        return saturate(result);
    }

    return color;
}


//=== PIXEL SHADER ===//

float4 PS_PhotoStudio(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    // Early out: master toggle off
    if (!ui_PhotoEnable) return float4(color, 1.0);


    // --- Preview Tint ---
    // Applied first so all overlays draw on top of the tinted image.

    [branch] if (ui_PhotoTint > 0)
    {
        color = Photo_ApplyTint(color, ui_PhotoTint);
    }


    // --- Focus Peaking ---

    [branch] if (ui_FocusPeakEnable)
    {
        float peakMask = Photo_FocusPeaking(txcoord);
        color = lerp(color, ui_FocusPeakColor, saturate(peakMask));
    }


    // --- Zebra Stripes ---

    [branch] if (ui_ZebraEnable)
    {
        float zebraMask = Photo_ZebraStripes(txcoord, color);
        // Zebra: alternate between red warning and scene color
        float3 zebraColor = float3(1.0, 0.0, 0.0);
        color = lerp(color, zebraColor, zebraMask);
    }


    // --- Histogram Overlay ---

    [branch] if (ui_HistogramEnable)
    {
        float3 histColor;
        float histMask = Photo_Histogram(txcoord, histColor);
        if (histMask > 0.0)
        {
            color = lerp(color, histColor, histMask * ui_HistogramOpacity);
        }
    }


    // --- Composition Guides ---
    // Drawn last so they are always visible on top of everything.

    [branch] if (ui_PhotoGuide > 0)
    {
        float guideMask = 0.0;
        float lineW = ui_PhotoGuideWidth;

        if (ui_PhotoGuide == 1)      guideMask = Photo_RuleOfThirds(txcoord, lineW);
        else if (ui_PhotoGuide == 2) guideMask = Photo_GoldenRatio(txcoord, lineW);
        else if (ui_PhotoGuide == 3) guideMask = Photo_Diagonal(txcoord, lineW);
        else if (ui_PhotoGuide == 4) guideMask = Photo_CenterCross(txcoord, lineW);

        float guideAlpha = saturate(guideMask) * ui_PhotoGuideOpacity;
        color = lerp(color, ui_PhotoGuideColor, guideAlpha);
    }


    return float4(color, 1.0);
}


//=== TECHNIQUE MACRO ===//
// Used by PrePassAddonTechniques.fxh codegen for dev builds.

#define PHOTO_TECH(name, p1) \
technique11 name##p1 <string UIName="Photo Studio";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_PhotoStudio())); } }


#endif // _PREPASS_PHOTOSTUDIO_
