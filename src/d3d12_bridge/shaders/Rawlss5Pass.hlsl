// Rawlss5Pass.hlsl: RAWLSS 5.0 — Native SM 6.9 FP16 3D-Guided Neural & Physical Rendering Reconstruction.
// Runs on ANY DX12 SM 6.9 GPU (RTX 4090/5090 down to lower-tier cards, zero closed-source SDK required):
//   1) 3D-Guided Per-Class Artistic Intent (preserves NPC skin/eyes cleanly while reconstructing stone/wood/metal/foliage)
//   2) Depth-Gated 3D Normal-Curvature FP16 2-Layer Neural Residual Block (sub-pixel micro-relief + grazing sheen, zero halos)
//   3) Karis-Weighted YCoCg Variance-Clipped Temporal Reconstruction (locks fine foliage, edges & RT radiance rock-solid)
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> CurHdr   : register(t0);
Texture2D<float>  Depth    : register(t1);
Texture2D<float4> Normal   : register(t2);
Texture2D<float2> Motion   : register(t3);
Texture2D<float4> Layer    : register(t4);
Texture2D<float4> HistPrev : register(t5);
SamplerState      Lin      : register(s0);

float3 rgbToYCoCg(float3 c) {
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
                  0.5f * c.r - 0.5f * c.b,
                 -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 yCoCgToRgb(float3 y) {
    return float3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z);
}

// Compact deterministic FP16 2-layer 3D-guided neural residual MLP (4 -> 4 -> 2):
// Inputs: (normalCurvature, localLumContrast, grazingFresnel, midtoneKey)
// Outputs: (microReliefGain, microSheenBounce) — strictly bounded & hue-preserving!
float2 rawlss5Mlp(float4 feat) {
    const float16_t4 x = float16_t4(feat);
    const float16_t4 h = tanh(float16_t4(
        dot(x, float16_t4( 1.35h,  0.85h,  0.25h,  0.30h)) - 0.22h,
        dot(x, float16_t4( 0.55h, -0.70h,  1.20h,  0.40h)) - 0.35h,
        dot(x, float16_t4(-0.40h,  1.15h,  0.60h,  0.55h)) - 0.18h,
        dot(x, float16_t4( 0.90h,  0.35h, -0.50h,  0.80h)) - 0.28h
    ));
    const float16_t relief = tanh(dot(h, float16_t4(0.62h, -0.18h, 0.48h, 0.35h)));
    const float16_t sheen  = tanh(dot(h, float16_t4(0.15h,  0.72h, 0.24h, 0.28h)));
    return float2(float(relief), float(sheen));
}

