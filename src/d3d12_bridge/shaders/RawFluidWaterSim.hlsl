// RawFluidWaterSim.hlsl: Real-Time GPU 2D Shallow-Water Equations (SWE), Directional JONSWAP Swell
// & Lacy Cellular Foam Advection Solver on a 256 x 256 Camera-Following Toroidal Grid (SM 6.6 / 6.10).
//
// Resources:
//   - StateIn / StateOut (RGBA16F, 256x256):
//       R = surface height perturbation h(x,y,t)
//       G = horizontal velocity u_x(x,y,t) (world units/s)
//       B = horizontal velocity u_y(x,y,t) (world units/s)
//       A = lacy cellular foam persistence f(x,y,t) [0..1]
//   - NormalOut (RGBA16F, 256x256):
//       RGB = analytical curl-free Z-up surface normal (n.x, n.y, n.z)
//       A   = Jacobian crest-folding + advected lacy cellular foam mask ready for RtWater.hlsl
//
// 3 Compute Entry Points ([numthreads(8, 8, 1)] -> 32 x 32 x 1 thread groups):
//   1. cs_water_inject  : Player + 16 live NPC wakes/splashes (footTrail[0..31]), raindrop impact rings, shoreline wave-break
//   2. cs_water_step    : Conservative semi-implicit SWE with CFL safeguard + JONSWAP spectral swell + MacCormack foam advection
//   3. cs_water_normals : Analytical curl-free Z-up normal + 2x2 Jacobian crest-folding lacy foam mask
//
// Original clean-room implementation, MIT License.

static const uint  kWaterRes = 256u;
static const float kWaterResF = 256.0f;
static const float kInvWaterRes = 1.0f / 256.0f;

cbuffer WaterSimCB : register(b0)
{
    float4 camAndGrid;          // xy = camera world XY, z = cellSize dx (world units), w = invGridRes (1/256)
    float4 prevCamAndTime;      // xy = prev camera world XY, z = dt (s), w = timeSec
    float4 swePhysics;          // x = gravity g, y = restDepth H0, z = velDrag gamma, w = viscosity nu
    float4 swellAndWind;        // xy = wind/swell unit dir, z = swellAmp, w = riverFlowSpeed (world units/s)
    float4 foamAndRain;         // x = foamDecayRate, y = crestFoamThresh, z = rainIntensity [0..1], w = shoreBreakStrength
    float4 waterPlaneAndFlags;  // x = waterPlaneZ, y = shorelineZBias, z = frameIndex, w = flags (bit0 = reset)
    uint4  counts;              // x = trailCount (0..32), y = gridRes (256), z = 0, w = 0
    float4 footTrail[32];       // xy = world XY (0..15 player, 16..31 live NPCs), z = world Z, w = wake impulse strength
    float4 footVel[32];         // xy = actor horizontal velocity (u/s), z = wake radius (world units), w = splash scale
};

Texture2D<float4>   StateIn   : register(t0);
RWTexture2D<float4> StateOut  : register(u0);
RWTexture2D<float4> NormalOut : register(u1);

SamplerState LinWrap : register(s0);

// ----------------------------------------------------------------------------
// Aperiodic 2D hash, smooth value noise & Voronoi cellular bubble web
// (Pure polynomial mixing: zero vendor drift, zero zebra stripes / static rings)
// ----------------------------------------------------------------------------
float waterHash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float waterValueNoise(float2 p)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = waterHash21(i + float2(0.0f, 0.0f));
    const float b = waterHash21(i + float2(1.0f, 0.0f));
    const float c = waterHash21(i + float2(0.0f, 1.0f));
    const float d = waterHash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Voronoi cell edge distance (F2 - F1): 0 along bubble filament walls, >0 inside open water pockets.
float waterCellWalls(float2 q, float t)
{
    const float2 c = floor(q);
    float f1 = 8.0f;
    float f2 = 8.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            const float2 cell = c + float2(float(dx), float(dy));
            const float2 h = float2(waterHash21(cell), waterHash21(cell + float2(31.7f, 19.3f)));
            const float2 pt = cell + 0.5f + 0.40f * sin(t * (0.45f + 0.35f * h.yx) + 6.2831853f * h);
            const float dd = length(q - pt);
            if (dd < f1) {
                f2 = f1;
                f1 = dd;
            } else if (dd < f2) {
                f2 = dd;
            }
        }
    }
    return f2 - f1;
}

