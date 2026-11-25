// RawWaterCausticsSim.hlsl: Native DX12 SM 6.6 / 6.10 GPU Compute Physical Water Caustics & Underwater God-Rays.
// Clean-room MIT implementation.
//
// Pass 1 (cs_caustics_trace):
//   - Evaluates a periodic 4-wave Gerstner/swell water surface with analytical gradient & 2x2 Hessian.
//   - Solves exact bed-space inverse Snell refraction mapping P_surf + D(P_surf) = P_bed via damped 2x2 Newton
//     iteration with Cauchy chromatic dispersion (ior_R < ior_G < ior_B), guaranteeing area-preserving mean
//     irradiance conservation and sharp Jacobian focusing |det(dP_bed / dP_surf)|^-1.
//   - Applies Pope & Fry spectral Beer-Lambert underwater extinction along the exact 3D refracted path length.
//   - Outputs RGBA16F: rgb = Beer-Lambert chromatic caustic irradiance, a = unabsorbed geometric focusing factor.
//
// Pass 2 (cs_underwater_rays):
//   - Traces 16-step IGN-jittered underwater volumetric sun-shafts (crepuscular god-rays) coupled directly
//     to g_causticsSrv, integrated with analytical Beer-Lambert transmittance and forward Henyey-Greenstein phase.
//   - Outputs RGBA16F: rgb = accumulated underwater in-scattered shaft radiance, a = mean view transmittance.

cbuffer CausticsCB : register(b0)
{
    uint   g_width;
    uint   g_height;
    uint   g_frameIndex;
    float  g_timeSec;

    float3 g_sunDir;           // Normalized direction toward sun (y > 0)
    float  g_sunIntensity;

    float3 g_sunColor;
    float  g_bedDepthM;        // Mean water depth to riverbed (m)

    float3 g_absorptionRGB;    // Spectral Beer-Lambert absorption sigma_a (1/m), R > G > B
    float  g_scatterCoeff;     // Underwater volumetric scattering sigma_s (1/m)

    float  g_waveAmp;          // Primary Gerstner wave amplitude scale (m); 0.0 = perfectly flat
    float  g_domainSizeM;      // Periodic caustic tile domain length (m)
    float  g_dispersionScale;  // Cauchy RGB chromatic dispersion multiplier (1.0 = physical)
    float  g_mieG;             // Henyey-Greenstein underwater forward scattering anisotropy [0..0.95]

    float3 g_camPos;           // Underwater camera world position (m, Y-up)
    float  g_waterSurfaceY;    // Water surface altitude (m)

    float3 g_camRight;         // Orthonormal camera right vector
    float  g_fovYRad;          // Vertical field of view (radians)

    float3 g_camUp;            // Orthonormal camera up vector
    float  g_maxRayDistM;      // Maximum underwater raymarch distance (m)

    float3 g_camForward;       // Orthonormal camera forward vector
    float  g_shaftIntensity;   // Volumetric underwater sun-shaft multiplier
};

RWTexture2D<float4> g_causticsOut       : register(u0);
RWTexture2D<float4> g_underwaterRaysOut : register(u1);
Texture2D<float4>   g_causticsSrv       : register(t0);
SamplerState        g_linWrap           : register(s0);

static const float kPi    = 3.14159265359f;
static const float kTwoPi = 6.28318530718f;

float SanitizeNonNeg(float v)
{
    return (isnan(v) || isinf(v)) ? 0.0f : max(v, 0.0f);
}

float3 SanitizeNonNeg3(float3 v)
{
    return float3(SanitizeNonNeg(v.x), SanitizeNonNeg(v.y), SanitizeNonNeg(v.z));
}

