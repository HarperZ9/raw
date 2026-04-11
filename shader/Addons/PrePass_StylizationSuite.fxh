//=============================================================================
//  PrePass_StylizationSuite.fxh — Artistic Stylization Filters
//
//  2-pass addon for enbeffectprepass.fx:
//    Pass A: Compute stylized image (Kuwahara / Oil Paint / Watercolor)
//    Pass B: Blend stylized result with original scene
//
//  Implements an anisotropic Kuwahara filter using structure tensor analysis
//  to align filter kernels with local edge orientation. This produces
//  painterly results that respect edge structure.
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef PREPASS_STYLIZATION_SUITE_FXH
#define PREPASS_STYLIZATION_SUITE_FXH

#define STYLE_LOADED 1


//=== UI PARAMETERS ===//

int ui_Style_Sep0
<
    string UIName = "===== STYLIZATION =====";
    int UIMin = 0; int UIMax = 0;
> = {0};

bool ui_Style_Enable
<
    string UIName = "Style | Enable";
> = {false};

int ui_Style_Mode
<
    string UIName = "Style | Mode (0=Kuwahara 1=OilPaint 2=Watercolor)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 2;
> = {0};

float ui_Style_Radius
<
    string UIName = "Style | Filter Radius";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 8.0;
    float UIStep = 0.1;
> = {3.0};

float ui_Style_Sharpness
<
    string UIName = "Style | Edge Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {8.0};

float ui_Style_Intensity
<
    string UIName = "Style | Blend Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.7};

float ui_Style_Quantize
<
    string UIName = "Style | Color Quantization (0=off)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 32.0;
    float UIStep = 1.0;
> = {0.0};

bool ui_Style_InkEdges
<
    string UIName = "Style | Ink Edge Outlines";
> = {false};

float ui_Style_InkThreshold
<
    string UIName = "Style | Ink Edge Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 0.5;
    float UIStep = 0.01;
> = {0.08};

float ui_Style_InkStrength
<
    string UIName = "Style | Ink Edge Darkness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.6};

float ui_Style_DepthFade
<
    string UIName = "Style | Depth Fade Start";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.6};


//=== STYLIZATION CORE ===//

