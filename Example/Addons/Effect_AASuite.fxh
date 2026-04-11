//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                Anti-Aliasing Suite Addon for ENBSeries  v1.0                                 //
//                by Zain Dana Harper                                                           //
//                                                                                              //
//  Two AA algorithms, runtime selectable:                                                      //
//    Mode 1: FXAA 3.11 — Fast Approximate Anti-Aliasing                                       //
//            3 quality tiers (12/24/39 taps), gradient walk, subpixel filtering               //
//    Mode 2: SMAA — Subpixel Morphological Anti-Aliasing (2-pass)                             //
//            Luma edge detect with depth predication → RT,                                    //
//            combined weight calculation + neighborhood blend                                  //
//            Analytical area computation (no lookup textures)                                  //
//                                                                                              //
//  Architecture:                                                                               //
//    EotE_AA  (base, UIName, RT=RenderTargetRGBA64F): Edge detection → RT                     //
//    EotE_AA1 (sub-technique): Weight + Blend / FXAA → TextureColor                           //
//                                                                                              //
//  TextureColor is preserved through the base technique (output goes to RT).                   //
//  Sub-technique reads edges from RT + scene from TextureColor.                                //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                                 UI Parameters                                                //
//----------------------------------------------------------------------------------------------//

int   ui_AAMode
<
    string UIName = "AA | Mode (0=Off 1=FXAA 2=SMAA)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 2;
> = {0};

// ---- FXAA Parameters ---- //

int   ui_FXAAQuality
<
    string UIName = "AA | FXAA Quality (0=Low 1=Med 2=High)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 2;
> = {1};

float ui_FXAAEdgeThresh
<
    string UIName = "AA | FXAA Edge Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.063;
    float UIMax = 0.333;
> = {0.166};

float ui_FXAAEdgeMin
<
    string UIName = "AA | FXAA Min Edge";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.1;
> = {0.0833};

float ui_FXAASubPix
<
    string UIName = "AA | FXAA Subpixel";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.75};

// ---- SMAA Parameters ---- //

float ui_SMAAThreshold
<
    string UIName = "AA | SMAA Edge Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.05;
    float UIMax = 0.5;
> = {0.1};

int   ui_SMAAMaxSteps
<
    string UIName = "AA | SMAA Max Search Steps";
    string UIWidget = "Spinner";
    int UIMin = 4;
    int UIMax = 64;
> = {32};

int   ui_SMAACorner
<
    string UIName = "AA | SMAA Corner Rounding";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 100;
> = {25};

bool  ui_SMAADepthPred
<
    string UIName = "AA | SMAA Depth Predication";
> = {true};

float ui_SMAAPredScale
<
    string UIName = "AA | SMAA Pred Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 5.0;
> = {2.0};


//----------------------------------------------------------------------------------------------//
//                              Constants & Helpers                                              //
//----------------------------------------------------------------------------------------------//

static const float AA_DELTA = 1e-6;
static const float3 AA_LUM  = float3(0.2126, 0.7152, 0.0722);

float AA_Luma(float3 c) { return dot(c, AA_LUM); }

float AA_Max3(float3 v) { return max(v.x, max(v.y, v.z)); }


//----------------------------------------------------------------------------------------------//
//                              SMAA Structs                                                    //
//----------------------------------------------------------------------------------------------//

struct AAVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 offset0  : TEXCOORD1;
    float4 offset1  : TEXCOORD2;
    float4 offset2  : TEXCOORD3;
};


//----------------------------------------------------------------------------------------------//
//                          SMAA Edge Detection Helpers                                         //
//----------------------------------------------------------------------------------------------//

float2 SMAA_LumaEdge(float2 UV, float Threshold)
{
    float L      = AA_Luma(TextureColor.SampleLevel(smpLinear, UV, 0).rgb);
    float Lleft  = AA_Luma(TextureColor.SampleLevel(smpLinear, UV + float2(-PixelSize.x, 0), 0).rgb);
    float Ltop   = AA_Luma(TextureColor.SampleLevel(smpLinear, UV + float2(0, -PixelSize.y), 0).rgb);
    float Lright = AA_Luma(TextureColor.SampleLevel(smpLinear, UV + float2( PixelSize.x, 0), 0).rgb);
    float Lbottom= AA_Luma(TextureColor.SampleLevel(smpLinear, UV + float2(0,  PixelSize.y), 0).rgb);

    float4 delta;
    delta.x = abs(L - Lleft);
    delta.y = abs(L - Ltop);
    delta.z = abs(L - Lright);
    delta.w = abs(L - Lbottom);

    // Local contrast adaptation
    float maxDelta = max(max(delta.x, delta.y), max(delta.z, delta.w));
    float localAdaptation = max(maxDelta, Threshold);
    float2 edges = step(localAdaptation * 0.5, delta.xy);
    return edges;
}

