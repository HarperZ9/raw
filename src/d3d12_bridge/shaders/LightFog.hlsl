// LightFog.hlsl: volumetric fog lit by every light, after the lighting pass (LightingPass.cpp).
// A homogeneous medium of extinction sigma along each camera ray, up to the surface or a
// maximum distance:
//   sun     marched in 12 jittered steps, each shadowed by the game's cascades, with the
//           Henyey-Greenstein phase
//   lights  closed form, unshadowed: along the ray x(t) = t w the game's window 1 - |x - c|^2 / r^2
//           is a quadratic in t, so its integral over the chord inside the sphere is exact
// The result: lit * T(t_end) + in-scatter, T(t) = exp(-sigma t). Sky pixels keep the game's
// atmosphere. With FLAG_FOG off the pass copies the lit frame. Original work, MIT.
#define ATLAS_SLOT t2
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"
#include "ElderOptics.hlsli"

Texture2D<float4> Lit         : register(t0);
Texture2D<float>  Depth       : register(t1);
Texture2D<float4> VolFog      : register(t3);
Texture2D<float4> AtmoSkyView : register(t4);
SamplerState      Lin         : register(s0);

float3 sampleHillaireSky(float3 rd) {
    const float az = atan2(rd.y, rd.x);
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    return AtmoSkyView.SampleLevel(Lin, float2(az / (2.0f * PI) + 0.5f, saturate(vNonLin)), 0.0f).rgb;
}

float phaseHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

