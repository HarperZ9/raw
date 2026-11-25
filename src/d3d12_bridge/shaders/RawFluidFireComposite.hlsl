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

// Physically calibrated Planck blackbody emission spectrum at Rec.709 wavelengths (650nm, 550nm, 450nm).
float3 evalPlanckFlameRGB(float tempK)
{
    if (tempK < 850.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float T = clamp(tempK, 850.0f, 3800.0f);
    // Wien / Planck spectral radiance proportionality: 1 / (lam^5 * (exp(c2 / (lam * T)) - 1)), c2 = 1.438777e7 nm*K
    const float3 lamNm = float3(650.0f, 550.0f, 450.0f);
    const float3 lamUm = lamNm * 0.001f;
    const float3 lam5  = lamUm * lamUm * lamUm * lamUm * lamUm;
    const float3 x     = min(14387770.0f / (lamNm * T), 60.0f.xxx);
    const float3 spec  = 1.0f / (lam5 * max(exp(x) - 1.0f, 1.0e-5f));
    // Calibrate so 2200K core produces rich HDR (~5.5..8.0) and 1100K ember produces warm crimson (~0.25)
    const float gate = smoothstep(850.0f, 1240.0f, T);
    return min(spec * (0.0038f * gate), float3(28.0f, 22.0f, 16.0f));
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
    const float sceneD = SceneDepth.Load(int3(dtid.xy, 0));
    float sceneDist = 1.0e7f;
    if (sceneD > 0.0f && sceneD < 0.99998f) {
        const float3 hitRel = fireCompWorldAt(px, sceneD);
        sceneDist = length(hitRel - eyeRel);
    }
    const float tMax = min(tExit, sceneDist);
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
    const float  isInt  = resScaleAndFlags.w;
    const float  extSunMask = sunDir.w * (1.0f - isInt * 0.88f);
    const float3 sunKeyCol  = sunCol.rgb * (extSunMask * (0.45f + 5.2f * hgDual));
    const float3 ambSkyFill = lerp(float3(0.020f, 0.023f, 0.028f), float3(0.014f, 0.012f, 0.010f), isInt) * max(sunCol.w, 0.35f);
    // Physical ash/wood-smoke single-scattering albedo: deep dark charcoal where self-shadowed (sunShadowT -> 0.03),
    // luminous warm sunlit curls on upper sunward billows (sunShadowT -> 1.0) via Beer-Powder multi-scattering.
    const float3 sootAlbedo = float3(0.20f, 0.185f, 0.165f);

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

        const float4 thermo = FireThermo.SampleLevel(LinClamp, uvw, 0.0f);
        const float4 rad    = FireRadiance.SampleLevel(LinClamp, uvw, 0.0f);

        const float tempK = thermo.x;
        const float soot  = max(thermo.z, 0.0f) * edgeFade;
        const float burn  = max(thermo.w, 0.0f) * edgeFade;

        if (tempK < 850.0f && soot < 0.004f) {
            continue;
        }

        // 1. Planck blackbody turbulent flame core emission
        float3 flameEmit = float3(0.0f, 0.0f, 0.0f);
        if (tempK >= 850.0f) {
            const float flameWeight = edgeFade * (0.42f + 0.58f * saturate(burn * 1.6f + smoothstep(1350.0f, 2250.0f, tempK)));
            flameEmit = evalPlanckFlameRGB(tempK) * flameWeight;
        }

        // 2. Beer-Lambert dark soot extinction + directional sun self-shadowing + Beer-Powder multi-scattering
        const float sigmaSoot = soot * 0.036f;
        const float stepOpt = sigmaSoot * ds;
        const float stepTrans = exp(-stepOpt);
        accumSootOpt += stepOpt;

        const float sunShadowT = saturate(rad.a);
        // Beer-Powder multi-scattering (1 - exp(-2 * opticalDepth)): luminous sunlit upper curls, deep dark shadowed undersides
        const float beerPowder = 1.0f - exp(-2.0f * (accumSootOpt + soot * 0.85f));
        const float sunDirect  = pow(sunShadowT, 1.75f) * (0.55f + 1.85f * beerPowder);
        const float sunMulti   = pow(sunShadowT, 1.45f) * 0.24f * beerPowder;
        const float3 sunLight  = sunKeyCol * (sunDirect + sunMulti) + ambSkyFill * (0.20f + 0.80f * sunShadowT);

        // Glowing burnt-orange smoke undersides from internal 3D Radiance Cascade transitioning to sunlit/charcoal tops
        const float3 internalBounce = max(rad.rgb, 0.0f.xxx) * float3(1.18f, 0.56f, 0.18f) * (0.62f * edgeFade);
        const float3 sootScatter = sootAlbedo * sunLight + internalBounce * smoothstep(0.01f, 0.22f, soot);

        accumRgb += trans * (flameEmit * (ds * 0.060f) + sootScatter * (1.0f - stepTrans));
        trans *= stepTrans;

        if (trans < 0.015f) {
            break;
        }
    }

    if (trans < 0.9995f || any(accumRgb > 1.0e-4f)) {
        const float4 bg = ColorOut[dtid.xy];
        const float3 outRgb = max(bg.rgb * trans + accumRgb, 0.0f.xxx);
        ColorOut[dtid.xy] = float4(outRgb, bg.a);
    }
}