float SMAA_DepthPredication(float2 UV, float PredScale)
{
    float D     = TextureDepth.SampleLevel(smpPoint, UV, 0).x;
    float Dleft = TextureDepth.SampleLevel(smpPoint, UV + float2(-PixelSize.x, 0), 0).x;
    float Dtop  = TextureDepth.SampleLevel(smpPoint, UV + float2(0, -PixelSize.y), 0).x;

    float2 depthDelta = abs(float2(D - Dleft, D - Dtop));
    float  depthEdge  = max(depthDelta.x, depthDelta.y);

    // At depth edges, reduce threshold (detect more edges)
    float pred = step(0.01, depthEdge) * 0.8;
    return saturate(1.0 - pred * PredScale);
}


//----------------------------------------------------------------------------------------------//
//                          SMAA Search Functions                                               //
//----------------------------------------------------------------------------------------------//

float SMAA_SearchXLeft(float2 UV, float end, int maxSteps)
{
    float2 e = float2(0, 1);
    [loop] for(int i = 0; i < 64 && i < maxSteps && UV.x > end && e.y > 0.8281 && e.x == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(smpLinear, UV, 0).rg;
        UV.x -= 2.0 * PixelSize.x;
    }
    return UV.x + (3.25 - (255.0 / 127.0) * e.x) * PixelSize.x;
}

float SMAA_SearchXRight(float2 UV, float end, int maxSteps)
{
    float2 e = float2(0, 1);
    [loop] for(int i = 0; i < 64 && i < maxSteps && UV.x < end && e.y > 0.8281 && e.x == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(smpLinear, UV, 0).rg;
        UV.x += 2.0 * PixelSize.x;
    }
    return UV.x - (3.25 - (255.0 / 127.0) * e.x) * PixelSize.x;
}

float SMAA_SearchYUp(float2 UV, float end, int maxSteps)
{
    float2 e = float2(1, 0);
    [loop] for(int i = 0; i < 64 && i < maxSteps && UV.y > end && e.x > 0.8281 && e.y == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(smpLinear, UV, 0).rg;
        UV.y -= 2.0 * PixelSize.y;
    }
    return UV.y + (3.25 - (255.0 / 127.0) * e.y) * PixelSize.y;
}

float SMAA_SearchYDown(float2 UV, float end, int maxSteps)
{
    float2 e = float2(1, 0);
    [loop] for(int i = 0; i < 64 && i < maxSteps && UV.y < end && e.x > 0.8281 && e.y == 0.0; i++)
    {
        e = RenderTargetRGBA64F.SampleLevel(smpLinear, UV, 0).rg;
        UV.y += 2.0 * PixelSize.y;
    }
    return UV.y - (3.25 - (255.0 / 127.0) * e.y) * PixelSize.y;
}


//----------------------------------------------------------------------------------------------//
//                   SMAA Analytical Area Calculation                                           //
//----------------------------------------------------------------------------------------------//
//
//  Replaces the AreaTex lookup in standard SMAA with an analytical approximation.
//  Computes the perpendicular coverage area based on edge endpoint distances
//  and crossing patterns. Corner rounding parameter controls sharpness at junctions.

float2 SMAA_Area(float2 dist, float e1, float e2, float cornerRound)
{
    float d1 = dist.x;
    float d2 = dist.y;
    float totalLen = d1 + d2;

    if(totalLen < AA_DELTA)
        return 0.0;

    float roundFactor = cornerRound / 100.0;

    float leftCoverage  = (e1 > 0.0) ? saturate(d1 / totalLen) : 0.0;
    float rightCoverage = (e2 > 0.0) ? saturate(d2 / totalLen) : 0.0;

    float t1 = saturate(1.0 - d1 * rcp(totalLen + AA_DELTA));
    float t2 = saturate(1.0 - d2 * rcp(totalLen + AA_DELTA));

    float2 area;
    area.x = lerp(leftCoverage  * e1 * 0.5, leftCoverage  * e1 * roundFactor, t1);
    area.y = lerp(rightCoverage * e2 * 0.5, rightCoverage * e2 * roundFactor, t2);

    return area;
}


