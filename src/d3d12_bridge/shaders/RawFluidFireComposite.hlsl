// RawFluidFireComposite.hlsl: Live GPU 64x64x64 3D Navier-Stokes Fire/Smoke/Radiance Volumetric Compositor (SM 6.6 / 6.10).
// Raymarches the real Eulerian Navier-Stokes combustion & soot fields from RawFluidFireSim.hlsl onto ColorOut (RGBA16F),
// strictly depth-occluded against Skyrim's SceneDepth (standard-Z: 0=near, 1=far) with fast AABB slab early-out.
// Produces incandescent Planck blackbody flame tongues + Beer-Lambert dark charcoal soot lit from within by the
// 3D Fire-to-Smoke Radiance Cascade.
// Original clean-room implementation, MIT License.

cbuffer FireCompCB : register(b0)
{
    float4 clipToWorld[4];      // camera-relative clip -> camera-relative world
    float4 camPosW;             // xyz = absolute camera origin (posAdjust), w = simTimeSec
    float4 domainMinW;          // xyz = world min corner of 64^3 fire cube, w = domainSpanW
    float4 domainInvSpanW;      // xyz = 1 / domainSpanW, w = frameIndex (for IGN jitter)
    float4 dimsAndInv;          // xy = width, height; zw = 1/width, 1/height
    float4 resScaleAndFlags;    // xy = resScale.xy; z = activeEmitters; w = interiorFlag (0=ext, 1=int)
    float4 sunDir;              // xyz = unit sun dir, w = sunIntensity
    float4 sunCol;              // rgb = sun HDR color, w = ambientFill
    float4 _pad[5];
};

Texture2D<float>    SceneDepth   : register(t0);
Texture3D<float4>   FireThermo   : register(t1); // x=TempK, y=Fuel, z=Soot, w=BurnRate
Texture3D<float4>   FireRadiance : register(t2); // rgb=InternalFireScatterRGB, a=SunShadowTransmittance [0..1]
RWTexture2D<float4> ColorOut     : register(u0);
SamplerState        LinClamp     : register(s0);

static const float kPi = 3.14159265358979f;