// Organic domain-warped lacy cellular foam web (stretches along flow, tears into open-cell foam lace).
float evalLacyFoamWeb(float2 worldXY, float2 flowVel, float timeSec, float density01)
{
    const float2 advPos = worldXY - flowVel * (timeSec * 0.12f);
    const float2 q = advPos * 0.032f;
    const float wx = (waterValueNoise(q * 0.75f + float2(4.1f, 9.2f)) - 0.5f) * 1.85f;
    const float wy = (waterValueNoise(q * 0.75f + float2(13.7f, 2.8f)) - 0.5f) * 1.85f;
    const float2 qw = q + float2(wx, wy);

    const float w1 = waterCellWalls(qw * 1.10f, timeSec * 0.25f);
    const float w2 = waterCellWalls(qw * 2.45f + float2(11.3f, -7.1f), timeSec * 0.42f);
    const float lace1 = 1.0f - smoothstep(0.015f, lerp(0.14f, 0.40f, density01), w1);
    const float lace2 = 1.0f - smoothstep(0.012f, lerp(0.12f, 0.34f, density01), w2);
    const float micro = smoothstep(0.38f, 0.85f, waterValueNoise(qw * 6.5f + float2(timeSec * 0.18f, -timeSec * 0.14f)));
    const float core  = smoothstep(0.62f, 0.94f, density01) * (0.68f + 0.32f * micro);
    return saturate(max(core, max(lace1 * 0.90f, lace2 * 0.68f) * (0.42f + 0.58f * micro)));
}

// ----------------------------------------------------------------------------
// Toroidal world-locked coordinate mapping & camera-centered boundary sponge
// ----------------------------------------------------------------------------
bool hasWaterReset()
{
    return (uint(waterPlaneAndFlags.w + 0.5f) & 1u) != 0u;
}

float2 texelToToroidalWorld(uint2 ip)
{
    const float dx = max(camAndGrid.z, 0.25f);
    const float span = kWaterResF * dx;
    const float2 uv = (float2(ip) + 0.5f) * kInvWaterRes;
    const float2 camXY = camAndGrid.xy;
    const float2 camWrap = floor(camXY / span) * span;
    float2 worldXY = camWrap + uv * span;
    const float2 dCam = worldXY - camXY;
    worldXY.x -= (dCam.x >  0.5f * span) ? span : ((dCam.x < -0.5f * span) ? -span : 0.0f);
    worldXY.y -= (dCam.y >  0.5f * span) ? span : ((dCam.y < -0.5f * span) ? -span : 0.0f);
    return worldXY;
}

float evalBoundarySponge(float2 worldXY)
{
    const float dx = max(camAndGrid.z, 0.25f);
    const float halfSpan = 0.5f * kWaterResF * dx;
    const float2 normDist = abs(worldXY - camAndGrid.xy) / max(halfSpan, 1.0f);
    const float maxEdge = max(normDist.x, normDist.y);
    return 1.0f - smoothstep(0.84f, 0.99f, maxEdge);
}

