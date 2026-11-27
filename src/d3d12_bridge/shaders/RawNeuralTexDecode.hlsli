// RawNeuralTexDecode.hlsli: Wave 90 Soraku-Inspired Compressed 4K PBR Neural/Latent Decoder (SM 6.6 / 6.9 / 6.10).
// Pure math-only HLSL include (zero unbound GPU resource declarations) matching RawNeuralTextureCache.h bit-for-bit.
// Decodes 9-channel PBR material state (Albedo RGB, Normal XY, Roughness, Metallic, AO, Height) from a bilinear
// half-res uint8 latent sample + compact 9->12->9 SiLU residual sharpening MLP.
// Clean-room MIT.

#ifndef RAW_NEURAL_TEX_DECODE_HLSLI
#define RAW_NEURAL_TEX_DECODE_HLSLI

struct RawNtcDecodedPbr {
    float3 albedo;
    float2 normalXY;
    float  roughness;
    float  metallic;
    float  ao;
    float  height;
};

float rawNtcSilu(float x)
{
    return x / (1.0f + exp(-clamp(x, -16.0f, 16.0f)));
}

// Decodes 9-channel PBR vector from dequantized bilinear latent `base9` + residual sharpening MLP gain.
// Keeps register footprint tiny (no large arrays in registers) and strictly NaN-free for SM 6.6/6.9/6.10.
RawNtcDecodedPbr rawNtcDecodePbr9(float base9[9], float w1Scale, float w2Scale, float resGain)
{
    const float hCoupled = (base9[8] - 0.5f) * w1Scale;
    float hid[12];
    [unroll]
    for (uint h = 0u; h < 12u; ++h) {
        const uint cIn = h % 9u;
        const float hW = (h < 3u) ? 24.0f : -12.0f;
        const float acc = (base9[cIn] - 0.5f) * (64.0f * w1Scale) + hCoupled * hW;
        hid[h] = rawNtcSilu(acc);
    }

    float out9[9];
    [unroll]
    for (uint c = 0u; c < 9u; ++c) {
        float res = hid[c] * (48.0f * w2Scale);
        if (c < 3u) {
            res += hid[c + 9u] * (48.0f * w2Scale);
        }
        out9[c] = saturate(base9[c] + res * clamp(resGain, 0.0f, 1.0f));
    }

    RawNtcDecodedPbr o;
    o.albedo    = float3(out9[0], out9[1], out9[2]);
    o.normalXY  = float2(out9[3], out9[4]);
    o.roughness = out9[5];
    o.metallic  = out9[6];
    o.ao        = out9[7];
    o.height    = out9[8];
    return o;
}

#endif // RAW_NEURAL_TEX_DECODE_HLSLI