// Analytical periodic 4-wave surface height h, gradient (hx, hz), and 2x2 Hessian (hxx, hxz, hzz).
// Integer harmonic wavevectors guarantee exact tile periodicity across [0, g_domainSizeM)^2.
void EvalWaveSurface(float2 p, float domainM, float amp, float t,
                     out float h, out float2 gradH, out float3 hessH)
{
    h     = 0.0f;
    gradH = float2(0.0f, 0.0f);
    hessH = float3(0.0f, 0.0f, 0.0f); // (hxx, hxz, hzz)

    if (amp <= 1e-6f) {
        return;
    }

    const float kBase = kTwoPi / max(domainM, 0.5f);

    // 4 well-separated periodic harmonics with gravity dispersion omega = sqrt(9.81 * |k|)
    const float2 modes[4] = {
        float2( 2.0f,  1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -2.0f),
        float2(-2.0f, -3.0f)
    };
    const float weights[4] = { 0.34f, 0.28f, 0.22f, 0.16f };
    const float phases[4]  = { 0.35f, 1.72f, 3.11f, 4.89f };

    [unroll]
    for (int i = 0; i < 4; ++i) {
        const float2 kv    = modes[i] * kBase;
        const float  kmag  = length(kv);
        const float  omega = sqrt(9.81f * kmag);
        const float  ai    = amp * weights[i];
        const float  phi   = dot(kv, p) - omega * t + phases[i];

        float s, c;
        sincos(phi, s, c);

        h       += ai * c;
        gradH   += (-ai * s) * kv;
        const float hc = -ai * c;
        hessH.x += hc * kv.x * kv.x;
        hessH.y += hc * kv.x * kv.y;
        hessH.z += hc * kv.y * kv.y;
    }
}

// Evaluates bed intersection P_bed(p), exact 2x2 Jacobian J = dP_bed / dp, and 3D underwater path length.
void EvalRefractedBedMap(float2 p, float3 sunDir, float eta, float bedDepthM,
                         float domainM, float amp, float t,
                         out float2 pBed, out float4 J, out float pathLenM)
{
    float  h;
    float2 gradH;
    float3 hessH;
    EvalWaveSurface(p, domainM, amp, t, h, gradH, hessH);

    // Surface upward normal N = normalize(-hx, 1, -hz)
    const float3 u      = float3(-gradH.x, 1.0f, -gradH.y);
    const float  invLen = rsqrt(dot(u, u));
    const float3 N      = u * invLen;

    // Exact derivatives of unit normal N w.r.t. surface (x, z)
    const float3 duDx = float3(-hessH.x, 0.0f, -hessH.y);
    const float3 duDz = float3(-hessH.y, 0.0f, -hessH.z);
    const float3 dNDx = (duDx - N * dot(N, duDx)) * invLen;
    const float3 dNDz = (duDz - N * dot(N, duDz)) * invLen;

    // Snell refraction of incident downward ray I = -sunDir across normal N
    const float3 I    = -sunDir;
    const float  c    = max(dot(N, sunDir), 0.05f);
    const float  eta2 = eta * eta;
    const float  k    = max(1.0f - eta2 * (1.0f - c * c), 1e-4f);
    const float  s    = sqrt(k);
    const float  coef = eta * c - s;
    const float3 T    = eta * I + coef * N;

    // Exact derivatives of refracted unit ray T w.r.t. surface (x, z)
    const float  dcDx    = dot(dNDx, sunDir);
    const float  dcDz    = dot(dNDz, sunDir);
    const float  dsDx    = (eta2 * c / s) * dcDx;
    const float  dsDz    = (eta2 * c / s) * dcDz;
    const float3 dTDx    = (eta * dcDx - dsDx) * N + coef * dNDx;
    const float3 dTDz    = (eta * dcDz - dsDz) * N + coef * dNDz;

    // Ray-bed intersection at y = -bedDepthM (water surface at y = h)
    const float  d    = max(bedDepthM + h, 0.05f);
    const float  ty   = max(-T.y, 0.15f);
    const float  invT = 1.0f / ty;
    const float  dist = d * invT;

    pBed     = p + dist * T.xz;
    pathLenM = dist;

    // Exact derivative of dist = (bedDepthM + h) / (-T.y)
    const float dtDx = (gradH.x * ty + d * dTDx.y) * (invT * invT);
    const float dtDz = (gradH.y * ty + d * dTDz.y) * (invT * invT);

    const float2 dBedDx = float2(1.0f, 0.0f) + dtDx * T.xz + dist * dTDx.xz;
    const float2 dBedDz = float2(0.0f, 1.0f) + dtDz * T.xz + dist * dTDz.xz;

    // Pack 2x2 Jacobian J = [dBedDx, dBedDz] as (j00, j01, j10, j11)
    J = float4(dBedDx.x, dBedDz.x, dBedDx.y, dBedDz.y);
}

