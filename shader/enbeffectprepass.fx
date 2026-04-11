//----------------------------------------------------------------------------------------------//
//                    ENB of the Elders - Screen-Space Prepass                                   //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  SSAO + SSIL (Screen-Space Indirect Lighting) with bilateral denoise.                        //
//                                                                                              //
//  SSAO: Normal-aware hemisphere sampling with Fibonacci spiral distribution.                   //
//  SSIL: Color gathered from occluding samples — provides bounce light.                        //
//  3-pass: Compute → Bilateral blur H → Bilateral blur V + composite.                         //
//                                                                                              //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=== COMPILE-TIME OPTIONS ===//

// AO sample count: 16=fast, 32=quality, 48=ultra
#define AO_SAMPLES    32

// Bilateral blur kernel radius (samples per side)
#define BLUR_RADIUS   5


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
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== TEXTURES ===//

Texture2D   TextureColor;
Texture2D   TextureOriginal;
Texture2D   TextureDepth;
Texture2D   RenderTargetRGBA64F;
Texture2D   RenderTargetRGBA32;


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
static const float  PI = 3.14159265;
static const float  GOLDEN_ANGLE = 2.39996323;  // pi * (3 - sqrt(5))


//=== UI PARAMETERS ===//

// --- AO ---

bool ui_AO_Enable
<
    string UIName = "AO | Enable SSAO";
> = {true};

float ui_AO_RadiusDay
<
    string UIName = "AO | Day - Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.2};

float ui_AO_RadiusNight
<
    string UIName = "AO | Night - Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_AO_RadiusInterior
<
    string UIName = "AO | Interior - Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.8};

float ui_AO_IntensityDay
<
    string UIName = "AO | Day - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.2};

float ui_AO_IntensityNight
<
    string UIName = "AO | Night - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.9};

float ui_AO_IntensityInterior
<
    string UIName = "AO | Interior - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.5};

float ui_AO_Power
<
    string UIName = "AO | Power Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 4.0;
    float UIStep = 0.01;
> = {1.5};

float ui_AO_Bias
<
    string UIName = "AO | Bias (self-occlusion fix)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.01;
    float UIStep = 0.0001;
> = {0.0005};

float ui_AO_FadeStart
<
    string UIName = "AO | Fade Start";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_AO_FadeEnd
<
    string UIName = "AO | Fade End";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.8};

// --- SSIL ---

bool ui_IL_Enable
<
    string UIName = "IL | Enable SSIL";
> = {true};

float ui_IL_Intensity
<
    string UIName = "IL | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.4};

float ui_IL_Saturation
<
    string UIName = "IL | Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.7};

// --- Blur ---

float ui_BlurSharpness
<
    string UIName = "BLUR | Edge Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {8.0};


//=== HELPER FUNCTIONS ===//

float GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}

// Interleaved gradient noise — temporally stable, no banding
float IGN(float2 coord)
{
    return frac(52.9829189 * frac(dot(coord, float2(0.06711056, 0.00583715))));
}

// Reconstruct approximate screen-space normal from depth
// Uses best-fit from 4 neighbors to avoid edge artifacts
float3 ReconstructNormal(float2 uv, float depthC)
{
    float dL = GetLinearDepth(uv - float2(PixelSize.x, 0));
    float dR = GetLinearDepth(uv + float2(PixelSize.x, 0));
    float dU = GetLinearDepth(uv - float2(0, PixelSize.y));
    float dD = GetLinearDepth(uv + float2(0, PixelSize.y));

    // Pick closest neighbor to avoid edges
    float3 dx = (abs(dL - depthC) < abs(dR - depthC))
              ? float3(-PixelSize.x, 0, dL - depthC)
              : float3( PixelSize.x, 0, dR - depthC);

    float3 dy = (abs(dU - depthC) < abs(dD - depthC))
              ? float3(0, -PixelSize.y, dU - depthC)
              : float3(0,  PixelSize.y, dD - depthC);

    return normalize(cross(dx, dy));
}


//=== VERTEX SHADER ===//

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADERS ===//

