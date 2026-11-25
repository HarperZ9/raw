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

#endif