// The integral of 1 - |x(t) - c|^2 / r^2 over t in [0, tEnd] where it is positive.
float windowIntegral(float3 w, float3 c, float r, float tEnd) {
    const float tc = dot(w, c);
    const float h2 = dot(c, c) - tc * tc;
    const float r2 = r * r;
    if (h2 >= r2) { return 0.0f; }
    const float half = sqrt(r2 - h2);
    const float sa = max(0.0f, tc - half) - tc, sb = min(tEnd, tc + half) - tc;   // s = t - tc
    if (sb <= sa) { return 0.0f; }
    return (1.0f - h2 / r2) * (sb - sa) - (sb * sb * sb - sa * sa * sa) / (3.0f * r2);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    float4 lit = Lit.Load(ip);
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return lit; }
    const float d = Depth.Load(ip);
    const float2 px = floor(at.xy);
    const float focusD = Depth.Load(int3(int2(dims.xy * 0.5f), 0));
    const float focusZ = focusD < 1.0f ? viewZOf(focusD) : 10000.0f;
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    // Pre-Pass Weather Optics: procedural HDR rain-on-lens droplets & blizzard crystalline frost edges
    // (3-pass Near/Far Physical Bokeh DoF runs in dedicated DofPass.hlsl right after LightFog).
    if (dofParams.w > 0.5f && (weather.w >= 0.12f || weather.z >= 0.25f)) {
        lit.rgb = elderWeatherLensPrepass(Lit, px, dims.xy, dims.zw, weather.w, weather.z, camPosW.w);
    }
    if (d >= 1.0f) { return lit; }   // the sky keeps the game's own atmosphere
    const float3 p = worldAt(px, min(d, 0.999999f));
    const float dist = length(p);
    const float3 w = p / max(dist, 1.0e-4f);
    const float tEnd = min(dist, tune2.w);
    const float sigma = tune.x;

    // Depth-aware bilateral upsample of the SM6.6 temporally-reprojected 32-slice VolumetricPass buffer
    // (effective ~192-step smoothness with zero halo bleeding across pine needles and rooftops).
    const float2 uv = (px + 0.5f) * dims.zw;
    const float2 volPx = (px + 0.5f) * 0.5f;
    const float2 volDim = max(floor((dims.xy + 1.0f) * 0.5f), float2(1.0f, 1.0f));
    const int2 vBase = clamp(int2(floor(volPx - 0.5f)), int2(0, 0), int2(volDim) - 1);
    const int2 vMax = int2(volDim) - 1;
    const float zPix = min(curZ, 65000.0f);
    float3 vSum = 0.0f.xxx;
    float vW = 0.0f;
    [unroll] for (int dy = 0; dy <= 1; dy++) {
        [unroll] for (int dx = 0; dx <= 1; dx++) {
            const int2 q = clamp(vBase + int2(dx, dy), int2(0, 0), vMax);
            const float4 tap = VolFog.Load(int3(q, 0));
            const float relZ = abs(tap.a - zPix) / max(zPix, 1.0f);
            const float wz = exp(-relZ * 18.0f);
            vSum += tap.rgb * wz;
            vW += wz;
        }
    }
    const float3 scatter = vW > 1.0e-4f ? (vSum / vW) : VolFog.SampleLevel(Lin, uv, 0.0f).rgb;
    // Engine-native underwater medium (FLAG_UNDERWATER from raw_engine.dll camera_in_water): physical Beer-Lambert absorption.
    if ((flags.x & FLAG_UNDERWATER) != 0u) {
        const float3 uw = elderUnderwaterMedium(lit.rgb, dist, 0.0022f);
        return float4(uw + scatter * float3(0.25f, 0.72f, 0.85f) * 1.4f, lit.a);
    }
    // Unified sky- and weather-coupled exponential height fog & alpine mountain mist (replaces Skyrim's flat 2011 metric fog).
    if ((flags.x & FLAG_FOG_REPLACES) != 0u && sunColour.w > 0.5f) {
        const float weatherDens = 1.0f + weather.w * 2.2f + weather.z * 2.6f + saturate(clouds.x - 0.35f) * 1.1f;
        const float baseSigma = 0.0000135f * weatherDens;
        // Smooth C2 elevation weight across w.z = 0 (eliminates the horizontal midpoint fog band at eye horizon).
        const float hStart = max(camPosW.z * 0.00025f, 0.0f);
        const float elevW = lerp(1.12f, 0.44f, smoothstep(-0.15f, 0.30f, w.z));
        // 3D rolling alpine mist & high-altitude mountain-collar cloud band (replaces static billboard fog planes).
        const float3 midPt = camPosW.xyz + w * min(dist * 0.55f, 45000.0f);
        const float3 windShift = float3(cloudWind.xy * camPosW.w * 0.00006f, 0.0f);
        const float alpineNoise = cNoise(midPt * 0.00014f + windShift) * 0.65f + cNoise(midPt * 0.00042f - windShift * 1.6f + 7.1f) * 0.35f;
        const float peakCollar = exp(-pow((midPt.z - 5200.0f) / 3600.0f, 2.0f)) * smoothstep(8000.0f, 28000.0f, dist);
        const float alpineMist = (0.76f + 0.48f * alpineNoise) * (1.0f + peakCollar * (0.45f + 0.55f * alpineNoise));
        const float od = baseSigma * min(dist, 240000.0f) * elevW * alpineMist * exp(-hStart * 0.15f);
        // Wavelength-dependent Rayleigh + Mie transmittance + smooth far-horizon aerial melt (eliminates the sharp Sea of Ghosts LOD seam).
        const float horizonMelt = smoothstep(85000.0f, 170000.0f, dist);
        const float3 T = exp(-od * float3(0.84f, 0.96f, 1.12f)) * (1.0f - horizonMelt);
        // Spectral 1:1 atmospheric gradient locked directly to live skyPalette(sZ, sM, sH) + cloud cover + Mie solar aureole:
        // eliminates the color discontinuity between mountain haze, horizon sky, sun, and cloud undersides.
        float3 sZ, sM, sH;
        skyPalette(sZ, sM, sH);
        const float sunLumF = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightF = saturate((0.26f - sunLumF) * 4.5f);
        const float lowSunF = saturate(1.0f - sunDir.z * 2.6f);
        const float highSunF = smoothstep(0.18f, 0.48f, sunDir.z);
        const float cosSunF = saturate(dot(w, sunDir.xyz));
        // Exact elevation-matched sky gradient coupled to Hillaire SIGGRAPH 2020 physical Sky-View radiative transfer LUT.
        const float3 skyGradPal = lerp(sH, lerp(sM, sZ, smoothstep(0.18f, 0.65f, w.z)), smoothstep(-0.08f, 0.28f, w.z));
        const float3 hillaireSky = sampleHillaireSky(float3(w.xy, max(w.z, 0.01f)));
        const float3 skyGrad = lerp(skyGradPal, hillaireSky, (1.0f - nightF) * (1.0f - saturate(clouds.x * 0.70f)) * 0.55f);
        // Blend softly with overcast cloud-layer ambient when cloud cover rises so fog never fights the cloud deck color.
        const float cloudCov = saturate(clouds.x);
        const float3 cloudDeckCol = lerp(skyGrad, dot(skyGrad, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(0.95f, 0.98f, 1.03f), 0.55f);
        float3 atmoCol = lerp(skyGrad, cloudDeckCol, cloudCov * 0.55f);
        // Dual-scale Mie solar scattering: broad golden/D65 aureole + tighter forward solar halo.
        const float3 d65Mie = lerp(float3(1.0f, 0.66f, 0.36f), float3(0.96f, 0.98f, 1.02f), highSunF);
        const float mieHalo = (pow(cosSunF, 5.0f) * 0.14f + pow(cosSunF, 28.0f) * 0.24f) * (1.0f - cloudCov * 0.72f) * (1.0f - nightF);
        atmoCol += d65Mie * mieHalo;
        // Snowstorm frosted whiteout vs night alpine indigo, with physical atmospheric highlight rolloff.
        atmoCol = lerp(atmoCol, lerp(sH, float3(0.58f, 0.63f, 0.70f), 0.55f), weather.z * 0.55f);
        atmoCol = lerp(atmoCol, max(sH * 0.55f, float3(0.05f, 0.09f, 0.18f)), nightF);
        const float atMax = max(atmoCol.r, max(atmoCol.g, atmoCol.b));
        atmoCol = atmoCol / (1.0f + max(atMax - 0.65f, 0.0f) * 0.55f);
        // Attenuate additive volumetric scatter by transmittance T so far-horizon fog never double-adds into blinding whiteout.
        return float4(lit.rgb * T + atmoCol * (1.0f.xxx - T) + scatter * saturate(T + 0.20f), lit.a);
    }
    // Dungeons, Caves, Nordic Tombs, Dwemer Ruins, Blackreach & Interiors (sunColour.w < 0.5):
    // Cell-ambient-coupled subterranean depth mist + drifting 3D cavern haze (preserves deep blacks up close
    // while giving Blackreach cyan bioluminescence, Dwemer brass steam, and Nordic tombs atmospheric depth).
    if (sunColour.w < 0.5f && (flags.x & FLAG_FOG) != 0u) {
        const float3 wpInt = camPosW.xyz + w * min(dist * 0.5f, 3200.0f);
        const float caveNoise = 0.72f + 0.56f * cNoise(wpInt * 0.00048f + float3(0.0f, 0.0f, camPosW.w * 0.015f));
        const float intOd = max(dist - 220.0f, 0.0f) * 0.000095f * caveNoise;
        const float intT = exp(-min(intOd, 1.25f));
        const float3 cellAmb = max(float3(ambient[0].w, ambient[1].w, ambient[2].w), float3(0.010f, 0.013f, 0.018f));
        const float3 caveMist = min(cellAmb * 0.65f, 0.22f.xxx);
        return float4(lit.rgb * intT + caveMist * (1.0f - intT) + scatter * 0.85f, lit.a);
    }
    const float T = (flags.x & FLAG_FOG_REPLACES) != 0u ? exp(-sigma * tEnd) : 1.0f;
    return float4(lit.rgb * T + scatter, lit.a);
}
