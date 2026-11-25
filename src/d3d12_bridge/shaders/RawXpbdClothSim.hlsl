// RawXpbdClothSim.hlsl: Real-Time GPU Compute Extended Position Based Dynamics (XPBD, Macklin et al. 2016)
// Multi-Chain Soft-Body, Hair Strand, Cloth Cape & Articulated Bush-Bone Foliage Solver (SM 6.6 / 6.10).
//
// Execution layout:
//   - [numthreads(64, 1, 1)]: each threadgroup simulates 4 independent chains x up to 16 nodes in groupshared LDS.
//   - Up to 256 chains x 16 nodes = 4,096 particles execute in ONE SINGLE compute dispatch with zero cross-dispatch stalls!
//
// Per substep (default 4 substeps/frame):
//   1. External forces: Z-up gravity (scaled by winter snowLoad01 mass multiplier) + Beaufort gust & 3D curl-noise wind
//      drag + exponential velocity damping.
//   2. Root pinning (invMass == 0) + optional rest-pose elastic spring-back (articulated bush branches & hair roots).
//   3. XPBD Distance Constraints (C = |p_i - p_{i-1}| - restLen, alphaTilde = compliance / subDt^2) + 2-hop Bending
//      Stiffness constraints (preventing hair/cape/branch kinking).
//   4. Analytic Sphere & Capsule Collision Projection (up to 8 live colliders: actor torso/head capsules + player wake sphere).
//   5. Exact velocity derivation v = (p - pPrev) / subDt + finite NaN/Inf velocity clamp.
//
// Original clean-room implementation, MIT License.

static const uint kMaxNodesPerChain = 16u;
static const uint kChainsPerGroup   = 4u;
static const uint kGroupThreads     = 64u; // 4 chains * 16 nodes

struct XpbdParticle {
    float3 pos;
    float  invMass;     // 0.0 = pinned root node
    float3 prevPos;
    float  restLen;     // segment length to parent node (i - 1)
    float3 vel;
    float  bendRestLen; // 2-hop rest chord length between (i - 1) and (i + 1)
    float3 restPos;     // reference rest-pose world position (for bush/hair spring-back)
    float  dragCoeff;   // aerodynamic wind drag multiplier
};

struct XpbdChainMeta {
    uint  startParticle;
    uint  nodeCount;           // 2..16
    uint  chainType;           // 0 = HairStrand, 1 = ClothCape, 2 = ArticulatedBush
    uint  flags;
    float distCompliance;      // XPBD distance compliance (m/N)
    float bendCompliance;      // XPBD 2-hop bending compliance
    float restSpringStiffness; // elastic rest-pose recovery gain [0..1]
    float damping;             // exponential velocity damping (1/s)
};

struct XpbdCollider {
    float3 p0;     // sphere center OR capsule endpoint A
    float  radius; // > 0 active; <= 0 disabled
    float3 p1;     // capsule endpoint B
    float  flags;  // 0 = sphere, 1 = capsule
};

cbuffer XpbdStepCB : register(b0)
{
    uint4        counts;       // x = chainCount, y = particleCount, z = substeps (1..8), w = colliderCount (0..8)
    float4       timeAndGrav;  // x = dt (s), y = timeSec, z = gravityZ, w = snowLoad01 [0..1]
    float4       windParams;   // xyz = unit windDir, w = windSpeedMps
    float4       gustAndClamp; // x = gustPhase, y = maxSpeed, z = collisionMargin, w = unused
    XpbdCollider colliders[8];
};

StructuredBuffer<XpbdChainMeta>   ChainMetaBuf : register(t0);
RWStructuredBuffer<XpbdParticle>  ParticleBuf  : register(u0);

// Groupshared per-chain simulation state (4 chains x 16 nodes = 64 entries)
groupshared float3 s_pos[kGroupThreads];
groupshared float3 s_prevPos[kGroupThreads];
groupshared float3 s_restPos[kGroupThreads];
groupshared float  s_invMass[kGroupThreads];
groupshared float  s_restLen[kGroupThreads];
groupshared float  s_bendLen[kGroupThreads];
groupshared float  s_lambdaDist[kGroupThreads];
groupshared float  s_lambdaBend[kGroupThreads];

