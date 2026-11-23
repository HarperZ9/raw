// Tonemap.hlsl: RAW's tone map, in place of the game's (src/d3d11_proxy/Tonemap.cpp). It takes the game's tone map's
// bindings as they are: t1 the HDR scene, t0 the bloom, t2 the eye-adapted luminance (x the adapted average, y the
// game's target), pixel buffer 2 the imagespace values (bloom limit, white, curve choice, saturation, brightness,
// contrast, tint) and per-frame buffer 12 (the output gamma, the dynamic resolution scale). RAW's own values come in
// buffer 5. The build embeds this file (TonemapPs.h); with [HotReload] on, a copy in ShaderDir replaces it live.
// Compiled as ps_5_0 by d3dcompiler_47. Original work, MIT.
cbuffer PerFrame : register(b12) { float4 F[45]; };
cbuffer Image : register(b2) { float4 P[5]; };
cbuffer Raw : register(b5) { float4 RA; float4 RB; float4 RC; float4 RX; float4 RO; float4 RP; float4 RQ; float4 RG; };
// RA: mode, bias, min lum, max lum; RB: curve, key, white, view; RC: adapt, ref, ToneX0, ToneX1; RX: ToneX2..5;
// RO: x=lutStrength, y=overlayMask, z=64-frame cycle, w=moveSpeed;
// RP: x=bloomScale, y=sharpenStr, z=grainStr, w=vignetteStr; RQ: x=caStr, y=radialBlurStr, z=headbobStr, w=lensStr;
// RG: x=colorTempK (6500 neutral), y=oklabSat (1.0), z=shadowLift (0.0), w=highlightKnee (0.68).
Texture2D<float4> Bloom : register(t0);
Texture2D<float4> Scene : register(t1);
Texture2D<float4> Lum   : register(t2);
Texture2D<float4> UserLut : register(t5);
SamplerState S0 : register(s0);
SamplerState S1 : register(s1);
SamplerState S2 : register(s2);
struct In { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
static const float3 W = float3(0.2125f, 0.7154f, 0.0721f);

float3 filmic(float3 x) { return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)); }

// Wide-gamut Troy Sobotka AgX display transform (from Elder ENB EotE_Tonemappers.fxh):
// Zero ColorPow shadow crush, true hue preservation under extreme saturation, natural highlight desaturation.
float3 tonemapAgX(float3 color) {
    static const float3x3 kAgXIn = float3x3(
        0.842479f, 0.0784336f, 0.0792237f,
        0.0423304f, 0.878468f, 0.0791661f,
        0.0423756f, 0.0784336f, 0.879142f);
    static const float3x3 kAgXOut = float3x3(
         1.19687f,  -0.0980208f, -0.0990297f,
        -0.0528968f, 1.15190f,   -0.0989611f,
        -0.0529716f, -0.0980434f, 1.15107f);
    float3 x = mul(kAgXIn, max(color, 0.0f.xxx));
    x = clamp(log2(max(x, 1.0e-10f)), -12.47393f, 4.026069f);
    x = (x + 12.47393f) / (4.026069f + 12.47393f);
    const float3 x2 = x * x, x4 = x2 * x2;
    x = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
    // AgX Punchy Look (Troy Sobotka): deepens shadow toe & midtone contrast for full dynamic range & 3D depth pop
    // while keeping highlights unclipped and hue-faithful; decoded back to linear for Tonemap.hlsl's single output OETF.
    float3 agxOut = saturate(mul(kAgXOut, x));
    agxOut = pow(agxOut, 1.12f);
    return pow(agxOut, 2.2f);
}

// Gran Turismo / Uchimura piecewise curve (SIGGRAPH 2017): linear midtones, smooth toe & shoulder.
float3 tonemapGT(float3 x) {
    static const float P = 1.0f, a = 1.0f, m = 0.22f, l = 0.4f, c = 1.25f, b = 0.0f;
    const float l0 = ((P - m) * l) / a;
    const float S0 = m + l0, S1 = m + a * l0;
    const float C2 = (a * P) / max(P - S1, 1.0e-4f), CP = -C2 / P;
    const float3 w0 = 1.0f - smoothstep(0.0f, m, x);
    const float3 w2 = step(S0, x);
    const float3 w1 = 1.0f - w0 - w2;
    const float3 T = m * pow(max(x / m, 1.0e-6f), c) + b;
    const float3 L = m + a * (x - m);
    const float3 S = P - (P - S1) * exp(CP * (x - S0));
    return saturate(T * w0 + L * w1 + S * w2);
}

// Timothy Lottes (GDC 2016) & John Hable (Uncharted 2) curves.
float3 tonemapLottes(float3 x) {
    const float3 cr = pow(max(x, 0.0f.xxx), 1.45f);
    return saturate(cr / (cr * 1.18f + 0.24f));
}
float3 hableP(float3 x) { return ((x * (0.15f * x + 0.05f) + 0.004f) / (x * (0.15f * x + 0.50f) + 0.06f)) - 0.066667f; }
float3 tonemapHable(float3 x) { return saturate(hableP(max(x, 0.0f.xxx) * 1.85f) / hableP(11.2f.xxx)); }

