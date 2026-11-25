// RawWorldEcosystem.cpp: Coupled Cellular Wildfire Ignition/Wind-Spread, Thermal Snowmelt Runoff,
// Black-Ice Refreezing & Seasonal Biomass Regrowth. Original work, MIT. Zero Windows/D3D12 deps.

#include "RawWorldEcosystem.h"

#include <algorithm>
#include <cmath>

namespace raw::worldgen {
namespace {

constexpr float kSqrt2 = 1.4142135623730950488f;

constexpr int32_t kNbDx[8]   = { 1,  1,  0, -1, -1, -1,  0,  1};
constexpr int32_t kNbDy[8]   = { 0,  1,  1,  1,  0, -1, -1, -1};
constexpr float   kNbDist[8] = {1.0f, kSqrt2, 1.0f, kSqrt2, 1.0f, kSqrt2, 1.0f, kSqrt2};

inline float SanitizeFinite(float v, float fallback = 0.0f) noexcept {
    return std::isfinite(v) ? v : fallback;
}

}  // namespace

RawWorldEcosystem::RawWorldEcosystem() {
    Reset(EcoGridConfig{});
}

RawWorldEcosystem::RawWorldEcosystem(const EcoGridConfig& cfg) {
    Reset(cfg);
}

void RawWorldEcosystem::Reset(const EcoGridConfig& cfg) {
    cfg_ = cfg;
    cfg_.gridW                 = std::max(4u, cfg_.gridW);
    cfg_.gridH                 = std::max(4u, cfg_.gridH);
    cfg_.cellSizeM             = std::max(0.25f, SanitizeFinite(cfg_.cellSizeM, 2.0f));
    cfg_.snowWaterEquivDensity = Clamp(SanitizeFinite(cfg_.snowWaterEquivDensity, 0.30f), 0.05f, 0.90f);

    const size_t total = static_cast<size_t>(cfg_.gridW) * static_cast<size_t>(cfg_.gridH);
    cells_.assign(total, EcoCell{});
    scratch_.resize(total);
    waterDelta_.assign(total, 0.0f);
    lastSteamFlux_ = 0.0f;
}

void RawWorldEcosystem::InitFromTerrainChunk(const TerrainChunkData& chunk, float unitsPerMeter,
                                             float waterLevelZ) {
    if (chunk.stride < 2u || chunk.heights.empty()) {
        return;
    }
    const float invScale = 1.0f / std::max(1.0f, unitsPerMeter);
    cfg_.originX   = chunk.originX * invScale;
    cfg_.originY   = chunk.originY * invScale;
    cfg_.cellSizeM = (chunk.chunkSizeU * invScale) / static_cast<float>(cfg_.gridW);

    for (uint32_t gy = 0u; gy < cfg_.gridH; ++gy) {
        const float v = (static_cast<float>(gy) + 0.5f) / static_cast<float>(cfg_.gridH);
        for (uint32_t gx = 0u; gx < cfg_.gridW; ++gx) {
            const float u = (static_cast<float>(gx) + 0.5f) / static_cast<float>(cfg_.gridW);
            const float hU = chunk.SampleHeightBilinear(u, v);
            EcoCell& c = cells_[static_cast<size_t>(gy) * cfg_.gridW + gx];
            c = EcoCell{};
            c.elevationM   = hU * invScale;
            c.isRiverWater = (hU <= waterLevelZ);
            if (c.isRiverWater) {
                c.fuelLoad01 = 0.0f;
                c.moisture01 = 1.0f;
            } else {
                const uint32_t sx = std::min(chunk.stride - 1u, static_cast<uint32_t>(u * static_cast<float>(chunk.gridN)));
                const uint32_t sy = std::min(chunk.stride - 1u, static_cast<uint32_t>(v * static_cast<float>(chunk.gridN)));
                const size_t sIdx = static_cast<size_t>(sy) * chunk.stride + sx;
                if (sIdx < chunk.wetness.size()) {
                    c.moisture01 = Clamp01(chunk.wetness[sIdx]);
                }
                if (sIdx < chunk.snow.size()) {
                    c.snowDepthM = std::max(0.0f, chunk.snow[sIdx] * 0.4f);
                }
                c.fuelLoad01 = Clamp01(0.75f - c.moisture01 * 0.45f);
            }
        }
    }
    lastSteamFlux_ = 0.0f;
}

EcoCell* RawWorldEcosystem::CellAt(uint32_t gx, uint32_t gy) {
    if (gx >= cfg_.gridW || gy >= cfg_.gridH) return nullptr;
    return &cells_[static_cast<size_t>(gy) * cfg_.gridW + gx];
}

const EcoCell* RawWorldEcosystem::CellAt(uint32_t gx, uint32_t gy) const {
    if (gx >= cfg_.gridW || gy >= cfg_.gridH) return nullptr;
    return &cells_[static_cast<size_t>(gy) * cfg_.gridW + gx];
}

bool RawWorldEcosystem::WorldToCell(float worldX, float worldY, uint32_t* outGx, uint32_t* outGy) const {
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) return false;
    const float fx = (worldX - cfg_.originX) / cfg_.cellSizeM;
    const float fy = (worldY - cfg_.originY) / cfg_.cellSizeM;
    if (fx < 0.0f || fy < 0.0f || fx >= static_cast<float>(cfg_.gridW) || fy >= static_cast<float>(cfg_.gridH)) {
        return false;
    }
    if (outGx) *outGx = static_cast<uint32_t>(fx);
    if (outGy) *outGy = static_cast<uint32_t>(fy);
    return true;
}