// ----------------------------------------------------------------------------
// Pure polynomial 3D analytical curl-noise for turbulent wind gusts (vendor-invariant)
// ----------------------------------------------------------------------------
float xpbdHash31(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float4 xpbdNoise3Grad(float3 x)
{
    const float3 i = floor(x);
    const float3 f = frac(x);
    const float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float3 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

    const float a  = xpbdHash31(i + float3(0.0f, 0.0f, 0.0f));
    const float b  = xpbdHash31(i + float3(1.0f, 0.0f, 0.0f));
    const float c  = xpbdHash31(i + float3(0.0f, 1.0f, 0.0f));
    const float d  = xpbdHash31(i + float3(1.0f, 1.0f, 0.0f));
    const float e  = xpbdHash31(i + float3(0.0f, 0.0f, 1.0f));
    const float f1 = xpbdHash31(i + float3(1.0f, 0.0f, 1.0f));
    const float g  = xpbdHash31(i + float3(0.0f, 1.0f, 1.0f));
    const float h  = xpbdHash31(i + float3(1.0f, 1.0f, 1.0f));

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

float3 xpbdCurlWind3D(float3 p)
{
    const float3 g0 = xpbdNoise3Grad(p + float3( 0.0f,  0.0f,  0.0f)).yzw;
    const float3 g1 = xpbdNoise3Grad(p + float3(19.3f,  7.1f, 31.7f)).yzw;
    const float3 g2 = xpbdNoise3Grad(p + float3(41.9f, 23.5f, 11.3f)).yzw;
    return float3(g2.y - g1.z, g0.z - g2.x, g1.x - g0.y);
}

// ----------------------------------------------------------------------------
// Analytic Sphere & Capsule Collision Projection (exact non-penetration)
// ----------------------------------------------------------------------------
float3 projectAnalyticColliders(float3 p, uint colliderCount, float margin)
{
    [loop] for (uint ci = 0u; ci < colliderCount; ++ci) {
        const float r = colliders[ci].radius;
        if (r <= 1.0e-5f) {
            continue;
        }
        float3 closestPt = colliders[ci].p0;
        if (colliders[ci].flags > 0.5f) {
            const float3 ab = colliders[ci].p1 - colliders[ci].p0;
            const float ab2 = dot(ab, ab);
            if (ab2 > 1.0e-6f) {
                const float t = saturate(dot(p - colliders[ci].p0, ab) / ab2);
                closestPt = colliders[ci].p0 + ab * t;
            }
        }
        const float3 diff = p - closestPt;
        const float dist = length(diff);
        const float minDist = r + margin;
        if (dist < minDist) {
            const float3 nrm = (dist > 1.0e-5f) ? (diff / dist) : float3(0.0f, 0.0f, 1.0f);
            p = closestPt + nrm * minDist;
        }
    }
    return p;
}

// ----------------------------------------------------------------------------
// Main XPBD Compute Kernel: cs_xpbd_step
// ----------------------------------------------------------------------------
[numthreads(64, 1, 1)]
void cs_xpbd_step(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    const uint lane       = gtid.x;
    const uint localChain = lane >> 4u;        // 0..3
    const uint nodeIdx    = lane & 15u;        // 0..15
    const uint chainIdx   = gid.x * kChainsPerGroup + localChain;
    const uint chainBase  = localChain << 4u;  // 0, 16, 32, 48

    const bool chainValid = (chainIdx < counts.x);
    XpbdChainMeta meta;
    meta.startParticle       = 0u;
    meta.nodeCount           = 0u;
    meta.chainType           = 0u;
    meta.flags               = 0u;
    meta.distCompliance      = 1.0e-6f;
    meta.bendCompliance      = 2.0e-3f;
    meta.restSpringStiffness = 0.0f;
    meta.damping             = 1.5f;

    if (chainValid) {
        meta = ChainMetaBuf[chainIdx];
        meta.nodeCount = min(meta.nodeCount, kMaxNodesPerChain);
    }

    const uint globalIdx = meta.startParticle + nodeIdx;
    const bool nodeActive = chainValid && (nodeIdx < meta.nodeCount) && (globalIdx < counts.y);

    float3 vel = float3(0.0f, 0.0f, 0.0f);
    float  dragCoeff = 1.0f;

    if (nodeActive) {
        const XpbdParticle pt = ParticleBuf[globalIdx];
        s_pos[lane]     = pt.pos;
        s_prevPos[lane] = pt.pos;
        s_restPos[lane] = pt.restPos;
        s_invMass[lane] = (nodeIdx == 0u) ? 0.0f : max(pt.invMass, 0.0f);
        s_restLen[lane] = max(pt.restLen, 1.0e-4f);
        s_bendLen[lane] = max(pt.bendRestLen, 1.0e-4f);
        vel             = pt.vel;
        dragCoeff       = max(pt.dragCoeff, 0.0f);
    } else {
        s_pos[lane]     = float3(0.0f, 0.0f, 0.0f);
        s_prevPos[lane] = float3(0.0f, 0.0f, 0.0f);
        s_restPos[lane] = float3(0.0f, 0.0f, 0.0f);
        s_invMass[lane] = 0.0f;
        s_restLen[lane] = 1.0f;
        s_bendLen[lane] = 2.0f;
    }
    s_lambdaDist[lane] = 0.0f;
    s_lambdaBend[lane] = 0.0f;

    GroupMemoryBarrierWithGroupSync();

    const uint  substeps      = clamp(counts.z, 1u, 8u);
    const uint  colliderCount = min(counts.w, 8u);
    const float frameDt       = clamp(timeAndGrav.x, 1.0e-4f, 0.05f);
    const float subDt         = frameDt / float(substeps);
    const float invSubDt      = 1.0f / subDt;
    const float invSubDt2     = invSubDt * invSubDt;
    const float timeSec       = timeAndGrav.y;
    const float gravZ         = timeAndGrav.z;
    const float snowLoad01    = saturate(timeAndGrav.w);

    const float  wLen         = length(windParams.xyz);
    const float3 windDir      = (wLen > 1.0e-4f) ? (windParams.xyz / wLen) : float3(1.0f, 0.0f, 0.0f);
    const float  windSpeed    = max(windParams.w, 0.0f);
    const float  gustPhase    = gustAndClamp.x;
    const float  maxSpeed     = max(gustAndClamp.y, 1.0f);
    const float  colMargin    = max(gustAndClamp.z, 0.0f);

    const float alphaDistTilde = max(meta.distCompliance, 0.0f) * invSubDt2;
    const float alphaBendTilde = max(meta.bendCompliance, 1.0e-6f) * invSubDt2;
    const float dampFactor     = exp(-max(meta.damping, 0.0f) * subDt);
    // Winter snow-load increases effective downward gravity mass on foliage & capes
    const float snowMassMult   = 1.0f + snowLoad01 * ((meta.chainType == 2u) ? 0.95f : 0.45f);

    [loop] for (uint sub = 0u; sub < substeps; ++sub) {
        const float subTime = timeSec + float(sub) * subDt;

        // 1) Semi-implicit Euler prediction with gravity, Beaufort gust + 3D curl wind, and snow load
        if (nodeActive) {
            const float wInv = s_invMass[lane];
            float3 p = s_pos[lane];
            s_prevPos[lane] = p;
            s_lambdaDist[lane] = 0.0f;
            s_lambdaBend[lane] = 0.0f;

            if (wInv > 0.0f) {
                // Beaufort gust envelope + divergence-free 3D curl turbulence
                const float gustWave = 1.0f + 0.35f * sin(gustPhase + dot(p, windDir) * 0.45f + float(chainIdx) * 0.37f);
                const float3 curl = xpbdCurlWind3D(p * 0.65f - windDir * (subTime * 1.4f));
                const float3 windVel = windDir * (windSpeed * gustWave) + curl * (windSpeed * 0.28f);

                // Quadratic aerodynamic drag force: C_d * |v_rel| * v_rel
                const float3 vRel = windVel - vel;
                const float  vRelMag = length(vRel);
                const float3 fWind = vRel * (dragCoeff * (0.28f + 0.14f * vRelMag));
                const float3 fGrav = float3(0.0f, 0.0f, gravZ * snowMassMult);

                vel = (vel + (fGrav + fWind * wInv) * subDt) * dampFactor;
                const float vMag = length(vel);
                if (vMag > maxSpeed) {
                    vel *= (maxSpeed / vMag);
                }
                p += vel * subDt;

                // Optional elastic rest-pose spring-back (strong on articulated bush branches, gentle near hair roots)
                if (meta.restSpringStiffness > 1.0e-5f) {
                    const float nodeFrac = float(nodeIdx) / max(float(meta.nodeCount - 1u), 1.0f);
                    const float springWeight = (meta.chainType == 2u)
                        ? (meta.restSpringStiffness * lerp(1.0f, 0.32f, nodeFrac))
                        : (meta.restSpringStiffness * (1.0f - nodeFrac) * (1.0f - nodeFrac));
                    const float kPull = 1.0f - exp(-springWeight * 16.0f * subDt);
                    p = lerp(p, s_restPos[lane], saturate(kPull));
                }

                s_pos[lane] = p;
            } else {
                // Exact root pinning: invMass == 0 never drifts
                s_pos[lane] = s_restPos[lane];
                s_prevPos[lane] = s_restPos[lane];
                vel = float3(0.0f, 0.0f, 0.0f);
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // 2) XPBD Distance & Bending Constraint Projection (2 Gauss-Seidel chain sweeps per substep)
        // Executed per-chain by lane 0 of each 16-lane sub-block in fast LDS (zero race conditions, exact inextensibility).
        if (chainValid && nodeIdx == 0u && meta.nodeCount >= 2u) {
            const uint nNodes = meta.nodeCount;

            [unroll] for (uint iter = 0u; iter < 2u; ++iter) {
                // 2a) Forward root-to-tip XPBD distance constraint sweep
                [loop] for (uint i = 1u; i < nNodes; ++i) {
                    const uint idxA = chainBase + (i - 1u);
                    const uint idxB = chainBase + i;
                    const float wA = s_invMass[idxA];
                    const float wB = s_invMass[idxB];
                    const float wSum = wA + wB;
                    if (wSum <= 1.0e-7f) {
                        continue;
                    }
                    const float3 delta = s_pos[idxB] - s_pos[idxA];
                    const float dist = length(delta);
                    if (dist <= 1.0e-6f) {
                        continue;
                    }
                    const float C = dist - s_restLen[idxB];
                    const float dLambda = (-C - alphaDistTilde * s_lambdaDist[idxB]) / (wSum + alphaDistTilde);
                    s_lambdaDist[idxB] += dLambda;
                    const float3 corr = (delta / dist) * dLambda;
                    s_pos[idxA] -= corr * wA;
                    s_pos[idxB] += corr * wB;
                }

                // 2b) 2-hop XPBD bending stiffness constraint (p_{i-1}, p_{i+1}) preventing sharp kinking
                if (nNodes >= 3u && meta.bendCompliance < 10.0f) {
                    [loop] for (uint i = 1u; i + 1u < nNodes; ++i) {
                        const uint idxPrev = chainBase + (i - 1u);
                        const uint idxCur  = chainBase + i;
                        const uint idxNext = chainBase + (i + 1u);
                        const float wA = s_invMass[idxPrev];
                        const float wB = s_invMass[idxNext];
                        const float wSum = wA + wB;
                        if (wSum <= 1.0e-7f) {
                            continue;
                        }
                        const float3 delta = s_pos[idxNext] - s_pos[idxPrev];
                        const float dist = length(delta);
                        const float restChord = s_bendLen[idxCur];
                        if (dist > 1.0e-6f && dist < restChord) {
                            const float C = dist - restChord;
                            const float dLambda = (-C - alphaBendTilde * s_lambdaBend[idxCur]) / (wSum + alphaBendTilde);
                            s_lambdaBend[idxCur] += dLambda;
                            const float3 corr = (delta / dist) * dLambda;
                            s_pos[idxPrev] -= corr * wA;
                            s_pos[idxNext] += corr * wB;
                        }
                    }
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // 3) Analytic Sphere & Capsule Collision Projection (all 16 nodes in parallel)
        if (nodeActive && s_invMass[lane] > 0.0f && colliderCount > 0u) {
            s_pos[lane] = projectAnalyticColliders(s_pos[lane], colliderCount, colMargin);
        }
        GroupMemoryBarrierWithGroupSync();

        // 4) Final inextensibility + collision polish pass so colliders never over-stretch segments
        if (chainValid && nodeIdx == 0u && meta.nodeCount >= 2u) {
            const uint nNodes = meta.nodeCount;
            [loop] for (uint i = 1u; i < nNodes; ++i) {
                const uint idxA = chainBase + (i - 1u);
                const uint idxB = chainBase + i;
                const float wB = s_invMass[idxB];
                if (wB <= 1.0e-7f) {
                    continue;
                }
                const float3 delta = s_pos[idxB] - s_pos[idxA];
                const float dist = length(delta);
                const float restL = s_restLen[idxB];
                if (dist > 1.0e-6f) {
                    // Tight length enforcement while keeping root-to-tip chain smooth
                    float3 pB = s_pos[idxA] + (delta / dist) * restL;
                    if (colliderCount > 0u) {
                        pB = projectAnalyticColliders(pB, colliderCount, colMargin);
                    }
                    s_pos[idxB] = pB;
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // 5) XPBD Velocity Derivation: v = (p - pPrev) / subDt + finite NaN/Inf velocity clamp
        if (nodeActive) {
            if (s_invMass[lane] > 0.0f) {
                float3 newV = (s_pos[lane] - s_prevPos[lane]) * invSubDt;
                const float spd = length(newV);
                if (!isfinite(spd) || any(isnan(newV))) {
                    newV = float3(0.0f, 0.0f, 0.0f);
                    s_pos[lane] = s_restPos[lane];
                } else if (spd > maxSpeed) {
                    newV *= (maxSpeed / spd);
                }
                vel = newV;
            } else {
                s_pos[lane] = s_restPos[lane];
                vel = float3(0.0f, 0.0f, 0.0f);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Write final converged state back to GPU structured buffer
    if (nodeActive) {
        XpbdParticle outP = ParticleBuf[globalIdx];
        outP.pos     = s_pos[lane];
        outP.prevPos = s_prevPos[lane];
        outP.vel     = vel;
        ParticleBuf[globalIdx] = outP;
    }
}