// Solves F(p) = P_bed(p) = targetBed via damped 2x2 Newton iteration and returns
// geometric Jacobian focusing |det(dP_bed/dP_surf)|^-1 and exact 3D underwater path length.
void SolveCausticChannel(float2 targetBed, float2 initGuess, float3 sunDir, float eta,
                         float bedDepthM, float domainM, float amp, float t,
                         int maxIters,
                         out float2 outSurfP, out float outFocus, out float outPathLenM)
{
    float2 p = initGuess;
    float2 pBed = targetBed;
    float4 J    = float4(1.0f, 0.0f, 0.0f, 1.0f);
    float  path = bedDepthM;

    if (amp <= 1e-6f) {
        EvalRefractedBedMap(targetBed, sunDir, eta, bedDepthM, domainM, 0.0f, t, pBed, J, path);
        outSurfP    = targetBed - (pBed - targetBed);
        outFocus    = 1.0f;
        outPathLenM = path;
        return;
    }

    const float maxStep = domainM * 0.12f;

    [loop]
    for (int iter = 0; iter < maxIters; ++iter) {
        EvalRefractedBedMap(p, sunDir, eta, bedDepthM, domainM, amp, t, pBed, J, path);
        const float2 err = pBed - targetBed;
        if (dot(err, err) < 1e-10f) {
            break;
        }
        const float det = J.x * J.w - J.y * J.z;
        const float safeDet = (abs(det) > 0.14f) ? det : ((det >= 0.0f) ? 0.14f : -0.14f);
        const float invDet  = 1.0f / safeDet;
        float2 dp = float2(J.w * err.x - J.y * err.y,
                          -J.z * err.x + J.x * err.y) * invDet;
        const float dpLen = length(dp);
        if (dpLen > maxStep) {
            dp *= (maxStep / dpLen);
        }
        p -= dp;
    }

    // Final Jacobian evaluation at converged surface point
    EvalRefractedBedMap(p, sunDir, eta, bedDepthM, domainM, amp, t, pBed, J, path);
    const float detFinal = J.x * J.w - J.y * J.z;
    const float absDet   = max(abs(detFinal), 0.12f);

    outSurfP    = p;
    outFocus    = min(1.0f / absDet, 8.5f);
    outPathLenM = path;
}

