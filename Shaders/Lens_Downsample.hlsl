// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 1: Downsample + Bright Pixel Extraction (quarter-resolution)
//
// 2x2 box-filter downsample with luminance-threshold bright extraction.
// Mip 0 applies the threshold to isolate bright pixels for lens effects;
// subsequent mips are pure 2x2 averages for the mip chain.
//
// Reference: Standard image pyramid construction; threshold uses
// BT.709 luminance weighting with soft-knee for smooth transition.

cbuffer DownsampleCB : register(b0)
{
    uint2  SrcDims;       // Source texture dimensions
    uint2  DstDims;       // Destination (output) dimensions
    float  BrightThreshold; // Threshold for bright extraction (mip 0 only)
    float  SoftKnee;      // Soft knee transition width
    uint   MipLevel;      // Which mip we are generating (0..3)
    float  pad0;
}

Texture2D<float4>   SrcTex  : register(t0);
SamplerState        LinSamp : register(s0);
RWTexture2D<float4> DstTex  : register(u0);

// BT.709 luminance weights
float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // Compute center UV of the 2x2 source texel block for bilinear filtering.
    // Adding 0.5 to destination pixel centers it; multiplying by 2 maps to
    // source space; adding 0.5 centers within the 2x2 block.
    float2 uv = (float2(DTid.xy) * 2.0 + 1.0) / float2(SrcDims);

    // 2x2 bilinear average -- the hardware sampler does the box filter
    // when we sample at the exact center of the 2x2 block.
    float4 color = SrcTex.SampleLevel(LinSamp, uv, 0);

    // On mip 0, apply brightness threshold to extract only bright pixels.
    // Subsequent mips are pure downsample (no threshold).
    if (MipLevel == 0)
    {
        float lum = Luminance(color.rgb);

        // Soft-knee: smoothstep transition from threshold-knee to threshold+knee.
        // This avoids hard cut artifacts in the lens flare source.
        float knee = BrightThreshold * SoftKnee;
        float soft = lum - (BrightThreshold - knee);
        soft = clamp(soft / (2.0 * knee + 1e-5), 0.0, 1.0);
        soft = soft * soft;

        // Blend between zero and full color based on soft threshold
        float contrib = max(soft, lum - BrightThreshold) / max(lum, 1e-5);
        contrib = max(contrib, 0.0);

        color.rgb *= contrib;
    }

    DstTex[DTid.xy] = color;
}