//----------------------------------------------------------------------------------------------//
//                          SMAA Combined Weight + Blend                                        //
//----------------------------------------------------------------------------------------------//
//
//  Reads edges from RenderTargetRGBA64F, computes blend weights, and immediately
//  applies neighborhood blending using the scene from TextureColor.
//  This combines the traditional SMAA passes 2+3 into a single pass.

float3 SMAA_WeightAndBlend(float2 UV)
{
    float3 color = TextureColor.SampleLevel(smpPoint, UV, 0).rgb;

    // Read edge data from RT
    float2 e = RenderTargetRGBA64F.SampleLevel(smpPoint, UV, 0).rg;

    if(e.x + e.y < AA_DELTA)
        return color;

    int maxSteps = clamp(ui_SMAAMaxSteps, 4, 64);
    float cornerRound = (float)clamp(ui_SMAACorner, 0, 100);

    float4 weights = 0.0;

    // Horizontal edge processing
    if(e.x > 0.0)
    {
        float2 searchUV_L = UV + float2(-0.25 * PixelSize.x, -0.125 * PixelSize.y);
        float2 searchUV_R = UV + float2( 1.25 * PixelSize.x, -0.125 * PixelSize.y);
        float  searchEnd_L = UV.x - (float)maxSteps * 2.0 * PixelSize.x;
        float  searchEnd_R = UV.x + (float)maxSteps * 2.0 * PixelSize.x;

        float left  = SMAA_SearchXLeft (searchUV_L, searchEnd_L, maxSteps);
        float right = SMAA_SearchXRight(searchUV_R, searchEnd_R, maxSteps);

        float d1 = UV.x - left;
        float d2 = right - UV.x;

        float e1 = RenderTargetRGBA64F.SampleLevel(smpLinear,
            float2(left  + 0.25 * PixelSize.x, UV.y - PixelSize.y), 0).r;
        float e2 = RenderTargetRGBA64F.SampleLevel(smpLinear,
            float2(right - 0.25 * PixelSize.x, UV.y - PixelSize.y), 0).r;

        weights.rg = SMAA_Area(float2(d1, d2) / PixelSize.x, e1, e2, cornerRound);
    }

    // Vertical edge processing
    if(e.y > 0.0)
    {
        float2 searchUV_U = UV + float2(-0.125 * PixelSize.x, -0.25 * PixelSize.y);
        float2 searchUV_D = UV + float2(-0.125 * PixelSize.x,  1.25 * PixelSize.y);
        float  searchEnd_U = UV.y - (float)maxSteps * 2.0 * PixelSize.y;
        float  searchEnd_D = UV.y + (float)maxSteps * 2.0 * PixelSize.y;

        float top    = SMAA_SearchYUp  (searchUV_U, searchEnd_U, maxSteps);
        float bottom = SMAA_SearchYDown(searchUV_D, searchEnd_D, maxSteps);

        float d1 = UV.y - top;
        float d2 = bottom - UV.y;

        float e1 = RenderTargetRGBA64F.SampleLevel(smpLinear,
            float2(UV.x - PixelSize.x, top    + 0.25 * PixelSize.y), 0).g;
        float e2 = RenderTargetRGBA64F.SampleLevel(smpLinear,
            float2(UV.x - PixelSize.x, bottom - 0.25 * PixelSize.y), 0).g;

        weights.ba = SMAA_Area(float2(d1, d2) / PixelSize.y, e1, e2, cornerRound);
    }

    // Check if we have any weights to apply
    if(dot(weights, 1.0) < AA_DELTA)
        return color;

    // Neighborhood blending using computed weights
    float4 blendColor = 0.0;

    // Sample neighbors in blending directions
    float4 a;
    a.x  = weights.a;  // right neighbor weight (from right pixel's .a)
    a.y  = weights.b;  // bottom neighbor weight (from bottom pixel's .b)

    // For complete SMAA we'd also read neighbor weights, but for our simplified
    // combined pass we use the current pixel's weights directly
    a.zw = weights.rg;  // center left/top

    // Weighted bilinear blend across detected edge
    float2 offset;
    float4 result = 0;

    // Horizontal blend
    offset = float2(0, PixelSize.y);
    float2 blendH = float2(a.x, a.z);
    result += blendH.x * TextureColor.SampleLevel(smpLinear, UV + offset, 0);
    result += blendH.y * TextureColor.SampleLevel(smpLinear, UV - offset, 0);

    // Vertical blend
    offset = float2(PixelSize.x, 0);
    float2 blendV = float2(a.y, a.w);
    result += blendV.x * TextureColor.SampleLevel(smpLinear, UV + offset, 0);
    result += blendV.y * TextureColor.SampleLevel(smpLinear, UV - offset, 0);

    float totalWeight = dot(a, 1.0);
    result /= max(totalWeight, AA_DELTA);

    // Blend with original based on weight coverage
    float orig = 1.0 - saturate(totalWeight);
    return result.rgb + orig * color;
}


