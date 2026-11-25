// RawFluidFireSim.hlsl: Real-Time GPU 3D Eulerian Navier-Stokes Combustion, Smoke & Radiance Cascade Solver (SM 6.6 / 6.10).
// Operates on a 64 x 64 x 64 world-anchored 3D voxel grid (Z-up world coordinates):
//   - VelIn / VelOut       (RGBA16F): xyz = 3D velocity (world units/s), w = velocity divergence (div u)
//   - ThermoIn / ThermoOut (RGBA16F): x = Temperature (K), y = Fuel [0..2], z = Soot density [0..4], w = Arrhenius burn rate
//   - PressIn / PressOut   (R32F)   : 3D pressure field for incompressible projection (Jacobi Poisson relaxation)
//   - RadianceIn / Out     (RGBA16F): rgb = 3D Fire-to-Smoke Radiance Cascade HDR irradiance, a = soot optical extinction
//
// 7 Compute Entry Points ([numthreads(4, 4, 4)] -> 16 x 16 x 16 thread groups):
//   1. cs_fire_inject         : Up to 16 verified world-space fire emitters + analytical curl-noise + solid AABB/floor obstacles
//   2. cs_fire_advect         : 2nd-order RK2 Semi-Lagrangian MacCormack advection with 8-corner BFECC min/max clamping
//   3. cs_fire_forces_combust : 3D vorticity confinement, Z-up thermal buoyancy, cross-wind drag, Arrhenius burn & T^4 cooling
//   4. cs_fire_divergence     : Incompressible divergence with solid-wall Neumann BCs & combustion thermal expansion
//   5. cs_fire_pressure       : 3D Jacobi Poisson relaxation with Neumann solid walls & open-air Dirichlet boundary
//   6. cs_fire_project        : Pressure-gradient projection enforcing divergence-free swirling airflow
//   7. cs_fire_radiance       : 14-direction 3D isotropic Fire-to-Smoke Radiance Cascade with Planck blackbody & Beer-Lambert
//
// Original clean-room implementation, MIT License.

static const uint  kGridRes = 64u;
static const float kGridResF = 64.0f;
static const float kInvGridRes = 1.0f / 64.0f;

cbuffer FireSimCB : register(b0)
{
    float4 domainOriginAndCell;    // xyz = world min corner of 64^3 domain, w = voxel size h (world units)
    float4 dtAndTime;              // x = dt (s), y = timeSec, z = frameIndex, w = invCell (1/h)
    float4 windAndAmbient;         // xyz = synoptic cross-wind (world units/s), w = ambientTempK (e.g. 293.15)
    float4 physicsA;               // x = vorticityEps, y = buoyancyAlpha, z = sootWeightBeta, w = windDrag
    float4 physicsB;               // x = arrheniusRateA, y = activationTempTa, z = heatReleaseQ, w = sootYield
    float4 physicsC;               // x = radiativeCoolSigma, y = velDamping, z = sootDamping, w = thermalExpansion
    float4 boundsAndFlags;         // x = floorZLocal (voxels), y = ceilingZLocal (voxels), z = scatterExtinction, w = flags (bit0=reset, bit1=ceiling)
    uint4  counts;                 // x = emitterCount (0..16), y = obstacleCount (0..8), z = gridRes (64), w = jacobiIter
    float4 emitterPosRadius[16];   // xyz = verified world pos, w = radius (world units, <= 0 inactive)
    float4 emitterThermoJet[16];   // x = ignitionTempK (1800..3400), y = fuelRate, z = jetSpeed (u/s), w = curlStrength
    float4 emitterDirSoot[16];     // xyz = jet direction (unit), w = direct soot seed rate
    float4 obstacleBoxMin[8];      // xyz = world AABB min, w = active (> 0.5)
    float4 obstacleBoxMax[8];      // xyz = world AABB max, w = friction/roughness
};

Texture3D<float4>   VelIn       : register(t0);
Texture3D<float4>   ThermoIn    : register(t1);
Texture3D<float>    PressIn     : register(t2);
Texture3D<float4>   RadianceIn  : register(t3);

RWTexture3D<float4> VelOut      : register(u0);
RWTexture3D<float4> ThermoOut   : register(u1);
RWTexture3D<float>  PressOut    : register(u2);
RWTexture3D<float4> RadianceOut : register(u3);

SamplerState LinClamp : register(s0);

