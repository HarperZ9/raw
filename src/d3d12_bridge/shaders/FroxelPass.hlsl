// FroxelPass.hlsl: True 160x90x64 3D Froxel Volumetric Participating Media Pipeline (SM 6.6+).
// Implements Wronski (SIGGRAPH 2014) / Hillaire (SIGGRAPH 2015) 3D Frustum-Aligned Voxel (Froxel) Grid
// packed into a 1280x720 RGBA16F 8x8 slice atlas (64 exponential-Z depth slices from kFroxNear=24u to kFroxFar=42,000u):
//   froxel_inject    : evaluates true 3D heterogeneous world-space alpine mist, valley/river ground fog banks,
//                      cascade + 3D cloud shadowed Hillaire sunlight, dynamically lit ambient sky multi-scattering,
//                      and all 128 clustered local lights into each (x, y, z) froxel.
//   froxel_integrate : front-to-back Beer-Lambert analytical scattering & transmittance integration along z = 0..63.
// Original work, MIT.
#ifdef FROXEL_INT
#define ATLAS_SLOT t1
#include "LightingCommon.hlsli"
Texture2D<float4> FroxelIn : register(t0);
#else
#define ATLAS_SLOT t0
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"
Texture2D<float4> TransLut    : register(t1);
Texture2D<float4> AtmoSkyView : register(t2);
Texture2D<float4> FroxelPrev  : register(t3);
SamplerState      Lin         : register(s0);
#endif

#ifndef FROXEL_INT
float4 samplePrevFroxel(float2 uv, float z) {
    const float sF = froxelZToSlice(z);
    const uint s0 = min(uint(floor(sF)), 63u);
    const uint s1 = min(s0 + 1u, 63u);
    const float sT = frac(sF);
    const float2 tileUv = clamp(uv, float2(0.5f / 160.0f, 0.5f / 90.0f), float2(159.5f / 160.0f, 89.5f / 90.0f));
    const float2 uv0 = (float2(float(s0 & 7u), float(s0 >> 3u)) + tileUv) * 0.125f;
    const float2 uv1 = (float2(float(s1 & 7u), float(s1 >> 3u)) + tileUv) * 0.125f;
    return lerp(FroxelPrev.SampleLevel(Lin, uv0, 0.0f), FroxelPrev.SampleLevel(Lin, uv1, 0.0f), sT);
}

float3 froxelTransAt(float mu) {
    const float r = 6360.0f + 0.5f;
    const float H = sqrt(6460.0f * 6460.0f - 6360.0f * 6360.0f);
    const float rho = sqrt(r * r - 6360.0f * 6360.0f);
    const float d = max(0.0f, -r * mu + sqrt(max(r * r * (mu * mu - 1.0f) + 6460.0f * 6460.0f, 0.0f)));
    return TransLut.SampleLevel(Lin, float2(saturate((d - (6460.0f - r)) / max(rho + H - (6460.0f - r), 1.0e-4f)), saturate(rho / H)), 0.0f).rgb;
}

float3 froxelSkyAt(float3 rd) {
    const float az = atan2(rd.y, rd.x);
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    const float3 c0 = AtmoSkyView.SampleLevel(Lin, float2(az / (2.0f * PI) + 0.5f, saturate(vNonLin)), 0.0f).rgb;
    const float poleW = smoothstep(0.94f, 0.995f, rd.z);
    if (poleW > 0.001f) {
        const float3 zAvg = (AtmoSkyView.SampleLevel(Lin, float2(0.25f, 0.99f), 0.0f).rgb +
                             AtmoSkyView.SampleLevel(Lin, float2(0.75f, 0.99f), 0.0f).rgb) * 0.5f;
        return lerp(c0, zAvg, poleW);
    }
    return c0;
}

float froxelHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

