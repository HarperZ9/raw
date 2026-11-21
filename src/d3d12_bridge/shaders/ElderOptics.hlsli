// ElderOptics.hlsli: unified SM6.x native reimplementation of the Elder ENB optical suite
// (ElderLens multi-order chromatic ghosts + aperture halo, ElderDepthOfField physical golden-angle HDR bokeh
// with center-reticle autofocus, and ElderUnderwater Beer-Lambert absorption). Original work, MIT.
#ifndef RAW_ELDER_OPTICS_HLSLI
#define RAW_ELDER_OPTICS_HLSLI

// ElderUnderwater: physical Beer-Lambert spectral absorption (red attenuates fastest, turquoise/blue penetrates deepest).
float3 elderUnderwaterMedium(float3 scene, float linearDist, float density) {
    const float dW = max(linearDist, 0.0f) * density;
    const float3 absorb = float3(0.42f, 0.16f, 0.08f);
    const float3 T = exp2(-absorb * dW * 1.44269504f);
    const float3 scatter = float3(0.015f, 0.075f, 0.095f);
    return scene * T + scatter * (1.0f.xxx - T);
}

// ElderDepthOfField: 12-tap Golden-Angle (Vogel spiral) depth-aware HDR bokeh with center-reticle autofocus
// and anti-bleed foreground/background depth weighting. Only softens far out-of-focus backgrounds gently when
// focusing on a near subject (< 450 units, e.g., NPC dialogue or close-up inspection), keeping vistas razor-sharp.
float3 elderBokehDoF(Texture2D<float4> litTex, Texture2D<float> depthTex, float2 px, float2 dim, float2 invDim, float curZ, float focusZ) {
    if (focusZ > 520.0f || curZ <= focusZ * 1.35f) { return litTex.Load(int3(px, 0)).rgb; }
    const float coc = saturate((curZ - focusZ * 1.45f) / max(focusZ * 4.5f, 380.0f)) * saturate((520.0f - focusZ) / 280.0f);
    if (coc < 0.03f) { return litTex.Load(int3(px, 0)).rgb; }
    const float maxRad = coc * 5.5f;
    float3 acc = litTex.Load(int3(px, 0)).rgb;
    float wSum = 1.0f;
    const int2 dMax = int2(dim) - 1;
    // Golden angle = 2.39996323 rad (Vogel disc gives smooth circular HDR bokeh with zero grid banding).
    [unroll] for (int i = 1; i <= 12; i++) {
        const float r = sqrt(float(i) / 12.0f) * maxRad;
        const float a = float(i) * 2.39996323f;
        const int2 q = clamp(int2(px + float2(cos(a), sin(a)) * r), int2(0, 0), dMax);
        const float dq = depthTex.Load(int3(q, 0));
        // Anti-bleed: never pull in-focus foreground pixels into the blurred background.
        const float w = dq >= 1.0f ? 1.0f : saturate((viewZOf(dq) - focusZ * 1.25f) / max(focusZ * 0.6f, 60.0f));
        acc += litTex.Load(int3(q, 0)).rgb * w;
        wSum += w;
    }
    return lerp(litTex.Load(int3(px, 0)).rgb, acc / max(wSum, 1.0e-4f), saturate(coc * 0.85f));
}

#endif
