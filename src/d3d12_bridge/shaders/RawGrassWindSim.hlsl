// RawGrassWindSim.hlsl: Native DX12 SM 6.6 / 6.10 GPU Compute 256x256 (65,536-Blade) Dynamic Grass Wind,
// Seasonal Snow-Weight Droop, Wildfire Char & Critically-Damped Actor-Trample Solver.
// Original clean-room implementation, MIT License.
//
// Single-pass ping-pong compute kernel (cs_grass_wind_step):
//   - World-anchored toroidal scrolling with clean border initialization (preserves trampled trails & scorch).
//   - Dual-harmonic travelling wind gust waves + divergence-free 2D curl-eddy turbulence.
//   - Up to 32 live actor/horse/cart trample capsules flattening blades in the direction of travel with
//     exact closed-form critically-damped elastic spring-back recovery (unconditionally stable, zero overshoot).
//   - Live seasonal snow-weight droop + up to 16 wildfire/campfire thermal char sources (scorched blades
//     persistently darken & stiffen).
//
// Textures:
//   t0 g_prevTrample (RGBA16F): (trampleX, trampleZ, velX, velZ)
//   t1 g_prevBend    (RGBA16F): (totalBendX, totalBendZ, droop01, charMask01)
//   u0 g_nextTrample (RGBA16F): updated 2D critically-damped spring state
//   u1 g_nextBend    (RGBA16F): (totalBendX, totalBendZ, droop01, charMask01) ready for grass/foliage shaders

struct GpuGrassCapsule {
    float4 segStartEnd; // (startX, startZ, endX, endZ) in world meters
    float4 velRadStr;   // (velX, velZ, radiusM, flattenAngleRad)
};

struct GpuGrassHeat {
    float4 posRadPower; // (worldX, worldZ, radiusM, heatPower)
};

cbuffer GrassWindCB : register(b0)
{
    uint   g_width;
    uint   g_height;
    int    g_shiftX;
    int    g_shiftZ;

    float  g_originWorldX;
    float  g_originWorldZ;
    float  g_cellSizeM;
    float  g_dtSec;

    float  g_timeSec;
    float  g_windSpeedMps;
    float  g_windDirX;
    float  g_windDirZ;

    float  g_gustStrength;
    float  g_springOmega;     // Critically damped natural frequency (rad/s)
    float  g_snowLoad01;      // Seasonal snow weight on blades [0..1]
    float  g_charRateScale;   // Wildfire char accumulation multiplier

    uint   g_capsuleCount;    // Up to 32 live trample capsules
    uint   g_heatCount;       // Up to 16 live thermal/wildfire heat sources
    uint   g_resetField;
    uint   g_pad0;

    GpuGrassCapsule g_capsules[32];
    GpuGrassHeat    g_heats[16];
};

Texture2D<float4>   g_prevTrample : register(t0);
Texture2D<float4>   g_prevBend    : register(t1);
RWTexture2D<float4> g_nextTrample : register(u0);
RWTexture2D<float4> g_nextBend    : register(u1);

float SanitizeFinite(float v)
{
    return (isnan(v) || isinf(v)) ? 0.0f : v;
}

float2 SanitizeFinite2(float2 v)
{
    return float2(SanitizeFinite(v.x), SanitizeFinite(v.y));
}

// Analytical divergence-free 2D curl-eddy field from smooth multi-harmonic streamfunction psi(x, z, t)
float2 EvalCurlWind2D(float2 p, float t)
{
    const float kx1 = 0.21f;
    const float kz1 = 0.23f;
    const float a1  = p.x * kx1 - t * 0.90f;
    const float b1  = p.y * kz1 + t * 0.70f;
    float sa1, ca1, sb1, cb1;
    sincos(a1, sa1, ca1);
    sincos(b1, sb1, cb1);

    const float dPsiDx1 =  kx1 * ca1 * cb1;
    const float dPsiDz1 = -kz1 * sa1 * sb1;

    const float kx2 = 0.47f;
    const float kz2 = 0.39f;
    const float a2  = p.x * kx2 + p.y * kz2 - t * 1.45f;
    const float ca2 = cos(a2);
    const float dPsiDx2 = 0.45f * kx2 * ca2;
    const float dPsiDz2 = 0.45f * kz2 * ca2;

    // 2D curl of scalar streamfunction: (dPsi/dz, -dPsi/dx) -> exact div == 0
    return float2((dPsiDz1 + dPsiDz2), -(dPsiDx1 + dPsiDx2));
}