// ----------------------------------------------------------------------------
// 6-Component Directional JONSWAP Spectral Gravity-Wave Swell
// Returns float4(height, dH/dx, dH/dy, crestSteepness) using exact deep/finite
// water dispersion omega = sqrt(g * k * tanh(k * H0)) with irrational wave angles.
// ----------------------------------------------------------------------------
float4 evalJonswapSwell(float2 worldXY, float timeSec)
{
    const float swellAmp = max(swellAndWind.z, 0.0f);
    if (swellAmp <= 1.0e-5f) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float rawLen = length(swellAndWind.xy);
    const float2 wDir = (rawLen > 1.0e-3f) ? (swellAndWind.xy / rawLen) : float2(1.0f, 0.25f) * rsqrt(1.0625f);
    const float2 wPerp = float2(-wDir.y, wDir.x);
    const float g = max(swePhysics.x, 1.0f);
    const float H0 = max(swePhysics.y, 2.0f);

    // Irrational wavenumbers & directional spread angles (zero periodic tiling)
    static const float kK[6]     = { 0.0113f, 0.0197f, 0.0341f, 0.0589f, 0.0973f, 0.1619f };
    static const float kWeight[6]= { 0.42f,   0.36f,   0.22f,   0.14f,   0.085f,  0.048f  };
    static const float kAng[6]   = { 0.00f,   0.37f,  -0.46f,   0.79f,  -0.88f,   1.19f   };
    static const float kPhase[6] = { 0.00f,   1.618f,  3.141f,  4.669f,  0.732f,  2.414f  };

    float h = 0.0f;
    float2 grad = float2(0.0f, 0.0f);
    float steep = 0.0f;

    [unroll] for (int i = 0; i < 6; ++i) {
        const float c = cos(kAng[i]);
        const float s = sin(kAng[i]);
        const float2 dir = wDir * c + wPerp * s;
        const float k = kK[i];
        const float omega = sqrt(g * k * tanh(min(k * H0, 12.0f)));
        const float a = swellAmp * kWeight[i];
        const float ph = dot(worldXY, dir) * k - omega * timeSec + kPhase[i];
        // Second-order Stokes trochoidal sharpening: sharper crests, broader troughs
        const float sinP = sin(ph);
        const float cosP = cos(ph);
        const float sin2P = 2.0f * sinP * cosP;
        const float cos2P = 2.0f * cosP * cosP - 1.0f;

        h += a * (sinP + 0.18f * sin2P);
        const float dH = a * k * (cosP + 0.36f * cos2P);
        grad += dir * dH;
        // Negative second derivative peaks on crests -> feeds Jacobian crest folding
        steep += max(0.0f, a * k * k * (sinP + 0.72f * sin2P));
    }
    return float4(h, grad.x, grad.y, steep);
}

// ----------------------------------------------------------------------------
// Pass 1: cs_water_inject
// Injects live player + 16 NPC wakes & splashes (footTrail[0..31]), raindrop
// impact rings, and shoreline wave-break energy into the 256x256 SWE state.
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void cs_water_inject(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= kWaterRes)) {
        return;
    }
    const uint2 ip = tid.xy;
    const float dt = clamp(prevCamAndTime.z, 1.0e-4f, 0.05f);
    const float timeSec = prevCamAndTime.w;
    const float dx = max(camAndGrid.z, 0.25f);
    const float2 worldXY = texelToToroidalWorld(ip);
    const float sponge = evalBoundarySponge(worldXY);

    float4 state = hasWaterReset() ? float4(0.0f, 0.0f, 0.0f, 0.0f) : StateIn.Load(int3(ip, 0));

    // Clear freshly wrapped seam band when camera translates
    const float span = kWaterResF * dx;
    const float2 dPrev = abs(worldXY - prevCamAndTime.xy);
    if (max(dPrev.x, dPrev.y) > 0.485f * span) {
        state = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 1) Player (0..15) + live NPC (16..31) wakes & splashes
    const uint trailCount = min(counts.x, 32u);
    [loop] for (uint fi = 0u; fi < trailCount; ++fi) {
        const float strength = footTrail[fi].w;
        if (strength <= 0.01f) {
            continue;
        }
        const float2 delta = worldXY - footTrail[fi].xy;
        const float radius = max(footVel[fi].z, dx * 1.5f);
        const float d2 = dot(delta, delta);
        const float r2 = radius * radius;
        if (d2 >= r2 * 6.25f) {
            continue;
        }
        const float q2 = d2 / max(r2, 1.0e-4f);
        const float gauss = exp(-0.5f * q2);
        // Zero-mean Ricker wavelet (mass-conserving depression + displaced bow/collar rim)
        const float ricker = (1.0f - 0.5f * q2) * gauss;

        const float2 actorVel = footVel[fi].xy;
        const float actorSpd = length(actorVel);
        const float2 dirNorm = (actorSpd > 0.5f) ? (actorVel / actorSpd) : float2(0.0f, 0.0f);
        const float bowBias = (actorSpd > 0.5f) ? dot(normalize(delta + float2(1.0e-4f, 0.0f)), dirNorm) : 0.0f;

        // Height displacement + momentum transfer (Kelvin V-wake + swirling wake vortex)
        state.x += (-ricker * 0.55f + bowBias * gauss * 0.30f) * strength * dt * 8.0f;
        const float2 radialDir = delta * rsqrt(max(d2, 1.0f));
        const float2 tangentDir = float2(-radialDir.y, radialDir.x);
        state.yz += (actorVel * 0.55f + radialDir * (strength * 4.5f) + tangentDir * (bowBias * strength * 2.2f)) * gauss * dt * 4.0f;

        // Splash aerated foam (modulated by lacy cellular web so wake foam is never a solid disc!)
        const float splashScale = max(footVel[fi].w, 0.5f);
        const float web = evalLacyFoamWeb(worldXY, state.yz, timeSec, saturate(strength));
        state.w = saturate(state.w + gauss * strength * splashScale * (0.35f + 0.65f * web) * dt * 3.5f);
    }

    // 2) Raindrop impact rings when raining
    const float rain = saturate(foamAndRain.z);
    if (rain > 0.02f) {
        const float dropCell = max(dx * 2.0f, 4.0f);
        const float2 cellIdx = floor(worldXY / dropCell);
        const float timeSlot = floor(timeSec * 16.0f);
        const float hDrop = waterHash21(cellIdx + float2(timeSlot * 13.7f, timeSlot * 29.3f));
        const float dropThresh = 1.0f - rain * 0.045f;
        if (hDrop > dropThresh) {
            const float2 jitter = float2(waterHash21(cellIdx + 17.1f), waterHash21(cellIdx + 53.9f));
            const float2 dropCenter = (cellIdx + 0.15f + 0.70f * jitter) * dropCell;
            const float2 dd = worldXY - dropCenter;
            const float r2 = dot(dd, dd) / max(dx * dx * 1.8f, 1.0f);
            if (r2 < 5.0f) {
                const float ring = (1.0f - 0.5f * r2) * exp(-0.5f * r2);
                state.x -= ring * rain * 0.28f;
                state.yz += dd * (exp(-0.5f * r2) * rain * 0.85f);
            }
        }
    }

    // 3) Shoreline wave-break & whitecap surf energy
    const float shoreBreak = max(foamAndRain.w, 0.0f);
    if (shoreBreak > 0.01f) {
        const float4 swell = evalJonswapSwell(worldXY, timeSec);
        const float crestMask = smoothstep(0.015f, 0.085f, swell.w);
        if (crestMask > 0.001f) {
            const float2 wDir = (length(swellAndWind.xy) > 1.0e-3f) ? normalize(swellAndWind.xy) : float2(1.0f, 0.0f);
            const float web = evalLacyFoamWeb(worldXY, wDir * swellAndWind.w, timeSec, crestMask);
            state.yz += wDir * (crestMask * shoreBreak * dt * 6.0f);
            state.w = saturate(state.w + crestMask * shoreBreak * web * dt * 1.8f);
        }
    }

    // Clamp state within physically safe bounds
    const float maxH = max(swePhysics.y * 0.65f, 1.0f);
    state.x = clamp(state.x, -maxH, maxH) * sponge;
    state.yz *= sponge;
    state.w = saturate(state.w) * sponge;

    StateOut[ip] = state;
}

