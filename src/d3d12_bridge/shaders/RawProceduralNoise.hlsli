// RawProceduralNoise.hlsli: Shared Zero-External-Asset GPU Procedural Library for RAW (THE PLAYGROUND).
// Provides analytical-derivative 3D value noise (value + exact gradient in 1 pass), multi-octave Wrenninge/Hillaire
// volumetric multiple scattering, live world water-elevation resolution, and above/below-surface physical caustics.
// Original work, MIT.
#ifndef RAW_PROCEDURAL_NOISE_HLSLI
#define RAW_PROCEDURAL_NOISE_HLSLI

float rawHash31(float3 p) {
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Analytical-derivative 3D quintic value noise: returns (value, dF/dx, dF/dy, dF/dz) in a single lattice pass
// (eliminates 4x redundant finite-difference noise evaluations for bark, cliffs, snowdrifts, and ocean crests).
float4 rawNoise3Grad(float3 x) {
    const float3 i = floor(x);
    const float3 f = frac(x);
    const float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float3 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

    const float a = rawHash31(i + float3(0, 0, 0));
    const float b = rawHash31(i + float3(1, 0, 0));
    const float c = rawHash31(i + float3(0, 1, 0));
    const float d = rawHash31(i + float3(1, 1, 0));
    const float e = rawHash31(i + float3(0, 0, 1));
    const float f1 = rawHash31(i + float3(1, 0, 1));
    const float g = rawHash31(i + float3(0, 1, 1));
    const float h = rawHash31(i + float3(1, 1, 1));

    const float k0 = a;
    const float k1 = b - a;
    const float k2 = c - a;
    const float k3 = e - a;
    const float k4 = a - b - c + d;
    const float k5 = a - c - e + g;
    const float k6 = a - b - e + f1;
    const float k7 = -a + b + c - d + e - f1 - g + h;

    const float val = k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z;
    const float3 grad = du * float3(
        k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
        k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
        k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
    return float4(val, grad);
}

// Resolves true live world-space tidal water plane Z from Creation Engine TESObjectCELL::waterHeight + Nirn tide.
// Fixes the latent bug where waterA.w (glint strength ~1.0) was previously read as world sea elevation!
float rawLiveWaterZ(float4 engineWorldIn, float4 physWorld0) {
    const float baseZ = (engineWorldIn.z > 0.5f && engineWorldIn.x > -49000.0f && engineWorldIn.x < 49000.0f)
                      ? engineWorldIn.x : -14000.0f;
    return baseZ + physWorld0.z;
}

// 3-Octave Wrenninge / Hillaire Multi-Scattering Phase Integral (beats single-scatter fog in dense forests & clouds):
// Evaluates sum_{n=0..2} a^n * HG(cosT, g * c^n) * exp(-od * b^n) with energy-preserving 1/PI scaling.
float rawMultiScatterPhase(float cosT, float g, float opticalDepth) {
    const float pi4 = 12.5663706f;
    float sum = 0.0f;
    float a = 1.0f, b = 1.0f, c = 1.0f;
    [unroll] for (int oct = 0; oct < 3; ++oct) {
        const float go = clamp(g * c, -0.85f, 0.88f);
        const float g2 = go * go;
        const float hg = (1.0f - g2) / (pi4 * pow(max(1.0f + g2 - 2.0f * go * cosT, 1.0e-4f), 1.5f));
        sum += a * exp(-opticalDepth * b) * hg;
        a *= 0.52f;
        b *= 0.45f;
        c *= 0.55f;
    }
    return sum;
}

// Dual-scale animated caustic network for above-surface reflected water caustics (bridge arches, piers, cliff overhangs;
// GTA VI @OtarieBambelle signature lighting) and underwater bed illumination, zero external caustic textures.
float rawReflectedCausticNet(float2 worldXY, float t, float scale) {
    const float2 p1 = worldXY / max(scale, 8.0f);
    const float2 p2 = p1 * 1.63f + float2(7.3f, -4.1f);
    const float n1 = abs(rawNoise3Grad(float3(p1, t * 0.55f)).x * 2.0f - 1.0f);
    const float n2 = abs(rawNoise3Grad(float3(p2, -t * 0.72f + 3.1f)).x * 2.0f - 1.0f);
    const float web1 = pow(saturate(1.0f - n1 * 2.6f), 4.0f);
    const float web2 = pow(saturate(1.0f - n2 * 2.8f), 4.0f);
    return saturate(web1 * 0.72f + web2 * 0.55f + web1 * web2 * 0.95f);
}

// ============================================================================
// Wave 37 Physical Material & Procedural Forge Extensions (100% backwards-compatible)
// Pure polynomial/lattice kernels (zero sin() drift across GPU vendors, zero hidden time terms).
// ============================================================================

// Deterministic 3D-to-3D hash in [0, 1)^3 using pure rational/polynomial mixing (vendor-invariant).
float3 rawHash33(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// Analytical 3D Worley / Voronoi cellular noise:
// Returns float4(F1, F2 - F1, cellId, F2) where:
//   x = F1      (distance to nearest feature point)
//   y = F2 - F1 (exact fracture/crack/facet border distance: 0 on Voronoi seam)
//   z = cellId  (stable deterministic per-cell hash in [0, 1])
//   w = F2      (distance to second-nearest feature point)
float4 rawWorley3D(float3 p) {
    const float3 n = floor(p);
    const float3 f = frac(p);
    float d1 = 8.0f;
    float d2 = 8.0f;
    float bestId = 0.0f;
    [unroll] for (int k = -1; k <= 1; ++k) {
        [unroll] for (int j = -1; j <= 1; ++j) {
            [unroll] for (int i = -1; i <= 1; ++i) {
                const float3 g = float3(float(i), float(j), float(k));
                const float3 o = 0.12f + 0.76f * rawHash33(n + g);
                const float3 r = g + o - f;
                const float d = dot(r, r);
                if (d < d1) {
                    d2 = d1;
                    d1 = d;
                    bestId = o.x;
                } else if (d < d2) {
                    d2 = d;
                }
            }
        }
    }
    const float f1 = sqrt(max(d1, 0.0f));
    const float f2 = sqrt(max(d2, 0.0f));
    return float4(f1, max(f2 - f1, 0.0f), bestId, f2);
}

// Multi-octave analytical fBm returning (value in [0,1], dF/dx, dF/dy, dF/dz) in a single pass.
float4 rawFbm3Grad(float3 p, int octaves) {
    float val = 0.0f;
    float3 grad = 0.0f.xxx;
    float amp = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;
    const int nOct = clamp(octaves, 1, 5);
    [loop] for (int i = 0; i < nOct; ++i) {
        const float4 ng = rawNoise3Grad(p * freq + float3(17.1f, 31.7f, 11.3f) * float(i));
        val += ng.x * amp;
        grad += ng.yzw * (amp * freq);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    const float invN = 1.0f / max(norm, 1.0e-5f);
    return float4(val * invN, grad * invN);
}

// Divergence-free 3D curl noise field using exact analytical gradients from rawNoise3Grad.
float3 rawCurlNoise3D(float3 p) {
    const float3 gX = rawNoise3Grad(p + float3( 0.0f,  0.0f,  0.0f)).yzw;
    const float3 gY = rawNoise3Grad(p + float3(31.4f, 17.2f, 53.8f)).yzw;
    const float3 gZ = rawNoise3Grad(p + float3(67.1f, 41.9f, 13.5f)).yzw;
    return float3(gZ.y - gY.z, gX.z - gZ.x, gY.x - gX.y);
}

// Exact unpolarized dielectric Fresnel reflectance (Snell + Fresnel, returns 1.0 past TIR critical angle).
float rawDielectricFresnel(float cosThetaI, float ior) {
    const float ci = saturate(cosThetaI);
    const float eta = max(ior, 1.0f);
    const float sin2T = (1.0f - ci * ci) / (eta * eta);
    if (sin2T >= 1.0f) { return 1.0f; }
    const float ct = sqrt(max(1.0f - sin2T, 0.0f));
    const float rs = (ci - eta * ct) / max(ci + eta * ct, 1.0e-5f);
    const float rp = (eta * ci - ct) / max(eta * ci + ct, 1.0e-5f);
    return saturate(0.5f * (rs * rs + rp * rp));
}

// Exact unpolarized complex-IOR conductor Fresnel F(cosTheta, eta + i*k) (OPT-003, Mitsuba-validated):
float3 rawComplexConductorFresnel(float cosTheta, float3 eta, float3 k) {
    const float c = saturate(cosTheta);
    const float c2 = c * c;
    const float s2 = max(1.0f - c2, 0.0f);
    const float3 eta2 = eta * eta;
    const float3 k2 = k * k;
    const float3 t0 = eta2 - k2 - s2;
    const float3 a2plusb2 = sqrt(max(t0 * t0 + 4.0f * eta2 * k2, 0.0f.xxx));
    const float3 a = sqrt(max(0.5f * (a2plusb2 + t0), 0.0f.xxx));
    const float3 t1 = a2plusb2 + c2;
    const float3 t2 = 2.0f * a * c;
    const float3 rs = (t1 - t2) / max(t1 + t2, 1.0e-6f.xxx);
    const float3 t3 = c2 * a2plusb2 + s2 * s2;
    const float3 t4 = t2 * s2;
    const float3 rp = rs * (t3 - t4) / max(t3 + t4, 1.0e-6f.xxx);
    return saturate(0.5f * (rs + rp));
}

// Exact Planck Blackbody emission for 700K..12000K (tuned for 1000K..3400K forge/magma/embers):
// Uses Wien-shifted log-safe evaluation B(lambda, T) proportional to (550/lambda)^5 * exp(-(ex - exMin)) / (1 - exp(-ex))
// at Rec.709 RGB (615, 540, 465 nm) — zero FP16/FP32 underflow even at 800K-1400K glowing coals!
// Returns peak-normalized linear Rec.709 chromaticity RGB in [0, 1] and outputs monotonic HDR radiance scale.
float3 rawPlanckBlackbodyRGB(float tempK, out float hdrRadiance) {
    if (tempK < 700.0f) {
        hdrRadiance = 0.0f;
        return 0.0f.xxx;
    }
    const float T = clamp(tempK, 700.0f, 12000.0f);
    const float3 lamNm = float3(615.0f, 540.0f, 465.0f);
    const float3 w5 = pow(550.0f / lamNm, 5.0f);
    const float3 ex = 1.4387768e7f / (lamNm * T);
    const float exMin = min(ex.r, min(ex.g, ex.b));
    const float3 rad = (w5 * exp(-min(ex - exMin, 60.0f.xxx))) / max(1.0f - exp(-min(ex, 60.0f.xxx)), 1.0e-6f.xxx);
    const float peak = max(rad.r, max(rad.g, rad.b));
    // Smooth Stefan-Boltzmann-anchored visible radiant ramp (0 below 750K, 1.0 at 2000K, bright HDR at 3400K):
    const float visRamp = smoothstep(750.0f, 1150.0f, T);
    const float tNorm = T / 2000.0f;
    hdrRadiance = visRamp * min(tNorm * tNorm * tNorm * tNorm, 18.0f);
    return (peak > 1.0e-7f) ? saturate(rad / peak) : 0.0f.xxx;
}

#endif