[numthreads(8, 8, 1)]
void cs_grass_wind_step(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_width || dtid.y >= g_height) {
        return;
    }

    const int2 srcCoord = int2(dtid.xy) + int2(g_shiftX, g_shiftZ);
    const bool outOfBounds = (g_resetField != 0u) ||
                             (srcCoord.x < 0) || (srcCoord.x >= int(g_width)) ||
                             (srcCoord.y < 0) || (srcCoord.y >= int(g_height));

    float2 trampleXZ = float2(0.0f, 0.0f);
    float2 velXZ     = float2(0.0f, 0.0f);
    float  charMask  = 0.0f;

    if (!outOfBounds) {
        const float4 prevT = g_prevTrample.Load(int3(srcCoord, 0));
        const float4 prevB = g_prevBend.Load(int3(srcCoord, 0));
        trampleXZ = SanitizeFinite2(prevT.xy);
        velXZ     = SanitizeFinite2(prevT.zw);
        charMask  = saturate(SanitizeFinite(prevB.a));
    }

    const float2 worldXZ = float2(g_originWorldX, g_originWorldZ) +
                           (float2(dtid.xy) + 0.5f - 0.5f * float2(g_width, g_height)) * g_cellSizeM;

    const float dt = clamp(g_dtSec, 0.0f, 0.10f);

    // 1. Wildfire / campfire thermal char accumulation (persistent across frames & toroidal scrolls)
    const uint heatCount = min(g_heatCount, 16u);
    for (uint h = 0u; h < heatCount; ++h) {
        const float4 hs   = g_heats[h].posRadPower;
        const float  rad  = max(hs.z, 0.1f);
        const float2 dPos = worldXZ - hs.xy;
        const float  d2   = dot(dPos, dPos);
        const float  r2   = rad * rad;
        if (d2 < r2) {
            const float normD   = sqrt(d2) / rad;
            const float falloff = (1.0f - normD) * (1.0f - normD);
            const float addChar = max(hs.w, 0.0f) * falloff * g_charRateScale * dt;
            charMask = saturate(charMask + addChar);
        }
    }

    // 2. Exact closed-form critically-damped elastic spring-back recovery toward upright (0, 0)
    //    ODE: x'' + 2*omega*x' + omega^2*x = 0 -> x(t) = (x0 + (v0 + omega*x0)*t) * exp(-omega*t)
    const float omega = clamp(g_springOmega, 0.25f, 12.0f) * (1.0f - 0.35f * charMask);
    if (dt > 1e-6f) {
        const float  e  = exp(-omega * dt);
        const float2 c2 = velXZ + omega * trampleXZ;
        trampleXZ = (trampleXZ + c2 * dt) * e;
        velXZ     = (velXZ - omega * c2 * dt) * e;
    }

    // 3. Up to 32 live actor/horse/cart trample capsules flattening blades in direction of travel
    const uint capCount = min(g_capsuleCount, 32u);
    for (uint c = 0u; c < capCount; ++c) {
        const float4 seg = g_capsules[c].segStartEnd;
        const float4 prm = g_capsules[c].velRadStr;
        const float  rad = max(prm.z, 0.05f);
        const float  str = clamp(prm.w, 0.0f, 1.38f);

        const float2 a  = seg.xy;
        const float2 ab = seg.zw - seg.xy;
        const float  abLenSq = dot(ab, ab);
        const float  projT   = (abLenSq > 1e-6f) ? saturate(dot(worldXZ - a, ab) / abLenSq) : 0.0f;
        const float2 closest = a + ab * projT;
        const float2 diff    = worldXZ - closest;
        const float  distSq  = dot(diff, diff);
        const float  r2      = rad * rad;

        if (distSq < r2) {
            const float  u = distSq / r2;
            const float  w = (1.0f - u) * (1.0f - u); // Smooth C2 bell profile (1 at core, 0 at edge)
            const float  dist = sqrt(distSq);
            const float2 radialDir = (dist > 1e-4f) ? (diff / dist) : float2(1.0f, 0.0f);

            const float2 capVel = prm.xy;
            const float  vLen   = length(capVel);
            float2 pushDir = radialDir;
            if (vLen > 0.05f) {
                const float2 moveDir = capVel / vLen;
                // Strongly align flattened blade along actor travel direction with slight radial splay
                pushDir = normalize(moveDir * 0.90f + radialDir * 0.10f);
            }

            const float2 targetT = pushDir * (str * w);
            const float  curMag  = length(trampleXZ);
            const float  tgtMag  = length(targetT);
            if (tgtMag > curMag) {
                // Rapidly flatten blade beneath active actor and zero outward velocity
                const float blend = saturate(1.0f - exp(-28.0f * max(dt, 1.0f / 60.0f)));
                trampleXZ = lerp(trampleXZ, targetT, blend);
                velXZ     *= (1.0f - blend);
            }
        }
    }

    // Clamp persistent elastic trample state to physical horizontal bend bounds
    const float trampleMag = length(trampleXZ);
    if (trampleMag > 1.38f) {
        trampleXZ *= (1.38f / trampleMag);
    }
    const float velMag = length(velXZ);
    if (velMag > 8.0f) {
        velXZ *= (8.0f / velMag);
    }

    // 4. Dual-harmonic curl-gust travelling wind waves
    const float2 rawWindDir = float2(g_windDirX, g_windDirZ);
    const float  wdLen      = length(rawWindDir);
    const float2 windDir    = (wdLen > 1e-4f) ? (rawWindDir / wdLen) : float2(1.0f, 0.0f);
    const float2 windPerp   = float2(-windDir.y, windDir.x);

    const float windSpeed = max(g_windSpeedMps, 0.0f);
    const float windLoad  = saturate(windSpeed / 16.0f);
    const float gustAmp   = clamp(g_gustStrength, 0.0f, 1.25f);

    float2 windBend = float2(0.0f, 0.0f);
    if (windLoad > 1e-4f) {
        const float  k1    = 0.28f;
        const float  phi1  = dot(worldXZ, windDir) * k1 - (0.65f + 0.35f * windSpeed) * g_timeSec;
        const float  wave1 = 0.5f + 0.5f * sin(phi1);

        const float2 dir2  = normalize(windDir + 0.42f * windPerp);
        const float  k2    = 0.54f;
        const float  phi2  = dot(worldXZ, dir2) * k2 - (1.15f + 0.50f * windSpeed) * g_timeSec + 1.37f;
        const float  wave2 = 0.5f + 0.5f * sin(phi2);

        const float2 curlEddy = EvalCurlWind2D(worldXZ, g_timeSec);

        // Charred brittle blades and heavy snow-laden blades resist high-frequency wind sway
        const float stiffMod = (1.0f - 0.55f * charMask) * (1.0f - 0.22f * saturate(g_snowLoad01));
        const float gustEnv  = 0.55f + gustAmp * (0.55f * wave1 + 0.30f * wave2);

        windBend = (windDir * (windLoad * 0.82f * gustEnv) +
                    curlEddy * (windLoad * 0.16f * gustAmp)) * stiffMod;
    }

    // 5. Combine elastic trample + live wind sway + seasonal snow-weight droop
    float2 totalBend = SanitizeFinite2(trampleXZ + windBend);
    const float totalMag = length(totalBend);
    if (totalMag > 1.45f) {
        totalBend *= (1.45f / totalMag);
    }

    const float snow01    = saturate(g_snowLoad01);
    const float snowDroop = snow01 * 0.72f * (1.0f - 0.30f * charMask);
    const float bendDroop = saturate(length(totalBend) / 1.35f);
    const float droop01   = saturate(max(snowDroop, bendDroop * 0.85f + snowDroop * 0.30f));

    g_nextTrample[dtid.xy] = float4(SanitizeFinite2(trampleXZ), SanitizeFinite2(velXZ));
    g_nextBend[dtid.xy]    = float4(totalBend, SanitizeFinite(droop01), saturate(SanitizeFinite(charMask)));
}