//----------------------------------------------------------------------------------------------//
//                                 FXAA Implementation                                          //
//----------------------------------------------------------------------------------------------//
//
//  FXAA 3.11 with 3 quality tiers:
//    Quality 0 (Low):  12 search taps
//    Quality 1 (Med):  24 search taps
//    Quality 2 (High): 39 search taps
//
//  Luma-based edge detection, gradient walk to find edge endpoints,
//  subpixel anti-aliasing response.

float3 FXAA_Apply(float2 UV)
{
    // Sample center and 4 cardinal neighbors
    float3 rgbM = TextureColor.SampleLevel(smpLinear, UV, 0).rgb;
    float3 rgbN = TextureColor.SampleLevel(smpLinear, UV + float2(0, -PixelSize.y), 0).rgb;
    float3 rgbS = TextureColor.SampleLevel(smpLinear, UV + float2(0,  PixelSize.y), 0).rgb;
    float3 rgbW = TextureColor.SampleLevel(smpLinear, UV + float2(-PixelSize.x, 0), 0).rgb;
    float3 rgbE = TextureColor.SampleLevel(smpLinear, UV + float2( PixelSize.x, 0), 0).rgb;

    float lumaM = AA_Luma(rgbM);
    float lumaN = AA_Luma(rgbN);
    float lumaS = AA_Luma(rgbS);
    float lumaW = AA_Luma(rgbW);
    float lumaE = AA_Luma(rgbE);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit: low contrast
    if(lumaRange < max(ui_FXAAEdgeMin, lumaMax * ui_FXAAEdgeThresh))
        return rgbM;

    // Sample 4 diagonal neighbors
    float3 rgbNW = TextureColor.SampleLevel(smpLinear, UV + float2(-PixelSize.x, -PixelSize.y), 0).rgb;
    float3 rgbNE = TextureColor.SampleLevel(smpLinear, UV + float2( PixelSize.x, -PixelSize.y), 0).rgb;
    float3 rgbSW = TextureColor.SampleLevel(smpLinear, UV + float2(-PixelSize.x,  PixelSize.y), 0).rgb;
    float3 rgbSE = TextureColor.SampleLevel(smpLinear, UV + float2( PixelSize.x,  PixelSize.y), 0).rgb;

    float lumaNW = AA_Luma(rgbNW);
    float lumaNE = AA_Luma(rgbNE);
    float lumaSW = AA_Luma(rgbSW);
    float lumaSE = AA_Luma(rgbSE);

    // Edge direction detection (horizontal vs vertical)
    float edgeH = abs((-2.0 * lumaN) + lumaNW + lumaNE)
                + abs((-2.0 * lumaM) + lumaW  + lumaE) * 2.0
                + abs((-2.0 * lumaS) + lumaSW + lumaSE);
    float edgeV = abs((-2.0 * lumaW) + lumaNW + lumaSW)
                + abs((-2.0 * lumaM) + lumaN  + lumaS) * 2.0
                + abs((-2.0 * lumaE) + lumaNE + lumaSE);
    bool isHorz = (edgeH >= edgeV);

    // Perpendicular gradient
    float gradPos = isHorz ? lumaS - lumaM : lumaE - lumaM;
    float gradNeg = isHorz ? lumaN - lumaM : lumaW - lumaM;
    bool  isNeg   = (abs(gradNeg) >= abs(gradPos));
    float gradMax = max(abs(gradPos), abs(gradNeg));

    float stepLen = isHorz ? PixelSize.y : PixelSize.x;
    float luma0   = isNeg ? (isHorz ? lumaN : lumaW) : (isHorz ? lumaS : lumaE);
    if(isNeg) stepLen = -stepLen;

    float lumaAvg    = 0.5 * (luma0 + lumaM);
    float gradScaled = 0.25 * gradMax;

    // Walk along edge to find endpoints
    float2 posN = UV;
    float2 posP = UV;
    float2 offNP = isHorz ? float2(PixelSize.x, 0) : float2(0, PixelSize.y);

    posN -= offNP;
    posP += offNP;

    float lumaEndN = AA_Luma(TextureColor.SampleLevel(smpLinear, posN, 0).rgb) - lumaAvg;
    float lumaEndP = AA_Luma(TextureColor.SampleLevel(smpLinear, posP, 0).rgb) - lumaAvg;

    bool doneN = abs(lumaEndN) >= gradScaled;
    bool doneP = abs(lumaEndP) >= gradScaled;

    // Quality tier determines search distance
    int quality;
    if(ui_FXAAQuality == 0)      quality = 12;
    else if(ui_FXAAQuality == 1) quality = 24;
    else                         quality = 39;

    [loop] for(int i = 1; i < 39 && i < quality && !(doneN && doneP); i++)
    {
        if(!doneN)
        {
            posN -= offNP;
            lumaEndN = AA_Luma(TextureColor.SampleLevel(smpLinear, posN, 0).rgb) - lumaAvg;
            doneN = abs(lumaEndN) >= gradScaled;
        }
        if(!doneP)
        {
            posP += offNP;
            lumaEndP = AA_Luma(TextureColor.SampleLevel(smpLinear, posP, 0).rgb) - lumaAvg;
            doneP = abs(lumaEndP) >= gradScaled;
        }
    }

    // Compute final offset
    float distN = isHorz ? (UV.x - posN.x) : (UV.y - posN.y);
    float distP = isHorz ? (posP.x - UV.x) : (posP.y - UV.y);
    float distMin = min(distN, distP);
    float spanLen = distN + distP;

    float pixelOffset = -distMin / (spanLen + AA_DELTA) + 0.5;

    // Subpixel anti-aliasing factor
    float subPixA = (2.0 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subPixB = saturate(abs(subPixA - lumaM) / (lumaRange + AA_DELTA));
    float subPixC = (-2.0 * subPixB + 3.0) * subPixB * subPixB;
    float subPixF = subPixC * subPixC * ui_FXAASubPix;

    float finalOffset = max(pixelOffset, subPixF);

    float2 finalUV = UV;
    if(isHorz) finalUV.y += finalOffset * stepLen;
    else       finalUV.x += finalOffset * stepLen;

    return TextureColor.SampleLevel(smpLinear, finalUV, 0).rgb;
}


//----------------------------------------------------------------------------------------------//
//                           Edge Detection Vertex Shader                                       //
//----------------------------------------------------------------------------------------------//

AAVSOutput VS_AAEdge(float3 pos : POSITION, float2 txcoord : TEXCOORD0)
{
    AAVSOutput OUT;
    OUT.pos      = float4(pos.xyz, 1.0);
    OUT.texcoord = txcoord;

    // Precompute neighbor offsets for edge detection
    OUT.offset0 = txcoord.xyxy + float4(-PixelSize.x, 0, 0, -PixelSize.y);
    OUT.offset1 = txcoord.xyxy + float4( PixelSize.x, 0, 0,  PixelSize.y);
    OUT.offset2 = txcoord.xyxy + float4(-2.0 * PixelSize.x, 0, 0, -2.0 * PixelSize.y);
    return OUT;
}


//----------------------------------------------------------------------------------------------//
//                     Edge Detection Pixel Shader (EotE_AA)                                    //
//----------------------------------------------------------------------------------------------//
//
//  Writes edge data to RenderTargetRGBA64F.
//  When FXAA mode or disabled: outputs 0 (minimal cost, no edges → blend pass skips SMAA).

float4 PS_AAEdge(AAVSOutput IN) : SV_Target
{
    // FXAA mode or disabled: no edge detection needed
    [branch] if(ui_AAMode != 2)
        return 0.0;

    float threshold = ui_SMAAThreshold;

    // Depth predication: lower threshold at depth discontinuities
    if(ui_SMAADepthPred)
    {
        float predFactor = SMAA_DepthPredication(IN.texcoord, ui_SMAAPredScale);
        threshold *= predFactor;
    }

    float2 edges = SMAA_LumaEdge(IN.texcoord, threshold);

    return float4(edges, 0, 0);
}


//----------------------------------------------------------------------------------------------//
//                  Combined Blend Pixel Shader (EotE_AA1)                                      //
//----------------------------------------------------------------------------------------------//
//
//  SMAA: reads edges from RenderTargetRGBA64F + scene from TextureColor.
//  FXAA: reads directly from TextureColor.

float4 PS_AABlend(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    [branch] if(ui_AAMode == 0)
        return TextureColor.SampleLevel(smpPoint, txcoord, 0);

    float3 result;

    if(ui_AAMode == 1) // FXAA
        result = FXAA_Apply(txcoord);
    else               // SMAA
        result = SMAA_WeightAndBlend(txcoord);

    return float4(result, 1.0);
}