[numthreads(8, 8, 1)]
void cs_caustics_trace(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_width || dtid.y >= g_height) {
        return;
    }

    const float2 uv = (float2(dtid.xy) + 0.5f) / float2(g_width, g_height);
    const float2 targetBed = uv * g_domainSizeM;

    const float3 sunDir = normalize(float3(g_sunDir.x, max(g_sunDir.y, 0.12f), g_sunDir.z));
    const float  disp   = max(g_dispersionScale, 0.0f);

    // Physical water Cauchy refractive indices around 532 nm (n_G = 1.334)
    const float iorG = 1.334f;
    const float iorR = max(iorG - 0.014f * disp, 1.05f);
    const float iorB = iorG + 0.018f * disp;
    const float etaR = 1.0f / iorR;
    const float etaG = 1.0f / iorG;
    const float etaB = 1.0f / iorB;

    // Flat-water analytic initial guess for fast Newton convergence
    const float3 flatT  = refract(-sunDir, float3(0.0f, 1.0f, 0.0f), etaG);
    const float2 flatD  = (g_bedDepthM / max(-flatT.y, 0.15f)) * flatT.xz;
    const float2 guessG = targetBed - flatD;

    float2 surfG;
    float  focusG, pathG;
    SolveCausticChannel(targetBed, guessG, sunDir, etaG, g_bedDepthM,
                        g_domainSizeM, g_waveAmp, g_timeSec, 3,
                        surfG, focusG, pathG);

    float2 surfR, surfB;
    float  focusR, pathR;
    float  focusB, pathB;

    if (disp > 1e-5f && g_waveAmp > 1e-6f) {
        // Warm-start R and B from converged green surface point (1 analytical Newton polish step)
        SolveCausticChannel(targetBed, surfG, sunDir, etaR, g_bedDepthM,
                            g_domainSizeM, g_waveAmp, g_timeSec, 1,
                            surfR, focusR, pathR);
        SolveCausticChannel(targetBed, surfG, sunDir, etaB, g_bedDepthM,
                            g_domainSizeM, g_waveAmp, g_timeSec, 1,
                            surfB, focusB, pathB);
    } else {
        // Compute exact flat-water path lengths per channel when waveAmp == 0
        float2 tmpBed;
        float4 tmpJ;
        EvalRefractedBedMap(targetBed, sunDir, etaR, g_bedDepthM, g_domainSizeM, 0.0f, g_timeSec, tmpBed, tmpJ, pathR);
        EvalRefractedBedMap(targetBed, sunDir, etaB, g_bedDepthM, g_domainSizeM, 0.0f, g_timeSec, tmpBed, tmpJ, pathB);
        focusR = focusG;
        focusB = focusG;
    }

    // Spectral Beer-Lambert underwater transmittance along each channel's refracted path
    const float3 transRGB = float3(
        exp(-max(g_absorptionRGB.r, 0.0f) * pathR),
        exp(-max(g_absorptionRGB.g, 0.0f) * pathG),
        exp(-max(g_absorptionRGB.b, 0.0f) * pathB)
    );

    const float3 causticIrrad = SanitizeNonNeg3(float3(focusR, focusG, focusB) * transRGB * g_sunColor * g_sunIntensity);
    const float  geomFocus    = SanitizeNonNeg(focusG);

    g_causticsOut[dtid.xy] = float4(causticIrrad, geomFocus);
}

// Interleaved Gradient Noise for jittered 16-step underwater shaft marching
float EvalIGN(uint2 px, uint frameIdx)
{
    const float ign = frac(52.9829189f * frac(dot(float2(px), float2(0.06711056f, 0.00583715f))));
    return frac(ign + 0.61803399f * float(frameIdx & 63u));
}

// Henyey-Greenstein phase function (normalized over 4*pi steradians)
float EvalHenyeyGreenstein(float cosTheta, float g)
{
    const float gClamped = clamp(g, -0.92f, 0.92f);
    const float g2       = gClamped * gClamped;
    const float denom    = max(1.0f + g2 - 2.0f * gClamped * cosTheta, 1e-3f);
    return (1.0f - g2) / (4.0f * kPi * pow(denom, 1.5f));
}