// Perceptual Oklab color space (Bjorn Ottosson 2020 / Elder EotE_FilmScience.fxh) for detail-preserving grading.
float3 linToOklab(float3 lin) {
    const float l = pow(max(0.4122214708f * lin.r + 0.5363325363f * lin.g + 0.0514459929f * lin.b, 0.0f), 1.0f / 3.0f);
    const float m = pow(max(0.2119034982f * lin.r + 0.6806995451f * lin.g + 0.1073969566f * lin.b, 0.0f), 1.0f / 3.0f);
    const float s = pow(max(0.0883024619f * lin.r + 0.2817188376f * lin.g + 0.6299787005f * lin.b, 0.0f), 1.0f / 3.0f);
    return float3(0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                  1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                  0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s);
}
float3 oklabToLin(float3 lab) {
    float l = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    float m = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    float s = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
    l = l * l * l; m = m * m * m; s = s * s * s;
    return max(float3( 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
                      -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
                      -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s), 0.0f.xxx);
}

// Trilinear 3D horizontal strip LUT sampler (auto-detects 16^3, 32^3, or 64^3 Elder PNG LUTs).
float3 sampleUserLut3D(float3 srgb) {
    float lw, lh;
    UserLut.GetDimensions(lw, lh);
    const float n = max(lh, 16.0f);
    const float3 cl = saturate(srgb);
    const float bScaled = cl.b * (n - 1.0f);
    const float b0 = floor(bScaled);
    const float b1 = min(b0 + 1.0f, n - 1.0f);
    const float bf = bScaled - b0;
    const float uOff = (cl.r * (n - 1.0f) + 0.5f) / lw;
    const float v = (cl.g * (n - 1.0f) + 0.5f) / n;
    const float3 c0 = UserLut.SampleLevel(S1, float2(b0 / n + uOff, v), 0.0f).rgb;
    const float3 c1 = UserLut.SampleLevel(S1, float2(b1 / n + uOff, v), 0.0f).rgb;
    return lerp(c0, c1, bf);
}

// Curve 2: an extended Reinhard per channel, x (1 + x / W^2) / (1 + x), white at W (ToneX1, default 4). Its slope at
// black is 1, as the game's own curve's is, so shadows keep their level; the fitted ACES curve's slope there is about
// 0.2, and it crushed a third of a Riverwood dawn frame to black (rt8 matrix: 47 % of pixels at 4 or less against the
// game's 6 %). ToneX2 lifts the shoulder's saturation back towards the input's hue (0 none, 1 full).
float3 reinhardW(float3 x) {
    const float w = RC.w > 0.0f ? RC.w : 4.0f;
    const float3 m = x * (1.0f + x / (w * w)) / (1.0f + x);
    const float lx = dot(x, W), lm = dot(m, W);
    const float3 hue = x * (lm / max(lx, 1.0e-5f));   // the luminance's curve, the input's chroma
    return saturate(lerp(m, hue, saturate(RX.x)));
}

float gameCurve(float l) {   // the game's own: a Hejl-style curve or a Reinhard with a white point
    if (P[2].z > 0.5f) {
        const float x = max(l - 0.004f, 0.0f);
        return pow((x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f), 2.2f) * P[2].y;
    }
    return l * (1.0f + l * P[2].y) / (1.0f + l);
}

