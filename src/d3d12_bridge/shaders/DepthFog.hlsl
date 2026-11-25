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

// Henyey-Greenstein Mie forward-scattering phase function for dawn/dusk valley mist.
float dfPhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1.0e-4);
    return (1.0 - g2) / (12.5663706 * pow(denom, 1.5));
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

    // Alpine ridge emergence: peaks rising well above eye elevation emerge crisply above the mist deck,
    // while sky pixels only receive a gentle horizon-hugging mist veil (never washing out zenith sky!):
    float ridgeClear = 1.0 - smoothstep(0.18, 0.58, rayDir.y) * 0.52;
    float skyBand = isSky ? (1.0 - smoothstep(-0.02, 0.16, rayDir.y)) * 0.65 : 1.0;

    float opticalDepth = clamp(baseDensity * effZ * exp(-h0 / scaleH) * max(heightFactor, 0.0) * vaporDrift * ridgeClear, 0.0, 12.0);
    float fog = saturate((1.0 - exp(-opticalDepth)) * maxOpacity * nearClear * skyBand);

    // Cool/warm Henyey-Greenstein Mie forward scattering: warm gold toward rising sun, cool slate-blue away!
    float3 scatterSunDir = normalize(float3(0.42, 0.24, 0.87));
    float cosSun = dot(rayDir, scatterSunDir);
    float mieFwd = saturate(dfPhaseHG(cosSun, 0.62) * 2.2);
    float valleyLow = saturate(0.55 - rayDir.y * 1.4);
    float3 coolSlate = gFogColor.rgb * float3(0.84, 0.93, 1.08);
    float3 warmGold  = gFogColor.rgb * float3(1.24, 1.03, 0.80);
    float3 mistColor = lerp(coolSlate, warmGold, saturate(mieFwd * (0.45 + 0.55 * valleyLow)));

    return float4(lerp(c.rgb, mistColor, fog), c.a);
}
