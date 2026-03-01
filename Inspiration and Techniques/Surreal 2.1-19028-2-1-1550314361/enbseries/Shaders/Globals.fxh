// http://entropymine.com/imageworsener/grayscale/
// Correct way to calc Gray i guess
float grayValue(float3 Color)
{
    return  dot(Color.xyz, float3(0.2126, 0.7152, 0.0722));
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


// Depth Linearization (Thanks to Marty and Trey)
float GetLinearizedDepth(float2 coord)
{
    float depth = TextureDepth.Sample(PointSampler, coord);
    depth *= rcp(mad(depth,-2999.0,3000.0));
    return depth;
}

// Converts the rgb value to hsv, where H's range is -1 to 5
float3 rgb_to_hsv(float3 RGB)
{
    float r = RGB.x;
    float g = RGB.y;
    float b = RGB.z;

    float minChannel = min(r, min(g, b));
    float maxChannel = max(r, max(g, b));

    float h = 0;
    float s = 0;
    float v = maxChannel;

    float delta = maxChannel - minChannel;

    if (delta != 0)
    {
        s = delta / v;

        if (r == v) h = (g - b) / delta;
        else if (g == v) h = 2 + (b - r) / delta;
        else if (b == v) h = 4 + (r - g) / delta;
    }

    return float3(h, s, v);
}

float3 hsv_to_rgb(float3 HSV)
{
    float3 RGB = HSV.z;

    float h = HSV.x;
    float s = HSV.y;
    float v = HSV.z;

    float i = floor(h);
    float f = h - i;

    float p = (1.0 - s);
    float q = (1.0 - s * f);
    float t = (1.0 - s * (1 - f));

    if (i == 0) { RGB = float3(1, t, p); }
    else if (i == 1) { RGB = float3(q, 1, p); }
    else if (i == 2) { RGB = float3(p, 1, t); }
    else if (i == 3) { RGB = float3(p, q, 1); }
    else if (i == 4) { RGB = float3(t, p, 1); }
    else /* i == -1 */ { RGB = float3(1, p, q); }

    RGB *= v;

    return RGB;
}

#define LUM_709 float3(0.2125, 0.7154, 0.0721)