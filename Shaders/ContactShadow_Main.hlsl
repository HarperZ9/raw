// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Contact Shadow CS — Screen-space ray march toward the directional light.
//
// Algorithm:
//   For each pixel, reconstruct a view-space position from depth, project the
//   sun direction into screen space, then march a 2D ray in UV space.  At each
//   step, interpolate the expected view-space depth along the ray and compare
//   it with the actual scene depth.  If the ray passes behind geometry (ray Z
//   is greater than scene Z) within a thickness tolerance, the pixel is
//   shadowed.  Temporal blue-noise dither removes banding across frames.

cbuffer ContactShadowCB : register(b0)
{
    float4x4 ViewMatrix;        // World-to-view (row-major)
    float4x4 ProjMatrix;        // View-to-clip   (row-major)
    uint2    ScreenDims;        // Full-res width, height
    float    NearZ;             // Camera near clip (game units)
    float    FarZ;              // Camera far clip  (game units)
    float3   SunDirWorld;       // Normalized world-space direction TOWARD the sun
    float    RayLength;         // Max ray travel in UV space (0-1 fraction of screen)
    float    Thickness;         // Depth-behind tolerance (fraction of view Z)
    float    Intensity;         // Shadow darkness multiplier (0-2)
    int      MaxSteps;          // Ray march step count (4-64)
    uint     FrameIndex;        // Monotonic frame counter for temporal dither
    float    FPDepthThreshold;  // First-person mask: skip if linearZ < this
    float    pad0;
};

// ── Resources ────────────────────────────────────────────────────────────
Texture2D<float>   DepthTex     : register(t0);   // HiZ mip 0 (reversed-Z: near=1, far=0)
Texture2D<float>   LinearDepth  : register(t31);  // Pre-computed view-space Z

RWTexture2D<float> ShadowOutput : register(u0);   // 1 = lit, 0 = shadowed

// ── Helpers ──────────────────────────────────────────────────────────────

// Reconstruct view-space position from UV + view-space linear depth.
float3 UVToView(float2 uv, float viewZ)
{
    // UV -> NDC: x in [-1,1], y in [-1,1] (D3D11: top-left is UV 0,0)
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return float3(ndc.x * viewZ / ProjMatrix[0][0],
                  ndc.y * viewZ / ProjMatrix[1][1],
                  viewZ);
}

// Project a view-space position back to UV.
float2 ViewToUV(float3 v)
{
    float2 ndc = float2(v.x * ProjMatrix[0][0] / v.z,
                        v.y * ProjMatrix[1][1] / v.z);
    return float2(ndc.x * 0.5 + 0.5,
                  1.0 - (ndc.y * 0.5 + 0.5));
}

// Blue-noise-quality hash dither with golden-ratio temporal offset.
// Returns a value in [0, 1) that changes each frame.
float TemporalDither(uint2 pixel, uint frame)
{
    // Spatial: integer-lattice hash (Jarzynski & Olano 2020)
    uint h = pixel.x * 1597u + pixel.y * 51749u + frame * 95317u;
    h = (h ^ (h >> 16u)) * 0x45d9f3bu;
    h = (h ^ (h >> 16u)) * 0x45d9f3bu;
    h =  h ^ (h >> 16u);
    float spatial = float(h) / 4294967295.0;

    // Temporal: golden-ratio additive recurrence
    float gr = float(frame) * 0.6180339887; // 1 / phi
    return frac(spatial + gr);
}