Vec2 RawWorldEcosystem::CellCenterWorldM(uint32_t gx, uint32_t gy) const {
    return {
        cfg_.originX + (static_cast<float>(gx) + 0.5f) * cfg_.cellSizeM,
        cfg_.originY + (static_cast<float>(gy) + 0.5f) * cfg_.cellSizeM
    };
}

uint32_t RawWorldEcosystem::IgniteAt(float worldX, float worldY, float radiusM, float heatImpulseC) {
    if (!std::isfinite(worldX) || !std::isfinite(worldY) ||
        !std::isfinite(radiusM) || radiusM <= 0.0f ||
        !std::isfinite(heatImpulseC) || heatImpulseC <= 0.0f) {
        return 0u;
    }

    const float rad2 = radiusM * radiusM;
    uint32_t ignited = 0u;
    for (uint32_t gy = 0u; gy < cfg_.gridH; ++gy) {
        for (uint32_t gx = 0u; gx < cfg_.gridW; ++gx) {
            const Vec2 cc = CellCenterWorldM(gx, gy);
            const float dx = cc.x - worldX;
            const float dy = cc.y - worldY;
            if (dx * dx + dy * dy > rad2) {
                continue;
            }
            EcoCell& c = cells_[static_cast<size_t>(gy) * cfg_.gridW + gx];
            // Strict ignition eligibility: dry enough, has fuel, not river water, not buried in deep snow:
            if (c.isRiverWater || c.fuelLoad01 <= 0.10f || c.moisture01 >= 0.65f || c.snowDepthM > 0.15f) {
                continue;
            }
            c.surfaceTempC = std::min(950.0f, std::max(c.surfaceTempC, 260.0f) + heatImpulseC * 0.6f);
            const float intensity = Clamp01((heatImpulseC / 350.0f) * (1.0f - 0.6f * c.moisture01));
            c.fireIntensity01 = std::max(c.fireIntensity01, std::max(0.35f, intensity));
            ++ignited;
        }
    }
    return ignited;
}