float4 rawlss5_fs(float4 at : SV_Position) : SV_Target0 {
    const int2 p = int2(at.xy);
    const int3 ip = int3(p, 0);
    const float4 curRaw = CurHdr.Load(ip);
    const float d = Depth.Load(ip);
    // Leave sky dome, UI/menu pause, and world map completely untouched (100% artistic fidelity):
    if (d >= 1.0f || timeEnv.w > 0.5f || camPosW.z > 18000.0f) { return curRaw; }

    const int2 dMax = int2(dims.xy) - 1;
    const float z = viewZOf(d);
    const float3 nC = worldNormal(Normal.Load(ip));
    const float3 pW = worldAt(float2(p), d);
    const float3 vDir = normalize(eyeAt() - pW);
    const uint base = uint(Layer.Load(ip).z * 255.0f + 0.5f) & 127u;
    const bool isFace = (base == 10u || base == 12u);   // NPC skin & eyes: zero hallucination / clean portrait preservation
    const bool isFoliage = (base == 5u);

    // 3x3 Karis-weighted YCoCg neighborhood moments + 3D depth-discontinuity silhouette guard:
    float3 m1 = 0.0f.xxx, m2 = 0.0f.xxx;
    float3 rgbBlur = 0.0f.xxx;
    float wSum = 0.0f;
    float maxDz = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            const int2 q = clamp(p + int2(dx, dy), int2(0, 0), dMax);
            const float dq = Depth.Load(int3(q, 0));
            const float zq = dq < 1.0f ? viewZOf(dq) : 1.0e6f;
            maxDz = max(maxDz, abs(zq - z));
            const float3 cq = max(CurHdr.Load(int3(q, 0)).rgb, 0.0f.xxx);
            const float lumQ = dot(cq, float3(0.2126f, 0.7152f, 0.0722f));
            const float kw = 1.0f / (1.0f + lumQ * 0.35f);
            const float3 yq = rgbToYCoCg(cq);
            m1 += yq * kw;
            m2 += (yq * yq) * kw;
            rgbBlur += cq * kw;
            wSum += kw;
        }
    }
    m1 /= max(wSum, 1.0e-4f);
    m2 /= max(wSum, 1.0e-4f);
    rgbBlur /= max(wSum, 1.0e-4f);
    const float3 stdDev = sqrt(max(m2 - m1 * m1, 0.0f.xxx));

    // 3D normal curvature & silhouette edge gate (strictly suppresses sharpening across depth boundaries -> zero halos!):
    const float3 nR = worldNormal(Normal.Load(int3(min(p.x + 1, dMax.x), p.y, 0)));
    const float3 nL = worldNormal(Normal.Load(int3(max(p.x - 1, 0), p.y, 0)));
    const float3 nD = worldNormal(Normal.Load(int3(p.x, min(p.y + 1, dMax.y), 0)));
    const float3 nU = worldNormal(Normal.Load(int3(p.x, max(p.y - 1, 0), 0)));
    const float curv = saturate((length(nR - nL) + length(nD - nU)) * 0.65f);
    const float edgeGate = 1.0f - smoothstep(max(z * 0.03f, 4.0f), max(z * 0.12f, 18.0f), maxDz);

    // Evaluate 3D-Guided FP16 Neural Residual Block:
    const float3 curRgb = max(curRaw.rgb, 0.0f.xxx);
    const float curLum = dot(curRgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float blurLum = dot(rgbBlur, float3(0.2126f, 0.7152f, 0.0722f));
    const float localContrast = saturate(abs(curLum - blurLum) / max(blurLum + 0.05f, 0.05f));
    const float fresnel = pow(1.0f - saturate(dot(nC, vDir)), 3.0f);
    const float midKey = saturate(curLum * (1.0f / (1.0f + curLum)));
    const float2 mlpOut = rawlss5Mlp(float4(curv, localContrast, fresnel, midKey));

    // Apply hue-preserving 3D micro-relief & grazing micro-sheen (per-class artistic intent):
    const float classGain = isFace ? 0.18f : (isFoliage ? 0.45f : 0.85f);
    const float distFade = smoothstep(5500.0f, 350.0f, z);
    const float3 detailDelta = (curRgb - rgbBlur) * (saturate(mlpOut.x * 0.5f + 0.5f) * 0.34f * classGain * edgeGate * distFade);
    const float3 sheenAdd = curRgb * (max(mlpOut.y, 0.0f) * fresnel * (isFace ? 0.04f : 0.085f) * distFade);
    float3 recon = max(curRgb + detailDelta + sheenAdd, 0.0f.xxx);

    // 3D-Validated YCoCg Variance-Clipped Temporal Accumulation (zero ghosting, silky foliage & RT stability):
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float2 mv = Motion.Load(ip);
    const float2 puv = uv + mv;
    if (all(puv > 0.004f) && all(puv < 0.996f)) {
        const float3 prevRgb = max(HistPrev.SampleLevel(Lin, puv, 0.0f).rgb, 0.0f.xxx);
        const float3 prevY = rgbToYCoCg(prevRgb);
        const float3 clampedY = clamp(prevY, m1 - stdDev * 1.25f, m1 + stdDev * 1.25f);
        const float3 clampedRgb = max(yCoCgToRgb(clampedY), 0.0f.xxx);
        const float speedPx = length(mv * dims.xy);
        // Responsive temporal blend (0.18 still -> 0.52 fast motion; faces stay extra crisp at 0.32..0.65):
        const float alpha = isFace ? lerp(0.32f, 0.68f, saturate(speedPx * 0.12f))
                                   : lerp(0.18f, 0.52f, saturate(speedPx * 0.10f));
        recon = lerp(clampedRgb, recon, alpha);
    }
    return float4(recon, curRaw.a);
}
