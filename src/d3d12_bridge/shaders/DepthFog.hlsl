// DepthFog.hlsl: Analytical Exponential Height Fog & Mountain/Valley Mist pass on RAW's D3D12 device.
// Original work, MIT. Shader model 5.1 compatible (builds cleanly with BOTH fxc sm_5_1 and dxc sm_6_6/6_10 -WX).
//
// Reads the game's colour and depth (shared from D3D11), writes colour with physically-grounded
// analytical ray-integrated exponential height fog & rolling valley mist.
// Mode 0 copies colour unchanged (round-trip test exact check), mode 1 is height fog, mode 2 shows linear depth.

cbuffer PassConstants : register(b0)
{
    float4 gFogColor;   // rgb haze colour, a = maximum opacity
    float4 gParams;     // x density per game unit, y near plane, z far plane, w mode
    float4 gParams2;    // x marker on, y frame index (pause-aware), z interior flag (>0.5 disables outdoor valley mist), w unused
    float4 gDims;       // width, height, 1/width, 1/height
};

Texture2D<float4> gColor : register(t0);
Texture2D<float>  gDepth : register(t1);

struct VSOut { float4 pos : SV_Position; };

VSOut VSMain(uint id : SV_VertexID)
{
    // One triangle that covers the screen.
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float LinearDepth(float d, float n, float f)
{
    // Standard-Z (0 = near, 1 = far) perspective depth to view distance, guarded against zero/NaN.
    float safeN = max(n, 1.0e-3);
    float safeF = max(f, safeN + 1.0);
    float denom = max(safeF - saturate(d) * (safeF - safeN), 1.0e-5);
    return (safeN * safeF) / denom;
}

float3 MarkerColour(uint i)
{
    // Four fixed swatches, rotated by frame index, so a capture shows the
    // D3D12 pass ran and that it advances frame to frame.
    const float3 sw[4] = { float3(1.0, 0.1, 0.6), float3(0.1, 0.9, 1.0),
                           float3(1.0, 0.85, 0.1), float3(0.5, 0.2, 1.0) };
    return sw[i & 3u];
}

float dfHash3(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float dfNoise3(float3 x)
{
    float3 i = floor(x);
    float3 f = frac(x);
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = dfHash3(i + float3(0.0, 0.0, 0.0));
    float n100 = dfHash3(i + float3(1.0, 0.0, 0.0));
    float n010 = dfHash3(i + float3(0.0, 1.0, 0.0));
    float n110 = dfHash3(i + float3(1.0, 1.0, 0.0));
    float n001 = dfHash3(i + float3(0.0, 0.0, 1.0));
    float n101 = dfHash3(i + float3(1.0, 0.0, 1.0));
    float n011 = dfHash3(i + float3(0.0, 1.0, 1.0));
    float n111 = dfHash3(i + float3(1.0, 1.0, 1.0));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y),
                u.z);
}

// Wave 62: FXC sm_5_1 + DXC sm_6_6/6_10 bit-exact IEEE-754 finite guard (zero FXC X3577 warnings without /Gis).
bool dfIsFinite(float v)
{
    return (asuint(v) & 0x7F800000u) != 0x7F800000u;
}

float3 dfSanitize3(float3 c)
{
    return float3(
        dfIsFinite(c.r) ? max(c.r, 0.0) : 0.0,
        dfIsFinite(c.g) ? max(c.g, 0.0) : 0.0,
        dfIsFinite(c.b) ? max(c.b, 0.0) : 0.0);
}

// Henyey-Greenstein Mie forward-scattering phase function for dawn/dusk valley mist & lunar halos.
float dfPhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * clamp(cosTheta, -1.0, 1.0), 1.0e-4);
    return (1.0 - g2) / (12.5663706 * denom * sqrt(denom));
}

