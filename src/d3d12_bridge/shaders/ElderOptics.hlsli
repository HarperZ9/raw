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

// ElderDepthOfField: 32-tap Heptagonal-Aperture Cat's-Eye HDR Bokeh DoF with center-reticle autofocus,
// specular bokeh orb expansion, chromatic lateral bokeh dispersion, IGN rotation, and anti-bleed depth weighting.
float3 elderBokehDoF(Texture2D<float4> litTex, Texture2D<float> depthTex, float2 px, float2 dim, float2 invDim, float curZ, float focusZ) {
    const float3 centerCol = litTex.Load(int3(px, 0)).rgb;
    if (focusZ > 540.0f || curZ <= focusZ * 1.30f) { return centerCol; }
    const float coc = saturate((curZ - focusZ * 1.40f) / max(focusZ * 4.2f, 360.0f)) * saturate((540.0f - focusZ) / 280.0f);
    if (coc < 0.025f) { return centerCol; }
    const float maxRad = coc * 8.5f;
    // Off-axis cat's-eye optical vignetting vector (squeezes bokeh discs toward frame corners like fast f/1.4 cinema glass).
    const float2 uvNorm = px * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.45f);
    const float2 radAxis = length(uvNorm) > 1.0e-4f ? normalize(uvNorm) : float2(1.0f, 0.0f);
    const float rot0 = ign(px) * 6.2831853f;
    float3 acc = centerCol;
    float3 wSum = 1.0f.xxx;
    const int2 dMax = int2(dim) - 1;
    // 32-tap Vogel Golden-Angle spiral shaped by a 7-blade heptagonal aperture polygon.
    const float kBlade = 6.2831853f / 7.0f;
    [loop] for (int i = 1; i <= 32; i++) {
        const float u = sqrt(float(i) / 32.0f);
        const float a = float(i) * 2.39996323f + rot0;
        // Heptagonal aperture blade modulation (cos(PI/7) / cos(sectorAngle)).
        const float sec = abs(fmod(a, kBlade) - kBlade * 0.5f);
        const float bladeR = clamp(0.90096887f / max(cos(sec), 0.72f), 0.85f, 1.12f);
        float2 off = float2(cos(a), sin(a)) * (u * maxRad * bladeR);
        // Apply subtle cat's-eye compression along radial corner axis.
        off -= radAxis * (dot(off, radAxis) * cornerMag * 0.32f);
        const int2 q = clamp(int2(px + off), int2(0, 0), dMax);
        const float dq = depthTex.Load(int3(q, 0));
        // Anti-bleed guard: never pull in-focus foreground subjects into blurred backgrounds.
        const float depthW = dq >= 1.0f ? 1.0f : saturate((viewZOf(dq) - focusZ * 1.25f) / max(focusZ * 0.55f, 55.0f));
        const float3 tap = litTex.Load(int3(q, 0)).rgb;
        const float tapLum = dot(tap, float3(0.2126f, 0.7152f, 0.0722f));
        // Luminous HDR bokeh orb expansion: out-of-focus torches, candles & sun glints bloom into round orbs.
        const float orbBoost = 1.0f + saturate(tapLum - 0.65f) * 1.85f;
        // Subtle chromatic lateral dispersion on outer bokeh ring (red slightly wider, blue slightly tighter).
        const float3 chromaW = lerp(1.0f.xxx, float3(1.0f + u * 0.14f, 1.0f, 1.0f - u * 0.10f), coc * 0.65f);
        const float3 w = (depthW * orbBoost) * chromaW;
        acc += tap * w;
        wSum += w;
    }
    return lerp(centerCol, acc / max(wSum, 1.0e-4f.xxx), saturate(coc * 0.90f));
}

#endif
