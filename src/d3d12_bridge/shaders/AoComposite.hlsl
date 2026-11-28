// AoComposite.hlsl: writes the game's frame, with Jimenez 2016 Colored Multi-Bounce GTAO visibility
// applied, into the shared output. One full-screen triangle through the engine's RHI, whose raster
// convention puts the uniform at b0, the storage buffer at u0 as a root UAV and the texture at t0.
// Original work, MIT.
//
// The frame here is the game's display-referred image (after its tone map, before the UI).
// Wave 50 replaces naive monochrome `color * ao` darkening with Jimenez et al. (SIGGRAPH 2016)
// physically-based cubic multi-bounce colored AO so shadowed corners on snow, skin, crimson banners,
// and warm timber preserve rich chromatic indirect bounce instead of turning dead muddy grey-black!

cbuffer C : register(b0)
{
    uint  width;
    uint  height;
    uint  mode;       // 0 pass the frame through, 1 apply colored multi-bounce AO, 2 show visibility
    uint  pad0;
    float strength;   // 0 leaves the frame unchanged, 1 applies full multi-bounce visibility
    float pad1, pad2, pad3;
};
RWStructuredBuffer<float> A : register(u0);   // visibility, one float a pixel, top row first
Texture2D<float4> Frame : register(t0);

float4 vs(uint i : SV_VertexID) : SV_Position
{
    return float4(i == 1u ? 3.0f : -1.0f, i == 2u ? -3.0f : 1.0f, 0.0f, 1.0f);
}

float3 ToLinear(float3 c)
{
    const float3 s = saturate(c);
    return float3(s.x <= 0.04045f ? s.x / 12.92f : pow(max((s.x + 0.055f) / 1.055f, 0.0f), 2.4f),
                  s.y <= 0.04045f ? s.y / 12.92f : pow(max((s.y + 0.055f) / 1.055f, 0.0f), 2.4f),
                  s.z <= 0.04045f ? s.z / 12.92f : pow(max((s.z + 0.055f) / 1.055f, 0.0f), 2.4f));
}

float3 ToSrgb(float3 c)
{
    const float3 s = saturate(c);
    return float3(s.x <= 0.0031308f ? s.x * 12.92f : 1.055f * pow(max(s.x, 0.0f), 1.0f / 2.4f) - 0.055f,
                  s.y <= 0.0031308f ? s.y * 12.92f : 1.055f * pow(max(s.y, 0.0f), 1.0f / 2.4f) - 0.055f,
                  s.z <= 0.0031308f ? s.z * 12.92f : 1.055f * pow(max(s.z, 0.0f), 1.0f / 2.4f) - 0.055f);
}

// Jimenez et al. (SIGGRAPH 2016) physically-based cubic multi-bounce colored ambient occlusion:
//   multiBounceAO(ao, albedo) = ao * (a * ao^2 + b * ao + c)
// Guaranteed monotonic in [0, 1], exact 0 at ao = 0, exact 1 at ao = 1 (sky/unoccluded untouched!).
float3 multiBounceAO(float ao, float3 albedo)
{
    const float v = saturate(ao);
    const float3 alb = saturate(albedo);
    const float3 a =  2.0404f * alb - 0.3324f;
    const float3 b = -4.7951f * alb + 0.6417f;
    const float3 c =  2.7552f * alb + 0.6903f;
    return saturate(max(v.xxx, ((v * a + b) * v + c) * v));
}

float4 fs(float4 at : SV_Position) : SV_Target0
{
    const uint safeW = max(width, 1u);
    const uint safeH = max(height, 1u);
    const uint x = min((uint)at.x, safeW - 1u);
    const uint y = min((uint)at.y, safeH - 1u);
    const float4 c = Frame.Load(int3(x, y, 0));
    if (mode == 0u) { return c; }

    const float rawV = A[y * safeW + x];
    const float v = (isnan(rawV) || isinf(rawV)) ? 1.0f : saturate(rawV);
    if (mode == 2u) { return float4(v, v, v, 1.0f); }

    const float str = isfinite(strength) ? saturate(strength) : 0.0f;
    const float safeAlpha = isfinite(c.a) ? saturate(c.a) : 1.0f;
    // Exact sky / unoccluded & zero-strength protection (untouched passthrough!):
    if (str <= 1.0e-5f || v >= 0.9995f) { return float4(c.rgb, safeAlpha); }

    const float3 safeRgb = (any(isnan(c.rgb)) || any(isinf(c.rgb))) ? 0.0f.xxx : saturate(c.rgb);
    const float3 linCol = ToLinear(safeRgb);

    // Chromatic surface reflectance proxy for Jimenez 2016 colored multi-bounce AO:
    // Preserves rich crimson banners, warm timber/skin, and luminous glacial snow in occluded corners,
    // and enforces a physical multi-bounce floor (0.18) so screen-space AO never paints dark cel-shading
    // outlines around moving characters or foreground silhouettes!
    const float luma = dot(linCol, float3(0.2126f, 0.7152f, 0.0722f));
    const float3 chromaAlbedo = saturate(pow(max(linCol, 0.0f.xxx), 0.55f));
    const float blackFloorGate = smoothstep(0.002f, 0.025f, luma);
    const float3 effAlbedo = chromaAlbedo * blackFloorGate;
    const float vBounded = lerp(0.18f, 1.0f, v);
    const float3 mbVis = multiBounceAO(vBounded, effAlbedo);
    const float3 k = lerp(1.0f.xxx, mbVis, str);

    return float4(ToSrgb(linCol * k), safeAlpha);
}