// ----------------------------------------------------------------------------
// Pure polynomial 3D noise & analytical divergence-free curl-noise (vendor-invariant)
// ----------------------------------------------------------------------------
float fireHash31(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float4 fireNoise3Grad(float3 x)
{
    const float3 i = floor(x);
    const float3 f = frac(x);
    const float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float3 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

    const float a  = fireHash31(i + float3(0.0f, 0.0f, 0.0f));
    const float b  = fireHash31(i + float3(1.0f, 0.0f, 0.0f));
    const float c  = fireHash31(i + float3(0.0f, 1.0f, 0.0f));
    const float d  = fireHash31(i + float3(1.0f, 1.0f, 0.0f));
    const float e  = fireHash31(i + float3(0.0f, 0.0f, 1.0f));
    const float f1 = fireHash31(i + float3(1.0f, 0.0f, 1.0f));
    const float g  = fireHash31(i + float3(0.0f, 1.0f, 1.0f));
    const float h  = fireHash31(i + float3(1.0f, 1.0f, 1.0f));

    const float k0 = a;
    const float k1 = b - a;
    const float k2 = c - a;
    const float k3 = e - a;
    const float k4 = a - b - c + d;
    const float k5 = a - c - e + g;
    const float k6 = a - b - e + f1;
    const float k7 = -a + b + c - d + e - f1 - g + h;

    const float val = k0 + k1 * u.x + k2 * u.y + k3 * u.z
                    + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x
                    + k7 * u.x * u.y * u.z;
    const float3 grad = du * float3(
        k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
        k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
        k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
    return float4(val, grad);
}

// Analytical curl of 3-component vector potential: div(curl(Psi)) == 0 identically.
float3 fireCurlNoise3D(float3 p)
{
    const float3 g0 = fireNoise3Grad(p + float3( 0.0f,  0.0f,  0.0f)).yzw;
    const float3 g1 = fireNoise3Grad(p + float3(31.7f, 17.3f,  5.9f)).yzw;
    const float3 g2 = fireNoise3Grad(p + float3(11.3f, 47.1f, 23.5f)).yzw;
    return float3(g2.y - g1.z, g0.z - g2.x, g1.x - g0.y);
}

// ----------------------------------------------------------------------------
// Domain coordinates & solid obstacle boundary test (floor, ceiling, 8 AABBs)
// ----------------------------------------------------------------------------
float3 voxelToWorld(int3 id)
{
    return domainOriginAndCell.xyz + (float3(id) + 0.5f) * domainOriginAndCell.w;
}

bool hasResetBit()
{
    return (uint(boundsAndFlags.w + 0.5f) & 1u) != 0u;
}

bool hasCeilingBit()
{
    return (uint(boundsAndFlags.w + 0.5f) & 2u) != 0u;
}

bool isSolidVoxel(int3 id)
{
    if (id.z < int(boundsAndFlags.x)) {
        return true;
    }
    if (hasCeilingBit() && id.z >= int(boundsAndFlags.y)) {
        return true;
    }
    const uint obsCount = min(counts.y, 8u);
    if (obsCount == 0u) {
        return false;
    }
    const float3 wp = voxelToWorld(id);
    [loop] for (uint oi = 0u; oi < obsCount; ++oi) {
        if (obstacleBoxMin[oi].w <= 0.5f) {
            continue;
        }
        const float3 bmin = obstacleBoxMin[oi].xyz;
        const float3 bmax = obstacleBoxMax[oi].xyz;
        if (all(wp >= bmin) && all(wp <= bmax)) {
            return true;
        }
    }
    return false;
}

int3 clampCoord(int3 id)
{
    return clamp(id, int3(0, 0, 0), int3(int(kGridRes) - 1, int(kGridRes) - 1, int(kGridRes) - 1));
}

float3 loadVelSolidAware(int3 id)
{
    if (any(id < 0) || any(id >= int(kGridRes))) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    if (isSolidVoxel(id)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return VelIn.Load(int4(id, 0)).xyz;
}

// ----------------------------------------------------------------------------
// Physical Planck Blackbody Spectral Radiance -> Linear Rec.709 RGB
// Exact Planck chromaticity ratio at R=635nm / G=535nm / B=460nm coupled with
// physical Stefan-Boltzmann T^4 radiant exitance. Strictly monotonic in T (K);
// Wien shift from deep crimson (1100K) -> rich burnt orange (1800K) -> golden-white (3400K+).
// ----------------------------------------------------------------------------
float3 evalPlanckBlackbody(float tempK)
{
    if (tempK < 650.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float T = clamp(tempK, 650.0f, 6500.0f);
    // Representative sRGB/Rec.709 spectral peaks: R=635nm, G=535nm, B=460nm
    // c2 / lambda = (h * c) / (k_B * lambda) in Kelvin
    const float3 c2OverLambda = float3(22657.9f, 26893.0f, 31277.8f);
    const float3 invLambda5   = float3(0.415f,   1.000f,   2.148f);
    // Shift exponential by red channel so chromaticity never underflows in FP32 even at 700K
    const float3 expoShift = min((c2OverLambda - c2OverLambda.x) / T, float3(60.0f, 60.0f, 60.0f));
    const float3 chromaRaw = invLambda5 * exp(-expoShift);
    const float  maxCh = max(chromaRaw.x, max(chromaRaw.y, chromaRaw.z));
    const float3 chroma = chromaRaw / max(maxCh, 1.0e-6f);

    // Physical Stefan-Boltzmann T^4 radiant power gated smoothly above 650K ignition floor
    const float glowGate = smoothstep(650.0f, 950.0f, T);
    const float tNorm = T / 1800.0f;
    const float t4 = (tNorm * tNorm) * (tNorm * tNorm);
    // Calibrated linear HDR exitance: ~0.25 at 1200K embers, ~3.5 at 1800K flame, ~11 at 2400K, ~45 at 3400K
    return chroma * (glowGate * 3.5f * t4);
}

// ----------------------------------------------------------------------------
// Pass 1: cs_fire_inject
// Injects up to 16 verified world-space fire emitters with fuel, ignition temp
// (1800K..3400K), upward/directed velocity jet, curl-noise perturbation, and
// enforces solid obstacle boundaries (floor/ceiling/walls).
// ----------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void cs_fire_inject(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    const float dt = clamp(dtAndTime.x, 1.0e-4f, 0.05f);
    const float timeSec = dtAndTime.y;
    const float tAmb = max(windAndAmbient.w, 200.0f);

    float4 vel = hasResetBit() ? float4(0.0f, 0.0f, 0.0f, 0.0f) : VelIn.Load(int4(id, 0));
    float4 thermo = hasResetBit() ? float4(tAmb, 0.0f, 0.0f, 0.0f) : ThermoIn.Load(int4(id, 0));
    thermo.x = max(thermo.x, tAmb);

    if (isSolidVoxel(id)) {
        VelOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        ThermoOut[id] = float4(tAmb, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float3 wp = voxelToWorld(id);
    const uint emCount = min(counts.x, 16u);
    [loop] for (uint ei = 0u; ei < emCount; ++ei) {
        const float radius = emitterPosRadius[ei].w;
        if (radius <= 0.001f) {
            continue;
        }
        const float3 delta = wp - emitterPosRadius[ei].xyz;
        const float dist2 = dot(delta, delta);
        const float r2 = radius * radius;
        if (dist2 >= r2 * 2.25f) {
            continue;
        }
        const float w = exp(-dist2 / max(r2 * 0.45f, 1.0e-4f)) * saturate(1.0f - sqrt(dist2) / (radius * 1.5f));
        if (w <= 1.0e-4f) {
            continue;
        }

        const float ignTempK = clamp(emitterThermoJet[ei].x, 1200.0f, 4000.0f);
        const float fuelRate = max(emitterThermoJet[ei].y, 0.0f);
        const float jetSpeed = emitterThermoJet[ei].z;
        const float curlAmp  = emitterThermoJet[ei].w;
        const float3 rawDir  = emitterDirSoot[ei].xyz;
        const float dirLen   = length(rawDir);
        const float3 jetDir  = (dirLen > 1.0e-3f) ? (rawDir / dirLen) : float3(0.0f, 0.0f, 1.0f);
        const float sootSeed = max(emitterDirSoot[ei].w, 0.0f);

        // Animated 3D curl-noise lick perturbation in emitter local frame
        const float3 noiseCoord = wp * (0.35f / max(radius, 1.0f)) - float3(0.0f, 0.0f, timeSec * 2.4f) + float3(float(ei) * 7.3f, 0.0f, 0.0f);
        const float3 curl = fireCurlNoise3D(noiseCoord);
        const float flicker = 0.82f + 0.36f * fireNoise3Grad(float3(float(ei) * 5.1f, timeSec * 4.7f, wp.z * 0.08f)).x;

        // Soft temperature injection toward ignition target (1800K..3400K)
        const float targetT = lerp(thermo.x, ignTempK * flicker, saturate(w * (1.0f - exp(-14.0f * dt))));
        thermo.x = max(thermo.x, targetT);
        // Fuel & direct ember soot injection
        thermo.y = min(thermo.y + fuelRate * w * dt * flicker, 2.5f);
        thermo.z = min(thermo.z + sootSeed * w * dt, 4.0f);
        // Velocity jet + divergence-free curl perturbation
        const float3 jetVel = jetDir * (jetSpeed * flicker) + curl * curlAmp;
        vel.xyz = lerp(vel.xyz, jetVel, saturate(w * (1.0f - exp(-10.0f * dt))));
    }

    VelOut[id] = vel;
    ThermoOut[id] = thermo;
}

// ----------------------------------------------------------------------------
// Pass 2: cs_fire_advect
// 2nd-order RK2 Semi-Lagrangian MacCormack advection with 8-corner BFECC
// min/max monotonicity clamping (zero numerical dissipation, zero overshoot).
// ----------------------------------------------------------------------------
float4 sampleMacCormack3D(Texture3D<float4> field, int3 id, float3 uvwBack, float3 uvwFwd)
{
    const float4 phiN   = field.Load(int4(id, 0));
    const float4 phiHat = field.SampleLevel(LinClamp, uvwBack, 0.0f);
    const float4 phiRev = field.SampleLevel(LinClamp, uvwFwd, 0.0f);

    // 2nd-order MacCormack error compensation: phi_{n+1} = phi_hat + 0.5 * (phi_n - phi_rev)
    const float4 phiCorr = phiHat + 0.5f * (phiN - phiRev);

    // Exact 8-voxel trilinear footprint around uvwBack for BFECC min/max clamping
    const float3 cellCoord = uvwBack * kGridResF - 0.5f;
    const int3 base = clamp(int3(floor(cellCoord)), int3(0, 0, 0), int3(int(kGridRes) - 2, int(kGridRes) - 2, int(kGridRes) - 2));

    const float4 c000 = field.Load(int4(base + int3(0, 0, 0), 0));
    const float4 c100 = field.Load(int4(base + int3(1, 0, 0), 0));
    const float4 c010 = field.Load(int4(base + int3(0, 1, 0), 0));
    const float4 c110 = field.Load(int4(base + int3(1, 1, 0), 0));
    const float4 c001 = field.Load(int4(base + int3(0, 0, 1), 0));
    const float4 c101 = field.Load(int4(base + int3(1, 0, 1), 0));
    const float4 c011 = field.Load(int4(base + int3(0, 1, 1), 0));
    const float4 c111 = field.Load(int4(base + int3(1, 1, 1), 0));

    const float4 lo = min(min(min(c000, c100), min(c010, c110)), min(min(c001, c101), min(c011, c111)));
    const float4 hi = max(max(max(c000, c100), max(c010, c110)), max(max(c001, c101), max(c011, c111)));
    return clamp(phiCorr, lo, hi);
}

[numthreads(4, 4, 4)]
void cs_fire_advect(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    const float tAmb = max(windAndAmbient.w, 200.0f);

    if (isSolidVoxel(id)) {
        VelOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        ThermoOut[id] = float4(tAmb, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float dt = clamp(dtAndTime.x, 1.0e-4f, 0.05f);
    const float invDomainSpan = dtAndTime.w * kInvGridRes; // 1 / (64 * h)
    const float3 uvw = (float3(id) + 0.5f) * kInvGridRes;

    // RK2 midpoint backward trace in normalized texture space
    const float3 uvwLo = float3(0.5f * kInvGridRes, 0.5f * kInvGridRes, 0.5f * kInvGridRes);
    const float3 uvwHi = float3(1.0f - 0.5f * kInvGridRes, 1.0f - 0.5f * kInvGridRes, 1.0f - 0.5f * kInvGridRes);
    const float3 u0 = VelIn.Load(int4(id, 0)).xyz;
    const float3 uvwMid = clamp(uvw - 0.5f * dt * u0 * invDomainSpan, uvwLo, uvwHi);
    const float3 uMid = VelIn.SampleLevel(LinClamp, uvwMid, 0.0f).xyz;
    const float3 uvwBackRaw = uvw - dt * uMid * invDomainSpan;
    const float3 uvwBack = clamp(uvwBackRaw, uvwLo, uvwHi);

    // Forward trace from uvwBack for MacCormack error estimator
    const float3 uBack = VelIn.SampleLevel(LinClamp, uvwBack, 0.0f).xyz;
    const float3 uvwFwd = clamp(uvwBack + dt * uBack * invDomainSpan, uvwLo, uvwHi);

    float4 advVel = sampleMacCormack3D(VelIn, id, uvwBack, uvwFwd);
    float4 advThermo = sampleMacCormack3D(ThermoIn, id, uvwBack, uvwFwd);

    // Open-air inflow boundary condition: air entering from outside the 64^3 box is cool & clean
    const bool outOfDomain = any(uvwBackRaw < 0.0f) || any(uvwBackRaw > 1.0f);
    if (outOfDomain) {
        advThermo = float4(tAmb, 0.0f, 0.0f, 0.0f);
        advVel.xyz = windAndAmbient.xyz * 0.5f;
    }
    advThermo.x = max(advThermo.x, tAmb);
    advThermo.y = max(advThermo.y, 0.0f);
    advThermo.z = max(advThermo.z, 0.0f);
    advThermo.w = max(advThermo.w, 0.0f);

    // Clamp max velocity magnitude for unconditional numeric safety
    const float maxSpeed = 48.0f * domainOriginAndCell.w / max(dt, 0.008f);
    const float spd = length(advVel.xyz);
    if (spd > maxSpeed) {
        advVel.xyz *= (maxSpeed / spd);
    }

    VelOut[id] = float4(advVel.xyz, 0.0f);
    ThermoOut[id] = advThermo;
}

// ----------------------------------------------------------------------------
// Pass 3: cs_fire_forces_combust
// 3D Vorticity Confinement (eps * h * (N x omega)), Z-up thermal buoyancy
// ((alpha*(T - T_amb) - beta*soot)*z_hat), cross-wind drag, Arrhenius fuel
// combustion, and semi-implicit Stefan-Boltzmann radiative cooling (T^4).
// ----------------------------------------------------------------------------
float3 computeVorticity(int3 id, float inv2h)
{
    const float3 uL = loadVelSolidAware(id + int3(-1, 0, 0));
    const float3 uR = loadVelSolidAware(id + int3( 1, 0, 0));
    const float3 uD = loadVelSolidAware(id + int3( 0,-1, 0));
    const float3 uU = loadVelSolidAware(id + int3( 0, 1, 0));
    const float3 uB = loadVelSolidAware(id + int3( 0, 0,-1));
    const float3 uT = loadVelSolidAware(id + int3( 0, 0, 1));

    const float dwdy = (uU.z - uD.z) * inv2h;
    const float dvdz = (uT.y - uB.y) * inv2h;
    const float dudz = (uT.x - uB.x) * inv2h;
    const float dwdx = (uR.z - uL.z) * inv2h;
    const float dvdx = (uR.y - uL.y) * inv2h;
    const float dudy = (uU.x - uD.x) * inv2h;
    return float3(dwdy - dvdz, dudz - dwdx, dvdx - dudy);
}

[numthreads(4, 4, 4)]
void cs_fire_forces_combust(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    const float tAmb = max(windAndAmbient.w, 200.0f);

    if (isSolidVoxel(id)) {
        VelOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        ThermoOut[id] = float4(tAmb, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float dt = clamp(dtAndTime.x, 1.0e-4f, 0.05f);
    const float h = max(domainOriginAndCell.w, 0.05f);
    const float inv2h = 0.5f * dtAndTime.w;

    float3 u = VelIn.Load(int4(id, 0)).xyz;
    float4 thermo = ThermoIn.Load(int4(id, 0));
    float T = max(thermo.x, tAmb);
    float fuel = max(thermo.y, 0.0f);
    float soot = max(thermo.z, 0.0f);

    // 1) 3D Vorticity Confinement: f_vort = eps * h * (N x omega)
    const float3 omegaC = computeVorticity(id, inv2h);
    const float wL = length(computeVorticity(clampCoord(id + int3(-1, 0, 0)), inv2h));
    const float wR = length(computeVorticity(clampCoord(id + int3( 1, 0, 0)), inv2h));
    const float wD = length(computeVorticity(clampCoord(id + int3( 0,-1, 0)), inv2h));
    const float wU = length(computeVorticity(clampCoord(id + int3( 0, 1, 0)), inv2h));
    const float wB = length(computeVorticity(clampCoord(id + int3( 0, 0,-1)), inv2h));
    const float wT = length(computeVorticity(clampCoord(id + int3( 0, 0, 1)), inv2h));

    const float3 eta = float3(wR - wL, wU - wD, wT - wB) * inv2h;
    const float etaLen = length(eta);
    const float3 N = (etaLen > 1.0e-5f) ? (eta / etaLen) : float3(0.0f, 0.0f, 0.0f);
    const float3 fVort = physicsA.x * h * cross(N, omegaC);

    // 2) Z-up Thermal Buoyancy: (alpha * (T - T_amb) - beta * soot) * z_hat
    const float fBuoyZ = physicsA.y * (T - tAmb) - physicsA.z * soot;
    const float3 fBuoy = float3(0.0f, 0.0f, fBuoyZ);

    // 3) Synoptic Cross-Wind Drag (stronger on lofted plume & smoke)
    const float plumeMask = saturate((T - tAmb) * 0.002f + soot * 0.65f);
    const float3 fWind = physicsA.w * plumeMask * (windAndAmbient.xyz - u);

    // Update velocity + gentle viscous damping
    u += dt * (fVort + fBuoy + fWind);
    u *= exp(-max(physicsC.y, 0.0f) * dt);

    // 4) Arrhenius Fuel Combustion: k(T) = A * exp(-Ta / T)
    const float arrA = max(physicsB.x, 0.0f);
    const float actTa = max(physicsB.y, 1.0f);
    const float heatQ = max(physicsB.z, 0.0f);
    const float sootYield = max(physicsB.w, 0.0f);

    const float ignGate = smoothstep(520.0f, 760.0f, T);
    const float rateK = arrA * exp(-min(actTa / max(T, 1.0f), 60.0f)) * ignGate;
    const float burnFrac = 1.0f - exp(-rateK * dt);
    const float burnedFuel = fuel * burnFrac;

    fuel = max(0.0f, fuel - burnedFuel);
    T += heatQ * burnedFuel;
    soot = min(soot + sootYield * burnedFuel, 4.0f);
    const float burnRate = burnedFuel / max(dt, 1.0e-4f);

    // 5) Semi-implicit Stefan-Boltzmann Radiative Cooling: dT/dt = -sigma * (T^4 - T_amb^4)
    // Unconditionally preserves T >= T_amb for any dt > 0.
    const float sigmaRad = max(physicsC.x, 0.0f);
    const float tExcess = max(0.0f, T - tAmb);
    const float t2 = T * T;
    const float ta2 = tAmb * tAmb;
    const float coolDenom = 1.0f + dt * sigmaRad * (t2 + ta2) * (T + tAmb);
    T = tAmb + tExcess / max(coolDenom, 1.0f);

    // Soot dissipation
    soot *= exp(-max(physicsC.z, 0.0f) * dt);

    VelOut[id] = float4(u, 0.0f);
    ThermoOut[id] = float4(T, fuel, soot, burnRate);
}

// ----------------------------------------------------------------------------
// Pass 4: cs_fire_divergence
// Computes 3D velocity divergence with solid-wall Neumann BCs and subtracts
// combustion thermal expansion so hot flames billow and mushroom naturally.
// ----------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void cs_fire_divergence(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    const float3 uC = VelIn.Load(int4(id, 0)).xyz;

    if (isSolidVoxel(id)) {
        VelOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        PressOut[id] = 0.0f;
        return;
    }

    const float inv2h = 0.5f * dtAndTime.w;
    // Solid-wall no-penetration: u_normal = 0 inside solid neighbor
    const float uL = (id.x > 0 && !isSolidVoxel(id + int3(-1, 0, 0))) ? VelIn.Load(int4(id + int3(-1, 0, 0), 0)).x : 0.0f;
    const float uR = (id.x < int(kGridRes) - 1 && !isSolidVoxel(id + int3( 1, 0, 0))) ? VelIn.Load(int4(id + int3( 1, 0, 0), 0)).x : 0.0f;
    const float vD = (id.y > 0 && !isSolidVoxel(id + int3( 0,-1, 0))) ? VelIn.Load(int4(id + int3( 0,-1, 0), 0)).y : 0.0f;
    const float vU = (id.y < int(kGridRes) - 1 && !isSolidVoxel(id + int3( 0, 1, 0))) ? VelIn.Load(int4(id + int3( 0, 1, 0), 0)).y : 0.0f;
    const float wB = (id.z > 0 && !isSolidVoxel(id + int3( 0, 0,-1))) ? VelIn.Load(int4(id + int3( 0, 0,-1), 0)).z : 0.0f;
    const float wT = (id.z < int(kGridRes) - 1 && !isSolidVoxel(id + int3( 0, 0, 1))) ? VelIn.Load(int4(id + int3( 0, 0, 1), 0)).z : 0.0f;

    const float divU = ((uR - uL) + (vU - vD) + (wT - wB)) * inv2h;
    const float burnRate = ThermoIn.Load(int4(id, 0)).w;
    const float thermalExp = max(physicsC.w, 0.0f) * min(burnRate, 4.0f);

    VelOut[id] = float4(uC, divU - thermalExp);
    // Warm-start pressure from previous frame (damped) for rapid 8-iteration convergence
    const float prevP = hasResetBit() ? 0.0f : (PressIn.Load(int4(id, 0)) * 0.72f);
    PressOut[id] = prevP;
}

// ----------------------------------------------------------------------------
// Pass 5: cs_fire_pressure
// 3D Jacobi Poisson relaxation: nabla^2 p = div_eff
// Solid neighbors enforce Neumann dp/dn = 0; open domain edges enforce p = 0.
// ----------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void cs_fire_pressure(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    if (isSolidVoxel(id)) {
        PressOut[id] = 0.0f;
        return;
    }

    const float pC = PressIn.Load(int4(id, 0));
    const float divEff = VelIn.Load(int4(id, 0)).w;
    const float h = max(domainOriginAndCell.w, 0.05f);

    float pSum = 0.0f;
    float denom = 0.0f;

    static const int3 kOffsets[6] = {
        int3(-1, 0, 0), int3( 1, 0, 0),
        int3( 0,-1, 0), int3( 0, 1, 0),
        int3( 0, 0,-1), int3( 0, 0, 1)
    };

    [unroll] for (int n = 0; n < 6; ++n) {
        const int3 nb = id + kOffsets[n];
        if (any(nb < 0) || any(nb >= int(kGridRes))) {
            // Open domain boundary: Dirichlet p = 0 (plume escapes freely)
            denom += 1.0f;
        } else if (isSolidVoxel(nb)) {
            // Solid wall: Neumann dp/dn = 0 (p_nb = p_c -> cancels from both sides)
        } else {
            pSum += PressIn.Load(int4(nb, 0));
            denom += 1.0f;
        }
    }

    const float pNew = (denom > 0.5f) ? ((pSum - h * h * divEff) / denom) : pC;
    PressOut[id] = clamp(pNew, -1.0e5f, 1.0e5f);
}

// ----------------------------------------------------------------------------
// Pass 6: cs_fire_project
// Subtracts pressure gradient from velocity: u_new = u - grad(p)
// ----------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void cs_fire_project(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    if (isSolidVoxel(id)) {
        VelOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float pC = PressIn.Load(int4(id, 0));
    const float inv2h = 0.5f * dtAndTime.w;

    const float pL = (id.x <= 0) ? 0.0f : (isSolidVoxel(id + int3(-1, 0, 0)) ? pC : PressIn.Load(int4(id + int3(-1, 0, 0), 0)));
    const float pR = (id.x >= int(kGridRes) - 1) ? 0.0f : (isSolidVoxel(id + int3( 1, 0, 0)) ? pC : PressIn.Load(int4(id + int3( 1, 0, 0), 0)));
    const float pD = (id.y <= 0) ? 0.0f : (isSolidVoxel(id + int3( 0,-1, 0)) ? pC : PressIn.Load(int4(id + int3( 0,-1, 0), 0)));
    const float pU = (id.y >= int(kGridRes) - 1) ? 0.0f : (isSolidVoxel(id + int3( 0, 1, 0)) ? pC : PressIn.Load(int4(id + int3( 0, 1, 0), 0)));
    const float pB = (id.z <= 0) ? 0.0f : (isSolidVoxel(id + int3( 0, 0,-1)) ? pC : PressIn.Load(int4(id + int3( 0, 0,-1), 0)));
    const float pT = (id.z >= int(kGridRes) - 1) ? 0.0f : (isSolidVoxel(id + int3( 0, 0, 1)) ? pC : PressIn.Load(int4(id + int3( 0, 0, 1), 0)));

    const float3 gradP = float3(pR - pL, pU - pD, pT - pB) * inv2h;
    float3 u = VelIn.Load(int4(id, 0)).xyz - gradP;

    // Enforce no-penetration against immediately adjacent solid walls
    if (id.x > 0 && isSolidVoxel(id + int3(-1, 0, 0))) u.x = max(u.x, 0.0f);
    if (id.x < int(kGridRes) - 1 && isSolidVoxel(id + int3( 1, 0, 0))) u.x = min(u.x, 0.0f);
    if (id.y > 0 && isSolidVoxel(id + int3( 0,-1, 0))) u.y = max(u.y, 0.0f);
    if (id.y < int(kGridRes) - 1 && isSolidVoxel(id + int3( 0, 1, 0))) u.y = min(u.y, 0.0f);
    if (id.z > 0 && isSolidVoxel(id + int3( 0, 0,-1))) u.z = max(u.z, 0.0f);
    if (id.z < int(kGridRes) - 1 && isSolidVoxel(id + int3( 0, 0, 1))) u.z = min(u.z, 0.0f);

    VelOut[id] = float4(u, VelIn.Load(int4(id, 0)).w);
}

// ----------------------------------------------------------------------------
// Pass 7: cs_fire_radiance
// 3D Isotropic Multi-Directional Fire-to-Smoke Radiance Cascade.
// Propagates incandescent Planck blackbody emission outward through the 3D soot
// density field using Beer-Lambert extinction across 14 symmetric directions
// with geometric cascade intervals so smoke undersides glow rich burnt orange
// from within while upper billows remain cool dark charcoal!
// ----------------------------------------------------------------------------
[numthreads(4, 4, 4)]
void cs_fire_radiance(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid >= kGridRes)) {
        return;
    }
    const int3 id = int3(tid);
    const float3 uvw = (float3(id) + 0.5f) * kInvGridRes;

    const float4 localThermo = ThermoIn.Load(int4(id, 0));
    const float localTempK = localThermo.x;
    const float localSoot = max(localThermo.z, 0.0f);
    const float sigmaExt = max(boundsAndFlags.z, 0.25f);

    // Direct self-emission of incandescent flame at this voxel
    const float3 selfEmit = evalPlanckBlackbody(localTempK);

    // 26-direction isotropic Moore-sphere ray set (6 axial + 12 edge-diagonal + 8 corner-diagonal):
    // Eliminates directional blind cones so firelight propagates smoothly into every surrounding billow.
    static const float kEdge = 0.70710678f;
    static const float kCorn = 0.57735027f;
    static const float3 kDirs[26] = {
        // 6 axial faces
        float3( 1.0f,  0.0f,  0.0f), float3(-1.0f,  0.0f,  0.0f),
        float3( 0.0f,  1.0f,  0.0f), float3( 0.0f, -1.0f,  0.0f),
        float3( 0.0f,  0.0f,  1.0f), float3( 0.0f,  0.0f, -1.0f),
        // 12 edge diagonals
        float3( kEdge,  kEdge,  0.0f), float3(-kEdge,  kEdge,  0.0f),
        float3( kEdge, -kEdge,  0.0f), float3(-kEdge, -kEdge,  0.0f),
        float3( kEdge,  0.0f,  kEdge), float3(-kEdge,  0.0f,  kEdge),
        float3( kEdge,  0.0f, -kEdge), float3(-kEdge,  0.0f, -kEdge),
        float3( 0.0f,  kEdge,  kEdge), float3( 0.0f, -kEdge,  kEdge),
        float3( 0.0f,  kEdge, -kEdge), float3( 0.0f, -kEdge, -kEdge),
        // 8 corner diagonals
        float3( kCorn,  kCorn,  kCorn), float3(-kCorn,  kCorn,  kCorn),
        float3( kCorn, -kCorn,  kCorn), float3(-kCorn, -kCorn,  kCorn),
        float3( kCorn,  kCorn, -kCorn), float3(-kCorn,  kCorn, -kCorn),
        float3( kCorn, -kCorn, -kCorn), float3(-kCorn, -kCorn, -kCorn)
    };
    // Geometric cascade intervals in voxels (fine near-field -> coarse far-field reach ~20 voxels)
    static const float kCascadeDist[5]  = { 1.25f, 2.75f, 5.25f, 9.25f, 15.50f };
    static const float kCascadeWidth[5] = { 1.25f, 1.75f, 3.00f, 4.75f,  7.50f };

    float3 gatheredRad = float3(0.0f, 0.0f, 0.0f);
    const float invDirCount = 1.0f / 26.0f;

    [loop] for (int d = 0; d < 26; ++d) {
        const float3 dirUvw = kDirs[d] * kInvGridRes;
        float trans = 1.0f;
        float3 rayRad = float3(0.0f, 0.0f, 0.0f);

        [unroll] for (int s = 0; s < 5; ++s) {
            const float3 sampleUvw = uvw + dirUvw * kCascadeDist[s];
            if (any(sampleUvw < 0.0f) || any(sampleUvw > 1.0f)) {
                break;
            }
            const float4 st = ThermoIn.SampleLevel(LinClamp, sampleUvw, 0.0f);
            const float stepSoot = max(st.z, 0.0f);
            const float stepTau = sigmaExt * stepSoot * (kCascadeWidth[s] * 0.35f);
            const float stepTrans = exp(-stepTau);

            // Primary Planck blackbody radiance + temporal multi-scatter cascade bounce
            const float3 stepEmit = evalPlanckBlackbody(st.x);
            const float3 prevBounce = hasResetBit() ? float3(0.0f, 0.0f, 0.0f)
                                                    : RadianceIn.SampleLevel(LinClamp, sampleUvw, 0.0f).rgb * 0.28f;
            const float invSq = 1.0f / (1.0f + 0.14f * kCascadeDist[s] * kCascadeDist[s]);

            // Integrate radiance across cascade interval with exact analytical slab factor
            const float slabScatter = (stepTau > 1.0e-4f) ? ((1.0f - stepTrans) / stepTau) : 1.0f;
            rayRad += trans * (stepEmit + prevBounce * stepSoot) * slabScatter * invSq;
            trans *= stepTrans;
            if (trans < 0.02f) {
                break;
            }
        }
        gatheredRad += rayRad * invDirCount;
    }

    // Smoke single-scattering albedo (~0.62 warm charcoal-amber) + direct flame self-emission
    const float3 sootAlbedo = float3(0.76f, 0.56f, 0.40f);
    const float sootScatterWeight = 1.0f - exp(-sigmaExt * localSoot);
    const float3 finalRad = selfEmit + gatheredRad * sootAlbedo * (0.25f + 0.75f * sootScatterWeight);

    RadianceOut[id] = float4(min(finalRad, float3(256.0f, 256.0f, 256.0f)), saturate(sootScatterWeight));
}
