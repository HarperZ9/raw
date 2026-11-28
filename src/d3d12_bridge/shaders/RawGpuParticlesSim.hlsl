// RawGpuParticlesSim.hlsl: Clean-Room SM 6.6 / SM 6.10 GPU Compute 16,384-Particle 3D Physics & Splat Engine.
// Entry points:
//   - cs_particles_sim   ([numthreads(64,1,1)]): deterministic emit/respawn, 3D turbulent curl-noise wind,
//                                                mode-dependent buoyancy/gravity (Embers/Snow/Rain/WaterfallMist),
//                                                ground elastic bounce restitution, lifespan fade, atomic stats.
//   - cs_particles_splat ([numthreads(64,1,1)]): race-free atomic fixed-point (Q22.10) screen-space additive HDR splat.
// Original clean-room implementation, MIT License.

struct GpuParticle
{
    float3 pos;
    float  age;
    float3 vel;
    float  maxLife;
    float3 color;
    float  size;
    uint   mode;
    uint   state;       // bit 0 = alive, bit 1 = bounced
    uint   seed;
    uint   bounceCount;
};

cbuffer ParticleCB : register(b0)
{
    row_major float4x4 g_viewProj;
    float4             g_emitterPosAndRadius; // xyz = emitter pos, w = radius
    float4   g_windAndCurl;         // xyz = base wind vel (m/s), w = curl strength
    float4   g_physParams;          // x = dtSec, y = timeSec, z = groundY, w = restitution
    float4   g_modeParams;          // x = gravityScale, y = buoyancyAccel, z = dragCoeff, w = splatIntensity
    uint4    g_countsAndFlags;      // x = particleCount, y = mode, z = frameIndex, w = flags (b0=reset, b1=respawn, b2=bounce)
    uint4    g_splatDims;           // x = splatW, y = splatH, z = 0, w = 0
};

RWStructuredBuffer<GpuParticle> g_particles : register(u0);
RWByteAddressBuffer             g_stats     : register(u1); // [0]=active, [4]=bounced, [8]=respawned, [12]=splatted
RWByteAddressBuffer             g_splatBuf  : register(u2); // 4 uints/px (R,G,B,A in Q22.10 fixed-point)

groupshared uint s_grpActive;
groupshared uint s_grpBounced;
groupshared uint s_grpRespawned;
groupshared uint s_grpSplatted;

float SanitizeFinite(float v, float fb)
{
    uint bits = asuint(v);
    return ((bits & 0x7F800000u) == 0x7F800000u) ? fb : v;
}

float3 Sanitize3(float3 v, float3 fb)
{
    return float3(SanitizeFinite(v.x, fb.x), SanitizeFinite(v.y, fb.y), SanitizeFinite(v.z, fb.z));
}