// ----------------------------------------------------------------------------
// Pass 2: cs_water_step
// Conservative semi-implicit Shallow-Water Equations (SWE):
//   dh/dt + div((H0 + h) * u) = 0
//   du/dt + (u . grad)u = -g * grad(h) - gamma * u + nu * lap(u)
// Coupled with directional JONSWAP swell forcing & clamped MacCormack lacy foam
// advection + exponential decay.
// ----------------------------------------------------------------------------
float4 sampleMacCormack2D(float2 uv, float2 velUv)
{
    const float2 uvBack = frac(uv - velUv);
    const float2 uvFwd  = frac(uvBack + velUv);

    const float4 phiN   = StateIn.SampleLevel(LinWrap, uv, 0.0f);
    const float4 phiHat = StateIn.SampleLevel(LinWrap, uvBack, 0.0f);
    const float4 phiRev = StateIn.SampleLevel(LinWrap, uvFwd, 0.0f);
    const float4 corr   = phiHat + 0.5f * (phiN - phiRev);

    // Local 4-neighbor BFECC monotonicity clamp
    const float2 ox = float2(kInvWaterRes, 0.0f);
    const float2 oy = float2(0.0f, kInvWaterRes);
    const float4 n0 = StateIn.SampleLevel(LinWrap, frac(uvBack - ox), 0.0f);
    const float4 n1 = StateIn.SampleLevel(LinWrap, frac(uvBack + ox), 0.0f);
    const float4 n2 = StateIn.SampleLevel(LinWrap, frac(uvBack - oy), 0.0f);
    const float4 n3 = StateIn.SampleLevel(LinWrap, frac(uvBack + oy), 0.0f);
    const float4 lo = min(phiHat, min(min(n0, n1), min(n2, n3)));
    const float4 hi = max(phiHat, max(max(n0, n1), max(n2, n3)));
    return clamp(corr, lo, hi);
}

