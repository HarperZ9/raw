// FroxelPass.hlsl: True 160x90x64 3D Froxel Volumetric Participating Media Pipeline (SM 6.9).
// Implements Wronski (SIGGRAPH 2014) / Hillaire (SIGGRAPH 2015) 3D Frustum-Aligned Voxel (Froxel) Grid
// packed into a 1280x720 RGBA16F 8x8 slice atlas (64 exponential-Z depth slices from 24u to 14,000u):
//   froxel_inject    : evaluates 3D heterogeneous world-space alpine mist, valley/river ground fog,
//                      cascade + cloud shadowed Hillaire sunlight, and all 128 clustered local lights
//                      (hearths, torches, braziers, 3D window portal lights) into each (x, y, z) froxel.
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

static const float kFroxNear = 24.0f;
static const float kFroxFar  = 14000.0f;

float froxelSliceZ(float s) {
    return kFroxNear * pow(kFroxFar / kFroxNear, saturate(s / 64.0f));
}

#ifndef FROXEL_INT
float4 samplePrevFroxel(float2 uv, float z) {
    const float zCl = clamp(z, kFroxNear, kFroxFar);
    const float sF = saturate(log(zCl / kFroxNear) / log(kFroxFar / kFroxNear)) * 63.0f;
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
    const float3 eye = eyeAt();
    const float3 farDir = normalize(worldAt(fullPx, 0.995f) - eye);
    const float z0 = froxelSliceZ(float(slice));
    const float z1 = froxelSliceZ(float(slice + 1u));
    const float jit = ign(float2(px.x % 160u, px.y % 90u) + float2(float(slice) * 3.7f, proj.z * 7.1f));
    const float zMid = lerp(z0, z1, 0.35f + 0.30f * jit);
    const float3 pRel = eye + farDir * zMid;
    const float3 wp = pRel + camPosW.xyz;

    // 3D heterogeneous world-space participating media density:
    // true 3D drifting alpine valley mist + mountain-collar cloud band + river-valley ground mist + weather storms.
    const bool isInt = (sunColour.w < 0.5f);
    const float weatherDens = 1.0f + weather.w * 1.55f + weather.z * 1.75f + saturate(clouds.x - 0.30f) * 0.72f;
    const float3 drift = wp * 0.00042f + float3(cloudWind.xy * camPosW.w * 0.00014f, 0.0f);
    const float mist3D = 0.52f + 0.78f * (cNoise(drift) * 0.65f + cNoise(drift * 2.3f + 7.1f) * 0.35f);
    const float peakCollar = isInt ? 0.0f : exp(-pow((wp.z - 4600.0f) / 2800.0f, 2.0f)) * smoothstep(3200.0f, 9500.0f, zMid) * 1.25f;
    const float heightFalloff = isInt ? 1.0f : (0.30f + 0.70f * exp(-max(pRel.z + 180.0f, 0.0f) * 0.00075f) + peakCollar);
    const float sigma = max(tune.x, 0.00011f) * weatherDens * mist3D * heightFalloff;

    float3 inScat = 0.0f.xxx;
    if (!isInt && sunDir.w > 0.5f && tune2.z > 0.0f) {
        const float4 sc = screenOf(pRel);
        const float cs = (flags.x & FLAG_CLOUDS) != 0u ? cloudShadowN(wp, 1u) : 1.0f;
        const float sh = cascadeShadow(pRel, sc.z, false) * cs;
        const float cosT = dot(farDir, sunDir.xyz);
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float nightW = saturate((0.28f - sunLum) * 4.5f);
        const float ph = min(0.75f * froxelHG(cosT, lerp(0.68f, 0.76f, nightW)) + 0.25f * froxelHG(cosT, -0.20f), 3.2f);
        const float3 hTrans = froxelTransAt(clamp(sunDir.z, -0.10f, 1.0f));
        const float htLum = max(dot(hTrans, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float3 physSun = lerp(sunColour.rgb, hTrans * (max(sunLum, 0.10f) / htLum), 0.58f * (1.0f - nightW));
        const float3 hSky = froxelSkyAt(float3(farDir.xy, max(farDir.z, 0.01f)));
        inScat += physSun * (sh * ph * tune2.z * 0.55f);
        inScat += hSky * (0.014f * (0.4f + 0.6f * cs) * (1.0f - nightW * 0.65f));
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
                const float att = pow(saturate(1.0f - dL / rL), 2.2f);
                const float cosL = dot(farDir, toL / max(dL, 1.0e-3f));
                const float phL = min(froxelHG(cosL, 0.52f) * 2.4f + 0.35f, 2.2f);
                inScat += lightColS[li].rgb * (att * phL * tune2.y * (isInt ? 0.42f : 0.28f));
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