float fireCompIgn(float2 px, float frameIdx)
{
    const float2 p = px + 5.588238f * frac(frameIdx * 0.015625f) * 64.0f;
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float3 fireCompEyeRel()
{
    const float4 e = float4(clipToWorld[0].z, clipToWorld[1].z, clipToWorld[2].z, clipToWorld[3].z);
    const float3 p = (abs(e.w) > 1.0e-9f) ? (e.xyz / e.w) : float3(0.0f, 0.0f, 0.0f);
    return (dot(p, p) < 2000.0f * 2000.0f) ? p : float3(0.0f, 0.0f, 0.0f);
}

float3 fireCompWorldAt(float2 px, float d)
{
    const float2 uv = (px + 0.5f) * dimsAndInv.zw;
    const float2 rs = (resScaleAndFlags.x > 0.01f && resScaleAndFlags.y > 0.01f) ? resScaleAndFlags.xy : float2(1.0f, 1.0f);
    const float4 c = float4(uv.x * rs.x * 2.0f - 1.0f, (1.0f - uv.y * rs.y) * 2.0f - 1.0f, d, 1.0f);
    const float4 w = float4(dot(clipToWorld[0], c), dot(clipToWorld[1], c),
                            dot(clipToWorld[2], c), dot(clipToWorld[3], c));
    return (abs(w.w) > 1.0e-9f) ? (w.xyz / w.w) : float3(0.0f, 0.0f, 0.0f);
}

// Physically calibrated Planck blackbody emission spectrum at Rec.709 wavelengths (635nm, 535nm, 460nm),
// matched to RawFluidFireSim::evalPlanckBlackbody with Stefan-Boltzmann T^4 scaling.
// Cutoff raised to 1180K so cool outer gas never bleeds a fuzzy neon-red halo onto sky or ceilings.
float3 evalPlanckFlameRGB(float tempK)
{
    if (isnan(tempK) || isinf(tempK) || tempK < 1180.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float T = clamp(tempK, 1180.0f, 4500.0f);
    const float3 c2OverLambda = float3(22657.9f, 26893.0f, 31277.8f);
    const float3 invLambda5   = float3(0.415f,   1.000f,   2.148f);
    const float3 expoShift = min((c2OverLambda - c2OverLambda.x) / T, 60.0f.xxx);
    const float3 chromaRaw = invLambda5 * exp(-expoShift);
    const float  maxCh = max(chromaRaw.x, max(chromaRaw.y, chromaRaw.z));
    const float3 chroma = chromaRaw / max(maxCh, 1.0e-6f);

    const float gate = smoothstep(1260.0f, 1850.0f, T);
    const float tNorm = T / 1800.0f;
    const float t4 = (tNorm * tNorm) * (tNorm * tNorm);
    // Calibrated linear HDR flame luminance: crisp golden-orange flame tongues above 1260K transitioning cleanly into dark soot
    return min(chroma * (gate * 1.15f * t4), float3(28.0f, 22.0f, 16.0f));
}

float fireCompHash31(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float fireCompNoise3(float3 x)
{
    const float3 i = floor(x);
    const float3 f = frac(x);
    const float3 u = f * f * (3.0f - 2.0f * f);
    const float n000 = fireCompHash31(i + float3(0.0f, 0.0f, 0.0f));
    const float n100 = fireCompHash31(i + float3(1.0f, 0.0f, 0.0f));
    const float n010 = fireCompHash31(i + float3(0.0f, 1.0f, 0.0f));
    const float n110 = fireCompHash31(i + float3(1.0f, 1.0f, 0.0f));
    const float n001 = fireCompHash31(i + float3(0.0f, 0.0f, 1.0f));
    const float n101 = fireCompHash31(i + float3(1.0f, 0.0f, 1.0f));
    const float n011 = fireCompHash31(i + float3(0.0f, 1.0f, 1.0f));
    const float n111 = fireCompHash31(i + float3(1.0f, 1.0f, 1.0f));
    const float nx00 = lerp(n000, n100, u.x);
    const float nx10 = lerp(n010, n110, u.x);
    const float nx01 = lerp(n001, n101, u.x);
    const float nx11 = lerp(n011, n111, u.x);
    return lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
}

// High-frequency upward-advected turbulent flame-tongue detail mask [0..1]
float fireCompTongueDetail(float3 pAbs, float timeSec)
{
    const float3 q = pAbs * 0.14f - float3(0.0f, 0.0f, timeSec * 5.2f);
    const float n0 = fireCompNoise3(q);
    const float n1 = fireCompNoise3(q * 2.15f + float3(7.3f, 3.1f, -timeSec * 3.4f));
    return saturate(n0 * 0.65f + n1 * 0.35f);
}

float evalPhaseHG(float cosT, float g)
{
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * kPi * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

[numthreads(8, 8, 1)]
void cs_fire_composite(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 dimsU = uint2(dimsAndInv.xy + 0.5f);
    if (dtid.x >= dimsU.x || dtid.y >= dimsU.y || resScaleAndFlags.z < 0.5f || domainMinW.w <= 1.0f) {
        return;
    }

    const float2 px = float2(dtid.xy);
    const float3 eyeRel = fireCompEyeRel();
    const float3 farRel = fireCompWorldAt(px, 0.99f);
    const float3 rayVec = farRel - eyeRel;
    const float  rayLen = length(rayVec);
    if (rayLen < 1.0e-4f) {
        return;
    }
    const float3 rayDir = rayVec / rayLen;
    const float3 rayOriginAbs = camPosW.xyz + eyeRel;

    // Fast slab ray-AABB intersection against live 64x64x64 world fire cube (early-outs ~95% of pixels in 3 ops).
    const float3 boxMin = domainMinW.xyz;
    const float3 boxMax = domainMinW.xyz + domainMinW.w.xxx;
    const float3 invDir = 1.0f / select(abs(rayDir) > 1.0e-6f, rayDir, float3(1.0e-6f, 1.0e-6f, 1.0e-6f));
    const float3 t0 = (boxMin - rayOriginAbs) * invDir;
    const float3 t1 = (boxMax - rayOriginAbs) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tBig   = max(t0, t1);
    const float  tEnter = max(max(tSmall.x, max(tSmall.y, tSmall.z)), 2.0f);
    const float  tExit  = min(tBig.x, min(tBig.y, tBig.z));
    if (tExit <= tEnter + 0.25f) {
        return;
    }

    // Strict occlusion against Skyrim's standard-Z SceneDepth (0 = near, 1 = far; sky >= 0.99998).
    const float sceneDRaw = SceneDepth.Load(int3(dtid.xy, 0));
    const float sceneD = (isnan(sceneDRaw) || isinf(sceneDRaw)) ? 1.0f : saturate(sceneDRaw);
    const bool isSkyPixel = (sceneD >= 0.99998f);
    float sceneDist = 1.0e7f;
    if (!isSkyPixel && sceneD > 0.0f) {
        const float3 hitRel = fireCompWorldAt(px, sceneD);
        const float dLen = length(hitRel - eyeRel);
        sceneDist = (isnan(dLen) || isinf(dLen)) ? 1.0e7f : dLen;
    }
    // On sky pixels, tighten raymarch extent to active near-field fire range so distant smudges never bleed onto sky.
    const float skyCapT = isSkyPixel ? min(tEnter + domainMinW.w * 1.15f, 1200.0f) : tExit;
    const float tMax = min(min(tExit, skyCapT), sceneDist);
    if (tMax <= tEnter + 0.25f) {
        return;
    }

    // 28-step IGN-jittered front-to-back volumetric raymarch through live 3D Navier-Stokes Thermo + Radiance fields.
    static const uint kSteps = 28u;
    const float spanT = tMax - tEnter;
    const float ds = spanT / float(kSteps);
    const float jit = fireCompIgn(px, domainInvSpanW.w);

    const float3 Lsun = (dot(sunDir.xyz, sunDir.xyz) > 1.0e-4f) ? normalize(sunDir.xyz) : float3(0.0f, 0.0f, 1.0f);
    // Dual-lobe Henyey-Greenstein Mie phase function (forward silver-lining peak + back-scatter billow modeling)
    const float  cosSun = dot(rayDir, Lsun);
    const float  hgFwd  = evalPhaseHG(cosSun, 0.62f);
    const float  hgBack = evalPhaseHG(cosSun, -0.22f);
    const float  hgDual = lerp(hgFwd, hgBack, 0.35f);
    const float  isInt  = saturate(resScaleAndFlags.w);
    const float  extSunMask = max(sunDir.w, 0.0f) * (1.0f - isInt * 0.88f);
    const float3 sunKeyCol  = max(sunCol.rgb, 0.0f.xxx) * (extSunMask * (0.45f + 5.2f * hgDual));
    const float3 ambSkyFill = lerp(float3(0.020f, 0.023f, 0.028f), float3(0.018f, 0.015f, 0.012f), isInt) * max(sunCol.w, 0.35f);
    // Physical ash/wood-smoke single-scattering albedo: authentic dark carbon charcoal where self-shadowed (sunShadowT -> 0.03),
    // luminous warm sunlit curls on upper sunward billows (sunShadowT -> 1.0) via Beer-Powder multi-scattering.
    const float3 sootAlbedo = float3(0.130f, 0.115f, 0.098f);
    // Hard indoor transmittance floor (>= 0.78f): mathematically prevents pitch-black indoor orbs!
    const float  minTransFloor = lerp(0.015f, 0.78f, isInt);

    float3 accumRgb = float3(0.0f, 0.0f, 0.0f);
    float  trans = 1.0f;
    float  accumSootOpt = 0.0f;

    [loop] for (uint i = 0u; i < kSteps; ++i) {
        const float t = tEnter + (float(i) + jit) * ds;
        const float3 pAbs = rayOriginAbs + rayDir * t;
        const float3 uvw = (pAbs - boxMin) * domainInvSpanW.xyz;

        // Smooth 3D box-edge fade over outer 8% of volume so cube boundaries are 100% invisible.
        const float3 edgeD = min(uvw, 1.0f.xxx - uvw);
        const float edgeFade = smoothstep(0.0f, 0.08f, min(edgeD.x, min(edgeD.y, edgeD.z)));
        if (edgeFade <= 1.0e-4f) {
            continue;
        }

        const float4 thermoRaw = FireThermo.SampleLevel(LinClamp, uvw, 0.0f);
        const float4 radRaw    = FireRadiance.SampleLevel(LinClamp, uvw, 0.0f);
        const float4 thermo = (any(isnan(thermoRaw)) || any(isinf(thermoRaw))) ? float4(293.15f, 0.0f, 0.0f, 0.0f) : thermoRaw;
        const float4 rad    = (any(isnan(radRaw)) || any(isinf(radRaw))) ? float4(0.0f, 0.0f, 0.0f, 1.0f) : radRaw;

        const float tempK = thermo.x;
        const float rawSoot = max(thermo.z, 0.0f) * edgeFade;
        const float burn  = max(thermo.w, 0.0f) * edgeFade;

        if (tempK < 1180.0f && rawSoot < 0.004f) {
            continue;
        }

        // 1. Planck blackbody turbulent flame-tongue core emission (cutoff 1180K, crisp turbulent edge erosion, zero sky fringe)
        float3 flameEmit = float3(0.0f, 0.0f, 0.0f);
        if (tempK >= 1180.0f) {
            const float tongueNoise = fireCompTongueDetail(pAbs, camPosW.w);
            const float coreHeat = smoothstep(1180.0f, 1780.0f, tempK);
            const float tongueAlpha = lerp(smoothstep(0.30f, 0.72f, tongueNoise + burn * 0.55f), 1.0f, smoothstep(1900.0f, 2450.0f, tempK));
            const float skyGuard = isSkyPixel
                ? (smoothstep(1340.0f, 1780.0f, tempK) * (1.0f - smoothstep(650.0f, 1150.0f, t)))
                : 1.0f;
            const float flameWeight = edgeFade * coreHeat * tongueAlpha * skyGuard * (0.48f + 0.52f * saturate(burn * 1.6f + coreHeat));
            flameEmit = evalPlanckFlameRGB(tempK) * flameWeight;
        }

        // 2. Beer-Lambert dark soot extinction + directional sun self-shadowing + Beer-Powder multi-scattering.
        // Indoors: attenuate cool soot away from active hot combustion AND strictly clamp transmittance >= 0.78f.
        const float radLuma = dot(max(rad.rgb, 0.0f.xxx), float3(0.333f, 0.333f, 0.333f));
        const float indoorHotGate = saturate(smoothstep(860.0f, 1380.0f, tempK) + burn * 1.25f + smoothstep(0.12f, 0.65f, radLuma) * 0.65f);
        const float soot = rawSoot * lerp(1.0f, 0.22f * indoorHotGate, isInt);

        const float sigmaSoot = soot * lerp(0.058f, 0.036f, isInt);
        const float stepOpt = sigmaSoot * ds;
        const float stepTrans = exp(-stepOpt);
        const float nextTrans = max(trans * stepTrans, minTransFloor);
        const float effSootAlpha = max(trans - nextTrans, 0.0f);
        accumSootOpt += stepOpt;

        const float sunShadowT = saturate(rad.a);
        // Beer-Powder multi-scattering (1 - exp(-2 * opticalDepth)): luminous sunlit upper curls, deep dark shadowed undersides
        const float beerPowder = 1.0f - exp(-2.0f * (accumSootOpt + soot * 0.85f));
        const float sunDirect  = pow(max(sunShadowT, 1.0e-5f), 2.05f) * (0.42f + 2.05f * beerPowder);
        const float sunMulti   = pow(max(sunShadowT, 1.0e-5f), 1.65f) * 0.22f * beerPowder;
        const float3 sunLight  = sunKeyCol * (sunDirect + sunMulti) + ambSkyFill * (0.20f + 0.80f * sunShadowT);

        // Glowing burnt-orange smoke undersides from internal 3D Radiance Cascade transitioning to sunlit/charcoal tops
        const float3 internalBounce = max(rad.rgb, 0.0f.xxx) * float3(1.18f, 0.56f, 0.18f) * (0.38f * edgeFade);
        const float3 sootScatter = sootAlbedo * sunLight + internalBounce * smoothstep(0.01f, 0.22f, soot);

        const float3 stepRgb = trans * (flameEmit * (ds * 0.062f)) + sootScatter * effSootAlpha;
        if (!any(isnan(stepRgb)) && !any(isinf(stepRgb))) {
            accumRgb += max(stepRgb, 0.0f.xxx);
        }
        trans = nextTrans;

        if (isInt < 0.5f && trans < 0.015f) {
            break;
        }
    }

    trans = clamp((isnan(trans) || isinf(trans)) ? 1.0f : trans, minTransFloor, 1.0f);
    accumRgb = (any(isnan(accumRgb)) || any(isinf(accumRgb))) ? 0.0f.xxx : clamp(accumRgb, 0.0f.xxx, 64.0f.xxx);

    if (trans < 0.9995f || any(accumRgb > 1.0e-4f)) {
        const float4 bgRaw = ColorOut[dtid.xy];
        const float3 bgRgb = (any(isnan(bgRaw.rgb)) || any(isinf(bgRaw.rgb))) ? 0.0f.xxx : max(bgRaw.rgb, 0.0f.xxx);
        const float3 outRgb = max(bgRgb * trans + accumRgb, 0.0f.xxx);
        ColorOut[dtid.xy] = float4(outRgb, bgRaw.a);
    }
}