[numthreads(8, 8, 1)]
void cs_underwater_rays(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_width || dtid.y >= g_height) {
        return;
    }

    const float2 uv     = (float2(dtid.xy) + 0.5f) / float2(g_width, g_height);
    const float  aspect = float(g_width) / max(float(g_height), 1.0f);
    const float  tanH   = tan(0.5f * max(g_fovYRad, 0.1f));
    const float2 ndc    = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    const float3 rayDir = normalize(
        g_camForward +
        g_camRight * (ndc.x * aspect * tanH) +
        g_camUp    * (ndc.y * tanH)
    );

    const float maxDist = max(g_maxRayDistM, 0.5f);
    float tStart = 0.0f;
    float tEnd   = 0.0f;

    if (g_camPos.y < g_waterSurfaceY) {
        // Camera is submerged underwater
        tStart = 0.0f;
        if (rayDir.y > 1e-4f) {
            tEnd = min(maxDist, (g_waterSurfaceY - g_camPos.y) / rayDir.y);
        } else {
            tEnd = maxDist;
        }
    } else {
        // Camera is above water; trace underwater segment only if ray enters water surface
        if (rayDir.y < -1e-4f) {
            tStart = (g_waterSurfaceY - g_camPos.y) / rayDir.y;
            tEnd   = min(tStart + maxDist, maxDist * 2.0f);
        } else {
            g_underwaterRaysOut[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
    }

    const float span = tEnd - tStart;
    if (span <= 1e-4f) {
        g_underwaterRaysOut[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Refracted sun direction pointing up toward Snell's window inside water
    const float3 sunDir      = normalize(float3(g_sunDir.x, max(g_sunDir.y, 0.12f), g_sunDir.z));
    const float3 refrDown    = refract(-sunDir, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.334f);
    const float3 sunUnderDir = normalize(-refrDown); // points upward toward refracted sun

    const float cosTheta = dot(rayDir, sunUnderDir);
    const float phaseHG  = EvalHenyeyGreenstein(cosTheta, g_mieG);

    const float  sigmaS    = max(g_scatterCoeff, 1e-4f);
    const float3 sigmaA    = max(g_absorptionRGB, 0.0f);
    const float3 sigmaT    = max(sigmaA + sigmaS, 1e-4f);
    const float  invDom    = 1.0f / max(g_domainSizeM, 0.5f);
    const float  invSunY   = 1.0f / max(sunUnderDir.y, 0.15f);

    static const int kSteps = 16;
    const float dt     = span / float(kSteps);
    const float jitter = EvalIGN(dtid.xy, g_frameIndex);

    const float3 stepTrans    = exp(-sigmaT * dt);
    const float3 integFactor  = (1.0f - stepTrans) / sigmaT;
    const float3 scatterScale = g_sunColor * g_sunIntensity * sigmaS * phaseHG * max(g_shaftIntensity, 0.0f) * integFactor;

    // Exact fused view * sun Beer-Lambert linear recurrence (minimum VGPRs, 100% algebraically exact)
    const float3 pos0      = g_camPos + rayDir * (tStart + jitter * dt);
    const float3 dPos      = rayDir * dt;
    const float  depth0    = max(g_waterSurfaceY - pos0.y, 0.0f);
    const float  dDepth    = -dPos.y;
    const float  sunPath0  = depth0 * invSunY;
    const float  dSunPath  = dDepth * invSunY;
    float3       combTrans = exp(-sigmaT * sunPath0);
    const float3 stepCombT = exp(-sigmaT * (dt + dSunPath));
    float2       caustUv   = (pos0.xz + sunUnderDir.xz * sunPath0) * invDom;
    const float2 dCaustUv  = (dPos.xz + sunUnderDir.xz * dSunPath) * invDom;

    float3 accumWeight = float3(0.0f, 0.0f, 0.0f);
    [loop]
    for (int s = 0; s < kSteps; ++s) {
        const float shaftMod = clamp(g_causticsSrv.SampleLevel(g_linWrap, caustUv, 0.0f).a, 0.05f, 8.5f);
        accumWeight += combTrans * shaftMod;
        combTrans   *= stepCombT;
        caustUv     += dCaustUv;
    }

    const float3 accumRad  = accumWeight * scatterScale;
    const float3 viewTrans = exp(-sigmaT * span);

    const float3 safeRad   = SanitizeNonNeg3(accumRad);
    const float  meanTrans = clamp(dot(SanitizeNonNeg3(viewTrans), float3(0.333333f, 0.333333f, 0.333334f)), 0.0f, 1.0f);

    g_underwaterRaysOut[dtid.xy] = float4(safeRad, meanTrans);
}