void RawWorldEcosystem::Step(float dtSec, const EcoWeatherInput& wx) {
    if (!std::isfinite(dtSec) || dtSec <= 0.0f) {
        return;
    }

    const float wLen = std::sqrt(wx.windDirX * wx.windDirX + wx.windDirY * wx.windDirY);
    const float windDx = (wLen > 1.0e-5f) ? (wx.windDirX / wLen) : 1.0f;
    const float windDy = (wLen > 1.0e-5f) ? (wx.windDirY / wLen) : 0.0f;
    const float beaufort = Clamp(SanitizeFinite(wx.windBeaufort, 0.0f), 0.0f, 12.0f);
    const float windMps  = 0.836f * std::pow(beaufort, 1.5f);

    const int subSteps = std::clamp(static_cast<int>(std::ceil(dtSec / 0.25f)), 1, 32);
    const float subDt  = dtSec / static_cast<float>(subSteps);

    lastSteamFlux_ = 0.0f;
    for (int s = 0; s < subSteps; ++s) {
        StepSubstep(subDt, wx, windDx, windDy, windMps);
    }
}

void RawWorldEcosystem::StepSubstep(float subDt, const EcoWeatherInput& wx,
                                    float windDx, float windDy, float windMps) {
    const uint32_t W = cfg_.gridW;
    const uint32_t H = cfg_.gridH;
    const float cellArea = cfg_.cellSizeM * cfg_.cellSizeM;
    const float ambientC = SanitizeFinite(wx.ambientTempC, 12.0f);
    const float rain01   = Clamp01(SanitizeFinite(wx.rainRate01, 0.0f));
    const float snow01   = Clamp01(SanitizeFinite(wx.snowRate01, 0.0f));
    const float solar01  = Clamp01(SanitizeFinite(wx.solarInsolation01, 0.5f));
    const float eqTempC  = ambientC + solar01 * 3.5f;

    float stepSteam = 0.0f;

    // -------------------------------------------------------------------------
    // Phase 1: Synchronous Wildfire Propagation, Combustion, Rain Suppression & Thermal Snowmelt
    // -------------------------------------------------------------------------
    for (uint32_t gy = 0u; gy < H; ++gy) {
        for (uint32_t gx = 0u; gx < W; ++gx) {
            const size_t idx = static_cast<size_t>(gy) * W + gx;
            const EcoCell& cur = cells_[idx];
            EcoCell next = cur;

            // Gather radiative + convective heat from burning 8-neighbors (Rothermel wind + slope factors):
            float heatFluxIn = 0.0f;
            for (int n = 0; n < 8; ++n) {
                const int32_t sx = static_cast<int32_t>(gx) + kNbDx[n];
                const int32_t sy = static_cast<int32_t>(gy) + kNbDy[n];
                if (sx < 0 || sy < 0 || sx >= static_cast<int32_t>(W) || sy >= static_cast<int32_t>(H)) {
                    continue;
                }
                const EcoCell& src = cells_[static_cast<size_t>(sy) * W + static_cast<size_t>(sx)];
                if (src.fireIntensity01 <= 0.02f) {
                    continue;
                }

                // Unit vector from burning source (sx, sy) -> target (gx, gy):
                const float invD = 1.0f / kNbDist[n];
                const float toX  = -static_cast<float>(kNbDx[n]) * invD;
                const float toY  = -static_cast<float>(kNbDy[n]) * invD;
                const float distM = cfg_.cellSizeM * kNbDist[n];

                // Rothermel wind factor: strongly favors downwind spread, strongly resists upwind backing!
                const float cosWind = toX * windDx + toY * windDy;
                float phiWind = 1.0f;
                if (cosWind >= 0.0f) {
                    phiWind = 1.0f + 0.42f * windMps * cosWind * (0.45f + 0.55f * cosWind);
                } else {
                    phiWind = std::max(0.06f, 1.0f / (1.0f + 0.55f * windMps * (-cosWind)));
                }

                // Rothermel slope factor: strongly favors upslope flame attachment & preheating!
                const float slopeTan = (cur.elevationM - src.elevationM) / std::max(0.25f, distM);
                float phiSlope = 1.0f;
                if (slopeTan >= 0.0f) {
                    phiSlope = 1.0f + 1.8f * slopeTan + 3.2f * slopeTan * slopeTan;
                } else {
                    phiSlope = std::max(0.30f, 1.0f / (1.0f + 1.4f * (-slopeTan)));
                }

                heatFluxIn += src.fireIntensity01 * 185.0f * phiWind * phiSlope * invD;
            }

            if (cur.isRiverWater) {
                next.fireIntensity01 = 0.0f;
                next.fuelLoad01      = 0.0f;
                next.moisture01      = 1.0f;
                next.snowDepthM      = 0.0f;
                next.surfaceTempC    = cur.surfaceTempC + (ambientC - cur.surfaceTempC) * Clamp01(0.4f * subDt);
                scratch_[idx] = next;
                continue;
            }

            // Rain wetting & snow accumulation:
            if (rain01 > 0.0f) {
                next.moisture01 = Clamp01(next.moisture01 + rain01 * 0.42f * subDt);
            }
            if (snow01 > 0.0f && ambientC <= 0.5f && cur.fireIntensity01 <= 0.02f) {
                next.snowDepthM += snow01 * 0.002f * subDt;
            }

            // Incoming neighbor heat warms cell and evaporates moisture / melts adjacent snow:
            if (heatFluxIn > 0.0f) {
                const float dTemp = heatFluxIn * 0.48f * subDt;
                next.surfaceTempC = std::min(900.0f, next.surfaceTempC + dTemp);
                if (next.moisture01 > 0.02f) {
                    const float dried = std::min(next.moisture01, heatFluxIn * 0.00045f * subDt);
                    next.moisture01 -= dried;
                    stepSteam += dried * cellArea * 0.5f;
                }
            }

            // Spontaneous ignition when preheated above pyrolysis temperature (~240 C):
            if (next.fireIntensity01 <= 0.02f &&
                next.fuelLoad01 > 0.10f &&
                next.moisture01 < 0.65f &&
                next.snowDepthM < 0.01f &&
                next.surfaceTempC >= 240.0f) {
                const float igniteGain = Clamp01((next.surfaceTempC - 220.0f) / 260.0f) *
                                         (1.0f - 0.5f * next.moisture01) * 1.8f * subDt;
                next.fireIntensity01 = Clamp01(next.fireIntensity01 + std::max(0.18f, igniteGain));
            }

            // Active combustion, fuel consumption, char scarring & rain suppression:
            if (next.fireIntensity01 > 0.0f) {
                // Active fire ramps up to peak flame temperature if dry & fueled:
                const float targetFlameTemp = 680.0f + 220.0f * next.fireIntensity01;
                next.surfaceTempC += (targetFlameTemp - next.surfaceTempC) * Clamp01(1.6f * subDt);

                // Consume biomass fuel & scar into char:
                const float burnRate = next.fireIntensity01 * (0.075f + 0.008f * windMps) * subDt;
                const float burned   = std::min(next.fuelLoad01, burnRate);
                next.fuelLoad01     -= burned;
                next.char01          = Clamp01(next.char01 + burned * 1.45f);

                // Rain / high-moisture / fuel-exhaustion suppression:
                float decayRate = 0.0f;
                if (rain01 > 0.35f) {
                    decayRate += (rain01 - 0.25f) * 3.2f;
                    stepSteam += next.fireIntensity01 * rain01 * cellArea * 1.2f;
                }
                if (next.moisture01 > 0.55f) {
                    decayRate += (next.moisture01 - 0.50f) * 2.8f;
                }
                if (next.fuelLoad01 <= 0.03f) {
                    decayRate += 2.6f;
                }

                if (decayRate > 0.0f) {
                    next.fireIntensity01 *= std::exp(-decayRate * subDt);
                } else {
                    // Self-sustaining growth while ample dry fuel remains:
                    const float targetInt = Clamp01(0.55f + 0.45f * next.fuelLoad01) * (1.0f - 0.45f * next.moisture01);
                    next.fireIntensity01 += (targetInt - next.fireIntensity01) * Clamp01(0.8f * subDt);
                }
                if (next.fireIntensity01 < 0.015f) {
                    next.fireIntensity01 = 0.0f;
                }
            } else {
                // Newton's law thermal relaxation toward ambient + solar equilibrium:
                next.surfaceTempC += (eqTempC - next.surfaceTempC) * Clamp01(0.45f * subDt);
            }

            // Thermal snowmelt (from warm spring air OR fire heat -> exact SWE conservation!):
            if (next.snowDepthM > 0.0f && next.surfaceTempC > 0.0f) {
                const float meltSnowM = std::min(next.snowDepthM, next.surfaceTempC * 0.0055f * subDt);
                next.snowDepthM -= meltSnowM;
                const float liquidAddedM = meltSnowM * cfg_.snowWaterEquivDensity;
                next.meltWaterM += liquidAddedM;
                next.moisture01  = Clamp01(next.moisture01 + liquidAddedM * 2.0f);
                if (next.surfaceTempC > 55.0f) {
                    stepSteam += liquidAddedM * cellArea * (next.surfaceTempC / 80.0f);
                }
            }

            // Black-ice thaw above freezing:
            if (next.blackIce01 > 0.0f && next.surfaceTempC > 0.5f) {
                next.blackIce01 = Clamp01(next.blackIce01 - next.surfaceTempC * 0.06f * subDt);
            }

            scratch_[idx] = next;
        }
    }

    // -------------------------------------------------------------------------
    // Phase 2: Exact Mass-Conserving D8 Downhill Meltwater Runoff & Sub-Zero Black-Ice Refreezing
    // -------------------------------------------------------------------------
    std::fill(waterDelta_.begin(), waterDelta_.end(), 0.0f);

    for (uint32_t gy = 0u; gy < H; ++gy) {
        for (uint32_t gx = 0u; gx < W; ++gx) {
            const size_t idx = static_cast<size_t>(gy) * W + gx;
            const EcoCell& c = scratch_[idx];
            if (c.meltWaterM <= 1.0e-6f || c.blackIce01 > 0.85f) {
                continue;
            }

            // Find steepest strictly-lower hydraulic-head D8 neighbor:
            const float headSelf = c.elevationM + c.meltWaterM;
            int   bestNb    = -1;
            float bestSlope = 0.0f;

            for (int n = 0; n < 8; ++n) {
                const int32_t nx = static_cast<int32_t>(gx) + kNbDx[n];
                const int32_t ny = static_cast<int32_t>(gy) + kNbDy[n];
                if (nx < 0 || ny < 0 || nx >= static_cast<int32_t>(W) || ny >= static_cast<int32_t>(H)) {
                    continue;
                }
                const EcoCell& nb = scratch_[static_cast<size_t>(ny) * W + static_cast<size_t>(nx)];
                const float headNb = nb.elevationM + nb.meltWaterM;
                const float drop   = headSelf - headNb;
                if (drop > 1.0e-5f) {
                    const float slope = drop / (cfg_.cellSizeM * kNbDist[n]);
                    if (slope > bestSlope) {
                        bestSlope = slope;
                        bestNb    = n;
                    }
                }
            }

            if (bestNb >= 0) {
                const size_t nbIdx = static_cast<size_t>(static_cast<int32_t>(gy) + kNbDy[bestNb]) * W +
                                     static_cast<size_t>(static_cast<int32_t>(gx) + kNbDx[bestNb]);
                const float flowFrac = Clamp(bestSlope * 2.2f * subDt, 0.0f, 0.45f);
                const float dW = c.meltWaterM * flowFrac;
                waterDelta_[idx]   -= dW;
                waterDelta_[nbIdx] += dW;
            }
        }
    }

    // Apply runoff deltas & sub-zero black-ice refreezing:
    for (size_t i = 0u; i < scratch_.size(); ++i) {
        EcoCell& c = scratch_[i];
        c.meltWaterM = std::max(0.0f, c.meltWaterM + waterDelta_[i]);

        // Standing meltwater freezes into slick black ice when surface drops below -1.5 C:
        if (!c.isRiverWater && c.surfaceTempC < -1.5f && c.meltWaterM > 0.0005f) {
            const float supercool = -1.5f - c.surfaceTempC;
            const float waterAvail = Clamp01(c.meltWaterM * 120.0f);
            const float freezeInc  = Clamp01(supercool * 0.09f) * waterAvail * subDt;
            c.blackIce01 = Clamp01(c.blackIce01 + freezeInc);
        }

        // Final finiteness guard:
        c.fuelLoad01      = Clamp01(SanitizeFinite(c.fuelLoad01));
        c.moisture01      = Clamp01(SanitizeFinite(c.moisture01));
        c.snowDepthM      = std::max(0.0f, SanitizeFinite(c.snowDepthM));
        c.meltWaterM      = std::max(0.0f, SanitizeFinite(c.meltWaterM));
        c.blackIce01      = Clamp01(SanitizeFinite(c.blackIce01));
        c.fireIntensity01 = Clamp01(SanitizeFinite(c.fireIntensity01));
        c.surfaceTempC    = Clamp(SanitizeFinite(c.surfaceTempC, ambientC), -80.0f, 1200.0f);
        c.char01          = Clamp01(SanitizeFinite(c.char01));
    }

    cells_.swap(scratch_);
    lastSteamFlux_ += stepSteam;
}