float4 froxel_inject(float4 at : SV_Position) : SV_Target0 {
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }
    const uint2 px = uint2(at.xy);
    const uint tileX = px.x / 160u;
    const uint tileY = px.y / 90u;
    const uint slice = tileY * 8u + tileX;   // 0..63
    const float2 uv = (float2(px.x % 160u, px.y % 90u) + 0.5f) / float2(160.0f, 90.0f);
    const float2 fullPx = uv * dims.xy;

    // Reconstruct exact 3D world-space ray & froxel center position from true camera eyeAt() (1st & 3rd person exact).
    // Convert view-space depth zMid -> Euclidean ray distance via dot(farDir, camFwd) so every 3D froxel light glow
    // and sunbeam aligns pixel-for-pixel with its 3D light source across the entire screen FOV!
    const float3 eye = eyeAt();
    const float3 camFwd = normalize(worldAt(dims.xy * 0.5f, 0.995f) - eye);
    const float3 farDir = normalize(worldAt(fullPx, 0.995f) - eye);
    const float z0 = froxelSliceZ(float(slice));
    const float z1 = froxelSliceZ(float(slice + 1u));
    const float jit = ign(float2(px.x % 160u, px.y % 90u) + float2(float(slice) * 3.7f, proj.z * 7.1f));
    const float zMid = lerp(z0, z1, 0.35f + 0.30f * jit);
    const float rayDist = zMid / max(dot(farDir, camFwd), 0.25f);
    const float3 pRel = eye + farDir * rayDist;
    const float3 wp = pRel + camPosW.xyz;

    // 3D heterogeneous world-space participating media density across 24u..42,000u:
    // Physically calibrated Beer-Lambert extinction (sigma) so froxel_integrate outputs genuine 3D transmittance
    // and dynamically lit valley/river fog banks + alpine mountain-collar clouds without foreground washout.
    const bool isInt = (sunColour.w < 0.5f);
    const float weatherDens = 1.0f + weather.w * 1.65f + weather.z * 1.95f + saturate(clouds.x - 0.30f) * 0.85f;
    const float3 drift = wp * 0.00032f + float3(cloudWind.xy * camPosW.w * 0.00012f, 0.0f);
    const float curlWarp = (cNoise(drift * 1.35f + 3.1f) - 0.5f) * 0.48f;
    const float mist3D = 0.42f + 0.92f * (cNoise(drift + curlWarp) * 0.56f + cNoise(drift * 2.4f + 7.1f) * 0.29f + cNoise(drift * 5.0f - 4.3f) * 0.15f);
    // Low-lying rolling 3D ground & river-basin fog banks (thickest near water level, dawn/dusk, and wet weather):
    const float aElevFog = astroSunElev();
    const float dawnDuskFog = exp(-pow((aElevFog - 0.03f) / 0.16f, 2.0f));
    const float basinH = max(wp.z - (waterA.w + 45.0f), 0.0f);
    const float groundBank = isInt ? 0.0f : exp(-basinH * 0.0024f) * (0.42f + 0.58f * cNoise(wp * 0.0011f + float3(cloudWind.xy * camPosW.w * 0.00008f, 0.0f))) * (0.48f + 0.85f * dawnDuskFog + weather.w * 0.78f);
    // True 3D alpine ridge collar clouds hugging mountain flanks (3,800u..7,800u world altitude):
    const float peakCollar = isInt ? 0.0f : exp(-pow((wp.z - 5000.0f) / 2600.0f, 2.0f)) * smoothstep(3600.0f, 11000.0f, zMid) * (0.55f + 0.65f * cNoise(wp * 0.00022f - float3(cloudWind.xy * camPosW.w * 0.00007f, 0.0f)));
    // True world-space altitude falloff (100% world-locked, zero camera-height drift!):
    const float worldElevF = max(wp.z - waterA.w, 0.0f);
    const float valleyFogF = exp(-worldElevF * 0.00042f) * (1.0f + 0.35f * saturate(clouds.x + weather.w));
    // Smooth near-camera exterior ramp so foreground (<650u) stays crystal crisp while valleys/mountains gain full 3D depth:
    const float extNearRamp = isInt ? 1.0f : lerp(smoothstep(220.0f, 1600.0f, rayDist), 1.0f, saturate(weather.w * 0.65f + weather.z * 0.75f));
    const float heightFalloff = isInt ? 1.0f : (0.24f + 0.66f * valleyFogF + peakCollar * 1.45f + groundBank * 1.25f);
    const float baseExt = isInt ? clamp(tune.x * 0.35f, 0.000018f, 0.000045f)
                                : clamp(tune.x * 0.145f, 0.000011f, 0.000024f);
    float sigma = baseExt * weatherDens * mist3D * heightFalloff * extNearRamp;

    // Radiative-transfer equilibrium in-scattered radiance (units of linear HDR radiance; multiplied by sigma on output
    // so froxel_integrate computes exact L_scat = integral(T * sigma * inScat * ds) matching physical Beer-Lambert transport!):
    float3 inScat = 0.0f.xxx;
    if (!isInt && sunDir.w > 0.5f && tune2.z > 0.0f) {
        const float4 sc = screenOf(pRel);
        const float cs = (flags.x & FLAG_CLOUDS) != 0u ? cloudShadowN(wp, 1u) : 1.0f;
        const float sh = (zMid < 12000.0f ? cascadeShadow(pRel, sc.z, false) : 1.0f) * cs;
        const float3 aSunF = astroSunDir();
        const float aElevF = astroSunElev();
        const float nightW = smoothstep(0.03f, -0.07f, aElevF);
        const float twilightW = smoothstep(-0.08f, 0.02f, aElevF) * (1.0f - smoothstep(0.08f, 0.26f, aElevF));
        const float3 beamDir = normalize(lerp(aSunF, sunDir.xyz, nightW));
        const float cosT = dot(farDir, beamDir);
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        // Dual-lobe Henyey-Greenstein Mie phase (crisp forward sun-shaft glow + restrained backscatter):
        const float ph = min(0.76f * froxelHG(cosT, lerp(0.70f, 0.76f, nightW)) + 0.24f * froxelHG(cosT, -0.22f), 2.8f);
        const float3 hTrans = froxelTransAt(clamp(aElevF, -0.10f, 1.0f));
        const float htLum = max(dot(hTrans, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 physSun = lerp(lerp(sunColour.rgb, hTrans * (max(sunLum, 0.10f) / htLum), 0.58f), float3(0.24f, 0.36f, 0.66f) * 0.55f, nightW);
        const float3 hSky = froxelSkyAt(float3(farDir.xy, max(farDir.z, 0.01f)));
        // RDR2-grade suspended 3D golden dust motes & alpine diamond-dust ice crystals catching direct sunlit beams:
        float moteBoost = 1.0f;
        if (zMid < 1800.0f && sh > 0.25f) {
            const float3 mCoord = wp * 0.075f - float3(cloudWind.xy * camPosW.w * 0.002f, camPosW.w * 0.35f);
            const float mCell = cHash(floor(mCoord));
            if (mCell > 0.965f) {
                const float mCore = exp(-dot(frac(mCoord) - 0.5f, frac(mCoord) - 0.5f) * 28.0f);
                moteBoost += mCore * (1.0f - zMid / 1800.0f) * (weather.z > 0.15f ? 2.8f : 1.6f);
            }
        }
        float3 uSunFr, uSkyZFr, uSkyHFr, uAmbUpFr, uAmbDnFr;
        evalWorldAtmosphere(uSunFr, uSkyZFr, uSkyHFr, uAmbUpFr, uAmbDnFr);
        const float3 syncSunFr = lerp(physSun, uSunFr, 0.65f);
        const float overcastFr = saturate(clouds.x * 0.85f - 0.12f + weather.w * 0.45f + weather.z * 0.42f);
        const float hLumFr = max(dot(hSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 skyDeSat = lerp(hSky, hLumFr.xxx * float3(0.92f, 0.96f, 1.02f), overcastFr * 0.72f);
        const float3 syncSkyFr = lerp(skyDeSat, lerp(uSkyHFr, uSkyZFr, saturate(max(farDir.z, 0.0f) * 2.2f)), 0.45f) * 0.72f;
        // Exact night horizon match so night mountain fogs stay deep indigo instead of bright grey-blue:
        const float3 ambSkyFr = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.025f.xxx);
        const float3 nightFogCol = lerp(float3(0.026f, 0.042f, 0.074f), ambSkyFr * float3(0.62f, 0.68f, 0.78f), saturate(clouds.x * 0.85f + weather.z * 0.50f) * 0.80f);
        // Disciplined twilight luminance envelope (prevents bright hazy mountain fogs during sunrise/sunset shifts!):
        const float todAmbGain = lerp(1.0f, 0.62f, twilightW) * lerp(0.46f, 0.94f, cs);
        const float3 ambInScat = lerp(syncSkyFr * todAmbGain, nightFogCol, nightW);
        // Dynamically lit 3D solar in-scattering: sunlit cloud gaps glow warm while shadowed ridges stay deep cool slate!
        const float sunInGain = tune2.z * lerp(0.36f, 0.16f, twilightW) * (1.0f - nightW * 0.78f) * (1.0f - overcastFr * 0.55f);
        inScat += ambInScat + syncSunFr * (sh * ph * sunInGain * moteBoost);
        // Pillar 7(b): RDR2-grade Cold-Weather Rhythmic Exhaled Breath Plume in 3D Froxel Space (snow/alpine/freezing nights):
        const float coldAmt = saturate(weather.z * 1.4f + smoothstep(2200.0f, 4500.0f, camPosW.z) * 0.75f + nightW * 0.35f);
        if (coldAmt > 0.25f && zMid > 24.0f && zMid < 105.0f) {
            const float breathPulse = pow(saturate(sin(camPosW.w * 1.65f)), 3.0f);
            if (breathPulse > 0.04f) {
                const float2 bUv = uv - float2(0.50f, 0.58f - breathPulse * 0.08f);
                const float bRad = dot(bUv, bUv);
                if (bRad < 0.032f) {
                    const float bNoise = cNoise(wp * 0.09f - float3(0.0f, 0.0f, camPosW.w * 1.4f));
                    const float bMask = exp(-bRad * 95.0f) * smoothstep(24.0f, 48.0f, zMid) * (1.0f - smoothstep(68.0f, 105.0f, zMid)) * bNoise * breathPulse * coldAmt;
                    sigma += bMask * 0.0038f;
                    inScat += lerp(ambInScat + 0.08f, syncSunFr * 0.65f + 0.12f, sh) * (bMask * 0.85f);
                }
            }
        }
    }
    // Clustered local lights in 3D froxel space (hearths, torches, braziers, 3D window portal lights, spells).
    if (tune2.y > 0.0f && flags.y > 0u) {
        const uint tx = min(uint(uv.x * TILES_X), TILES_X - 1u);
        const uint ty = min(uint(uv.y * TILES_Y), TILES_Y - 1u);
        const float sl = log(max(zMid, cluster.x) / cluster.x) * cluster.z;
        const uint cIdx = (min(uint(max(sl, 0.0f)), SLICES - 1u) * TILES_Y + ty) * TILES_X + tx;
        const uint4 bits = clusters[cIdx];
        [loop] for (uint w = 0u; w < 4u; w++) {
            uint b = bits[w];
            [loop] while (b != 0u) {
                const uint li = w * 32u + firstbitlow(b);
                b &= b - 1u;
                if (li >= flags.y) { continue; }
                const float3 toL = lightPosR[li].xyz - pRel;
                const float dL = length(toL);
                const float rL = lightPosR[li].w;
                if (dL >= rL) { continue; }
                // Clear near-camera & flame core (55..220u from eye) so holding a torch never creates a milky fog cloud:
                const float coreClear = smoothstep(48.0f, 165.0f, dL) * smoothstep(55.0f, 220.0f, rayDist);
                const float att = pow(saturate(1.0f - dL / rL), 2.2f) * coreClear;
                const float cosL = dot(farDir, toL / max(dL, 1.0e-3f));
                const float phL = min(froxelHG(cosL, isInt ? 0.50f : 0.46f) * (isInt ? 1.8f : 1.6f) + 0.20f, 1.5f);
                const float intSmoke = isInt ? (0.78f + 0.44f * cNoise(wp * 0.0095f - float3(0.0f, 0.0f, camPosW.w * 0.16f))) : 1.0f;
                // Ensure nearby exterior local lights scatter visibly even when extNearRamp keeps daylight air clear:
                sigma = max(sigma, (isInt ? 0.000024f : 0.000020f) * att * intSmoke);
                inScat += min(lightColS[li].rgb * (att * phL * tune2.y * intSmoke * (isInt ? 0.16f : 0.28f)), (isInt ? 0.14f : 0.35f).xxx);
            }
        }
    }
    float4 curVoxel = float4(inScat * sigma, sigma);
    // Hillaire (SIGGRAPH 2015) Temporal 3D Froxel Reprojection: reprojects 3D froxel center through prevWorldToClip
    // into last frame's 160x90x64 volume for silky, flicker-free 3D participating media (5x effective sample density).
    const float4 pClip = float4(dot(prevWorldToClip[0], float4(pRel, 1.0f)),
                                dot(prevWorldToClip[1], float4(pRel, 1.0f)),
                                dot(prevWorldToClip[2], float4(pRel, 1.0f)),
                                dot(prevWorldToClip[3], float4(pRel, 1.0f)));
    if (pClip.w > 1.0f && proj.z > 0.5f) {
        const float2 pUv = float2(pClip.x / pClip.w * 0.5f + 0.5f, 0.5f - pClip.y / pClip.w * 0.5f) / resScale.xy;
        if (all(pUv > 0.005f) && all(pUv < 0.995f)) {
            const float4 prevVoxel = samplePrevFroxel(pUv, pClip.w);
            // Motion- and depth-adaptive spatiotemporal blend: silky 0.22 when still, responsive 0.72 during fast pans/sprinting.
            const float motMag = saturate(length(pUv - uv) * 12.0f + abs(pClip.w - zMid) / max(zMid, 1.0f) * 3.0f);
            curVoxel = lerp(prevVoxel, curVoxel, lerp(0.22f, 0.72f, motMag));
        }
    }
    return curVoxel;
}
#else
// Front-to-back Beer-Lambert integration across z = 0..63 slices in the 8x8 froxel atlas.
float4 froxel_integrate(float4 at : SV_Position) : SV_Target0 {
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }
    const uint2 px = uint2(at.xy);
    const uint targetSlice = (px.y / 90u) * 8u + (px.x / 160u);
    const uint lx = px.x % 160u;
    const uint ly = px.y % 90u;

    float3 accScat = 0.0f.xxx;
    float  accTrans = 1.0f;
    [loop] for (uint s = 0u; s <= targetSlice; s++) {
        const uint sx = (s & 7u) * 160u + lx;
        const uint sy = (s >> 3u) * 90u + ly;
        const float4 voxel = FroxelIn.Load(int3(sx, sy, 0));
        const float dz = max(froxelSliceZ(float(s + 1u)) - froxelSliceZ(float(s)), 1.0f);
        const float ext = max(voxel.a, 0.0f);
        const float stepT = exp(-ext * dz);
        const float3 stepS = ext > 1.0e-6f ? (voxel.rgb * ((1.0f - stepT) / ext)) : (voxel.rgb * dz);
        accScat += accTrans * stepS;
        accTrans *= stepT;
        if (accTrans < 0.02f) { break; }
    }
    return float4(min(accScat, 3.5f.xxx), accTrans);
}
#endif