[numthreads(8, 8, 1)]
void cs_water_step(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= kWaterRes)) {
        return;
    }
    const uint2 ip = tid.xy;
    const float2 uv = (float2(ip) + 0.5f) * kInvWaterRes;
    const float dx = max(camAndGrid.z, 0.25f);
    const float span = kWaterResF * dx;
    const float rawDt = clamp(prevCamAndTime.z, 1.0e-4f, 0.05f);
    const float timeSec = prevCamAndTime.w;

    const float g     = max(swePhysics.x, 1.0f);
    const float H0    = max(swePhysics.y, 1.0f);
    const float gamma = max(swePhysics.z, 0.0f);
    const float nu    = max(swePhysics.w, 0.0f);

    // Explicit SWE CFL stability guard: c * dt / dx <= 0.42 (< 0.5 guaranteed)
    const float waveSpeedC = sqrt(g * H0);
    const float maxCflDt = 0.42f * dx / max(waveSpeedC, 1.0e-3f);
    const float dt = min(rawDt, maxCflDt);

    const uint ixL = (ip.x + kWaterRes - 1u) & (kWaterRes - 1u);
    const uint ixR = (ip.x + 1u) & (kWaterRes - 1u);
    const uint iyD = (ip.y + kWaterRes - 1u) & (kWaterRes - 1u);
    const uint iyU = (ip.y + 1u) & (kWaterRes - 1u);

    const float4 cC = StateIn.Load(int3(ip, 0));
    const float4 cL = StateIn.Load(int3(ixL, ip.y, 0));
    const float4 cR = StateIn.Load(int3(ixR, ip.y, 0));
    const float4 cD = StateIn.Load(int3(ip.x, iyD, 0));
    const float4 cU = StateIn.Load(int3(ip.x, iyU, 0));

    // Background river / wind drift velocity
    const float rawLen = length(swellAndWind.xy);
    const float2 windDir = (rawLen > 1.0e-3f) ? (swellAndWind.xy / rawLen) : float2(1.0f, 0.0f);
    const float2 bgFlow = windDir * swellAndWind.w;
    const float2 totalVel = cC.yz + bgFlow;

    // 1) MacCormack semi-Lagrangian advection of velocity (u . grad)u and foam
    const float2 advUv = clamp((totalVel * dt) / span, float2(-0.02f, -0.02f), float2(0.02f, 0.02f));
    const float4 advState = sampleMacCormack2D(uv, advUv);

    // 2) Momentum equation: du/dt = -g * grad(h) - gamma * u + nu * lap(u) (semi-implicit drag)
    const float inv2dx = 0.5f / dx;
    const float2 gradH = float2(cR.x - cL.x, cU.x - cD.x) * inv2dx;
    const float2 lapU = (cL.yz + cR.yz + cD.yz + cU.yz - 4.0f * cC.yz) / (dx * dx);

    float2 uNew = (advState.yz - dt * g * gradH + dt * nu * lapU) / (1.0f + dt * gamma);
    const float maxVel = 0.45f * dx / max(dt, 1.0e-4f);
    const float uMag = length(uNew);
    if (uMag > maxVel) {
        uNew *= (maxVel / uMag);
    }

    // 3) Conservative mass continuity: dh/dt + div((H0 + h) * u) = 0
    // With mild Rusanov numerical viscosity (0.04) preventing checkerboard odd-even decoupling
    const float HL = max(H0 + cL.x, 0.05f * H0);
    const float HR = max(H0 + cR.x, 0.05f * H0);
    const float HD = max(H0 + cD.x, 0.05f * H0);
    const float HU = max(H0 + cU.x, 0.05f * H0);

    const float fluxDiv = ((HR * cR.y - HL * cL.y) + (HU * cU.z - HD * cD.z)) * inv2dx;
    const float lapH = (cL.x + cR.x + cD.x + cU.x) * 0.25f - cC.x;
    float hNew = cC.x - dt * fluxDiv + 0.04f * lapH;

    // 4) Lacy cellular foam advection, convergence/crest generation & exponential decay
    const float2 worldXY = texelToToroidalWorld(ip);
    const float sponge = evalBoundarySponge(worldXY);
    const float conv = max(0.0f, -((cR.y - cL.y) + (cU.z - cD.z)) * inv2dx);
    const float crestCurv = max(0.0f, -lapH / max(dx, 0.25f));
    const float4 swell = evalJonswapSwell(worldXY, timeSec);

    const float foamGenRaw = saturate(conv * 0.45f + crestCurv * 2.4f + max(0.0f, swell.w - foamAndRain.y) * 4.5f);
    const float lacyWeb = evalLacyFoamWeb(worldXY, totalVel, timeSec, max(advState.w, foamGenRaw));
    const float foamDecay = exp(-max(foamAndRain.x, 0.02f) * rawDt);
    float foamNew = advState.w * foamDecay;
    foamNew = saturate(foamNew + foamGenRaw * lacyWeb * rawDt * 2.2f);
    // Subtle cellular tearing as foam ages so trailing wakes dissolve into delicate webs
    foamNew *= lerp(0.86f + 0.14f * lacyWeb, 1.0f, saturate(foamNew * 1.4f));

    const float maxH = max(H0 * 0.65f, 1.0f);
    hNew = clamp(hNew, -maxH, maxH) * sponge;
    uNew *= sponge;
    foamNew = saturate(foamNew) * sponge;

    StateOut[ip] = float4(hNew, uNew.x, uNew.y, foamNew);
}