// Pass 0: Compute AO + SSIL
// Output: R = AO, GBA = indirect light color
float4 PS_Compute(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float depth = GetLinearDepth(txcoord);

    // Early out for sky
    if (depth > 0.99) return float4(1.0, 0.0, 0.0, 0.0);

    // Fade at distance
    float fadeFactor = 1.0 - saturate((depth - ui_AO_FadeStart) / max(ui_AO_FadeEnd - ui_AO_FadeStart, 0.001));
    if (fadeFactor < 0.01) return float4(1.0, 0.0, 0.0, 0.0);

    // DNI interpolation
    float radius = lerp(lerp(ui_AO_RadiusNight, ui_AO_RadiusDay, ENightDayFactor),
                        ui_AO_RadiusInterior, EInteriorFactor);
    float intensity = lerp(lerp(ui_AO_IntensityNight, ui_AO_IntensityDay, ENightDayFactor),
                           ui_AO_IntensityInterior, EInteriorFactor);

    // Scale radius by depth (closer objects get larger screen-space radius)
    float scaledRadius = radius * 0.015 / max(depth, 0.001);
    scaledRadius = min(scaledRadius, 0.15);

    // Reconstruct normal
    float3 normal = ReconstructNormal(txcoord, depth);

    // Random rotation per pixel (IGN gives stable noise without temporal shimmer)
    float noiseAngle = IGN(pos.xy) * PI * 2.0;

    float aoSum = 0.0;
    float3 ilSum = 0.0;
    float sampleWeight = 0.0;

    // Fibonacci spiral sampling
    [loop]
    for (int i = 0; i < AO_SAMPLES; i++)
    {
        // Fibonacci disc: even coverage, no clustering
        float fi = float(i) + 0.5;
        float r = sqrt(fi / float(AO_SAMPLES));
        float theta = fi * GOLDEN_ANGLE + noiseAngle;

        float2 dir;
        sincos(theta, dir.y, dir.x);
        float2 offset = dir * r * scaledRadius * float2(1.0, 1.0 / ScreenSize.z);

        float2 sampleUV = txcoord + offset;

        // Skip out-of-bounds
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = GetLinearDepth(sampleUV);

        // Depth difference (positive = sample is behind center = potential occluder from our POV)
        float depthDiff = depth - sampleDepth;

        // Normal-aware weighting: reject samples in the wrong hemisphere
        float3 sampleVec = float3(offset, depthDiff);
        float normalDot = dot(normalize(sampleVec), normal);

        // Only count samples in the normal hemisphere (above the surface)
        if (normalDot < 0.05) continue;

        // Range check: ignore samples too far away in depth
        float rangeWeight = saturate(1.0 - abs(depthDiff) / (radius * 0.015));

        // Occlusion: sample is closer to camera than expected (occluding)
        float occl = step(ui_AO_Bias, depthDiff) * rangeWeight * normalDot;

        aoSum += occl;
        sampleWeight += 1.0;

        // SSIL: gather color from occluding samples (they bounce light toward us)
        if (ui_IL_Enable && occl > 0.01)
        {
            float3 sampleColor = TextureColor.SampleLevel(smpLinear, sampleUV, 0).rgb;
            ilSum += sampleColor * occl;
        }
    }

    // Normalize
    float ao = (sampleWeight > 0.5) ? (aoSum / sampleWeight) : 0.0;

    // Apply power curve and intensity
    ao = pow(saturate(ao), ui_AO_Power) * intensity * fadeFactor;
    ao = 1.0 - saturate(ao);

    // Normalize IL
    float3 il = 0.0;
    if (ui_IL_Enable && sampleWeight > 0.5)
    {
        il = ilSum / sampleWeight;

        // Saturation control: lerp between luminance (white bounce) and full color
        float ilLuma = dot(il, float3(0.2126, 0.7152, 0.0722));
        il = lerp(ilLuma, il, ui_IL_Saturation);

        il *= ui_IL_Intensity * fadeFactor;
    }

    return float4(ao, il);
}


// Pass 1: Bilateral blur horizontal
float4 PS_BlurH(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float4 center = RenderTargetRGBA64F.Sample(smpPoint, txcoord);
    float  centerDepth = GetLinearDepth(txcoord);

    float4 result = center;
    float  totalWeight = 1.0;

    [unroll]
    for (int i = 1; i <= BLUR_RADIUS; i++)
    {
        float spatialWeight = exp(-float(i * i) / (2.0 * float(BLUR_RADIUS)));

        [unroll]
        for (int sign = -1; sign <= 1; sign += 2)
        {
            float2 sampleUV = txcoord + float2(PixelSize.x * float(i * sign), 0);
            float4 s = RenderTargetRGBA64F.Sample(smpPoint, sampleUV);
            float  sd = GetLinearDepth(sampleUV);

            // Depth-aware weight (edge preservation)
            float depthWeight = exp(-abs(sd - centerDepth) * ui_BlurSharpness / max(centerDepth, 0.001));

            float w = spatialWeight * depthWeight;
            result += s * w;
            totalWeight += w;
        }
    }

    return result / totalWeight;
}


// Pass 2: Bilateral blur vertical + composite
float4 PS_Composite(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    // Vertical blur on the horizontally-blurred data
    float4 center = RenderTargetRGBA32.Sample(smpPoint, txcoord);
    float  centerDepth = GetLinearDepth(txcoord);

    float4 blurred = center;
    float  totalWeight = 1.0;

    [unroll]
    for (int i = 1; i <= BLUR_RADIUS; i++)
    {
        float spatialWeight = exp(-float(i * i) / (2.0 * float(BLUR_RADIUS)));

        [unroll]
        for (int sign = -1; sign <= 1; sign += 2)
        {
            float2 sampleUV = txcoord + float2(0, PixelSize.y * float(i * sign));
            float4 s = RenderTargetRGBA32.Sample(smpPoint, sampleUV);
            float  sd = GetLinearDepth(sampleUV);

            float depthWeight = exp(-abs(sd - centerDepth) * ui_BlurSharpness / max(centerDepth, 0.001));

            float w = spatialWeight * depthWeight;
            blurred += s * w;
            totalWeight += w;
        }
    }

    blurred /= totalWeight;

    // Extract AO and IL from blurred result
    float ao = blurred.r;
    float3 il = blurred.gba;

    // Read original scene color
    float3 color = TextureColor.Sample(smpPoint, txcoord).rgb;

    // Apply: darken by AO, add indirect light
    if (ui_AO_Enable)
        color *= ao;

    if (ui_IL_Enable)
        color += il;

    return float4(color, 1.0);
}


//=== TECHNIQUES ===//

// Pass 0: Compute raw AO + SSIL → RenderTargetRGBA64F
technique11 EotE_PrePass <string UIName = "EotE: Pre-Pass"; string RenderTarget = "RenderTargetRGBA64F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Compute()));
    }
}

// Pass 1: Bilateral blur horizontal → RenderTargetRGBA32
technique11 EotE_PrePass1 <string RenderTarget = "RenderTargetRGBA32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_BlurH()));
    }
}

// Pass 2: Bilateral blur vertical + composite → TextureColor
technique11 EotE_PrePass2
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Composite()));
    }
}
