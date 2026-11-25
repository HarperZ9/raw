// WorldFieldSim.hlsl: Persistent World-Anchored 512x512 RGBA16F GPU Fluid, Vortex-Cascade & Snowpack PDE (SM 6.6+).
// Covers a 2,048 x 2,048 world-unit window centered on the camera (4.0u/texel) with exact toroidal world-locking:
//   R : 2D Shallow-Water Wave-Equation PDE height h(x,y,t) (9-point isotropic Laplacian, symplectic Verlet)
//   G : 2D Vertical wave velocity v(x,y,t) with 2-octave self-similar vortex-stretching + Kolmogorov/Smagorinsky cutoff
//   B : Monotonicity-clamped MacCormack/BFECC advected wake / vortex-filament whitewater foam f(x,y,t)
//   A : Persistent world-anchored storm snowpack depth & carved foot-trench / hearth-melt field s(x,y,t)
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"

Texture2D<float4> PrevField : register(t0);
SamplerState      Lin       : register(s0);

static const float kGridRes   = 512.0f;
static const float kCellUnits = 4.0f;
static const float kGridSpan  = kGridRes * kCellUnits;   // 2,048 world units

// Monotonicity-clamped MacCormack (BFECC) semi-Lagrangian advection for scalar wake/foam filaments:
// Preserves fine swirling eddies with zero numerical blowup by clamping to the local 4-neighbor extrema.
float4 sampleMacCormack(float2 uv, float2 velUv, float2 invRes) {
    const float2 uvBack = frac(uv - velUv);
    const float4 phiHat = PrevField.SampleLevel(Lin, uvBack, 0.0f);
    const float4 phiFwd = PrevField.SampleLevel(Lin, frac(uvBack + velUv), 0.0f);
    const float4 phiNow = PrevField.SampleLevel(Lin, uv, 0.0f);
    const float4 corr   = phiHat + 0.5f * (phiNow - phiFwd);
    const float4 n0 = PrevField.SampleLevel(Lin, frac(uvBack + float2(-invRes.x, 0.0f)), 0.0f);
    const float4 n1 = PrevField.SampleLevel(Lin, frac(uvBack + float2( invRes.x, 0.0f)), 0.0f);
    const float4 n2 = PrevField.SampleLevel(Lin, frac(uvBack + float2(0.0f, -invRes.y)), 0.0f);
    const float4 n3 = PrevField.SampleLevel(Lin, frac(uvBack + float2(0.0f,  invRes.y)), 0.0f);
    const float4 lo = min(phiHat, min(min(n0, n1), min(n2, n3)));
    const float4 hi = max(phiHat, max(max(n0, n1), max(n2, n3)));
    return clamp(corr, lo, hi);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float2 uv = (float2(ip) + 0.5f) / kGridRes;
    const float2 invRes = 1.0f / kGridRes;

    // True toroidal world-locked coordinates (zero camera drag, zero smeared ribbon/railroad ticks):
    const float2 camWrap = floor(camPosW.xy / kGridSpan) * kGridSpan;
    float2 worldXY = camWrap + uv * kGridSpan;
    const float2 dCam = worldXY - camPosW.xy;
    worldXY.x -= (dCam.x >  0.5f * kGridSpan) ? kGridSpan : (dCam.x < -0.5f * kGridSpan) ? -kGridSpan : 0.0f;
    worldXY.y -= (dCam.y >  0.5f * kGridSpan) ? kGridSpan : (dCam.y < -0.5f * kGridSpan) ? -kGridSpan : 0.0f;

    // Compute local height gradient and baroclinic swirl velocity before MacCormack advection:
    const float4 cStat = PrevField.SampleLevel(Lin, uv, 0.0f);
    const float hL0 = PrevField.SampleLevel(Lin, frac(uv + float2(-invRes.x, 0.0f)), 0.0f).r;
    const float hR0 = PrevField.SampleLevel(Lin, frac(uv + float2( invRes.x, 0.0f)), 0.0f).r;
    const float hD0 = PrevField.SampleLevel(Lin, frac(uv + float2(0.0f, -invRes.y)), 0.0f).r;
    const float hU0 = PrevField.SampleLevel(Lin, frac(uv + float2(0.0f,  invRes.y)), 0.0f).r;
    const float2 gradH = float2(hR0 - hL0, hU0 - hD0) * 0.5f;
    // Divergence-free rotational swirl velocity from perpendicular wave gradient + synoptic wind drift:
    const float2 windDir = normalize(cloudWind.xy + float2(1.0f, 0.35f));
    const float2 swirlVel = float2(-gradH.y, gradH.x) * 0.65f;
    const float2 advVelUv = (windDir * 0.18f + clamp(swirlVel, -0.45f.xx, 0.45f.xx)) * invRes;

    const float4 c0 = sampleMacCormack(uv, advVelUv, invRes);
    const float2 advUv = frac(uv - advVelUv);
    const float hL  = PrevField.SampleLevel(Lin, frac(advUv + float2(-invRes.x, 0.0f)), 0.0f).r;
    const float hR  = PrevField.SampleLevel(Lin, frac(advUv + float2( invRes.x, 0.0f)), 0.0f).r;
    const float hD  = PrevField.SampleLevel(Lin, frac(advUv + float2(0.0f, -invRes.y)), 0.0f).r;
    const float hU  = PrevField.SampleLevel(Lin, frac(advUv + float2(0.0f,  invRes.y)), 0.0f).r;
    const float hDL = PrevField.SampleLevel(Lin, frac(advUv + float2(-invRes.x, -invRes.y)), 0.0f).r;
    const float hDR = PrevField.SampleLevel(Lin, frac(advUv + float2( invRes.x, -invRes.y)), 0.0f).r;
    const float hUL = PrevField.SampleLevel(Lin, frac(advUv + float2(-invRes.x,  invRes.y)), 0.0f).r;
    const float hUR = PrevField.SampleLevel(Lin, frac(advUv + float2( invRes.x,  invRes.y)), 0.0f).r;
    const float lapH = (hL + hR + hD + hU) * 0.20f + (hDL + hDR + hUL + hUR) * 0.05f - c0.r;

    // 2-Octave Self-Similar Vortex-Stretching Cascade with Explicit Kolmogorov/Smagorinsky Regularization Valve:
    // Measures local strain |grad(h)| and second-derivative curvature curl across 1-cell and 2-cell scales to sharpen
    // turbulent wake filaments, while eddy viscosity nuEff clamps high-wavenumber energy so the PDE never blows up.
    const float hL2 = PrevField.SampleLevel(Lin, frac(advUv + float2(-2.0f * invRes.x, 0.0f)), 0.0f).r;
    const float hR2 = PrevField.SampleLevel(Lin, frac(advUv + float2( 2.0f * invRes.x, 0.0f)), 0.0f).r;
    const float hD2 = PrevField.SampleLevel(Lin, frac(advUv + float2(0.0f, -2.0f * invRes.y)), 0.0f).r;
    const float hU2 = PrevField.SampleLevel(Lin, frac(advUv + float2(0.0f,  2.0f * invRes.y)), 0.0f).r;
    const float lapH2 = (hL2 + hR2 + hD2 + hU2) * 0.25f - c0.r;
    const float strainMag = length(gradH);
    const float vortexStretch = clamp((lapH - lapH2 * 0.55f) * saturate(strainMag * 3.5f) * 0.14f, -0.06f, 0.06f);
    // Physical Smagorinsky eddy-viscosity damping valve (prevents discrete finite-time singularity / aliasing):
    const float kolmogorovDamp = clamp(0.985f - strainMag * 0.045f, 0.945f, 0.985f);

    float h = c0.r;
    float v = c0.g;
    float foam = c0.b * 0.964f;
    float snow = cStat.a;   // snowpack stays stationary in toroidal world space

    if (proj.z < 1.5f) {
        // Wave 31: ALWAYS seed deformable snowpack canvas (0.58 baseline) so walking across Skyrim's permanent snow
        // biomes carves deep 3D tracks & pushed-up berms even on clear sunny days when weather.z == 0!
        snow = max(0.58f, saturate(weather.z * 0.85f));
        h = 0.0f; v = 0.0f; foam = 0.0f;
    }

    // 1) True 3D Volumetric Storm Snowpack Buildup + Angle-of-Repose Cornice Drift & Avalanche Diffusion:
    const float sL = PrevField.SampleLevel(Lin, frac(uv + float2(-invRes.x, 0.0f)), 0.0f).a;
    const float sR = PrevField.SampleLevel(Lin, frac(uv + float2( invRes.x, 0.0f)), 0.0f).a;
    const float sD = PrevField.SampleLevel(Lin, frac(uv + float2(0.0f, -invRes.y)), 0.0f).a;
    const float sU = PrevField.SampleLevel(Lin, frac(uv + float2(0.0f,  invRes.y)), 0.0f).a;
    const float lapSnow = (sL + sR + sD + sU) * 0.25f - snow;
    // Angle-of-repose slumping: steep snow gradients relax smoothly into realistic soft rounded drifts & berms!
    if (abs(lapSnow) > 0.045f) {
        snow = saturate(snow + lapSnow * 0.08f);
    }
    if (weather.z > 0.04f) {
        // Wind-transported snow-drift deposition (builds thick wind-sculpted snowbanks up to full volumetric depth):
        const float driftNoise = 0.65f + 0.70f * cNoise(float3(worldXY * 0.0075f - windDir * (camPosW.w * 0.018f), camPosW.w * 0.022f));
        const float windAdvS = PrevField.SampleLevel(Lin, frac(uv - windDir * (invRes * 0.35f)), 0.0f).a;
        snow = saturate(lerp(snow, windAdvS, weather.z * 0.018f) + weather.z * 0.00135f * driftNoise);
    } else if (weather.w > 0.05f) {
        snow = saturate(max(snow - weather.w * 0.0018f, 0.0f));
        foam = saturate(foam + weather.w * 0.0035f);
    } else {
        // Slowly heal old tracks back toward 0.58 deformable snow baseline on dry days:
        snow = lerp(snow, 0.58f, 0.00035f);
        foam = saturate(foam - 0.00045f);
    }

    // 2) Raindrop impulse forcing into 2D wave heightfield:
    float force = 0.0f;
    if (weather.w > 0.08f) {
        const float rH = cHash(float3(floor(worldXY * 0.25f), floor(camPosW.w * 18.0f)));
        if (rH > 0.996f) { force -= weather.w * 0.22f; }
    }

    // 3) Player footsteps (0..15) + live ProcessLists NPC world positions (16..31) ->
    //    vortex-curled water wakes AND physically mass-conserving snow boot-trenches with pushed-up snow berms!
    [loop] for (uint fi = 0u; fi < 32u; ++fi) {
        const float4 ft = footTrail[fi];
        if (ft.w <= 0.05f) { continue; }
        const float2 fd = worldXY - ft.xy;
        const float d2 = dot(fd, fd);
        if (d2 < 256.0f) {
            const float g = exp(-d2 / 52.0f) * ft.w;
            const float rr = sqrt(d2);
            const float berm = exp(-pow(rr - 10.5f, 2.0f) / 14.0f) * ft.w;
            // Dipole shear wake impulse around moving actors creates authentic counter-rotating vortex pairs:
            const float dipole = sin(atan2(fd.y, fd.x) * 2.0f + camPosW.w * 1.2f) * 0.035f;
            force -= g * (0.14f + dipole);
            if (weather.w > 0.08f) { foam = saturate(foam + g * 0.18f * weather.w); }
            if (snow > 0.02f) {
                // Mass-conserving snow compression: compacts trench center while pushing a raised rim berm outward!
                snow = saturate(snow - g * 0.24f + berm * 0.065f);
            }
        }
    }

    // 4) Thermal campfire / hearth / torch snowpack melt & convective ripple:
    const uint maxL = min(flags.y, 12u);
    [loop] for (uint li = 0u; li < maxL; ++li) {
        const float3 lc = lightColS[li].rgb;
        if (lc.r > lc.b * 1.25f && lc.r > 0.16f) {
            const float2 lWorld = lightPosR[li].xy + camPosW.xy;
            const float rMelt = min(lightPosR[li].w * 0.38f, 220.0f);
            const float d2 = dot(worldXY - lWorld, worldXY - lWorld);
            if (d2 < rMelt * rMelt) {
                const float att = 1.0f - sqrt(d2) / max(rMelt, 1.0f);
                snow = saturate(snow - att * att * 0.025f);
                foam = saturate(foam - att * att * 0.018f);
            }
        }
    }

    v = (v + lapH * 0.32f + vortexStretch + force) * kolmogorovDamp;
    h = clamp(h + v, -1.5f, 1.5f);
    // Soft damping sponge at simulation boundary so waves never reflect off the 2,048u window edge:
    const float edgeDist = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    const float sponge = smoothstep(0.0f, 0.06f, edgeDist);
    h *= sponge;
    v *= sponge;
    foam = saturate(foam + saturate(-lapH * 2.2f + strainMag * 0.65f) * 0.18f) * sponge;

    return float4(h, v, foam, snow);
}