// Structure tensor: computes local edge orientation
// Returns float3(Sxx, Syy, Sxy) for the dominant direction
float3 ComputeStructureTensor(float2 uv)
{
    // Sobel gradients
    float lL = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-PixelSize.x, 0), 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float lR = dot(TextureColor.SampleLevel(smpLinear, uv + float2( PixelSize.x, 0), 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float lU = dot(TextureColor.SampleLevel(smpLinear, uv + float2(0, -PixelSize.y), 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float lD = dot(TextureColor.SampleLevel(smpLinear, uv + float2(0,  PixelSize.y), 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));

    float gx = lR - lL;
    float gy = lD - lU;

    return float3(gx * gx, gy * gy, gx * gy);
}

// Anisotropic Kuwahara filter
// Divides the neighborhood into 4 sectors oriented along the local edge
// direction, picks the sector with minimum variance for a painterly look
float3 AnisotropicKuwahara(float2 uv, float radius)
{
    // Get local orientation from structure tensor
    float3 st = ComputeStructureTensor(uv);

    // Eigenvalue decomposition for principal direction
    float trace = st.x + st.y;
    float det = st.x * st.y - st.z * st.z;
    float disc = sqrt(max(trace * trace * 0.25 - det, 0.0));
    float lambda1 = trace * 0.5 + disc;
    float lambda2 = trace * 0.5 - disc;

    // Anisotropy factor
    float aniso = (lambda1 + lambda2 > 0.0001)
                ? (lambda1 - lambda2) / (lambda1 + lambda2)
                : 0.0;

    // Edge direction angle
    float angle = 0.5 * atan2(2.0 * st.z, st.x - st.y);

    // Rotation matrix for sector alignment
    float cosA = cos(angle);
    float sinA = sin(angle);

    // 4-sector Kuwahara: compute mean and variance for each sector
    float3 sectorMean[4];
    float3 sectorVar[4];
    float sectorCount[4];

    [unroll]
    for (int s = 0; s < 4; s++)
    {
        sectorMean[s] = 0.0;
        sectorVar[s] = 0.0;
        sectorCount[s] = 0.0;
    }

    int iRadius = (int)radius;

    [loop]
    for (int dy = -iRadius; dy <= iRadius; dy++)
    {
        [loop]
        for (int dx = -iRadius; dx <= iRadius; dx++)
        {
            // Skip corners for a more circular kernel
            if (dx * dx + dy * dy > iRadius * iRadius)
                continue;

            float2 offset = float2(float(dx), float(dy));

            // Rotate offset to align with edge direction
            float2 rotated;
            rotated.x = offset.x * cosA - offset.y * sinA;
            rotated.y = offset.x * sinA + offset.y * cosA;

            // Determine sector (quadrant)
            int sector = (rotated.x >= 0 ? 1 : 0) + (rotated.y >= 0 ? 2 : 0);

            float2 sampleUV = uv + offset * PixelSize.xy * float2(1.0, ScreenSize.z);
            float3 c = TextureColor.SampleLevel(smpLinear, sampleUV, 0).rgb;

            sectorMean[sector] += c;
            sectorVar[sector] += c * c;
            sectorCount[sector] += 1.0;
        }
    }

    // Find sector with minimum variance
    float minVar = 1e10;
    float3 result = TextureColor.SampleLevel(smpLinear, uv, 0).rgb;

    [unroll]
    for (int s2 = 0; s2 < 4; s2++)
    {
        if (sectorCount[s2] < 1.0)
            continue;

        float3 mean = sectorMean[s2] / sectorCount[s2];
        float3 var = sectorVar[s2] / sectorCount[s2] - mean * mean;
        float totalVar = var.x + var.y + var.z;

        // Weight by sharpness — higher sharpness = more selective
        float weight = exp(-totalVar * ui_Style_Sharpness);

        if (totalVar < minVar)
        {
            minVar = totalVar;
            result = mean;
        }
    }

    return result;
}

// Oil paint effect: Kuwahara with stronger quantization
float3 OilPaint(float2 uv, float radius)
{
    float3 color = AnisotropicKuwahara(uv, radius * 1.3);

    // Stronger quantization for oil paint look
    float levels = max(ui_Style_Quantize, 8.0);
    color = floor(color * levels + 0.5) / levels;

    return color;
}

// Watercolor effect: Kuwahara with edge bleeding
float3 Watercolor(float2 uv, float radius)
{
    float3 color = AnisotropicKuwahara(uv, radius);

    // Wet edge: brighten near edges, darken at edge boundaries
    float depth = GetLinearDepth(uv);
    float3 normal = ReconstructNormal(uv, depth);

    // Edge detection via normal discontinuity
    float3 normalR = ReconstructNormal(uv + float2(PixelSize.x, 0), GetLinearDepth(uv + float2(PixelSize.x, 0)));
    float3 normalD = ReconstructNormal(uv + float2(0, PixelSize.y), GetLinearDepth(uv + float2(0, PixelSize.y)));

    float edgeStrength = 1.0 - dot(normal, normalR) + 1.0 - dot(normal, normalD);
    edgeStrength = saturate(edgeStrength * 2.0);

    // Watercolor: lighten the overall image, darken edges (pigment pooling)
    color = lerp(color, color * 1.15, 0.3);  // Paper brightness
    color *= lerp(1.0, 0.7, edgeStrength * 0.5);  // Edge pigment pooling

    // Slight desaturation for watercolor feel
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(luma, color, 0.85);

    return color;
}

// Sobel edge detection for ink outlines
float SobelEdge(float2 uv)
{
    float tl = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-1,-1) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float tc = dot(TextureColor.SampleLevel(smpLinear, uv + float2( 0,-1) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float tr = dot(TextureColor.SampleLevel(smpLinear, uv + float2( 1,-1) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float ml = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-1, 0) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float mr = dot(TextureColor.SampleLevel(smpLinear, uv + float2( 1, 0) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float bl = dot(TextureColor.SampleLevel(smpLinear, uv + float2(-1, 1) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float bc = dot(TextureColor.SampleLevel(smpLinear, uv + float2( 0, 1) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));
    float br = dot(TextureColor.SampleLevel(smpLinear, uv + float2( 1, 1) * PixelSize.xy, 0).rgb,
                   float3(0.2126, 0.7152, 0.0722));

    float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;

    return sqrt(gx*gx + gy*gy);
}


//=== PIXEL SHADERS ===//

// Pass A: Compute stylized image
float4 PS_StyleCompute(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 original = TextureColor.Sample(smpPoint, uv).rgb;

    if (!ui_Style_Enable)
        return float4(original, 1.0);

    // Depth-based intensity fade
    float depth = GetLinearDepth(uv);
    if (depth > 0.99)
        return float4(original, 1.0);

    float depthFade = 1.0 - saturate((depth - ui_Style_DepthFade) /
                     max(1.0 - ui_Style_DepthFade, 0.001));

    float3 stylized;

    if (ui_Style_Mode == 0)
        stylized = AnisotropicKuwahara(uv, ui_Style_Radius);
    else if (ui_Style_Mode == 1)
        stylized = OilPaint(uv, ui_Style_Radius);
    else
        stylized = Watercolor(uv, ui_Style_Radius);

    // Optional color quantization
    if (ui_Style_Quantize > 0.5)
    {
        float levels = ui_Style_Quantize;
        stylized = floor(stylized * levels + 0.5) / levels;
    }

    // Pack: RGB = stylized, A = depth fade factor for blend pass
    return float4(stylized, depthFade);
}

// Pass B: Blend stylized with original + optional ink edges
float4 PS_StyleBlend(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 original = TextureColor.Sample(smpPoint, uv).rgb;

    if (!ui_Style_Enable)
        return float4(original, 1.0);

    // Read stylized result from previous pass
    float4 stylizedData = RenderTargetRGBA64F.Sample(smpLinear, uv);
    float3 stylized = stylizedData.rgb;
    float depthFade = stylizedData.a;

    // Blend based on intensity and depth
    float blend = ui_Style_Intensity * depthFade;
    float3 result = lerp(original, stylized, blend);

    // Optional ink edge outlines
    if (ui_Style_InkEdges)
    {
        float edge = SobelEdge(uv);
        float inkMask = saturate((edge - ui_Style_InkThreshold) * 10.0);
        inkMask *= depthFade;
        result *= lerp(1.0, 1.0 - ui_Style_InkStrength, inkMask);
    }

    return float4(result, 1.0);
}


//=== TECHNIQUE MACRO ===//

#define STYLE_TECHS(name, p1, p2) \
technique11 name##p1 <string UIName="Style: Compute"; string RenderTarget="RenderTargetRGBA64F";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_StyleCompute())); } } \
technique11 name##p2 <string UIName="Style: Blend";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_StyleBlend())); } }

#endif // PREPASS_STYLIZATION_SUITE_FXH
