// Luma Coefficients
#define Rec709      0
#define Rec709_5    1
#define Rec601      2
#define Rec2020     3
#define Lum333      4

// Calculate perceived luminance color by using the ITU-R BT standards
float GetLuma(in float3 color, int btspec)
{
    static const float3 LumaCoeff[5] =
    {
        // 0: HD TV - Rec.709
        float3(0.2126, 0.7152f, 0.0722),
        // 1: HD TV - Rec.709-5
        float3(0.212395, 0.701049, 0.086556),
        // 2: CRT TV - Rec.601
        float3(0.299, 0.587, 0.114),
        // 3: HDR Spec - Rec.2020
        float3(0.2627, 0.6780, 0.0593),
        // 4: Incorrect Equal Weighting
        float3(0.3333, 0.3333, 0.3333)
    };

    return dot(color.rgb, LumaCoeff[btspec]);
}

// B-Spline bicubic filtering function for FO4 enbseries (dx11) by kingeric1992
//     http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=4714
//
//sample usage:
//	    float4 filteredcolor = BicubicFilter(TextureColor, Sampler1, coord);
//ref:
//      http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter20.html
//      http://vec3.ca/bicubic-filtering-in-fewer-taps/

float4 BicubicFilter( Texture2D InputTex, sampler texSampler, float2 texcoord)
{
    // Get size of input tex
    float2 texsize;
    InputTex.GetDimensions( texsize.x, texsize.y );

    float4 uv;
    uv.xy = texcoord * texsize;

    //distant to nearest center
    float2 center  = floor(uv - 0.5) + 0.5;
    float2 dist1st = uv - center;
    float2 dist2nd = dist1st * dist1st;
    float2 dist3rd = dist2nd * dist1st;

    //B-Spline weights
    float2 weight0 =     -dist3rd + 3 * dist2nd - 3 * dist1st + 1;
    float2 weight1 =  3 * dist3rd - 6 * dist2nd               + 4;
    float2 weight2 = -3 * dist3rd + 3 * dist2nd + 3 * dist1st + 1;
    float2 weight3 =      dist3rd;

    weight0 += weight1;
    weight2 += weight3;

    //sample point to utilize bilinear filtering interpolation
    uv.xy  = center - 1 + weight1 / weight0;
    uv.zw  = center + 1 + weight3 / weight2;
    uv    /= texsize.xyxy;

    //Sample and blend
    return ( weight0.y * ( InputTex.Sample( texSampler, uv.xy) * weight0.x + InputTex.Sample( texSampler, uv.zy) * weight2.x) +
             weight2.y * ( InputTex.Sample( texSampler, uv.xw) * weight0.x + InputTex.Sample( texSampler, uv.zw) * weight2.x)) / 36;
}


// Depth Linearization (Thanks to Marty)
float GetLinearizedDepth(float2 coord)
{
    float depth = TextureDepth.Sample(PointSampler, coord);
    depth *= rcp(mad(depth,-2999.0,3000.0));
    return depth;
}

// Overlay blending mode
float3 Overlay(float3 LayerA, float3 LayerB)
{
    float3 MinA = min(LayerA, 0.5) * 2;
    float3 MinB = min(LayerB, 0.5) * 2;

    float3 MaxA = 1 - (max(LayerA, 0.5) * 2 - 1);
    float3 MaxB = 1 - (max(LayerB, 0.5) * 2 - 1);

    float3 Result = (MinA * MinB + 1 - MaxA * MaxB) * 0.5;
    return Result;
}

// Screen in HDR
float3 LDRToLinear(float3 incol)
{
   float3   res;
   res=1.0/(1.0-incol) - 1.0;
   return res;
}

float3 LinearToLDR(float3 incol)
{
   float3   res;
   res=1.0 - (1.0/(incol+1.0));
   return res;
}

float3 HDRScreen(float3 c, float3 b)
{
	float3   res;
    float3 cx, bx;
    cx=LinearToLDR(c);
    bx=LinearToLDR(b);
    res=1-(1-cx)*(1-bx);
    res=LDRToLinear(res);
    return res;
}
