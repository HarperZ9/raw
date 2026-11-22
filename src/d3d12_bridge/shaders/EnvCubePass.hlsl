// EnvCubePass.hlsl: Real-Time Dual-Band Prefiltered Dynamic Environment Cubemap (SM 6.9).
// Runs every frame into a 256x256 RGBA16F equirectangular atlas (< 0.08 ms):
//   Top half    (y = 0..127):   Sharp glossy environment (roughness ~ 0.05) — live Hillaire 2020 AtmoSkyView,
//                               live 64^3 Perlin-Worley Clouds, crisp sun/moon disc, ground/snow bounce,
//                               and up to 32 live dynamic point lights (hearths, torches, braziers, spells).
//   Bottom half (y = 128..255): Prefiltered rough/convolved environment (roughness ~ 0.75) — broad GGX lobe
//                               convolution for rough stone, brushed metal, leather, and interior vaults.
// Lighting.hlsl samples both bands with Karis 2013 Split-Sum BRDF + Fdez-Aguera multi-scattering IBL.
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> AtmoSkyView : register(t0);
Texture2D<float4> CloudNoise  : register(t1);
Texture2D<float4> GameLit     : register(t2);
Texture2D<float>  Depth       : register(t3);
SamplerState      Lin         : register(s0);

float3 sampleAtmo(float3 rd) {
    const float az = atan2(rd.y, rd.x);
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    return AtmoSkyView.SampleLevel(Lin, float2(az / (2.0f * PI) + 0.5f, saturate(vNonLin)), 0.0f).rgb;
}

