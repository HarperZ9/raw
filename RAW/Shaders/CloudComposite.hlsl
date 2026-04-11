// Bilateral upsample + composite for volumetric clouds
// Reference: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//            of Horizon: Zero Dawn" (upsampling section)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer CompositeCB : register(b0)
{
    float2 QuarterSize;    // quarter-res dimensions
    float2 FullSize;       // full-res dimensions
    float  DepthThreshold; // bilateral depth threshold
    float  Pad0, Pad1, Pad2;
}

Texture2D<float4> CloudTex   : register(t0);  // quarter-res cloud (inscatter.rgb, transmittance.a)
Texture2D<float>  DepthTex   : register(t1);  // full-res depth
Texture2D<float4> SceneColor : register(t2);  // current backbuffer
SamplerState PointSampler   : register(s0);
SamplerState LinearSampler  : register(s1);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // ---- Fetch scene color --------------------------------------------------
    float4 scene = SceneColor.SampleLevel(PointSampler, uv, 0);

    // ---- Sky pixel guard (reversed-Z: sky < 0.0001) -------------------------
    // At PostGeometry, sky hasn't rendered yet. If we composite clouds onto sky
    // pixels we'll get a black sky. Pass through scene color unchanged.
    float fullDepth = DepthTex.SampleLevel(PointSampler, uv, 0).r;
    if (fullDepth < 0.0001)
    {
        return scene;
    }

    // ---- Bilateral upsample from quarter-res --------------------------------
    // Sample the 4 nearest quarter-res texels and weight by depth similarity
    // to the full-res pixel.  This preserves sharp edges at depth discontinuities
    // (e.g., mountains against sky) while smoothly interpolating in flat regions.

    float2 quarterUV = uv * FullSize / QuarterSize;  // texel coordinate in quarter-res
    float2 texelCenter = floor(quarterUV - 0.5) + 0.5;

    // Bilinear interpolation weights
    float2 frac_ = quarterUV - texelCenter;

    // Four nearest quarter-res texel centers
    float2 uv00 = (texelCenter + float2(0.0, 0.0)) / QuarterSize;
    float2 uv10 = (texelCenter + float2(1.0, 0.0)) / QuarterSize;
    float2 uv01 = (texelCenter + float2(0.0, 1.0)) / QuarterSize;
    float2 uv11 = (texelCenter + float2(1.0, 1.0)) / QuarterSize;

    // Sample cloud at four neighbours
    float4 c00 = CloudTex.SampleLevel(PointSampler, uv00, 0);
    float4 c10 = CloudTex.SampleLevel(PointSampler, uv10, 0);
    float4 c01 = CloudTex.SampleLevel(PointSampler, uv01, 0);
    float4 c11 = CloudTex.SampleLevel(PointSampler, uv11, 0);

    // Sample depth at four quarter-res positions (map back to full-res UV)
    float d00 = DepthTex.SampleLevel(PointSampler, uv00, 0).r;
    float d10 = DepthTex.SampleLevel(PointSampler, uv10, 0).r;
    float d01 = DepthTex.SampleLevel(PointSampler, uv01, 0).r;
    float d11 = DepthTex.SampleLevel(PointSampler, uv11, 0).r;

    // Bilateral weights: bilinear weight * depth similarity
    float w00 = (1.0 - frac_.x) * (1.0 - frac_.y);
    float w10 =        frac_.x  * (1.0 - frac_.y);
    float w01 = (1.0 - frac_.x) *        frac_.y;
    float w11 =        frac_.x  *        frac_.y;

    // Depth-based bilateral weighting: exponential falloff with depth difference
    w00 *= exp(-abs(fullDepth - d00) / max(DepthThreshold, 1e-6));
    w10 *= exp(-abs(fullDepth - d10) / max(DepthThreshold, 1e-6));
    w01 *= exp(-abs(fullDepth - d01) / max(DepthThreshold, 1e-6));
    w11 *= exp(-abs(fullDepth - d11) / max(DepthThreshold, 1e-6));

    float totalWeight = w00 + w10 + w01 + w11;

    float4 cloud;
    if (totalWeight > 1e-6)
    {
        cloud = (c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11) / totalWeight;
    }
    else
    {
        // Fallback: simple bilinear
        cloud = CloudTex.SampleLevel(LinearSampler, uv, 0);
    }

    // ---- Composite: inscatter + scene * transmittance -----------------------
    float3 inscatter    = cloud.rgb;
    float  transmittance = cloud.a;

    float3 result = inscatter + scene.rgb * transmittance;
    return float4(result, scene.a);
}