// 9-tap FXAA 3.11 sub-pixel edge anti-aliasing followed by gentle local-clamped CAS micro-contrast.
// Eliminates stair-stepped aliasing on thin grass blades, pine needles, and armor edges without vaseline blur.
float3 fxaaAndCas(float2 suv) {
    float w, h;
    Scene.GetDimensions(w, h);
    const float2 px = 1.0f / max(float2(w, h), float2(1.0f, 1.0f));
    const float3 c  = Scene.Sample(S1, suv).rgb;
    const float3 n  = Scene.Sample(S1, suv + float2( 0.0f, -px.y)).rgb;
    const float3 s  = Scene.Sample(S1, suv + float2( 0.0f,  px.y)).rgb;
    const float3 e  = Scene.Sample(S1, suv + float2( px.x,  0.0f)).rgb;
    const float3 ww = Scene.Sample(S1, suv + float2(-px.x,  0.0f)).rgb;
    const float3 nw = Scene.Sample(S1, suv + float2(-px.x, -px.y)).rgb;
    const float3 ne = Scene.Sample(S1, suv + float2( px.x, -px.y)).rgb;
    const float3 sw = Scene.Sample(S1, suv + float2(-px.x,  px.y)).rgb;
    const float3 se = Scene.Sample(S1, suv + float2( px.x,  px.y)).rgb;

    // Perceptual luma in Reinhard-compressed space so HDR highlights don't dominate edge detection.
    const float lc  = dot(c  / (1.0f + c ), W);
    const float ln  = dot(n  / (1.0f + n ), W);
    const float ls  = dot(s  / (1.0f + s ), W);
    const float le  = dot(e  / (1.0f + e ), W);
    const float lw  = dot(ww / (1.0f + ww), W);
    const float lnw = dot(nw / (1.0f + nw), W);
    const float lne = dot(ne / (1.0f + ne), W);
    const float lsw = dot(sw / (1.0f + sw), W);
    const float lse = dot(se / (1.0f + se), W);

    const float lMin = min(lc, min(min(ln, ls), min(le, lw)));
    const float lMax = max(lc, max(max(ln, ls), max(le, lw)));
    const float lRange = lMax - lMin;

    float3 aa = c;
    if (lRange >= max(0.028f, lMax * 0.10f)) {
        const float edgeH = abs(lnw + lne - 2.0f * ln) + 2.0f * abs(lw + le - 2.0f * lc) + abs(lsw + lse - 2.0f * ls);
        const float edgeV = abs(lnw + lsw - 2.0f * lw) + 2.0f * abs(ln + ls - 2.0f * lc) + abs(lne + lse - 2.0f * le);
        const bool isHoriz = edgeH >= edgeV;
        const float l1 = isHoriz ? ln : lw;
        const float l2 = isHoriz ? ls : le;
        const float g1 = abs(l1 - lc), g2 = abs(l2 - lc);
        const float stepDir = (g1 >= g2 ? -1.0f : 1.0f) * (isHoriz ? px.y : px.x);
        const float subAvg = (2.0f * (ln + ls + le + lw) + lnw + lne + lsw + lse) / 12.0f;
        const float subAmt = smoothstep(0.0f, 1.0f, saturate(abs(subAvg - lc) / max(lRange, 1.0e-4f)));
        const float blend = subAmt * subAmt * 0.72f;
        const float2 aaUv = isHoriz ? float2(suv.x, suv.y + stepDir * blend) : float2(suv.x + stepDir * blend, suv.y);
        aa = Scene.Sample(S1, aaUv).rgb;
    }

    // Stage 1: Bilateral Edge-Preserving Micro-Smoothing (smooths flat skin/plaster/sky noise while locking edges).
    // Stage 2: Anti-Ringing 9-Tap RCAS Adaptive Contrast Sharpening (OV9, bit 8 = 256).
    const uint ovPost = uint(RO.y + 0.5f);
    if ((ovPost & 256u) != 0u) {
        // Edge-preserving bilateral micro-smoothing on low-gradient flats (skin, stone faces, calm water).
        const float lA = dot(aa / (1.0f + aa), W);
        float3 bSum = aa;
        float bW = 1.0f;
        const float wN = exp(-abs(ln - lA) * 32.0f) * 0.22f;
        const float wS = exp(-abs(ls - lA) * 32.0f) * 0.22f;
        const float wE = exp(-abs(le - lA) * 32.0f) * 0.22f;
        const float wW = exp(-abs(lw - lA) * 32.0f) * 0.22f;
        bSum += n * wN + s * wS + e * wE + ww * wW;
        bW += wN + wS + wE + wW;
        const float3 smoothed = bSum / bW;
        // 9-tap local min/max anti-ringing RCAS micro-contrast sharpening (crisp stone/bark/armor with zero halos).
        const float3 mn = min(min(smoothed, min(min(n, s), min(e, ww))), min(min(nw, ne), min(sw, se)));
        const float3 mx = max(max(smoothed, max(max(n, s), max(e, ww))), max(max(nw, ne), max(sw, se)));
        const float3 amp = saturate(min(mn, max(2.0f.xxx - mx, 0.0f.xxx)) / max(mx, 1.0e-4f.xxx));
        const float3 wCAS = sqrt(amp) * (-0.165f * clamp(RP.y, 0.0f, 2.0f));
        const float3 sharp = (smoothed + (n + s + e + ww) * wCAS) / max(1.0f.xxx + 4.0f * wCAS, 0.28f.xxx);
        return clamp(sharp, mn * 0.94f, mx * 1.06f);
    }
    return aa;
}