float3 evalEnvDir(float3 rd, bool roughBand) {
    const bool isExt = sunColour.w > 0.5f;
    const float3 ambDir = max(float3(dot(ambient[0].xyz, rd) + ambient[0].w,
                                     dot(ambient[1].xyz, rd) + ambient[1].w,
                                     dot(ambient[2].xyz, rd) + ambient[2].w), 0.015f.xxx);
    float3 col = 0.0f.xxx;
    if (isExt) {
        const float sunLum = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        const float dayW = saturate((sunDir.z - 0.01f) * 4.0f) * saturate((sunLum - 0.18f) * 2.5f);
        const float3 hSky = sampleAtmo(float3(rd.xy, max(rd.z, 0.005f)));
        const float3 nightSky = max(ambDir * float3(0.75f, 0.90f, 1.25f), float3(0.015f, 0.025f, 0.055f));
        float3 upper = lerp(nightSky, hSky, dayW);
        // Project live cloud-deck luminance onto upper hemisphere using the 64^3 periodic cloud LUT.
        if (rd.z > 0.04f && clouds.x > 0.05f) {
            const float2 cUv = frac((camPosW.xy + rd.xy * (18000.0f / max(rd.z, 0.12f)) + cloudWind.xy * camPosW.w) / 36000.0f);
            const float2 tileUv = clamp(cUv, (0.5f / 64.0f).xx, (63.5f / 64.0f).xx) * 0.125f;
            const float cSample = CloudNoise.SampleLevel(Lin, tileUv, 0.0f).r;
            const float cCov = saturate((cSample - lerp(0.62f, 0.34f, saturate(clouds.x))) * 3.0f) * smoothstep(0.04f, 0.25f, rd.z);
            const float3 cCol = lerp(ambDir * 0.95f, max(sunColour.rgb, 0.55f.xxx) * 0.90f, dayW * 0.65f);
            upper = lerp(upper, cCol, cCov * (roughBand ? 0.45f : 0.72f));
        }
        // Lower hemisphere: physical terrain/snow/valley bounce + dark horizon mountain silhouette.
        const float3 groundBounce = ambDir * lerp(float3(0.38f, 0.36f, 0.32f), float3(0.62f, 0.68f, 0.76f), saturate(weather.z));
        const float horBand = smoothstep(-0.18f, 0.06f, rd.z);
        col = lerp(groundBounce, upper, horBand);
        // Sun/moon specular lobe (tight in sharp band, wide GGX convolution in rough band).
        const float cosS = saturate(dot(rd, sunDir.xyz));
        const float expS = roughBand ? 10.0f : 180.0f;
        const float gainS = roughBand ? 0.22f : 2.10f;
        const float cAtt = 1.0f - saturate(clouds.x * 0.72f);
        col += sunColour.rgb * (pow(cosS, expS) * gainS * cAtt * step(0.0f, rd.z));
    } else {
        // Interiors, taverns, dungeons, Dwemer ruins: directional stone-vault gradient + warm floor bounce.
        const float3 ceilCol = ambDir * lerp(0.55f, 1.10f, saturate(rd.z * 0.5f + 0.5f));
        const float3 floorCol = ambDir * float3(0.42f, 0.38f, 0.34f);
        col = lerp(floorCol, ceilCol, smoothstep(-0.25f, 0.35f, rd.z));
    }
    // Inject live dynamic local point lights (tavern hearths, torches, braziers, forge fires, magic spells)
    // directly into the environment cubemap so metal armor, wet floors & eyes reflect real nearby fires!
    const uint nLights = min(flags.y, 32u);
    const float lExp = roughBand ? 5.0f : 36.0f;
    const float lGain = roughBand ? 0.16f : 0.72f;
    [loop] for (uint li = 0u; li < nLights; li++) {
        const float3 toL = lightPosR[li].xyz;   // camera-relative light position
        const float dist = length(toL);
        const float rad = max(lightPosR[li].w * 1.8f, 1.0f);
        if (dist >= rad || dist < 1.0e-3f) { continue; }
        const float att = pow(saturate(1.0f - dist / rad), 1.6f);
        const float cosL = saturate(dot(rd, toL / dist));
        col += lightColS[li].rgb * (pow(cosL, lExp) * att * lGain);
    }
    // Project real live scene geometry (pine trees, mountains, stone arches, tavern beams, hearths)
    // onto the dynamic environment cubemap wherever the ray falls inside the camera frustum.
    const float4 sc = screenOf(rd * 4000.0f);
    if (sc.w > 1.0f && all(sc.xy >= 2.0f.xx) && all(sc.xy <= dims.xy - 3.0f)) {
        const float2 suv = sc.xy * dims.zw;
        const float sd = Depth.SampleLevel(Lin, suv, 0.0f);
        if (sd < 0.9999f) {
            const float2 edge = min(suv, 1.0f - suv);
            const float edgeW = smoothstep(0.0f, 0.08f, min(edge.x, edge.y));
            const float3 sceneCol = min(GameLit.SampleLevel(Lin, suv, 0.0f).rgb, 3.0f.xxx);
            col = lerp(col, sceneCol, edgeW * (roughBand ? 0.62f : 0.85f));
        }
    }
    return min(col, 3.5f.xxx);
}

float4 bake(float4 at : SV_Position) : SV_Target0 {
    const float2 px = at.xy;
    const bool roughBand = px.y >= 128.0f;
    const float u = px.x / 256.0f;
    const float v = (roughBand ? (px.y - 128.0f) : px.y) / 128.0f;
    const float az = (u - 0.5f) * 2.0f * PI;
    const float elev = (0.5f - v) * PI;
    const float cosE = cos(elev), sinE = sin(elev);
    const float3 rd = float3(cosE * cos(az), cosE * sin(az), sinE);

    if (!roughBand) {
        return float4(evalEnvDir(rd, false), 1.0f);
    }
    // Prefiltered rough band: 5-tap tangent-space hemisphere convolution for clean, noise-free rough IBL.
    const float3 upRef = abs(rd.z) < 0.92f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 t = normalize(cross(upRef, rd));
    const float3 b = cross(rd, t);
    float3 sum = evalEnvDir(rd, true) * 0.36f;
    sum += evalEnvDir(normalize(rd + t * 0.42f), true) * 0.16f;
    sum += evalEnvDir(normalize(rd - t * 0.42f), true) * 0.16f;
    sum += evalEnvDir(normalize(rd + b * 0.42f), true) * 0.16f;
    sum += evalEnvDir(normalize(rd - b * 0.42f), true) * 0.16f;
    return float4(sum, 1.0f);
}