// ----------------------------------------------------------------------------
// Pass 3: cs_water_normals
// Computes analytical curl-free Z-up surface normal + 2x2 Jacobian crest-folding
// foam mask ready for direct binding in RtWater.hlsl.
// ----------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void cs_water_normals(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= kWaterRes)) {
        return;
    }
    const uint2 ip = tid.xy;
    const float dx = max(camAndGrid.z, 0.25f);
    const float inv2dx = 0.5f / dx;
    const float timeSec = prevCamAndTime.w;
    const float2 worldXY = texelToToroidalWorld(ip);

    const uint ixL = (ip.x + kWaterRes - 1u) & (kWaterRes - 1u);
    const uint ixR = (ip.x + 1u) & (kWaterRes - 1u);
    const uint iyD = (ip.y + kWaterRes - 1u) & (kWaterRes - 1u);
    const uint iyU = (ip.y + 1u) & (kWaterRes - 1u);

    const float4 cC = StateIn.Load(int3(ip, 0));
    const float4 cL = StateIn.Load(int3(ixL, ip.y, 0));
    const float4 cR = StateIn.Load(int3(ixR, ip.y, 0));
    const float4 cD = StateIn.Load(int3(ip.x, iyD, 0));
    const float4 cU = StateIn.Load(int3(ip.x, iyU, 0));

    // Analytical JONSWAP swell height gradient
    const float4 swell = evalJonswapSwell(worldXY, timeSec);

    // Total curl-free height gradient: grad(h_swe + h_swell)
    const float2 gradSwe = float2(cR.x - cL.x, cU.x - cD.x) * inv2dx;
    const float2 totalGrad = gradSwe + swell.yz;
    const float3 normalZUp = normalize(float3(-totalGrad.x, -totalGrad.y, 1.0f));

    // 2x2 Jacobian crest-folding determinant from horizontal flow strain + swell steepness:
    // J = det(I + tau * grad(u)) - swellSteepness; J < 1 indicates surface compression / wave crest folding.
    const float tau = 0.35f;
    const float du_dx = (cR.y - cL.y) * inv2dx;
    const float du_dy = (cU.y - cD.y) * inv2dx;
    const float dv_dx = (cR.z - cL.z) * inv2dx;
    const float dv_dy = (cU.z - cD.z) * inv2dx;
    const float jacDet = (1.0f + tau * du_dx) * (1.0f + tau * dv_dy) - (tau * du_dy) * (tau * dv_dx);
    const float foldStrength = saturate((1.0f - jacDet) * 1.45f + max(0.0f, swell.w - foamAndRain.y) * 5.0f);

    const float rawLen = length(swellAndWind.xy);
    const float2 bgFlow = ((rawLen > 1.0e-3f) ? (swellAndWind.xy / rawLen) : float2(1.0f, 0.0f)) * swellAndWind.w;
    const float lacyFold = evalLacyFoamWeb(worldXY, cC.yz + bgFlow, timeSec, max(cC.w, foldStrength));
    const float finalFoamMask = saturate(max(cC.w, foldStrength * lacyFold));

    NormalOut[ip] = float4(normalZUp, finalFoamMask);
}
