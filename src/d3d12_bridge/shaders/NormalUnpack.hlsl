// NormalUnpack.hlsl: SM 6.6/6.9/6.10 compute pass that unpacks Skyrim SE's R8G8B8A8_UNORM
// stereographic octahedral view-space normal + Specular gloss into a linear RGBA16F packed
// world-space normal + roughness buffer (RGB = linear world-space normal, A = linear perceptual
// roughness) for NVIDIA DLSS Ray Reconstruction (ePacked).
// Wave 57: Safe Z-reconstruction clamping + physically grounded Toksvig / Kaplanyan-Hill normal-
// variance roughness anti-aliasing (eliminates distant specular shimmer on stone, slate & pine bark
// without blurring smooth surfaces or leaking across silhouette edges). Original work, MIT.
Texture2D<float4> NormalIn : register(t0);
Texture2D<float4> SpecIn   : register(t1);
RWTexture2D<float4> OutNR  : register(u0);

cbuffer UnpackCB : register(b0) {
    float4 v2w0;
    float4 v2w1;
    float4 v2w2;
    uint   width;
    uint   height;
    uint   pad0;
    uint   pad1;
};

// Safe stereographic octahedral view-normal decode with strict Z-reconstruction clamping & world-space transform.
float3 decodeWorldNormalSafe(int2 p, int2 maxCoord) {
    const int2 pc = clamp(p, int2(0, 0), maxCoord);
    const float2 raw = NormalIn.Load(int3(pc, 0)).xy;
    const float2 rawSafe = all(isfinite(raw)) ? saturate(raw) : float2(0.5f, 0.5f);
    const float2 e = rawSafe - 0.5f;
    const float s = clamp(dot(e, e), 0.0f, 0.25f);
    // Strict Z-reconstruction clamping prevents negative-sqrt NaN at the stereographic equator.
    const float zRad = sqrt(max(1.0f - 4.0f * s, 0.0f));
    const float3 nvRaw = float3(e * (4.0f * zRad), clamp(8.0f * s - 1.0f, -1.0f, 1.0f));
    const float nvLen2 = dot(nvRaw, nvRaw);
    const float3 nv = (isfinite(nvLen2) && nvLen2 > 1.0e-8f) ? (nvRaw * rsqrt(nvLen2)) : float3(0.0f, 0.0f, -1.0f);
    const float3 nwRaw = float3(dot(v2w0.xyz, nv), dot(v2w1.xyz, nv), dot(v2w2.xyz, nv));
    const float nwLen2 = dot(nwRaw, nwRaw);
    return (isfinite(nwLen2) && nwLen2 > 1.0e-8f) ? (nwRaw * rsqrt(nwLen2)) : float3(0.0f, 0.0f, 1.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height || width == 0u || height == 0u) { return; }
    const int2 p = int2(id.xy);
    const int2 maxCoord = int2(int(width) - 1, int(height) - 1);

    const float3 nw = decodeWorldNormalSafe(p, maxCoord);
    const float glossRaw = SpecIn.Load(int3(p, 0)).y;
    const float gloss = isfinite(glossRaw) ? saturate(glossRaw) : 0.0f;
    const float roughBase = clamp(1.0f - gloss, 0.04f, 1.0f);

    // Wave 57: Physically grounded Toksvig / Kaplanyan-Hill normal-variance roughness anti-aliasing.
    // Reconstructs screen-space normal derivatives (compute-shader equivalent of ddx/ddy) and
    // silhouette-stopped mean normal length |N_avg| so high-frequency stone/slate/bark micro-normals
    // smoothly broaden GGX roughness instead of shimmering, while smooth surfaces & edges stay crisp.
    const float3 nL = decodeWorldNormalSafe(p + int2(-1,  0), maxCoord);
    const float3 nR = decodeWorldNormalSafe(p + int2( 1,  0), maxCoord);
    const float3 nU = decodeWorldNormalSafe(p + int2( 0, -1), maxCoord);
    const float3 nD = decodeWorldNormalSafe(p + int2( 0,  1), maxCoord);

    // Silhouette-safe neighbor weights (rejects hard geometric edges where dot(nTap, nw) < 0.20):
    const float wL = smoothstep(0.20f, 0.55f, dot(nL, nw));
    const float wR = smoothstep(0.20f, 0.55f, dot(nR, nw));
    const float wU = smoothstep(0.20f, 0.55f, dot(nU, nw));
    const float wD = smoothstep(0.20f, 0.55f, dot(nD, nw));

    // Toksvig mean-normal shrinkage factor ft = (1 - |N_avg|) / |N_avg|:
    const float wTotal = 2.0f + wL + wR + wU + wD;
    const float3 nAvg = (nw * 2.0f + nL * wL + nR * wR + nU * wU + nD * wD) / max(wTotal, 1.0e-4f);
    const float nAvgLen = clamp(length(nAvg), 0.15f, 1.0f);
    const float toksvigVar = saturate((1.0f - nAvgLen) / max(nAvgLen, 0.15f));

    // Best-sided finite-difference Jacobian variance (compute equivalent of min-side ddx/ddy):
    const float3 dxR = nR - nw;
    const float3 dxL = nw - nL;
    const float3 dyD = nD - nw;
    const float3 dyU = nw - nU;
    const float varX = min(dot(dxR, dxR), dot(dxL, dxL));
    const float varY = min(dot(dyD, dyD), dot(dyU, dyU));
    const float derivVar = clamp(0.5f * (varX + varY), 0.0f, 0.40f);

    // Map perceptual roughness -> GGX alpha^2, add bounded normal variance, and map back to perceptual roughness:
    const float alpha = roughBase * roughBase;
    const float alpha2 = alpha * alpha;
    const float varianceBoost = min(toksvigVar * 0.48f + derivVar * 0.16f, 0.36f);
    const float alphaFiltered = sqrt(saturate(alpha2 + varianceBoost));
    const float roughAA = clamp(sqrt(max(alphaFiltered, 1.6e-3f)), roughBase, 1.0f);

    OutNR[id.xy] = float4(nw, isfinite(roughAA) ? roughAA : roughBase);
}