void RawWorldEcosystem::AdvanceSeasonDays(float days) {
    if (!std::isfinite(days) || days <= 0.0f) {
        return;
    }
    const float charDecay = std::exp(-days / 45.0f);
    const float regrowAlpha = 1.0f - std::exp(-days / 50.0f);
    const float moistureAlpha = 1.0f - std::exp(-days / 20.0f);

    for (EcoCell& c : cells_) {
        c.fireIntensity01 = 0.0f;
        c.blackIce01      = 0.0f;
        if (c.isRiverWater) {
            continue;
        }
        c.char01       = Clamp01(c.char01 * charDecay);
        c.moisture01   = Clamp01(Lerp(c.moisture01, 0.32f, moistureAlpha));
        const float targetFuel = 0.78f;
        c.fuelLoad01   = Clamp01(c.fuelLoad01 + (targetFuel - c.fuelLoad01) * regrowAlpha);
        c.surfaceTempC = Lerp(c.surfaceTempC, 15.0f, moistureAlpha);
    }
    lastSteamFlux_ = 0.0f;
}

EcoFieldTelemetry RawWorldEcosystem::ComputeTelemetry() const {
    EcoFieldTelemetry t{};
    const float cellArea = cfg_.cellSizeM * cfg_.cellSizeM;
    t.peakTempC = -1000.0f;
    t.steamFlux = lastSteamFlux_;

    for (const EcoCell& c : cells_) {
        if (c.fireIntensity01 > 0.02f) {
            ++t.activeFireCells;
        }
        if (c.char01 > 0.15f) {
            t.burnedAreaM2 += cellArea;
        }
        t.totalSnowVolumeM3 += c.snowDepthM * cellArea;
        t.totalMeltRunoffM3 += c.meltWaterM * cellArea;
        t.totalWaterMassM3  += (c.snowDepthM * cfg_.snowWaterEquivDensity + c.meltWaterM) * cellArea;
        if (c.blackIce01 > 0.25f) {
            ++t.blackIceCells;
        }
        if (c.surfaceTempC > t.peakTempC) {
            t.peakTempC = c.surfaceTempC;
        }
    }
    if (cells_.empty()) {
        t.peakTempC = 0.0f;
    }
    return t;
}

}  // namespace raw::worldgen