// ── Main ─────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2   coord     = int2(DTid.xy);
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv        = (float2(coord) + 0.5) * texelSize;

    // ── 1. Load reversed-Z depth from HiZ.  Skip sky (sky=0 in reversed-Z). ──
    float rawDepth = DepthTex.Load(int3(coord, 0));
    if (rawDepth < 0.0001)
    {
        ShadowOutput[coord] = 1.0;
        return;
    }

    // ── 2. Load linear depth.  Skip first-person geometry. ──────────
    float linearZ = LinearDepth.Load(int3(coord, 0));
    if (linearZ < FPDepthThreshold)
    {
        ShadowOutput[coord] = 1.0;
        return;
    }

    // ── 3. Reconstruct view-space position. ─────────────────────────
    float3 viewPos = UVToView(uv, linearZ);

    // ── 4. Transform sun direction: world -> view space. ────────────
    //       ViewMatrix columns give the view-space axes in world space,
    //       so dotting SunDirWorld against each column yields view coords.
    float3 sunView;
    sunView.x = dot(SunDirWorld, float3(ViewMatrix[0][0], ViewMatrix[1][0], ViewMatrix[2][0]));
    sunView.y = dot(SunDirWorld, float3(ViewMatrix[0][1], ViewMatrix[1][1], ViewMatrix[2][1]));
    sunView.z = dot(SunDirWorld, float3(ViewMatrix[0][2], ViewMatrix[1][2], ViewMatrix[2][2]));
    sunView = normalize(sunView);

    // ── 5. Project ray endpoint to screen to get 2D march direction. ─
    //       Push a short segment along the sun direction in view space,
    //       project both endpoints to UV, and take the difference.
    float  segLen     = linearZ * 0.5;
    float3 endView    = viewPos + sunView * segLen;
    float2 endUV      = ViewToUV(endView);
    float2 rayDir2D   = endUV - uv;
    float  rayDirLen  = length(rayDir2D);

    if (rayDirLen < 1e-6)
    {
        ShadowOutput[coord] = 1.0;
        return;
    }

    rayDir2D /= rayDirLen; // normalize to unit direction in UV space

    // ── 6. Set up the march. ────────────────────────────────────────
    //       Each step advances by (RayLength / MaxSteps) along the 2D
    //       ray direction.  We also interpolate view-space Z along the
    //       3D ray to know what depth the ray "expects" at each sample.
    float2 stepUV = rayDir2D * (RayLength / float(MaxSteps));

    // View-space Z at the far end of the full ray
    float3 farView = viewPos + sunView * (linearZ * RayLength * 2.0);
    float  deltaZ  = (farView.z - linearZ) / float(MaxSteps);

    // ── 11. Temporal dither: jitter the starting offset. ─────────────
    float dither = TemporalDither(DTid.xy, FrameIndex);

    // ── 7-10. March and test. ───────────────────────────────────────
    float shadow = 1.0;

    for (int i = 1; i <= MaxSteps; i++)
    {
        float  offsetF  = float(i) + dither;
        float  t        = offsetF / float(MaxSteps); // 0..1 progress

        // Current UV sample position
        float2 sampleUV = uv + stepUV * offsetF;

        // Bail if ray leaves the screen
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
            break;

        int2 sampleCoord = clamp(int2(sampleUV * float2(ScreenDims)),
                                 int2(0, 0),
                                 int2(ScreenDims) - 1);

        // Skip sky pixels along the ray
        float sampleRawZ = DepthTex.Load(int3(sampleCoord, 0));
        if (sampleRawZ < 0.0001)
            continue;

        // ── 7. Compare interpolated ray depth with scene depth. ──
        float sceneZ    = LinearDepth.Load(int3(sampleCoord, 0));
        float expectedZ = linearZ + deltaZ * offsetF;

        // ── 8. Shadow test: ray passed behind geometry. ──────────
        //       depthDelta > 0 means the ray is deeper (farther) than
        //       the scene surface → the ray went behind it.
        //       We require the overshoot to be within a thickness
        //       band to avoid shadowing from distant background.
        float depthDelta = expectedZ - sceneZ;

        if (depthDelta > 0.0 && depthDelta < Thickness * expectedZ)
        {
            // ── 9. Soft falloff: near shadows are darker. ────────
            float falloff = 1.0 - t;
            falloff *= falloff; // quadratic

            // ── 10. Accumulate: keep the darkest shadow. ─────────
            shadow = min(shadow, 1.0 - Intensity * falloff);
        }
    }

    ShadowOutput[coord] = saturate(shadow);
}
