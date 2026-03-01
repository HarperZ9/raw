// Gaussian kernel generator by The Sandvich Maker 
// Tweaked for Skin smoothing by Adyss

// 30x30 gaussian taps
static const float gaussianWeights[8] = { 0.135833760113, 0.236318968617, 0.135039410638, 0.0485714876209, 0.0106857272766, 0.00136996503546, 9.44803472733e-05, 3.04775313785e-06 };
static const float gaussianOffsets[8] = { 0.0,            1.45714285714,  3.4,            5.34285714286,   7.28571428571,   9.22857142857,    11.1714285714,     13.1142857143     };
static const int gaussianLoopLength = 8;

float4 PS_Hblur(VS_OUTPUT IN) : SV_Target
{
    float2 coord    = IN.txcoord.xy;
    float4 Original = TextureColor.Sample(LinearSampler, coord);
    float Mask      = max(Original.r, max(Original.g, Original.b));
    if (SkinSmoothing==false) {return Original;}

    float4 res   = TextureColor.Sample(LinearSampler, coord) * gaussianWeights[0];

    // Horizontal
    for (int i = 1; i < gaussianLoopLength; i++)
    {
        res += TextureColor.Sample(LinearSampler, coord + float2(gaussianOffsets[i], 0.0) * PixelSize) * gaussianWeights[i];
        res += TextureColor.Sample(LinearSampler, coord - float2(gaussianOffsets[i], 0.0) * PixelSize) * gaussianWeights[i];
    }

    res          = lerp(Original, ApplyToSkin(res, coord), SkinSmoothingPower);
    Mask         = lerp(0.0, 1.0, smoothstep(Threshold - (Threshold*ThresholdRange), Threshold,Mask * 20));
    if (HighlightMasking==true) { res = lerp(Original, res, Mask); }
    return res;
}

float4 PS_Vblur(VS_OUTPUT IN) : SV_Target
{
    float2 coord    = IN.txcoord.xy;
    float4 Original = TextureColor.Sample(LinearSampler, coord);
    float Mask      = max(Original.r, max(Original.g, Original.b));
    if (SkinSmoothing==false) {return Original;}

    float4 res      = TextureColor.Sample(LinearSampler, coord) * gaussianWeights[0];

    // Vertical
    for (int i = 1; i < gaussianLoopLength; i++)
    {
        res += TextureColor.Sample(LinearSampler, coord + float2(0.0, gaussianOffsets[i]) * PixelSize) * gaussianWeights[i];
        res += TextureColor.Sample(LinearSampler, coord - float2(0.0, gaussianOffsets[i]) * PixelSize) * gaussianWeights[i];
	}

    res             = lerp(Original, ApplyToSkin(res, coord), SkinSmoothingPower);
    Mask            = lerp(0.0, 1.0, smoothstep(Threshold - (Threshold*ThresholdRange), Threshold,Mask * 20));
    if (HighlightMasking==true) { res = lerp(Original, res, Mask); }
    return res;
}