float4 PSMain(VSOut i) : SV_Target
{
    int2 p = int2(i.pos.xy);
    float4 c = gColor.Load(int3(p, 0));
    uint mode = (uint)gParams.w;

    if (gParams2.x > 0.5 && p.x < 96 && p.y < 24)
        return float4(MarkerColour((uint)p.x / 24u + (uint)gParams2.y), c.a);

    if (mode == 0u)
        return c;

    float d = gDepth.Load(int3(p, 0));
    float z = LinearDepth(d, gParams.y, gParams.z);
    if (mode == 2u)
    {
        float safeFar = max(gParams.z, 1.0);
        float depthVis = saturate(z / safeFar);
        return float4(depthVis, depthVis, depthVis, c.a);
    }

    // Mode 1: Wave 49 Analytical Ray-Integrated Exponential Height Fog & Mountain/Valley Mist.
    float baseDensity = max(gParams.x, 0.0);
    float maxOpacity = saturate(gFogColor.a);
    if (baseDensity <= 1.0e-8 || maxOpacity <= 1.0e-5)
        return c;

    // Strictly preserve clear indoor readability when interior flag (gParams2.z > 0.5) is set:
    bool isInterior = (gParams2.z > 0.5);
    float nearClear = smoothstep(max(gParams.y, 1.0), max(gParams.y * 4.0, 48.0), z);
    if (isInterior)
    {
        float intFog = (1.0 - exp(-baseDensity * z * 0.22)) * maxOpacity * 0.28 * nearClear;
        return float4(lerp(c.rgb, gFogColor.rgb, saturate(intFog)), c.a);
    }

    // Reconstruct perspective view ray direction (Y-up view space; aspect-aware):
    float2 safeInvDim = float2(max(gDims.z, 1.0e-6), max(gDims.w, 1.0e-6));
    float2 uv = (i.pos.xy + 0.5) * safeInvDim;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float aspect = max(gDims.x * safeInvDim.y, 0.5);
    float tanHalfFov = 0.637; // ~65 deg vertical FOV
    float3 rayDir = normalize(float3(ndc.x * aspect * tanHalfFov, ndc.y * tanHalfFov, 1.0));

    // Standard-Z sky protection (d >= 0.99998): keep zenith crisp; only hug low valley horizon softly.
    bool isSky = (d >= 0.99998);
    float effZ = isSky ? min(gParams.z * 0.65, 24000.0) : z;

    // Analytical ray-integrated exponential height fog:
    //   opticalDepth = baseDensity * effZ * exp(-h0 / H) * (1 - exp(-dy / H)) / (dy / H)
    // Exact 4th-order Taylor limit when |dy / H| < 1e-4 guarantees ZERO 0/0 NaN on horizontal rays!
    float scaleH = clamp(gParams.z * 0.045, 220.0, 1800.0);
    float h0 = scaleH * 0.28; // eye sits slightly above valley floor so river basins fill with mist
    float dy = rayDir.y * effZ;
    float u = dy / scaleH;
    float heightFactor = (abs(u) < 1.0e-4)
        ? (1.0 - u * (0.5 - u * (0.16666667 - u * 0.04166667)))
        : ((1.0 - exp(-clamp(u, -8.0, 24.0))) / u);

    // Pause-aware low-frequency rolling vapor drift in valley basins (gParams2.y advances per live frame):
    float simTime = gParams2.y * (1.0 / 60.0);
    float3 midP = rayDir * min(effZ * 0.45, scaleH * 4.5);
    float3 mistCoord = midP * float3(0.00055, 0.00145, 0.00055) + float3(simTime * 0.025, 0.0, simTime * 0.012);
    float nLo = dfNoise3(mistCoord);
    float nHi = dfNoise3(mistCoord * 2.15 + float3(5.3, 1.7, 9.1) - float3(simTime * 0.018, 0.0, 0.0));
    float vaporDrift = lerp(0.76, 1.24, saturate(nLo * 0.64 + nHi * 0.36));

    // Wave 62: Altitude-Stratified Alpine Cloud-Sea Inversion Fog Deck + Ridge Emergence:
    // Pools a crisp horizontal sea-of-clouds inversion layer in lower valley basins while mountain summits rise clear!
    float invDeckElev = smoothstep(0.12, -0.05, rayDir.y);
    float invDeckDist = smoothstep(scaleH * 0.55, scaleH * 3.6, effZ);
    float invCloudSea = invDeckElev * invDeckDist * lerp(0.18, 0.52, saturate(nLo * 0.70 + nHi * 0.30));

    float ridgeClear = 1.0 - smoothstep(0.18, 0.58, rayDir.y) * 0.52;
    float skyBand = isSky ? (1.0 - smoothstep(-0.02, 0.16, rayDir.y)) * 0.65 : 1.0;

    float baseOptDepth = baseDensity * effZ * exp(-h0 / scaleH) * max(heightFactor, 0.0) * vaporDrift * ridgeClear;
    float opticalDepth = clamp(baseOptDepth + invCloudSea * min(baseDensity * scaleH * 2.2, 1.4), 0.0, 12.0);
    float fog = saturate((1.0 - exp(-opticalDepth)) * maxOpacity * nearClear * skyBand);

    // Wave 62: Unified Atmospheric Scattering Color (Daytime Golden Mie, Twilight Anti-Solar Belt-of-Venus Arch,
    // and Nocturnal Cool Silver-Blue Moonlight Forward-Scatter Halo) - 100% FXC 5.1 & DXC SM 6.10 clean!
    float3 fogBaseRgb = dfSanitize3(gFogColor.rgb);
    float fogLum = dot(fogBaseRgb, float3(0.2126, 0.7152, 0.0722));
    float nightBlend = 1.0 - smoothstep(0.05, 0.18, fogLum);
    float twilightBlend = smoothstep(0.04, 0.16, fogLum) * (1.0 - smoothstep(0.26, 0.48, fogLum));

    float3 scatterDir = normalize(float3(0.42, 0.24, 0.87));
    float cosLight = clamp(dot(rayDir, scatterDir), -1.0, 1.0);
    float antiLight = saturate(-cosLight);
    float mieFwd = saturate(dfPhaseHG(cosLight, 0.62) * 2.2);
    float moonHalo = saturate(dfPhaseHG(cosLight, 0.74) * 1.85);
    float valleyLow = saturate(0.55 - rayDir.y * 1.4);

    float3 coolSlate = fogBaseRgb * float3(0.84, 0.93, 1.08);
    float3 warmGold  = fogBaseRgb * float3(1.24, 1.03, 0.80);
    float3 dayMist   = lerp(coolSlate, warmGold, saturate(mieFwd * (0.45 + 0.55 * valleyLow)));

    // Twilight anti-solar Belt-of-Venus rose-magenta band hugging right above a cool slate-indigo Earth shadow skirt:
    float beltDy = (rayDir.y - 0.045) / 0.075;
    float beltBand = exp(-min(beltDy * beltDy, 24.0)) * antiLight * twilightBlend;
    float earthSkirt = (1.0 - smoothstep(-0.04, 0.03, rayDir.y)) * antiLight * twilightBlend;
    float3 venusRose = fogBaseRgb * float3(1.22, 0.84, 1.08) + float3(0.025, 0.010, 0.022) * twilightBlend;
    float3 shadowIndigo = fogBaseRgb * float3(0.72, 0.82, 1.12);
    float3 twiMist = lerp(dayMist, venusRose, saturate(beltBand * 0.55));
    twiMist = lerp(twiMist, shadowIndigo, saturate(earthSkirt * 0.42));

    // Nocturnal moonlight silver-blue forward-scatter halo + crisp dark indigo mist floor:
    float3 moonMistBase = fogBaseRgb * float3(0.80, 0.92, 1.14);
    float3 moonHaloCol  = fogBaseRgb * float3(0.95, 1.08, 1.32) + float3(0.012, 0.018, 0.032);
    float3 nightMist    = lerp(moonMistBase, moonHaloCol, saturate(moonHalo * (0.40 + 0.60 * valleyLow)));

    float3 mistColor = lerp(twiMist, nightMist, nightBlend);
    float3 safeSrc = dfSanitize3(c.rgb);
    float3 outRgb = dfSanitize3(lerp(safeSrc, mistColor, fog));
    float outAlpha = dfIsFinite(c.a) ? saturate(c.a) : 1.0;
    return float4(outRgb, outAlpha);
}