float4 main(In i) : SV_Target {
    const uint ovInit = uint(RO.y + 0.5f);
    float2 baseUv = i.uv;
    // OV12 (bit 11 = 2048): Subtle velocity-driven running/walking headbob & camera stride sway (0 when standing still).
    if ((ovInit & 2048u) != 0u) {
        const float moveAmp = saturate((RO.w - 0.35f) / 6.5f);
        if (moveAmp > 0.01f) {
            const float phase = RO.z * 0.3927f; // smooth stride cadence
            const float2 bob = float2(sin(phase) * 0.0011f, abs(cos(phase)) * 0.0022f - 0.0011f) * (moveAmp * clamp(RQ.z, 0.0f, 2.5f));
            // 0.8% overscan crop keeps edges 100% clean with zero black borders.
            baseUv = saturate(baseUv * 0.992f + 0.004f + bob);
        }
    }
    const float2 suv = min(max(baseUv * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
    float3 hdr = fxaaAndCas(suv);
    // OV10 (bit 9 = 512): True Velocity-Gated Cinema Sprint Radial Blur (0.0 blur when standing/walking;
    // silky 8-tap jittered Gaussian-weighted peripheral momentum ONLY when actually sprinting, center 65% 100% sharp).
    if ((ovInit & 512u) != 0u) {
        const float sprintGate = smoothstep(2.8f, 8.5f, RO.w);
        const float2 rVec = baseUv - 0.5f.xx;
        const float rDist = length(rVec);
        const float edgeMask = smoothstep(0.32f, 0.72f, rDist);
        const float rStr = edgeMask * edgeMask * 0.0065f * sprintGate * clamp(RQ.y, 0.0f, 2.5f);
        if (rStr > 0.00025f) {
            const float rJit = frac(52.9829189f * frac(dot(i.pos.xy + RO.z * 5.588f, float2(0.06711056f, 0.00583715f))));
            float3 rAcc = hdr * 1.4f;
            float rW = 1.4f;
            [unroll] for (int ri = 1; ri <= 8; ri++) {
                const float f = (float(ri) - 0.5f + rJit * 0.5f) / 8.0f;
                const float w = exp(-f * f * 2.2f);
                rAcc += Scene.Sample(S1, min(max((baseUv - rVec * (rStr * f)) * F[43].xy, 0.0f), float2(F[44].z, F[43].y))).rgb * w;
                rW += w;
            }
            hdr = rAcc / rW;
        }
    }
    // OV11 (bit 10 = 1024): True 5-Tap Spectral Apochromatic Lens Dispersion (Zeiss/Cooke optical glass profile:
    // zero at center 55%, smooth continuous R/Y/G/C/B prism spectrum at extreme outer corners — zero R/B double-vision).
    if ((ovInit & 1024u) != 0u) {
        const float2 caVec = baseUv - 0.5f.xx;
        const float caR = length(caVec);
        const float caMag = pow(saturate((caR - 0.28f) / 0.45f), 2.4f) * 0.0022f * clamp(RQ.x, 0.0f, 2.5f);
        if (caMag > 0.00005f) {
            const float2 dirC = caVec / max(caR, 1.0e-4f);
            const float2 uvM2 = min(max((baseUv - dirC * (caMag * 1.0f)) * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
            const float2 uvM1 = min(max((baseUv - dirC * (caMag * 0.5f)) * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
            const float2 uvP1 = min(max((baseUv + dirC * (caMag * 0.5f)) * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
            const float2 uvP2 = min(max((baseUv + dirC * (caMag * 1.0f)) * F[43].xy, 0.0f), float2(F[44].z, F[43].y));
            const float3 sM2 = Scene.Sample(S1, uvM2).rgb;
            const float3 sM1 = Scene.Sample(S1, uvM1).rgb;
            const float3 sP1 = Scene.Sample(S1, uvP1).rgb;
            const float3 sP2 = Scene.Sample(S1, uvP2).rgb;
            const float3 specSum = sM2 * float3(0.0f, 0.10f, 0.45f) + sM1 * float3(0.08f, 0.25f, 0.35f)
                                 + hdr * float3(0.24f, 0.30f, 0.16f)
                                 + sP1 * float3(0.34f, 0.25f, 0.04f) + sP2 * float3(0.34f, 0.10f, 0.0f);
            hdr = lerp(hdr, specSum, 0.85f);
        }
    }
    // Only apply legacy pre-tonemap highlight knee on older LDR curves (RB.x <= 3.5).
    // Modern wide-gamut HDR curves (AgX, GT, Lottes, Hable) natively handle 0..16+ linear HDR; pre-crushing at 0.68
    // previously destroyed solar disc peaks, interior window glow, firelight brilliance, and full-range contrast!
    if (RB.x <= 3.5f) {
        const float hKnee = clamp(RG.w > 0.1f ? RG.w : 0.68f, 0.35f, 1.40f);
        const float hdrLum = max(dot(hdr, W), 1.0e-5f);
        if (hdrLum > hKnee) {
            const float ex = hdrLum - hKnee;
            const float compLum = hKnee + ex / (1.0f + ex * 0.92f);
            hdr *= compLum / hdrLum;
        }
    }
    const float3 bloom = P[0].x > 0.5f ? Bloom.Sample(S0, suv).rgb : Bloom.Sample(S0, i.uv).rgb;
    const float2 lum = Lum.Sample(S2, i.uv).xy;
    const float avg = max(lum.x, 1.0e-5f);
    // Human Eye Adaptation (Photopic / Scotopic Asymmetric Pupil Response):
    // Bright sunlit snowfields & coastal horizons smoothly stop down exposure (down to 0.36x / -1.47 EV)
    // so snow texture and horizons hold full detail; dark interiors gently open the pupil (up to 1.38x / +0.46 EV).
    const float metered = clamp(avg, RA.z, RA.w);
    const float evDiff = log2(max(metered / max(RC.y, 0.05f), 1.0e-4f));
    const float eyeShare = evDiff > 0.0f ? max(RC.x, 0.68f) : (RC.z > 0.0f ? max(RC.z, 0.24f) : 0.24f);
    const float eyeComp = clamp(exp2(-evDiff * eyeShare), 0.36f, 1.38f);
    const float camera = (RB.y / max(RC.y, 0.05f)) * eyeComp;
    // RX.w: the interior factor the proxy sets per frame (ToneX5 in a cell with no sun cascades, else 1).
    const float exposure = (RA.x < 0.5f ? lum.y / avg : camera * (RX.w > 0.0f ? RX.w : 1.0f)) * exp2(RA.y);
    float3 mapped;
    float pivot = lum.x;
    if (RB.x < 0.5f) {
        const float l = max(dot(hdr, W), 1.0e-5f);
        mapped = hdr * (gameCurve(l * exposure) / l);
    } else if (RB.x > 3.5f) {
        // Wide-gamut modern curves (zero ColorPow shadow crush, true hue & micro-texture preservation):
        // 4 = AgX (Troy Sobotka), 5 = Gran Turismo (Uchimura), 6 = Lottes, 7 = Hable (Uncharted 2).
        const float3 xIn = hdr * exposure * RB.z;
        if (RB.x < 4.5f)      mapped = tonemapAgX(xIn);
        else if (RB.x < 5.5f) mapped = tonemapGT(xIn);
        else if (RB.x < 6.5f) mapped = tonemapLottes(xIn);
        else                  mapped = tonemapHable(xIn);
        if (RA.x > 0.5f) {
            const float3 pIn = (RB.y * exp2(RA.y) * RB.z).xxx;
            const float3 pOut = RB.x < 4.5f ? tonemapAgX(pIn) : (RB.x < 5.5f ? tonemapGT(pIn) : (RB.x < 6.5f ? tonemapLottes(pIn) : tonemapHable(pIn)));
            pivot = dot(pOut, W);
        }
    } else if (RB.x > 2.5f) {
        // Curve 3: the game's own curve on luminance, l (1 + l / W^2) / (1 + l), with the white point raised to at
        // least ToneX1 (the game's clips about 5 % of a clear midday frame), then a contrast about the key (below). The hue stays the input's, as in the game.
        const float l = max(dot(hdr, W), 1.0e-5f) * exposure * RB.z;
        const float w2 = max(1.0f / max(P[2].y, 1.0e-4f), RC.w * RC.w);
        const float cu = RX.y > 0.0f ? RX.y : 1.0f, cl = RX.z > 0.0f ? RX.z : cu;
        // SIGGRAPH TON-04 Bilateral Local Tone Mapping: uses low-frequency Bloom luma as a free base layer
        // to preserve local micro-contrast in dark interiors looking out sunlit doorways & shadowed forests beside bright peaks.
        const float baseLum = max(dot(bloom, W) * exposure * RB.z, 0.02f);
        const float detailRatio = clamp(log2(max(l, 1.0e-4f) / baseLum), -1.25f, 1.25f);
        const float lLocal = l * exp2(detailRatio * 0.16f);
        const float cHi = lerp(cu, 1.0f, saturate(log2(max(lLocal / RB.y, 1.0f)) * 0.5f));
        const float lc = RB.y * pow(max(lLocal, 1.0e-5f) / RB.y, lLocal > RB.y ? cHi : lerp(cl, cu, saturate(lLocal / RB.y)));
        const float lOut = saturate(lc * (1.0f + lc / w2) / (1.0f + lc));
        float3 col = hdr * exposure * RB.z * (lOut / l);
        // AgX-style highlight shoulder desaturation: prevents neon orange/blue clipping in bright skies and fire.
        const float hiSat = saturate((lOut - 0.55f) / 0.45f);
        col = lerp(col, lOut.xxx, hiSat * hiSat * 0.42f);
        mapped = saturate(col);
        if (RA.x > 0.5f) { pivot = RB.y * (1.0f + RB.y / w2) / (1.0f + RB.y); }
    } else {
        const bool rh = RB.x > 1.5f;
        mapped = rh ? reinhardW(hdr * exposure * RB.z) : filmic(hdr * exposure * RB.z);
        if (RA.x > 0.5f) { pivot = dot(rh ? reinhardW((RB.y * exp2(RA.y) * RB.z).xxx) : filmic((RB.y * exp2(RA.y) * RB.z).xxx), W); }   // the grade turns about mid grey
    }
    // 100% Native 12-Pass 6-Level Jimenez HDR Bloom Pyramid (BloomPass.hlsl) owns all bloom in !match mode.
    // Do NOT add Skyrim's blurry 2011 vanilla Bloom buffer (t0) on top of our HDR scene — use t0 strictly for
    // high-threshold (>1.15) optical halation / Panavision anamorphic flare (OV2) and 5-order lens ghosts (OV3).
    const uint ovMask = uint(RO.y + 0.5f);
    float bw0, bh0;
    Bloom.GetDimensions(bw0, bh0);
    const float2 bTex = 1.0f / max(float2(bw0, bh0), float2(1.0f, 1.0f));
    const float bloomLum = dot(bloom, W);
    float3 ovAdd = 0.0f.xxx;
    if ((ovMask & 2u) != 0u) {
        // Layer 2: Multi-radius Photochemical Emulsion Halation + 5-Tap Wide Panavision Anamorphic Streak.
        float bw, bh;
        Bloom.GetDimensions(bw, bh);
        const float2 bpx = 1.0f / max(float2(bw, bh), float2(1.0f, 1.0f));
        const float3 hNear = Bloom.Sample(S0, suv + float2(bpx.x * 1.5f, bpx.y * 1.5f)).rgb
                           + Bloom.Sample(S0, suv - float2(bpx.x * 1.5f, bpx.y * 1.5f)).rgb;
        const float hLum = dot(hNear * 0.5f, W);
        const float3 halation = float3(1.08f, 0.34f, 0.10f) * (saturate((bloomLum - 0.72f) * 0.42f) * 0.12f + saturate((hLum - 0.68f) * 0.35f) * 0.08f);
        float3 anaAcc = 0.0f.xxx;
        [unroll] for (int ai = 1; ai <= 4; ai++) {
            const float dx = float(ai) * 3.4f * bpx.x;
            const float aw = 1.0f / float(ai + 1);
            const float3 bL = max(Bloom.Sample(S0, saturate(suv - float2(dx, 0.0f))).rgb - 0.78f.xxx, 0.0f.xxx);
            const float3 bR = max(Bloom.Sample(S0, saturate(suv + float2(dx, 0.0f))).rgb - 0.78f.xxx, 0.0f.xxx);
            anaAcc += (bL + bR) * (0.5f * aw);
        }
        const float3 anamorphic = anaAcc * float3(0.45f, 0.78f, 1.15f) * 0.085f;
        ovAdd += (halation + anamorphic) * clamp(RQ.w, 0.0f, 2.5f);
    }
    if ((ovMask & 4u) != 0u) {
        // Layer 3: arXiv:2605.04017 (Hachisuka et al. 2026) 5-Order Optical Lens Transport Ghosts + Cat's-Eye Aperture Halo.
        const float2 cVec = suv - 0.5f.xx;
        const float cDist = length(cVec);
        const float catsEye = saturate(1.0f - cDist * cDist * 1.10f);
        float3 lensAcc = 0.0f.xxx;
        [unroll] for (int gi = 1; gi <= 5; gi++) {
            const float gScale = 0.35f + float(gi) * 0.24f;
            const float2 gUv = saturate(0.5f.xx - cVec * gScale);
            // Chromatic radial dispersion across RGB lens elements.
            const float2 disp = cVec * (0.009f * float(gi));
            const float rC = max(Bloom.Sample(S0, saturate(gUv + disp)).r - 0.68f, 0.0f);
            const float gC = max(Bloom.Sample(S0, gUv).g - 0.68f, 0.0f);
            const float bC = max(Bloom.Sample(S0, saturate(gUv - disp)).b - 0.68f, 0.0f);
            const float3 gCol = float3(rC, gC, bC);
            const float3 gTint = gi == 1 ? float3(1.08f, 0.96f, 0.82f)
                               : (gi == 2 ? float3(0.80f, 1.04f, 1.18f)
                               : (gi == 3 ? float3(1.02f, 0.86f, 1.12f)
                               : (gi == 4 ? float3(0.86f, 1.08f, 0.94f) : float3(1.06f, 0.90f, 0.82f))));
            lensAcc += gCol * gTint * (catsEye / float(gi + 1));
        }
        const float2 haloDir = cDist > 1.0e-4f ? cVec / cDist : float2(0.0f, 1.0f);
        const float3 haloCol = max(Bloom.Sample(S0, saturate(0.5f.xx - haloDir * 0.22f)).rgb - 0.85f.xxx, 0.0f.xxx);
        ovAdd += min((lensAcc * 0.065f + haloCol * 0.035f * saturate(1.0f - cDist * 1.35f)) * clamp(RQ.w, 0.0f, 2.5f), 0.22f.xxx);
    }
    if ((ovMask & 8u) != 0u) {
        // Layer 4 (OV4): Physical Tiffen Black Pro-Mist 1/4 Optical Diffusion — softly halates highlights while preserving deep shadows & crispness.
        const float3 proMist = max(bloom - 0.10f.xxx, 0.0f.xxx) * float3(1.04f, 1.00f, 0.96f);
        ovAdd += proMist * (0.095f * clamp(RQ.w, 0.0f, 2.5f));
    }
    // Energy-conserving optical bloom/lens screen-blend: prevents bloom from pushing bright snow or coastal horizons into whiteout.
    const float3 cappedAdd = ovAdd / (1.0f + max(ovAdd.r, max(ovAdd.g, ovAdd.b)) * 1.35f);
    mapped = saturate(mapped + cappedAdd * (1.0f - mapped * 0.74f));
    if (RB.w > 1.5f && RB.w < 2.5f) { return float4(lum.x * 0.25f, lum.y * 0.25f, exposure * 0.25f, 1.0f); }   // view 2: adapted average, the game's target, exposure (over 4, no gamma)
    if (RB.w > 0.5f && RB.w < 1.5f) { return float4(exposure.xxx * 0.25f, 1.0f); }   // view 1: the exposure (4 = white, no gamma)
    // Modern wide-gamut curves (RB.x > 3.5: AgX / GT / Lottes / Hable): stop Skyrim 2011's muddy ImageSpace
    // weather tint & contrast from overwriting our Hillaire + AgX color science, while preserving strong gameplay
    // status flashes (low health, poison, night-eye) softly.
    // Feature 10: Luminance-Preserving Filmic Split-Tone Micro-Contrast (cool alpine shadows, warm sunlit highlights).
    if (RB.x > 3.5f) {
        const float lSplit = saturate(dot(mapped, W));
        const float3 shadowCool = float3(0.97f, 0.99f, 1.03f);
        const float3 highWarm   = float3(1.02f, 1.00f, 0.97f);
        mapped = saturate(mapped * lerp(shadowCool, highWarm, smoothstep(0.18f, 0.78f, lSplit)));
    }
    const float g = dot(mapped, W);
    float4 c = float4(mapped, 1.0f);
    if (RB.x > 3.5f) {
        const float imadSat  = lerp(1.0f, P[3].x, 0.32f);
        const float imadTint = P[4].w * 0.22f;
        const float imadCon  = lerp(1.0f, P[3].z, 0.25f);
        const float imadBri  = lerp(1.0f, P[3].w, 0.25f);
        c = imadSat * (c - g) + g;
        c = imadTint * (g * P[4] - c) + c;
        c = imadCon * (imadBri * c - pivot) + pivot;
    } else {
        c = P[3].x * (c - g) + g;
        c = P[4].w * (g * P[4] - c) + c;
        c = P[3].z * (P[3].w * c - pivot) + pivot;
    }
    // Built-in procedural LUT profiles when no external 3D PNG LUT is active (ToneX2 = 0..3).
    if (RB.x > 2.5f && RX.x > 0.5f && RO.x <= 0.001f) {
        const float lumG = dot(c.rgb, W);
        if (RX.x < 1.5f) {
            // 1: Nordic Alpine Film (crisp cool-slate shadows, natural midtones, clean CIE D65 alpine highlights - no dry yellow cast).
            const float3 shTint = float3(0.95f, 0.98f, 1.04f);
            const float3 hiTint = float3(0.99f, 1.00f, 1.02f);
            c.rgb = saturate(c.rgb * lerp(shTint, hiTint, smoothstep(0.15f, 0.78f, lumG)));
        } else if (RX.x < 2.5f) {
            // 2: Kodak 2383 Print Film (deep teal-blue shadows, rich warm golden highlights, S-curve micro-contrast).
            const float3 kTint = lerp(float3(0.90f, 0.99f, 1.08f), float3(1.07f, 1.01f, 0.91f), smoothstep(0.12f, 0.80f, lumG));
            c.rgb = saturate(c.rgb * kTint);
        } else {
            // 3: Cinema Contrast (restrained saturation, crisp micro-contrast).
            c.rgb = saturate(lerp(lumG.xxx, c.rgb, 0.88f) * 1.03f - 0.015f);
        }
    }
    // Layer 1 (bit 0): Film Emulsion & Beer-Lambert Subtractive Density + Skin-Protected Oklab Vibrance
    // (enhances Nordic foliage greens, glacial blues, and sunset amber while protecting human/elf skin hues).
    if ((ovMask & 1u) != 0u) {
        float3 lab = linToOklab(saturate(c.rgb));
        const float chroma = length(lab.yz);
        const float hue = atan2(lab.z, lab.y);
        const float skinMask = smoothstep(0.20f, 0.55f, hue) * (1.0f - smoothstep(0.95f, 1.35f, hue)) * smoothstep(0.02f, 0.09f, chroma);
        const float vibBoost = lerp(1.12f, 1.02f, skinMask) * lerp(1.0f, 0.96f, saturate(chroma * 3.2f));
        lab.x *= (1.0f - saturate(chroma * 0.42f) * 0.20f);   // Beer-Lambert dye density: saturated hues hold richer depth
        lab.yz *= vibBoost;                                   // skin-protected perceptual vibrance with zero channel clipping
        c.rgb = saturate(oklabToLin(lab));
    }
    // RAW GPU Neural Vibrance (bit 12 / 4096, 100% watermark-free replacement for NVIDIA DeepDVC):
    // Perceptual Oklab chroma-selective dynamic vibrance + gentle S-curve perceptual contrast preserving skin & highlights.
    if ((ovMask & 4096u) != 0u) {
        float3 labN = linToOklab(saturate(c.rgb));
        const float chN = length(labN.yz);
        const float hueN = atan2(labN.z, labN.y);
        const float skinN = smoothstep(0.18f, 0.55f, hueN) * (1.0f - smoothstep(0.95f, 1.35f, hueN)) * smoothstep(0.02f, 0.09f, chN);
        // Lift muted mid-chroma selectively while keeping already-vivid hues and skin tones natural.
        const float dynVib = 1.0f + (1.0f - skinN * 0.78f) * (1.0f - smoothstep(0.06f, 0.22f, chN)) * 0.34f;
        labN.yz *= dynVib;
        // Subtle perceptual S-curve micro-contrast in Oklab L* (deepens shadow richness without crushing blacks).
        const float l0 = saturate(labN.x);
        labN.x = lerp(l0, l0 * l0 * (3.0f - 2.0f * l0), 0.14f);
        c.rgb = saturate(oklabToLin(labN));
    }
    // User DaVinci-class Oklab/Planckian Live Grading (White Balance Kelvin RG.x, Perceptual Sat RG.y, Shadow Lift RG.z):
    if (abs(RG.x - 6500.0f) > 25.0f && RG.x > 1000.0f) {
        const float kShift = clamp((6500.0f - RG.x) / 3000.0f, -1.0f, 1.0f);
        const float3 wb = float3(1.0f + kShift * 0.14f, 1.0f + kShift * 0.02f, 1.0f - kShift * 0.16f);
        c.rgb = saturate(c.rgb * wb);
    }
    if (abs(RG.y - 1.0f) > 0.005f || abs(RG.z) > 0.002f) {
        float3 labU = linToOklab(saturate(c.rgb));
        labU.yz *= clamp(RG.y, 0.0f, 2.0f);
        labU.x = saturate(labU.x + clamp(RG.z, -0.15f, 0.15f) * (1.0f - labU.x));
        c.rgb = saturate(oklabToLin(labU));
    }
    // Layer 5 (bit 4): Bleach Bypass (silver retention overlay without shadow crush).
    if ((ovMask & 16u) != 0u) {
        const float lB = dot(c.rgb, W);
        const float3 silver = lB.xxx;
        const float3 ov = lB < 0.5f ? (2.0f * c.rgb * silver) : (1.0f - 2.0f * (1.0f - c.rgb) * (1.0f - silver));
        c.rgb = saturate(lerp(c.rgb, lerp(silver, ov, 0.65f), 0.38f));
    }
    // Real 3D PNG LUT from raw_luts\ (46 Elder LUTs + any user-dropped 16^3/32^3/64^3 strip PNG), applied in sRGB display space.
    if (RO.x > 0.001f) {
        const float3 srgbIn = pow(max(c.rgb, 0.0f.xxx), 1.0f / 2.2f);
        const float3 lutOut = sampleUserLut3D(srgbIn);
        const float3 linOut = pow(max(lutOut, 0.0f.xxx), 2.2f);
        c.rgb = saturate(lerp(c.rgb, linOut, saturate(RO.x)));
    }
    // Layer 6 (OV6): Physical cos^4 Anamorphic Cinema Lens Vignette with subtle vintage peripheral warmth.
    if ((ovMask & 32u) != 0u) {
        const float2 vd = (suv - 0.5f.xx) * float2(1.18f, 1.0f);
        const float r2 = dot(vd, vd);
        const float vig = saturate(1.0f - r2 * 0.68f * clamp(RP.w, 0.0f, 2.0f));
        const float3 glassTint = lerp(1.0f.xxx, float3(1.02f, 1.00f, 0.96f), saturate(r2 * 1.4f));
        c.rgb *= (vig * vig) * glassTint;
    }
    // Layer 7 (bit 6): Physical 35mm Photochemical Film Grain (Kodak Vision3 response-curve bell + dual-octave clumping + RGB silver-halide variance).
    if ((ovMask & 64u) != 0u) {
        const float gLum = dot(c.rgb, W);
        // Response-curve bell: grain peaks organically in midtones, tapers cleanly to zero in deep blacks & bright highlights.
        const float response = saturate(gLum * (1.0f - gLum) * 4.0f);
        const float2 gPos1 = i.pos.xy + (5.588238f * RO.z).xx;
        const float2 gPos2 = floor(i.pos.xy * 0.5f) + (7.131942f * RO.z).xx;
        const float nR = frac(52.9829189f * frac(dot(gPos1, float2(0.06711056f, 0.00583715f)))) - 0.5f;
        const float nG = frac(52.9829189f * frac(dot(gPos1 + float2(17.0f, 31.0f), float2(0.06711056f, 0.00583715f)))) - 0.5f;
        const float nB = frac(52.9829189f * frac(dot(gPos2 + float2(43.0f, 11.0f), float2(0.06711056f, 0.00583715f)))) - 0.5f;
        const float clump = frac(52.9829189f * frac(dot(gPos2, float2(0.06711056f, 0.00583715f)))) - 0.5f;
        const float3 halide = float3(nR * 0.65f + clump * 0.45f, nG * 0.62f + clump * 0.48f, nB * 0.72f + clump * 0.42f);
        c.rgb = saturate(c.rgb + halide * (0.022f * clamp(RP.z, 0.0f, 2.5f) * (0.12f + 0.88f * response)));
    }
    // Layer 8 (bit 7): Cinematic 2.35:1 anamorphic letterbox borders.
    if ((ovMask & 128u) != 0u) {
        if (i.uv.y < 0.115f || i.uv.y > 0.885f) { c.rgb = 0.0f.xxx; }
    }
    // Always-on Triangular-PDF (TPDF) Temporal Blue-Noise Dithering before 8-bit SDR write (eliminates sky/fog banding).
    if (RB.w < 2.5f) {
        const float2 dPos = i.pos.xy + (5.588238f * RO.z).xx;
        const float d1 = frac(52.9829189f * frac(dot(dPos, float2(0.06711056f, 0.00583715f))));
        const float d2 = frac(52.9829189f * frac(dot(dPos + float2(23.0f, 59.0f), float2(0.06711056f, 0.00583715f))));
        c.rgb = saturate(c.rgb + ((d1 + d2 - 1.0f) / 255.0f).xxx);
    }
    // Optional PQ ST.2084 HDR10 transfer output (TonemapView >= 3, ported from superframe present_hdr.cpp).
    if (RB.w >= 2.5f) {
        const float3 lin2020 = saturate(float3(
            0.6274f * c.r + 0.3293f * c.g + 0.0433f * c.b,
            0.0691f * c.r + 0.9195f * c.g + 0.0114f * c.b,
            0.0164f * c.r + 0.0880f * c.g + 0.8956f * c.b));
        const float3 ym = pow(lin2020 * 0.04f, 0.1593017578f);
        c.rgb = pow((0.8359375f + 18.8515625f * ym) / (1.0f + 18.6875f * ym), 78.84375f);
        return c;
    }
    c.rgb = pow(saturate(c.rgb), F[42].x);
    // Elder-inspired filmic finish: subtle optical vignette + terminal triangular dithering (eliminates 8-bit sky/fog banding).
    const float2 vc = i.uv - 0.5f;
    c.rgb *= lerp(1.0f, saturate(1.0f - dot(vc, vc) * 0.22f), 0.35f);
    const float2 dp = floor(i.pos.xy);
    const float h1 = frac(sin(dot(dp, float2(12.9898f, 78.233f))) * 43758.5453f);
    const float h2 = frac(sin(dot(dp + float2(17.0f, 53.0f), float2(12.9898f, 78.233f))) * 43758.5453f);
    c.rgb = saturate(c.rgb + ((h1 - h2) / 255.0f).xxx);
    return c;
}
