// RawWildlifeBoids.cpp: Clean-Room O(N) 3D Spatial-Hash Wildlife Boids & Ecological Fauna Engine.
// Original work, MIT. Zero Windows / D3D12 / proprietary SDK dependencies.

#include "RawWildlifeBoids.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace raw::worldgen {
namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline bool IsFiniteVec3(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline float LengthSq(Vec3 v) noexcept {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

inline Vec3 ClampMagnitude(Vec3 v, float maxMag) noexcept {
    const float l2 = LengthSq(v);
    if (l2 > maxMag * maxMag && l2 > 1.0e-10f) {
        return v * (maxMag / std::sqrt(l2));
    }
    return v;
}

inline Vec3 NormalizeSafe(Vec3 v, Vec3 fallback = {1.0f, 0.0f, 0.0f}) noexcept {
    const float l2 = LengthSq(v);
    return (l2 > 1.0e-10f) ? (v / std::sqrt(l2)) : fallback;
}

inline uint32_t NextRng(uint32_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

inline float Rng01(uint32_t& state) noexcept {
    return static_cast<float>(NextRng(state) & 0x00FFFFFFu) / 16777216.0f;
}

}  // namespace

void RawWildlifeBoids::Clear() {
    agents_.clear();
    stimuli_.clear();
    nextInBucket_.clear();
    steerAccel_.clear();
    nextId_        = 1u;
    lastTelemetry_ = {};
}

uint32_t RawWildlifeBoids::HashCell3I(int32_t cx, int32_t cy, int32_t cz) noexcept {
    const uint32_t ux = static_cast<uint32_t>(cx) * 73856093u;
    const uint32_t uy = static_cast<uint32_t>(cy) * 19349663u;
    const uint32_t uz = static_cast<uint32_t>(cz) * 83492791u;
    return (ux ^ uy ^ uz) & (kHashBuckets - 1u);
}

void RawWildlifeBoids::RebuildSpatialHash(float invCellSize) {
    bucketHead_.fill(-1);
    const size_t n = agents_.size();
    nextInBucket_.assign(n, -1);

    for (size_t i = 0u; i < n; ++i) {
        const Vec3& p = agents_[i].position;
        const int32_t cx = static_cast<int32_t>(std::floor(p.x * invCellSize));
        const int32_t cy = static_cast<int32_t>(std::floor(p.y * invCellSize));
        const int32_t cz = static_cast<int32_t>(std::floor(p.z * invCellSize));
        const uint32_t b = HashCell3I(cx, cy, cz);
        nextInBucket_[i] = bucketHead_[b];
        bucketHead_[b]   = static_cast<int32_t>(i);
    }
}

uint32_t RawWildlifeBoids::SpawnGuild(FaunaGuild guild, uint32_t count, Vec3 centerPos,
                                      float spreadRadiusU, uint32_t seed) {
    if (count == 0u || !IsFiniteVec3(centerPos) || agents_.size() >= kMaxTotalAgents) {
        return 0u;
    }
    const uint32_t remaining = kMaxTotalAgents - static_cast<uint32_t>(agents_.size());
    const uint32_t toSpawn   = std::min(count, remaining);
    const float    spread    = std::isfinite(spreadRadiusU) ? std::max(10.0f, spreadRadiusU) : 120.0f;

    uint32_t rng = (seed == 0u) ? 0x9E3779B9u : seed;
    agents_.reserve(agents_.size() + toSpawn);

    for (uint32_t i = 0u; i < toSpawn; ++i) {
        const float ang = Rng01(rng) * kTwoPi;
        const float rad = spread * std::sqrt(Rng01(rng));
        const float zOff = (Rng01(rng) - 0.5f) * 2.0f;

        WildlifeAgent a{};
        a.id         = nextId_++;
        a.guild      = guild;
        a.homeAnchor = centerPos;
        a.phaseRad   = Rng01(rng) * kTwoPi;

        if (guild == FaunaGuild::AlpineHawk) {
            a.position = {centerPos.x + std::cos(ang) * rad,
                          centerPos.y + std::sin(ang) * rad,
                          centerPos.z + zOff * 120.0f};
            a.velocity = {-std::sin(ang) * 260.0f, std::cos(ang) * 260.0f, 0.0f};
        } else if (guild == FaunaGuild::PineCrowFlock) {
            a.position = {centerPos.x + std::cos(ang) * rad,
                          centerPos.y + std::sin(ang) * rad,
                          centerPos.z + zOff * 45.0f};
            a.velocity = {std::cos(ang) * 180.0f, std::sin(ang) * 180.0f, 0.0f};
        } else if (guild == FaunaGuild::TundraDeerHerd) {
            a.position = {centerPos.x + std::cos(ang) * rad,
                          centerPos.y + std::sin(ang) * rad,
                          centerPos.z};
            a.velocity = {std::cos(ang) * 75.0f, std::sin(ang) * 75.0f, 0.0f};
        } else {
            // RiverSalmonSchool
            a.position = {centerPos.x + std::cos(ang) * rad,
                          centerPos.y + std::sin(ang) * rad,
                          centerPos.z + zOff * 15.0f};
            a.velocity = {-90.0f, (Rng01(rng) - 0.5f) * 20.0f, 0.0f};
        }

        agents_.push_back(a);
    }
    return toSpawn;
}

void RawWildlifeBoids::EmitStimulus(Vec3 pos, float radiusU, StimulusKind kind,
                                    float strength01, float durationSec) {
    if (!IsFiniteVec3(pos) || !std::isfinite(radiusU) || radiusU <= 0.0f ||
        !std::isfinite(durationSec) || durationSec <= 0.0f) {
        return;
    }
    WildlifeStimulus s{};
    s.position   = pos;
    s.radiusU    = radiusU;
    s.kind       = kind;
    s.strength01 = Clamp01(std::isfinite(strength01) ? strength01 : 1.0f);
    s.ttlSec     = std::min(durationSec, 30.0f);
    stimuli_.push_back(s);
}

WildlifeTelemetry RawWildlifeBoids::Step(float dtSec,
                                         const WildlifeEnvInput& env,
                                         const RawWorldPhysics* physics,
                                         const RawWorldEcosystem* eco) {
    if (!std::isfinite(dtSec) || dtSec <= 0.0f || agents_.empty()) {
        lastTelemetry_.activeAgents = static_cast<uint32_t>(agents_.size());
        lastTelemetry_.stepMs       = 0.0;
        return lastTelemetry_;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const float dt = std::min(dtSec, 0.10f);

    const float solar01  = Clamp01(std::isfinite(env.solarInsolation01) ? env.solarInsolation01 : 0.6f);
    const float rain01   = Clamp01(std::isfinite(env.rainIntensity01) ? env.rainIntensity01 : 0.0f);
    const float snow01   = Clamp01(std::isfinite(env.snowIntensity01) ? env.snowIntensity01 : 0.0f);
    const float beaufort = Clamp(std::isfinite(env.windBeaufort) ? env.windBeaufort : 2.0f, 0.0f, 12.0f);
    const float stormSev = std::max(snow01, std::max(rain01 * 0.9f, Clamp01((beaufort - 6.0f) / 5.0f)));
    const bool  severeStorm = (snow01 > 0.55f || rain01 > 0.70f || beaufort > 8.5f);

    const Vec2 windDir    = Normalize(env.windDirXY);
    const Vec2 riverFlow  = Normalize(env.riverCurrentXY);
    const float ecoScaleU = std::max(0.01f, std::isfinite(env.ecoUnitsPerMeter) ? env.ecoUnitsPerMeter : 70.0f);

    // 1. Precompute active wildfire centroid from RawWorldEcosystem ONCE per step (O(gridCells), fast):
    bool  hasEcoFire = false;
    Vec3  ecoFireCenterU{};
    float ecoFireStrength = 0.0f;
    if (eco != nullptr) {
        double sumX = 0.0, sumY = 0.0, sumW = 0.0;
        for (uint32_t gy = 0u; gy < eco->Height(); ++gy) {
            for (uint32_t gx = 0u; gx < eco->Width(); ++gx) {
                const EcoCell* c = eco->CellAt(gx, gy);
                if (c && c->fireIntensity01 > 0.08f) {
                    const Vec2 cm = eco->CellCenterWorldM(gx, gy);
                    const double w = static_cast<double>(c->fireIntensity01);
                    sumX += static_cast<double>(cm.x) * w;
                    sumY += static_cast<double>(cm.y) * w;
                    sumW += w;
                }
            }
        }
        if (sumW > 0.05) {
            hasEcoFire      = true;
            ecoFireCenterU  = Vec3{static_cast<float>((sumX / sumW) * ecoScaleU),
                                   static_cast<float>((sumY / sumW) * ecoScaleU),
                                   0.0f};
            ecoFireStrength = Clamp01(static_cast<float>(sumW) * 0.15f);
        }
    }

    // 2. Rebuild O(N) 3D spatial hash grid (zero allocations once warmed):
    constexpr float kHashCellSizeU    = 280.0f;
    constexpr float kInvHashCellSizeU = 1.0f / kHashCellSizeU;
    RebuildSpatialHash(kInvHashCellSizeU);

    const size_t n = agents_.size();
    steerAccel_.assign(n, Vec3{});

    // 3. Evaluate O(N) Reynolds neighbor flocking + guild-specific ecological steering:
    for (size_t i = 0u; i < n; ++i) {
        WildlifeAgent& a = agents_[i];

        // Terrain & water height sample for this agent:
        float terrainZ = 0.0f;
        float waterZ   = env.defaultWaterLevelZ;
        if (physics != nullptr) {
            physics->SampleTerrainHeight(a.position.x, a.position.y, &terrainZ, nullptr, &waterZ, nullptr);
        }

        // Decay startled timer and check active stimuli:
        if (a.startledTimerSec > 0.0f) {
            a.startledTimerSec = std::max(0.0f, a.startledTimerSec - dt);
        }
        bool triggeredNow = false;
        Vec3 fleeStimVec{};
        for (const auto& st : stimuli_) {
            const Vec3 diff = a.position - st.position;
            const float d2  = LengthSq(diff);
            if (d2 <= st.radiusU * st.radiusU) {
                const float d = std::sqrt(std::max(1.0e-4f, d2));
                const float falloff = (1.0f - d / st.radiusU) * st.strength01;
                if (a.guild == FaunaGuild::PineCrowFlock ||
                    a.guild == FaunaGuild::TundraDeerHerd ||
                    (a.guild == FaunaGuild::AlpineHawk && st.kind == StimulusKind::LoudNoise)) {
                    a.startledTimerSec = std::max(a.startledTimerSec, 2.2f);
                    triggeredNow       = true;
                    fleeStimVec = fleeStimVec + (diff / d) * (falloff * 420.0f);
                }
            }
        }
        a.startled = (a.startledTimerSec > 0.01f);

        // O(1) expected neighbor query via 3x3x3 spatial hash buckets (capped at 16 same-guild neighbors):
        const float neighborRad = (a.guild == FaunaGuild::RiverSalmonSchool) ? 140.0f : 260.0f;
        const float sepRad      = (a.guild == FaunaGuild::RiverSalmonSchool) ? 45.0f  : 85.0f;
        const float nbRad2      = neighborRad * neighborRad;
        const float sepRad2     = sepRad * sepRad;

        const int32_t bcx = static_cast<int32_t>(std::floor(a.position.x * kInvHashCellSizeU));
        const int32_t bcy = static_cast<int32_t>(std::floor(a.position.y * kInvHashCellSizeU));
        const int32_t bcz = static_cast<int32_t>(std::floor(a.position.z * kInvHashCellSizeU));

        Vec3 sepSum{}, alignSum{}, cohSum{};
        uint32_t nbCount = 0u;
        bool neighborStartled = false;

        for (int32_t dz = -1; dz <= 1 && nbCount < 16u; ++dz) {
            for (int32_t dy = -1; dy <= 1 && nbCount < 16u; ++dy) {
                for (int32_t dx = -1; dx <= 1 && nbCount < 16u; ++dx) {
                    const uint32_t bucket = HashCell3I(bcx + dx, bcy + dy, bcz + dz);
                    for (int32_t j = bucketHead_[bucket]; j >= 0 && nbCount < 16u; j = nextInBucket_[static_cast<size_t>(j)]) {
                        if (static_cast<size_t>(j) == i) continue;
                        const WildlifeAgent& b = agents_[static_cast<size_t>(j)];
                        if (b.guild != a.guild) continue;
                        const Vec3 diff = a.position - b.position;
                        const float d2  = LengthSq(diff);
                        if (d2 > nbRad2) continue;

                        ++nbCount;
                        alignSum = alignSum + b.velocity;
                        cohSum   = cohSum + b.position;
                        if (d2 < sepRad2 && d2 > 1.0e-5f) {
                            const float d = std::sqrt(d2);
                            sepSum = sepSum + (diff / d) * ((sepRad - d) / sepRad);
                        }
                        if (b.startled) {
                            neighborStartled = true;
                        }
                    }
                }
            }
        }

        // Propagate startle wave through cohesive crow flocks & deer herds:
        if (neighborStartled && !a.startled &&
            (a.guild == FaunaGuild::PineCrowFlock || a.guild == FaunaGuild::TundraDeerHerd)) {
            a.startledTimerSec = std::max(a.startledTimerSec, 1.5f);
            a.startled         = true;
        }

        Vec3 accel{};
        if (nbCount > 0u) {
            const float invN = 1.0f / static_cast<float>(nbCount);
            const Vec3 avgVel = alignSum * invN;
            const Vec3 avgPos = cohSum * invN;
            accel = accel + sepSum * 260.0f
                          + (avgVel - a.velocity) * 1.15f
                          + (avgPos - a.position) * 0.55f;
        }

        // Guild-specific first-principles dynamics:
        if (a.guild == FaunaGuild::AlpineHawk) {
            const Vec2 toHome{a.homeAnchor.x - a.position.x, a.homeAnchor.y - a.position.y};
            const float distHome = Length(toHome);
            const Vec2 radial = (distHome > 1.0f) ? (toHome * (1.0f / distHome)) : Vec2{1.0f, 0.0f};
            const Vec2 tangent{-radial.y, radial.x};

            if (severeStorm) {
                // Blizzard / severe storm forces hawk to dive cleanly to sheltered treeline roost:
                const float roostZ = terrainZ + 140.0f;
                const float dz = roostZ - a.position.z;
                accel.x += toHome.x * 0.35f - a.velocity.x * 2.0f;
                accel.y += toHome.y * 0.35f - a.velocity.y * 2.0f;
                accel.z += Clamp(dz * 3.0f, -540.0f, 220.0f) - a.velocity.z * 0.9f;
                a.roosting = (a.position.z <= roostZ + 80.0f);
            } else {
                a.roosting = false;
                // Thermal soaring circle around home mountain updraft:
                const float orbitR = 520.0f;
                const float radialErr = distHome - orbitR;
                accel.x += tangent.x * 260.0f + radial.x * (radialErr * 0.55f);
                accel.y += tangent.y * 260.0f + radial.y * (radialErr * 0.55f);

                // Sunny thermal updraft lift within [600..2200u] AGL:
                const float agl = a.position.z - terrainZ;
                const float thermalLift = solar01 * (1.0f - stormSev) * 165.0f;
                if (agl < 600.0f) {
                    accel.z += 220.0f + thermalLift;
                } else if (agl > 2200.0f) {
                    accel.z -= 200.0f;
                } else {
                    // Gently ride rising sunny thermals upward toward upper thermal band (~1800u AGL):
                    const float targetThermalZ = terrainZ + 700.0f + solar01 * 1150.0f;
                    accel.z += thermalLift * 0.65f + Clamp((targetThermalZ - a.position.z) * 0.45f, -90.0f, 140.0f);
                }
            }
        } else if (a.guild == FaunaGuild::PineCrowFlock) {
            if (severeStorm && !a.startled) {
                a.roosting = true;
                const float perchZ = terrainZ + 140.0f;
                accel = accel + Vec3{-a.velocity.x * 2.0f, -a.velocity.y * 2.0f,
                                     Clamp((perchZ - a.position.z) * 2.0f, -220.0f, 180.0f)};
            } else {
                a.roosting = false;
                if (a.startled) {
                    // Explosive upward acoustic/predator flush!
                    accel = accel + fleeStimVec + Vec3{0.0f, 0.0f, 380.0f};
                    if (triggeredNow) {
                        a.velocity.z = std::max(a.velocity.z, 220.0f);
                    }
                } else {
                    // Canopy cruise band [220..520u] AGL around home grove:
                    const float cruiseZ = terrainZ + 340.0f + std::sin(a.phaseRad) * 60.0f;
                    const Vec2 toHome{a.homeAnchor.x - a.position.x, a.homeAnchor.y - a.position.y};
                    accel.x += toHome.x * 0.18f;
                    accel.y += toHome.y * 0.18f;
                    accel.z += Clamp((cruiseZ - a.position.z) * 1.4f, -180.0f, 180.0f);
                }
            }
        } else if (a.guild == FaunaGuild::TundraDeerHerd) {
            a.roosting = false;
            // Wildfire evasion (flee away from active fire + slightly upwind):
            if (hasEcoFire) {
                const Vec2 awayFire{a.position.x - ecoFireCenterU.x, a.position.y - ecoFireCenterU.y};
                const float fireDist = Length(awayFire);
                if (fireDist < 2800.0f) {
                    const Vec2 awayDir = (fireDist > 1.0f) ? (awayFire * (1.0f / fireDist)) : Vec2{-windDir.x, -windDir.y};
                    const Vec2 upwind{-windDir.x, -windDir.y};
                    const Vec2 evadeDir = Normalize(awayDir + upwind * 0.35f);
                    a.startledTimerSec = std::max(a.startledTimerSec, 2.0f);
                    a.startled         = true;
                    accel.x += evadeDir.x * (520.0f * std::max(0.5f, ecoFireStrength));
                    accel.y += evadeDir.y * (520.0f * std::max(0.5f, ecoFireStrength));
                }
            }
            if (a.startled) {
                accel.x += fleeStimVec.x;
                accel.y += fleeStimVec.y;
            } else {
                // Gentle grazing drift near home meadow:
                const Vec2 toHome{a.homeAnchor.x - a.position.x, a.homeAnchor.y - a.position.y};
                accel.x += toHome.x * 0.12f;
                accel.y += toHome.y * 0.12f;
            }
            accel.z = 0.0f;
        } else {
            // RiverSalmonSchool: cohesive schooling swimming upstream against riverFlow, strictly depth-confined!
            a.roosting = false;
            const Vec2 upstream{-riverFlow.x, -riverFlow.y};
            accel.x += upstream.x * 130.0f;
            accel.y += upstream.y * 130.0f;
            // Gentle tether to river reach around homeAnchor:
            const Vec2 toHome{a.homeAnchor.x - a.position.x, a.homeAnchor.y - a.position.y};
            if (Length(toHome) > 450.0f) {
                accel.x += toHome.x * 0.25f;
                accel.y += toHome.y * 0.25f;
            }
            const float minSalmonZ = terrainZ + 12.0f;
            const float maxSalmonZ = waterZ - 15.0f;
            const float midZ       = 0.5f * (minSalmonZ + maxSalmonZ);
            accel.z += Clamp((midZ - a.position.z) * 2.5f, -120.0f, 120.0f);
        }

        steerAccel_[i] = ClampMagnitude(accel, 900.0f);
    }

    // 4. Integrate positions & enforce exact physical domain constraints:
    uint32_t roostingCount = 0u;
    uint32_t startledCount = 0u;
    double   speedSum      = 0.0;

    for (size_t i = 0u; i < n; ++i) {
        WildlifeAgent& a = agents_[i];
        const Vec3 oldVel = a.velocity;
        a.velocity = a.velocity + steerAccel_[i] * dt;
        a.phaseRad += dt * 1.7f;

        float minSpd = 20.0f, maxSpd = 320.0f;
        if (a.guild == FaunaGuild::AlpineHawk) {
            minSpd = a.roosting ? 0.0f : 160.0f;
            maxSpd = a.roosting ? 90.0f : 380.0f;
        } else if (a.guild == FaunaGuild::PineCrowFlock) {
            minSpd = a.roosting ? 0.0f : (a.startled ? 280.0f : 110.0f);
            maxSpd = a.roosting ? 60.0f : (a.startled ? 480.0f : 260.0f);
        } else if (a.guild == FaunaGuild::TundraDeerHerd) {
            a.velocity.z = 0.0f;
            minSpd = a.startled ? 220.0f : 25.0f;
            maxSpd = a.startled ? 420.0f : 140.0f;
        } else {
            minSpd = 55.0f;
            maxSpd = 175.0f;
        }

        const float spd = Length(a.velocity);
        if (spd > maxSpd && spd > 1.0e-5f) {
            a.velocity = a.velocity * (maxSpd / spd);
        } else if (spd < minSpd && minSpd > 0.0f) {
            const Vec3 dir = NormalizeSafe(a.velocity, {1.0f, 0.0f, 0.0f});
            a.velocity = dir * minSpd;
        }

        a.position = a.position + a.velocity * dt;

        // Post-integration domain constraints (exact heightfield lock / aquatic depth box / flight floor):
        float terrainZ = 0.0f;
        float waterZ   = env.defaultWaterLevelZ;
        if (physics != nullptr) {
            physics->SampleTerrainHeight(a.position.x, a.position.y, &terrainZ, nullptr, &waterZ, nullptr);
        }

        if (a.guild == FaunaGuild::TundraDeerHerd) {
            a.position.z = terrainZ;
            a.velocity.z = 0.0f;
        } else if (a.guild == FaunaGuild::RiverSalmonSchool) {
            const float loZ = terrainZ + 12.0f;
            const float hiZ = waterZ - 15.0f;
            if (loZ <= hiZ) {
                if (a.position.z < loZ) { a.position.z = loZ; a.velocity.z = std::max(0.0f, a.velocity.z); }
                if (a.position.z > hiZ) { a.position.z = hiZ; a.velocity.z = std::min(0.0f, a.velocity.z); }
            } else {
                a.position.z = 0.5f * (terrainZ + waterZ);
                a.velocity.z = 0.0f;
            }
        } else if (a.guild == FaunaGuild::AlpineHawk) {
            const float minHawkZ = a.roosting ? (terrainZ + 90.0f) : (terrainZ + 180.0f);
            if (a.position.z < minHawkZ) {
                a.position.z = minHawkZ;
                a.velocity.z = std::max(0.0f, a.velocity.z);
            }
            // Compute smooth aerodynamic bank angle from lateral centripetal turn acceleration:
            const Vec3 dv = (a.velocity - oldVel) / dt;
            const Vec2 fwd = Normalize(Vec2{a.velocity.x, a.velocity.y});
            const float latAccel = -fwd.y * dv.x + fwd.x * dv.y;
            a.bankAngleRad = Clamp(std::atan2(latAccel, 980.0f), -0.85f, 0.85f);
        } else {
            // PineCrowFlock: never dip underground
            const float minCrowZ = terrainZ + 40.0f;
            if (a.position.z < minCrowZ) {
                a.position.z = minCrowZ;
                a.velocity.z = std::max(0.0f, a.velocity.z);
            }
        }

        if (!IsFiniteVec3(a.position)) a.position = a.homeAnchor;
        if (!IsFiniteVec3(a.velocity)) a.velocity = {};

        if (a.roosting) ++roostingCount;
        if (a.startled) ++startledCount;
        speedSum += static_cast<double>(Length(a.velocity));
    }

    // 5. Age out expired stimuli:
    for (auto& st : stimuli_) {
        st.ttlSec -= dt;
    }
    stimuli_.erase(
        std::remove_if(stimuli_.begin(), stimuli_.end(), [](const WildlifeStimulus& s) {
            return s.ttlSec <= 0.0f;
        }),
        stimuli_.end());

    const auto t1 = std::chrono::steady_clock::now();
    lastTelemetry_.activeAgents  = static_cast<uint32_t>(n);
    lastTelemetry_.roostingCount = roostingCount;
    lastTelemetry_.startledCount = startledCount;
    lastTelemetry_.meanSpeed     = static_cast<float>(speedSum / static_cast<double>(n));
    lastTelemetry_.stepMs        = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return lastTelemetry_;
}

}  // namespace raw::worldgen
