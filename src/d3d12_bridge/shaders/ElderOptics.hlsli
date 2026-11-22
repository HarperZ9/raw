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
float3 elderBokehDoF(Texture2D<float4> litTex, Texture2D<float> depthTex, float2 px, float2 dim, float2 invDim,
                     float curZ, float focusZ, float dofMaxDist, float dofRadius, float dofOrbBoost) {
    const float3 centerCol = litTex.Load(int3(px, 0)).rgb;
    if (dofMaxDist <= 1.0f || focusZ > dofMaxDist || curZ <= focusZ * 1.30f) { return centerCol; }
    const float coc = saturate((curZ - focusZ * 1.40f) / max(focusZ * 4.2f, 360.0f)) * saturate((dofMaxDist - focusZ) / max(dofMaxDist * 0.52f, 120.0f));
    if (coc < 0.025f) { return centerCol; }
    const float maxRad = coc * clamp(dofRadius, 1.5f, 24.0f);
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
        const float orbBoost = 1.0f + saturate(tapLum - 0.65f) * dofOrbBoost;
        // Subtle chromatic lateral dispersion on outer bokeh ring (red slightly wider, blue slightly tighter).
        const float3 chromaW = lerp(1.0f.xxx, float3(1.0f + u * 0.14f, 1.0f, 1.0f - u * 0.10f), coc * 0.65f);
        const float3 w = (depthW * orbBoost) * chromaW;
        acc += tap * w;
        wSum += w;
    }
    return lerp(centerCol, acc / max(wSum, 1.0e-4f.xxx), saturate(coc * 0.90f));
}

// Pre-Pass Physical Rain-on-Lens Droplets & Blizzard Frost Edge (runs on Linear HDR pre-tonemap):
// Procedural gravity-sliding lens droplets during rain (weather.w) refract HDR light behind each bead
// and catch sky specular glints; blizzard frost (weather.z) adds subtle crystalline ice refraction at screen corners.
float3 elderWeatherLensPrepass(Texture2D<float4> litTex, float2 px, float2 dim, float2 invDim, float rainAmt, float snowAmt, float timeSec) {
    const float3 base = litTex.Load(int3(px, 0)).rgb;
    if (rainAmt < 0.12f && snowAmt < 0.25f) { return base; }
    const float2 uv = px * invDim;
    const float2 cVec = uv - 0.5f.xx;
    const float edgeMask = smoothstep(0.28f, 0.72f, length(cVec));
    float3 outCol = base;
    if (rainAmt >= 0.12f && edgeMask > 0.01f) {
        const float2 grid = float2(uv.x * 18.0f, uv.y * 10.5f + timeSec * 0.42f);
        const float2 cell = floor(grid);
        const float2 f = frac(grid) - 0.5f;
        const float h1 = frac(sin(dot(cell, float2(127.1f, 311.7f))) * 43758.5453f);
        const float h2 = frac(sin(dot(cell, float2(269.5f, 183.3f))) * 43758.5453f);
        const float2 dropC = float2((h1 - 0.5f) * 0.55f, sin(timeSec * (0.8f + h2) + h1 * 6.28f) * 0.22f);
        const float2 d = f - dropC;
        const float r = 0.09f + h2 * 0.09f;
        const float dist = length(d);
        if (dist < r && h1 > 0.42f) {
            const float dome = sqrt(max(1.0f - (dist / r) * (dist / r), 0.0f));
            const float2 refractPx = clamp(px - (d / max(r, 1.0e-3f)) * (14.0f * dome * rainAmt * edgeMask), 0.0f.xx, dim - 1.0f);
            const float3 refrCol = litTex.Load(int3(refractPx, 0)).rgb;
            const float glint = pow(saturate(dot(normalize(float3(d / r, dome)), normalize(float3(0.35f, -0.45f, 0.82f)))), 28.0f);
            outCol = lerp(outCol, refrCol + glint * 0.35f, saturate(dome * rainAmt * edgeMask * 0.85f));
        }
    }
    if (snowAmt >= 0.25f && edgeMask > 0.15f) {
        const float2 fUv = uv * 28.0f;
        const float frostN = frac(sin(dot(floor(fUv), float2(41.3f, 289.1f))) * 43758.5453f);
        const float frostW = smoothstep(0.35f, 0.85f, edgeMask) * saturate((snowAmt - 0.25f) * 1.35f) * (0.35f + 0.65f * frostN);
        outCol = lerp(outCol, outCol * float3(0.92f, 0.98f, 1.06f) + float3(0.035f, 0.055f, 0.075f), frostW * 0.32f);
    }
    return outCol;
}

#endif