// Fast PCG hash for deterministic GPU PRNG
uint PcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Rand01(inout uint state)
{
    state = PcgHash(state);
    return float(state & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// Smooth 3D value noise for analytic 3D curl-noise turbulence
float Hash31(int3 p, uint salt)
{
    uint3 q = asuint(p);
    uint h = q.x * 1664525u + q.y * 1013904223u + q.z * 374761393u + salt * 668265263u;
    h ^= h >> 15u;
    h *= 2246822519u;
    h ^= h >> 13u;
    h *= 3266489917u;
    h ^= h >> 16u;
    return float(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float ValueNoise3D(float3 p, uint salt)
{
    float3 fp = floor(p);
    int3   ip = int3(fp);
    float3 f  = p - fp;
    float3 u  = f * f * (3.0f - 2.0f * f);

    float n000 = Hash31(ip + int3(0, 0, 0), salt);
    float n100 = Hash31(ip + int3(1, 0, 0), salt);
    float n010 = Hash31(ip + int3(0, 1, 0), salt);
    float n110 = Hash31(ip + int3(1, 1, 0), salt);
    float n001 = Hash31(ip + int3(0, 0, 1), salt);
    float n101 = Hash31(ip + int3(1, 0, 1), salt);
    float n011 = Hash31(ip + int3(0, 1, 1), salt);
    float n111 = Hash31(ip + int3(1, 1, 1), salt);

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    return lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
}

// Divergence-free 3D curl-noise turbulence vector via compact central differences
float3 EvalCurlNoise3D(float3 worldPos, float timeSec)
{
    float3 p = worldPos * 0.35f + float3(0.0f, -timeSec * 0.45f, timeSec * 0.25f);
    const float eps = 0.08f;
    const float inv2Eps = 0.5f / eps;

    float dPz_dy = (ValueNoise3D(p + float3(0.0f, eps, 0.0f), 3u) - ValueNoise3D(p - float3(0.0f, eps, 0.0f), 3u)) * inv2Eps;
    float dPy_dz = (ValueNoise3D(p + float3(0.0f, 0.0f, eps), 2u) - ValueNoise3D(p - float3(0.0f, 0.0f, eps), 2u)) * inv2Eps;

    float dPx_dz = (ValueNoise3D(p + float3(0.0f, 0.0f, eps), 1u) - ValueNoise3D(p - float3(0.0f, 0.0f, eps), 1u)) * inv2Eps;
    float dPz_dx = (ValueNoise3D(p + float3(eps, 0.0f, 0.0f), 3u) - ValueNoise3D(p - float3(eps, 0.0f, 0.0f), 3u)) * inv2Eps;

    float dPy_dx = (ValueNoise3D(p + float3(eps, 0.0f, 0.0f), 2u) - ValueNoise3D(p - float3(eps, 0.0f, 0.0f), 2u)) * inv2Eps;
    float dPx_dy = (ValueNoise3D(p + float3(0.0f, eps, 0.0f), 1u) - ValueNoise3D(p - float3(0.0f, eps, 0.0f), 1u)) * inv2Eps;

    return float3(dPz_dy - dPy_dz, dPx_dz - dPz_dx, dPy_dx - dPx_dy);
}

GpuParticle SpawnParticle(uint idx, uint mode, uint frameIdx, uint prevSeed)
{
    uint rng = PcgHash(idx * 1973u ^ frameIdx * 9277u ^ prevSeed ^ 0x9E3779B9u);

    // Uniform point inside sphere of radius emitterRadius
    float u1 = Rand01(rng);
    float u2 = Rand01(rng);
    float u3 = Rand01(rng);
    float phi = u1 * 6.2831853f;
    float cosTheta = u2 * 2.0f - 1.0f;
    float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
    float rad = g_emitterPosAndRadius.w * pow(max(u3, 1.0e-5f), 0.3333333f);
    float3 offset = float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi)) * rad;

    GpuParticle p;
    p.pos         = g_emitterPosAndRadius.xyz + offset;
    p.age         = 0.0f;
    p.mode        = mode;
    p.state       = 1u; // alive, not yet bounced
    p.bounceCount = 0u;

    float rvx = (Rand01(rng) * 2.0f - 1.0f);
    float rvy = Rand01(rng);
    float rvz = (Rand01(rng) * 2.0f - 1.0f);

    if (mode == 0u) {
        // Mode 0: Rising incandescent fire embers / sparks
        p.vel     = float3(rvx * 0.85f, 1.25f + rvy * 1.35f, rvz * 0.85f);
        p.maxLife = 1.4f + Rand01(rng) * 1.6f;
        p.color   = lerp(float3(2.8f, 0.95f, 0.18f), float3(3.6f, 1.95f, 0.48f), Rand01(rng));
        p.size    = 0.03f + Rand01(rng) * 0.04f;
    } else if (mode == 1u) {
        // Mode 1: Falling snowflakes
        p.vel     = float3(rvx * 0.35f, -(0.45f + rvy * 0.55f), rvz * 0.35f);
        p.maxLife = 2.0f + Rand01(rng) * 1.5f;
        p.color   = float3(0.92f, 0.96f, 1.05f);
        p.size    = 0.04f + Rand01(rng) * 0.03f;
    } else if (mode == 2u) {
        // Mode 2: Falling rain droplets
        p.vel     = float3(rvx * 0.25f, -(3.5f + rvy * 2.5f), rvz * 0.25f);
        p.maxLife = 1.5f + Rand01(rng) * 1.0f;
        p.color   = float3(0.55f, 0.72f, 0.95f);
        p.size    = 0.02f + Rand01(rng) * 0.02f;
    } else {
        // Mode 3: Waterfall mist spray
        p.vel     = float3(rvx * 1.4f, 0.35f + rvy * 0.95f, rvz * 1.4f);
        p.maxLife = 1.6f + Rand01(rng) * 1.4f;
        p.color   = float3(0.75f, 0.86f, 0.95f);
        p.size    = 0.10f + Rand01(rng) * 0.08f;
    }

    p.seed = rng;
    return p;
}

[numthreads(64, 1, 1)]
void cs_particles_sim(uint3 tid : SV_DispatchThreadID, uint gIdx : SV_GroupIndex)
{
    if (gIdx == 0u) {
        s_grpActive    = 0u;
        s_grpBounced   = 0u;
        s_grpRespawned = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint count = g_countsAndFlags.x;
    if (tid.x < count) {
        const uint mode          = g_countsAndFlags.y;
        const uint frameIdx      = g_countsAndFlags.z;
        const uint flags         = g_countsAndFlags.w;
        const bool resetAll      = (flags & 1u) != 0u;
        const bool enableRespawn = (flags & 2u) != 0u;
        const bool enableBounce  = (flags & 4u) != 0u;

        const float dt          = clamp(SanitizeFinite(g_physParams.x, 1.0f / 60.0f), 0.0f, 0.15f);
        const float timeSec     = SanitizeFinite(g_physParams.y, 0.0f);
        const float groundY     = SanitizeFinite(g_physParams.z, 0.0f);
        const float restitution = clamp(SanitizeFinite(g_physParams.w, 0.55f), 0.0f, 0.95f);

        GpuParticle p = g_particles[tid.x];
        bool respawned = false;

        if (resetAll) {
            p = SpawnParticle(tid.x, mode, frameIdx, tid.x * 2654435761u);
            respawned = true;
        } else {
            bool alive = (p.state & 1u) != 0u;
            if (!alive && enableRespawn) {
                p = SpawnParticle(tid.x, mode, frameIdx, p.seed);
                alive = true;
                respawned = true;
            }

            if (alive) {
                p.age += dt;
                if (p.age >= max(p.maxLife, 1.0e-3f)) {
                    if (enableRespawn) {
                        p = SpawnParticle(tid.x, mode, frameIdx, p.seed);
                        respawned = true;
                    } else {
                        p.state &= ~1u;
                        alive = false;
                    }
                }
            }

            if (alive && !respawned && dt > 0.0f) {
                // Mode-dependent gravity & buoyancy
                const float gravScale = SanitizeFinite(g_modeParams.x, 1.0f);
                const float buoyAccel = SanitizeFinite(g_modeParams.y, 3.8f);
                const float dragCoeff = clamp(SanitizeFinite(g_modeParams.z, 0.8f), 0.0f, 12.0f);

                float3 accel = float3(0.0f, 0.0f, 0.0f);
                if (p.mode == 0u) {
                    // Embers: upward thermal buoyancy overcomes light gravity
                    accel.y = buoyAccel - 0.15f * 9.81f * gravScale;
                } else if (p.mode == 1u) {
                    // Snow: gentle gravity
                    accel.y = -1.65f * gravScale;
                } else if (p.mode == 2u) {
                    // Rain: full gravity
                    accel.y = -9.81f * gravScale;
                } else {
                    // Waterfall mist: gentle upward buoyant spread
                    accel.y = 0.45f * buoyAccel - 0.35f * 9.81f * gravScale;
                }

                // 3D turbulent curl-noise + wind advection
                float3 curl = float3(0.0f, 0.0f, 0.0f);
                const float curlStrength = max(SanitizeFinite(g_windAndCurl.w, 0.0f), 0.0f);
                if (curlStrength > 1.0e-4f) {
                    curl = EvalCurlNoise3D(p.pos, timeSec) * curlStrength;
                }
                const float3 targetWind = Sanitize3(g_windAndCurl.xyz, float3(0.0f, 0.0f, 0.0f)) + curl;
                const float  windCouple = (p.mode == 2u) ? 0.35f : 1.25f;
                accel += (targetWind - p.vel) * (dragCoeff * windCouple);

                // Semi-implicit Euler integration
                p.vel = Sanitize3(p.vel + accel * dt, float3(0.0f, 0.0f, 0.0f));
                p.vel = clamp(p.vel, float3(-80.0f, -80.0f, -80.0f), float3(80.0f, 80.0f, 80.0f));
                p.pos = Sanitize3(p.pos + p.vel * dt, g_emitterPosAndRadius.xyz);

                // Ground elastic bounce restitution
                if (enableBounce && p.pos.y < groundY) {
                    const float pen = groundY - p.pos.y;
                    p.pos.y = groundY + pen * restitution;
                    if (p.vel.y < 0.0f) {
                        p.vel.y = -p.vel.y * restitution;
                        p.vel.x *= 0.88f;
                        p.vel.z *= 0.88f;
                        p.state |= 2u;
                        p.bounceCount += 1u;
                    }
                }
            }
        }

        // Strict NaN/Inf sanitization before store
        p.pos     = Sanitize3(p.pos, g_emitterPosAndRadius.xyz);
        p.vel     = Sanitize3(p.vel, float3(0.0f, 0.0f, 0.0f));
        p.color   = max(Sanitize3(p.color, float3(1.0f, 1.0f, 1.0f)), float3(0.0f, 0.0f, 0.0f));
        p.age     = max(SanitizeFinite(p.age, 0.0f), 0.0f);
        p.maxLife = max(SanitizeFinite(p.maxLife, 1.0f), 0.05f);
        p.size    = clamp(SanitizeFinite(p.size, 0.04f), 0.005f, 1.0f);

        g_particles[tid.x] = p;

        if ((p.state & 1u) != 0u) InterlockedAdd(s_grpActive, 1u);
        if ((p.state & 2u) != 0u) InterlockedAdd(s_grpBounced, 1u);
        if (respawned)            InterlockedAdd(s_grpRespawned, 1u);
    }

    GroupMemoryBarrierWithGroupSync();
    if (gIdx == 0u) {
        if (s_grpActive    > 0u) g_stats.InterlockedAdd(0u, s_grpActive);
        if (s_grpBounced   > 0u) g_stats.InterlockedAdd(4u, s_grpBounced);
        if (s_grpRespawned > 0u) g_stats.InterlockedAdd(8u, s_grpRespawned);
    }
}

[numthreads(64, 1, 1)]
void cs_particles_splat(uint3 tid : SV_DispatchThreadID, uint gIdx : SV_GroupIndex)
{
    if (gIdx == 0u) {
        s_grpSplatted = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint count  = g_countsAndFlags.x;
    const uint splatW = g_splatDims.x;
    const uint splatH = g_splatDims.y;

    if (tid.x < count && splatW > 0u && splatH > 0u) {
        const GpuParticle p = g_particles[tid.x];
        if ((p.state & 1u) != 0u) {
            const float4 clip = mul(float4(p.pos, 1.0f), g_viewProj);
            if (clip.w > 0.05f) {
                const float invW = 1.0f / clip.w;
                const float3 ndc = clip.xyz * invW;
                if (ndc.x >= -1.0f && ndc.x <= 1.0f &&
                    ndc.y >= -1.0f && ndc.y <= 1.0f &&
                    ndc.z >=  0.0f && ndc.z <= 1.0f) {
                    const uint px = min(uint((ndc.x * 0.5f + 0.5f) * float(splatW)), splatW - 1u);
                    const uint py = min(uint((0.5f - ndc.y * 0.5f) * float(splatH)), splatH - 1u);

                    const float life01 = saturate(p.age / max(p.maxLife, 1.0e-4f));
                    const float fadeIn  = max(smoothstep(0.0f, 0.08f, life01), 0.35f);
                    const float fadeOut = 1.0f - smoothstep(0.72f, 1.0f, life01);
                    const float fade    = saturate(fadeIn * fadeOut);

                    // Embers cool from bright gold-orange to deep red over lifespan
                    float3 col = p.color;
                    if (p.mode == 0u) {
                        col = lerp(col, float3(1.6f, 0.22f, 0.03f), life01 * 0.75f);
                    }
                    const float intensity = max(SanitizeFinite(g_modeParams.w, 1.0f), 0.0f);
                    const float3 hdr = clamp(col * (fade * intensity), float3(0.0f, 0.0f, 0.0f), float3(64.0f, 64.0f, 64.0f));

                    // Q22.10 fixed-point race-free atomic additive splat
                    const uint rQ = uint(hdr.r * 1024.0f + 0.5f);
                    const uint gQ = uint(hdr.g * 1024.0f + 0.5f);
                    const uint bQ = uint(hdr.b * 1024.0f + 0.5f);
                    const uint aQ = max(uint(fade * 1024.0f + 0.5f), 1u);

                    const uint byteOff = (py * splatW + px) * 16u;
                    if (rQ > 0u) g_splatBuf.InterlockedAdd(byteOff +  0u, rQ);
                    if (gQ > 0u) g_splatBuf.InterlockedAdd(byteOff +  4u, gQ);
                    if (bQ > 0u) g_splatBuf.InterlockedAdd(byteOff +  8u, bQ);
                    g_splatBuf.InterlockedAdd(byteOff + 12u, aQ);

                    InterlockedAdd(s_grpSplatted, 1u);
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (gIdx == 0u && s_grpSplatted > 0u) {
        g_stats.InterlockedAdd(12u, s_grpSplatted);
    }
}
