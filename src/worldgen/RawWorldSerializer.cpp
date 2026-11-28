// RawWorldSerializer.cpp: Clean-Room Bit-Exact IEEE 802.3 CRC-32 Binary + JSON Persistent World
// State Serializer. Original work, MIT. Zero Windows / D3D12 / third-party dependencies.

#include "RawWorldSerializer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace raw::worldgen {
namespace {

constexpr char     kMagic[8]      = {'R', 'A', 'W', 'W', 'O', 'R', 'L', 'D'};
constexpr size_t   kHeaderBytes   = 20u;
constexpr uint32_t kMaxCaravans   = 4096u;
constexpr uint32_t kMaxEvents     = 256u;

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

class ByteWriter {
public:
    void WriteU8(uint8_t v) { buf_.push_back(v); }
    void WriteBool(bool v)  { buf_.push_back(v ? 1u : 0u); }
    void WriteU32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFFu));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }
    void WriteF32(float v) {
        uint32_t bits = 0u;
        std::memcpy(&bits, &v, sizeof(bits));
        WriteU32(bits);
    }

    const std::vector<uint8_t>& Data() const noexcept { return buf_; }
    void Reserve(size_t n) { buf_.reserve(n); }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool ReadU8(uint8_t& out) {
        if (pos_ + 1u > size_) return false;
        out = data_[pos_++];
        return true;
    }
    bool ReadBool(bool& out) {
        uint8_t b = 0u;
        if (!ReadU8(b)) return false;
        out = (b != 0u);
        return true;
    }
    bool ReadU32(uint32_t& out) {
        if (pos_ + 4u > size_) return false;
        out = static_cast<uint32_t>(data_[pos_ + 0u]) |
              (static_cast<uint32_t>(data_[pos_ + 1u]) << 8) |
              (static_cast<uint32_t>(data_[pos_ + 2u]) << 16) |
              (static_cast<uint32_t>(data_[pos_ + 3u]) << 24);
        pos_ += 4u;
        return true;
    }
    bool ReadF32(float& out) {
        uint32_t bits = 0u;
        if (!ReadU32(bits)) return false;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool AtEnd() const noexcept { return pos_ == size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t         size_ = 0u;
    size_t         pos_  = 0u;
};

inline float SafeJsonNum(float v) noexcept {
    return std::isfinite(v) ? v : 0.0f;
}

std::string FmtFloat(float v) {
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%.4f", static_cast<double>(SafeJsonNum(v)));
    return std::string(tmp);
}

}  // namespace

// ============================================================================
// IEEE 802.3 CRC-32
// ============================================================================

uint32_t RawWorldSerializer::ComputeCrc32(const uint8_t* data, size_t size) noexcept {
    if (!data && size > 0u) return 0u;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// ============================================================================
// Capture & Restore Helpers
// ============================================================================

WorldSnapshotState RawWorldSerializer::CaptureFromSystems(const RawWeatherSim*         weather,
                                                          const RawSettlementSim*      settlement,
                                                          const RawWorldEventDirector* events,
                                                          const RawFactionDiplomacy*   diplomacy,
                                                          const RawWorldEcosystem*     ecosystem) {
    WorldSnapshotState snap{};

    if (weather != nullptr) {
        const CalendarState& cal      = weather->GetCalendarState();
        const SurfaceThermalState& th = weather->GetSurfaceThermalState();
        const WeatherGpuUniforms& gpu = weather->GetGpuUniforms();

        snap.weather.dayOfYear             = cal.dayOfYear;
        snap.weather.timeOfDayHours        = cal.timeOfDayHours;
        snap.weather.timeScale             = cal.timeScale;
        snap.weather.snowDepthM            = th.snow.depthM;
        snap.weather.surfaceTempC          = th.surfaceTempC;
        snap.weather.iceThicknessM         = th.ice.iceThicknessM;
        snap.weather.beaufortWind          = gpu.beaufortScale;
        snap.weather.cloudCover01          = gpu.cloudWind.z;
        snap.weather.barometricPressureHpa = gpu.barometricPressureHpa;
        snap.weather.activeRegime          = weather->GetActiveRegime();
        snap.weather.season                = cal.season;
    }

    if (settlement != nullptr) {
        const SettlementTelemetry st = settlement->ComputeWorldTelemetry();
        snap.settlement.simHoursElapsed     = st.simHoursElapsed;
        snap.settlement.timeOfDayHours      = st.timeOfDayHours;
        snap.settlement.deliveriesCompleted = st.deliveriesCompleted;
        for (size_t i = 0u; i < kHoldCount; ++i) {
            const HoldState* h = settlement->GetHold(static_cast<HoldId>(i));
            if (h != nullptr) {
                snap.settlement.holds[i].treasuryGold      = h->treasuryGold;
                snap.settlement.holds[i].stock             = h->stock;
                snap.settlement.holds[i].price             = h->price;
                snap.settlement.holds[i].marketOpen        = h->marketOpen;
                snap.settlement.holds[i].activeGuards      = h->activeGuards;
                snap.settlement.holds[i].tavernOccupancy01 = h->tavernOccupancy01;
            }
        }
        snap.settlement.caravans = settlement->Caravans();
    }

    if (events != nullptr) {
        const EventDirectorTelemetry et = events->ComputeTelemetry();
        snap.events.totalSpawned  = et.totalSpawned;
        snap.events.totalResolved = et.totalResolved;
        snap.events.totalExpired  = et.totalExpired;
        snap.events.spawnedByKind = et.spawnedByKind;
        for (size_t k = 0u; k < kWorldEventKindCount; ++k) {
            snap.events.cooldownRemaining[k] = events->CooldownRemainingHours(static_cast<WorldEventKind>(k));
        }
        snap.events.activeEvents = events->ActiveEvents();
    }

    if (diplomacy != nullptr) {
        for (size_t i = 0u; i < kHoldCount; ++i) {
            if (const HoldStanding* hs = diplomacy->GetHoldStanding(static_cast<HoldId>(i))) {
                snap.diplomacy.holds[i] = *hs;
            }
        }
        for (size_t i = 0u; i < kFactionCount; ++i) {
            if (const FactionStanding* fs = diplomacy->GetFactionStanding(static_cast<FactionId>(i))) {
                snap.diplomacy.factions[i] = *fs;
            }
        }
    }

    if (ecosystem != nullptr) {
        const EcoFieldTelemetry ecoT = ecosystem->ComputeTelemetry();
        snap.ecosystem.activeFireCells   = ecoT.activeFireCells;
        snap.ecosystem.blackIceCells     = ecoT.blackIceCells;
        snap.ecosystem.burnedAreaM2      = ecoT.burnedAreaM2;
        snap.ecosystem.totalSnowVolumeM3 = ecoT.totalSnowVolumeM3;
        snap.ecosystem.totalMeltRunoffM3 = ecoT.totalMeltRunoffM3;
        snap.ecosystem.totalWaterMassM3  = ecoT.totalWaterMassM3;
        snap.ecosystem.peakTempC         = ecoT.peakTempC;
        snap.ecosystem.steamFlux         = ecoT.steamFlux;
    }

    return snap;
}

void RawWorldSerializer::RestoreIntoSystems(const WorldSnapshotState& snap,
                                            RawWeatherSim*            weather,
                                            RawSettlementSim*         settlement,
                                            RawWorldEventDirector*    events,
                                            RawFactionDiplomacy*      diplomacy) {
    if (weather != nullptr) {
        weather->SetTimeOfDay(snap.weather.timeOfDayHours);
        weather->SetDayOfYear(std::floor(snap.weather.dayOfYear), true);
        weather->SetTimeScale(snap.weather.timeScale);
        weather->SetRegime(snap.weather.activeRegime, true);
    }

    if (settlement != nullptr) {
        for (size_t i = 0u; i < kHoldCount; ++i) {
            if (HoldState* h = settlement->GetHoldMutable(static_cast<HoldId>(i))) {
                h->treasuryGold      = snap.settlement.holds[i].treasuryGold;
                h->stock             = snap.settlement.holds[i].stock;
                h->price             = snap.settlement.holds[i].price;
                h->marketOpen        = snap.settlement.holds[i].marketOpen;
                h->activeGuards      = snap.settlement.holds[i].activeGuards;
                h->tavernOccupancy01 = snap.settlement.holds[i].tavernOccupancy01;
            }
        }
    }

    if (events != nullptr) {
        events->Reset(events->Config());
    }

    if (diplomacy != nullptr) {
        diplomacy->Reset();
        for (size_t i = 0u; i < kHoldCount; ++i) {
            diplomacy->AdjustHoldReputation(static_cast<HoldId>(i), snap.diplomacy.holds[i].reputation);
        }
    }
}

// ============================================================================
// Binary Serialization ("RAWWORLD" v1 + IEEE 802.3 CRC-32)
// ============================================================================

bool RawWorldSerializer::SerializeWorldBinary(const WorldSnapshotState& state,
                                              std::vector<uint8_t>& outBytes) {
    ByteWriter pw;
    pw.Reserve(1024u);

    // 1. Weather
    pw.WriteF32(state.weather.dayOfYear);
    pw.WriteF32(state.weather.timeOfDayHours);
    pw.WriteF32(state.weather.timeScale);
    pw.WriteF32(state.weather.snowDepthM);
    pw.WriteF32(state.weather.surfaceTempC);
    pw.WriteF32(state.weather.iceThicknessM);
    pw.WriteF32(state.weather.beaufortWind);
    pw.WriteF32(state.weather.cloudCover01);
    pw.WriteF32(state.weather.barometricPressureHpa);
    pw.WriteU8(static_cast<uint8_t>(state.weather.activeRegime));
    pw.WriteU8(static_cast<uint8_t>(state.weather.season));

    // 2. Settlement
    pw.WriteF32(state.settlement.simHoursElapsed);
    pw.WriteF32(state.settlement.timeOfDayHours);
    pw.WriteU32(state.settlement.deliveriesCompleted);
    for (size_t i = 0u; i < kHoldCount; ++i) {
        const HoldSnapshot& hs = state.settlement.holds[i];
        pw.WriteF32(hs.treasuryGold);
        for (size_t c = 0u; c < kCommodityCount; ++c) pw.WriteF32(hs.stock[c]);
        for (size_t c = 0u; c < kCommodityCount; ++c) pw.WriteF32(hs.price[c]);
        pw.WriteBool(hs.marketOpen);
        pw.WriteU32(hs.activeGuards);
        pw.WriteF32(hs.tavernOccupancy01);
    }
    const uint32_t caravanN = static_cast<uint32_t>(std::min<size_t>(state.settlement.caravans.size(), kMaxCaravans));
    pw.WriteU32(caravanN);
    for (uint32_t i = 0u; i < caravanN; ++i) {
        const TradeCaravan& tc = state.settlement.caravans[i];
        pw.WriteU32(tc.id);
        pw.WriteU8(static_cast<uint8_t>(tc.originHold));
        pw.WriteU8(static_cast<uint8_t>(tc.destHold));
        pw.WriteU8(static_cast<uint8_t>(tc.commodity));
        pw.WriteF32(tc.cargoUnits);
        pw.WriteF32(tc.totalDistanceKm);
        pw.WriteF32(tc.traveledKm);
        pw.WriteF32(tc.baseSpeedKmh);
        pw.WriteF32(tc.mountainPassExposure01);
        pw.WriteF32(tc.banditRisk01);
        pw.WriteF32(tc.banditDelayHours);
        pw.WriteBool(tc.snowbound);
        pw.WriteBool(tc.active);
    }

    // 3. Events
    pw.WriteU32(state.events.totalSpawned);
    pw.WriteU32(state.events.totalResolved);
    pw.WriteU32(state.events.totalExpired);
    for (size_t k = 0u; k < kWorldEventKindCount; ++k) pw.WriteF32(state.events.cooldownRemaining[k]);
    for (size_t k = 0u; k < kWorldEventKindCount; ++k) pw.WriteU32(state.events.spawnedByKind[k]);
    const uint32_t evN = static_cast<uint32_t>(std::min<size_t>(state.events.activeEvents.size(), kMaxEvents));
    pw.WriteU32(evN);
    for (uint32_t i = 0u; i < evN; ++i) {
        const WorldEvent& ev = state.events.activeEvents[i];
        pw.WriteU32(ev.id);
        pw.WriteU8(static_cast<uint8_t>(ev.kind));
        pw.WriteU8(static_cast<uint8_t>(ev.holdId));
        pw.WriteU8(static_cast<uint8_t>(ev.commodity));
        pw.WriteF32(ev.urgency01);
        pw.WriteF32(ev.rewardGold);
        pw.WriteF32(ev.tavernHarmonyBonus01);
        pw.WriteU32(ev.suggestedFlankers);
        pw.WriteU32(ev.suggestedArchers);
        pw.WriteF32(ev.ageHours);
        pw.WriteF32(ev.maxDurationHours);
    }

    // 4. Diplomacy
    for (size_t i = 0u; i < kHoldCount; ++i) {
        const HoldStanding& hs = state.diplomacy.holds[i];
        pw.WriteF32(hs.reputation);
        pw.WriteF32(hs.bountyGold);
        pw.WriteU8(static_cast<uint8_t>(hs.guardPosture));
        pw.WriteF32(hs.merchantPriceMult);
        pw.WriteF32(hs.bountyHunterRisk01);
    }
    for (size_t i = 0u; i < kFactionCount; ++i) {
        pw.WriteF32(state.diplomacy.factions[i].reputation);
    }

    // 5. Ecosystem
    pw.WriteU32(state.ecosystem.activeFireCells);
    pw.WriteU32(state.ecosystem.blackIceCells);
    pw.WriteF32(state.ecosystem.burnedAreaM2);
    pw.WriteF32(state.ecosystem.totalSnowVolumeM3);
    pw.WriteF32(state.ecosystem.totalMeltRunoffM3);
    pw.WriteF32(state.ecosystem.totalWaterMassM3);
    pw.WriteF32(state.ecosystem.peakTempC);
    pw.WriteF32(state.ecosystem.steamFlux);

    const std::vector<uint8_t>& payload = pw.Data();
    const uint32_t payloadBytes = static_cast<uint32_t>(payload.size());
    const uint32_t crc32        = ComputeCrc32(payload.data(), payload.size());

    ByteWriter hw;
    hw.Reserve(kHeaderBytes + payload.size());
    for (char ch : kMagic) hw.WriteU8(static_cast<uint8_t>(ch));
    hw.WriteU32(kRawWorldBinaryVersion);
    hw.WriteU32(payloadBytes);
    hw.WriteU32(crc32);

    outBytes.clear();
    outBytes.reserve(kHeaderBytes + payload.size());
    outBytes.insert(outBytes.end(), hw.Data().begin(), hw.Data().end());
    outBytes.insert(outBytes.end(), payload.begin(), payload.end());
    return true;
}

bool RawWorldSerializer::DeserializeWorldBinary(const uint8_t* data, size_t size,
                                                WorldSnapshotState& outState,
                                                std::string* outErr) {
    if (!data || size < kHeaderBytes) {
        SetErr(outErr, "Buffer null or smaller than 20-byte RAWWORLD header.");
        return false;
    }
    if (std::memcmp(data, kMagic, 8u) != 0) {
        SetErr(outErr, "Invalid binary container magic (expected 'RAWWORLD').");
        return false;
    }

    ByteReader hdr(data + 8u, 12u);
    uint32_t version = 0u, payloadBytes = 0u, storedCrc = 0u;
    if (!hdr.ReadU32(version) || !hdr.ReadU32(payloadBytes) || !hdr.ReadU32(storedCrc)) {
        SetErr(outErr, "Failed to read RAWWORLD header fields.");
        return false;
    }
    if (version != kRawWorldBinaryVersion) {
        SetErr(outErr, "Unsupported RAWWORLD version: " + std::to_string(version));
        return false;
    }
    if (size != kHeaderBytes + static_cast<size_t>(payloadBytes)) {
        SetErr(outErr, "RAWWORLD payload truncated or length mismatch.");
        return false;
    }

    const uint8_t* payloadPtr  = data + kHeaderBytes;
    const uint32_t computedCrc = ComputeCrc32(payloadPtr, payloadBytes);
    if (computedCrc != storedCrc) {
        SetErr(outErr, "RAWWORLD IEEE 802.3 CRC-32 checksum mismatch (corrupted data).");
        return false;
    }

    ByteReader pr(payloadPtr, payloadBytes);
    WorldSnapshotState s{};

    // 1. Weather
    uint8_t regimeByte = 0u, seasonByte = 0u;
    if (!pr.ReadF32(s.weather.dayOfYear) || !pr.ReadF32(s.weather.timeOfDayHours) ||
        !pr.ReadF32(s.weather.timeScale) || !pr.ReadF32(s.weather.snowDepthM) ||
        !pr.ReadF32(s.weather.surfaceTempC) || !pr.ReadF32(s.weather.iceThicknessM) ||
        !pr.ReadF32(s.weather.beaufortWind) || !pr.ReadF32(s.weather.cloudCover01) ||
        !pr.ReadF32(s.weather.barometricPressureHpa) ||
        !pr.ReadU8(regimeByte) || !pr.ReadU8(seasonByte)) {
        SetErr(outErr, "Truncated weather block in RAWWORLD payload.");
        return false;
    }
    if (regimeByte >= static_cast<uint8_t>(WeatherRegime::Count) || seasonByte > 3u) {
        SetErr(outErr, "Invalid weather enum value in RAWWORLD payload.");
        return false;
    }
    s.weather.activeRegime = static_cast<WeatherRegime>(regimeByte);
    s.weather.season       = static_cast<SeasonId>(seasonByte);

    // 2. Settlement
    if (!pr.ReadF32(s.settlement.simHoursElapsed) || !pr.ReadF32(s.settlement.timeOfDayHours) ||
        !pr.ReadU32(s.settlement.deliveriesCompleted)) {
        SetErr(outErr, "Truncated settlement header in RAWWORLD payload.");
        return false;
    }
    for (size_t i = 0u; i < kHoldCount; ++i) {
        HoldSnapshot& hs = s.settlement.holds[i];
        if (!pr.ReadF32(hs.treasuryGold)) return false;
        for (size_t c = 0u; c < kCommodityCount; ++c) if (!pr.ReadF32(hs.stock[c])) return false;
        for (size_t c = 0u; c < kCommodityCount; ++c) if (!pr.ReadF32(hs.price[c])) return false;
        if (!pr.ReadBool(hs.marketOpen) || !pr.ReadU32(hs.activeGuards) || !pr.ReadF32(hs.tavernOccupancy01)) {
            SetErr(outErr, "Truncated hold block in RAWWORLD payload.");
            return false;
        }
    }
    uint32_t caravanN = 0u;
    if (!pr.ReadU32(caravanN) || caravanN > kMaxCaravans) {
        SetErr(outErr, "Invalid caravan count in RAWWORLD payload.");
        return false;
    }
    s.settlement.caravans.resize(caravanN);
    for (uint32_t i = 0u; i < caravanN; ++i) {
        TradeCaravan& tc = s.settlement.caravans[i];
        uint8_t oH = 0u, dH = 0u, comm = 0u;
        if (!pr.ReadU32(tc.id) || !pr.ReadU8(oH) || !pr.ReadU8(dH) || !pr.ReadU8(comm) ||
            !pr.ReadF32(tc.cargoUnits) || !pr.ReadF32(tc.totalDistanceKm) || !pr.ReadF32(tc.traveledKm) ||
            !pr.ReadF32(tc.baseSpeedKmh) || !pr.ReadF32(tc.mountainPassExposure01) ||
            !pr.ReadF32(tc.banditRisk01) || !pr.ReadF32(tc.banditDelayHours) ||
            !pr.ReadBool(tc.snowbound) || !pr.ReadBool(tc.active) ||
            oH >= kHoldCount || dH >= kHoldCount || comm >= kCommodityCount) {
            SetErr(outErr, "Invalid caravan entry in RAWWORLD payload.");
            return false;
        }
        tc.originHold = static_cast<HoldId>(oH);
        tc.destHold   = static_cast<HoldId>(dH);
        tc.commodity  = static_cast<CommodityKind>(comm);
    }

    // 3. Events
    if (!pr.ReadU32(s.events.totalSpawned) || !pr.ReadU32(s.events.totalResolved) ||
        !pr.ReadU32(s.events.totalExpired)) {
        SetErr(outErr, "Truncated event counters in RAWWORLD payload.");
        return false;
    }
    for (size_t k = 0u; k < kWorldEventKindCount; ++k) if (!pr.ReadF32(s.events.cooldownRemaining[k])) return false;
    for (size_t k = 0u; k < kWorldEventKindCount; ++k) if (!pr.ReadU32(s.events.spawnedByKind[k])) return false;
    uint32_t evN = 0u;
    if (!pr.ReadU32(evN) || evN > kMaxEvents) {
        SetErr(outErr, "Invalid active event count in RAWWORLD payload.");
        return false;
    }
    s.events.activeEvents.resize(evN);
    for (uint32_t i = 0u; i < evN; ++i) {
        WorldEvent& ev = s.events.activeEvents[i];
        uint8_t kB = 0u, hB = 0u, cB = 0u;
        if (!pr.ReadU32(ev.id) || !pr.ReadU8(kB) || !pr.ReadU8(hB) || !pr.ReadU8(cB) ||
            !pr.ReadF32(ev.urgency01) || !pr.ReadF32(ev.rewardGold) || !pr.ReadF32(ev.tavernHarmonyBonus01) ||
            !pr.ReadU32(ev.suggestedFlankers) || !pr.ReadU32(ev.suggestedArchers) ||
            !pr.ReadF32(ev.ageHours) || !pr.ReadF32(ev.maxDurationHours) ||
            kB >= kWorldEventKindCount || hB >= kHoldCount || cB >= kCommodityCount) {
            SetErr(outErr, "Invalid active event record in RAWWORLD payload.");
            return false;
        }
        ev.kind      = static_cast<WorldEventKind>(kB);
        ev.holdId    = static_cast<HoldId>(hB);
        ev.commodity = static_cast<CommodityKind>(cB);
    }

    // 4. Diplomacy
    for (size_t i = 0u; i < kHoldCount; ++i) {
        HoldStanding& hs = s.diplomacy.holds[i];
        hs.id = static_cast<HoldId>(i);
        uint8_t postureB = 0u;
        if (!pr.ReadF32(hs.reputation) || !pr.ReadF32(hs.bountyGold) || !pr.ReadU8(postureB) ||
            !pr.ReadF32(hs.merchantPriceMult) || !pr.ReadF32(hs.bountyHunterRisk01) || postureB > 3u) {
            SetErr(outErr, "Invalid hold diplomacy record in RAWWORLD payload.");
            return false;
        }
        hs.guardPosture = static_cast<GuardPosture>(postureB);
    }
    for (size_t i = 0u; i < kFactionCount; ++i) {
        s.diplomacy.factions[i].id = static_cast<FactionId>(i);
        if (!pr.ReadF32(s.diplomacy.factions[i].reputation)) {
            SetErr(outErr, "Truncated faction diplomacy record in RAWWORLD payload.");
            return false;
        }
    }

    // 5. Ecosystem
    if (!pr.ReadU32(s.ecosystem.activeFireCells) || !pr.ReadU32(s.ecosystem.blackIceCells) ||
        !pr.ReadF32(s.ecosystem.burnedAreaM2) || !pr.ReadF32(s.ecosystem.totalSnowVolumeM3) ||
        !pr.ReadF32(s.ecosystem.totalMeltRunoffM3) || !pr.ReadF32(s.ecosystem.totalWaterMassM3) ||
        !pr.ReadF32(s.ecosystem.peakTempC) || !pr.ReadF32(s.ecosystem.steamFlux)) {
        SetErr(outErr, "Truncated ecosystem block in RAWWORLD payload.");
        return false;
    }

    if (!pr.AtEnd()) {
        SetErr(outErr, "Unexpected trailing bytes inside RAWWORLD payload.");
        return false;
    }

    outState = std::move(s);
    return true;
}

// ============================================================================
// Disk File Helpers
// ============================================================================

bool RawWorldSerializer::SaveWorldToFile(const WorldSnapshotState& state,
                                         const std::string& filePath,
                                         std::string* outErr) {
    if (filePath.empty()) {
        SetErr(outErr, "Empty output file path.");
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!SerializeWorldBinary(state, bytes)) {
        SetErr(outErr, "Failed to serialize WorldSnapshotState.");
        return false;
    }
    std::ofstream f(filePath, std::ios::binary | std::ios::trunc);
    if (!f) {
        SetErr(outErr, "Failed to open file for writing: " + filePath);
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f.good()) {
        SetErr(outErr, "I/O error writing world snapshot file: " + filePath);
        return false;
    }
    return true;
}

bool RawWorldSerializer::LoadWorldFromFile(const std::string& filePath,
                                           WorldSnapshotState& outState,
                                           std::string* outErr) {
    if (filePath.empty()) {
        SetErr(outErr, "Empty input file path.");
        return false;
    }
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f) {
        SetErr(outErr, "Failed to open world snapshot file: " + filePath);
        return false;
    }
    const std::streamoff sz = f.tellg();
    if (sz < static_cast<std::streamoff>(kHeaderBytes)) {
        SetErr(outErr, "World snapshot file smaller than header: " + filePath);
        return false;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
        SetErr(outErr, "I/O error reading world snapshot file: " + filePath);
        return false;
    }
    return DeserializeWorldBinary(bytes.data(), bytes.size(), outState, outErr);
}

// ============================================================================
// Human-Readable Deterministic JSON Export
// ============================================================================

std::string RawWorldSerializer::SerializeWorldJson(const WorldSnapshotState& s) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"format\": \"RAWWORLD\",\n";
    os << "  \"version\": " << kRawWorldBinaryVersion << ",\n";

    os << "  \"weather\": {\n";
    os << "    \"dayOfYear\": " << FmtFloat(s.weather.dayOfYear) << ",\n";
    os << "    \"timeOfDayHours\": " << FmtFloat(s.weather.timeOfDayHours) << ",\n";
    os << "    \"timeScale\": " << FmtFloat(s.weather.timeScale) << ",\n";
    os << "    \"snowDepthM\": " << FmtFloat(s.weather.snowDepthM) << ",\n";
    os << "    \"surfaceTempC\": " << FmtFloat(s.weather.surfaceTempC) << ",\n";
    os << "    \"iceThicknessM\": " << FmtFloat(s.weather.iceThicknessM) << ",\n";
    os << "    \"beaufortWind\": " << FmtFloat(s.weather.beaufortWind) << ",\n";
    os << "    \"cloudCover01\": " << FmtFloat(s.weather.cloudCover01) << ",\n";
    os << "    \"regime\": \"" << RawWeatherSim::GetRegimeName(s.weather.activeRegime) << "\",\n";
    os << "    \"season\": \"" << RawWeatherSim::GetSeasonName(s.weather.season) << "\"\n";
    os << "  },\n";

    os << "  \"settlement\": {\n";
    os << "    \"simHoursElapsed\": " << FmtFloat(s.settlement.simHoursElapsed) << ",\n";
    os << "    \"deliveriesCompleted\": " << s.settlement.deliveriesCompleted << ",\n";
    os << "    \"activeCaravans\": " << s.settlement.caravans.size() << ",\n";
    os << "    \"holds\": [\n";
    for (size_t i = 0u; i < kHoldCount; ++i) {
        const HoldSnapshot& hs = s.settlement.holds[i];
        os << "      {\"name\": \"" << GetHoldName(static_cast<HoldId>(i)) << "\", "
           << "\"treasuryGold\": " << FmtFloat(hs.treasuryGold) << ", "
           << "\"grainStock\": " << FmtFloat(hs.stock[0]) << ", "
           << "\"grainPrice\": " << FmtFloat(hs.price[0]) << ", "
           << "\"marketOpen\": " << (hs.marketOpen ? "true" : "false") << ", "
           << "\"activeGuards\": " << hs.activeGuards << "}"
           << (i + 1u < kHoldCount ? "," : "") << "\n";
    }
    os << "    ]\n";
    os << "  },\n";

    os << "  \"events\": {\n";
    os << "    \"activeCount\": " << s.events.activeEvents.size() << ",\n";
    os << "    \"totalSpawned\": " << s.events.totalSpawned << ",\n";
    os << "    \"totalResolved\": " << s.events.totalResolved << ",\n";
    os << "    \"totalExpired\": " << s.events.totalExpired << "\n";
    os << "  },\n";

    os << "  \"diplomacy\": {\n";
    os << "    \"holds\": [\n";
    for (size_t i = 0u; i < kHoldCount; ++i) {
        const HoldStanding& hs = s.diplomacy.holds[i];
        os << "      {\"name\": \"" << GetHoldName(static_cast<HoldId>(i)) << "\", "
           << "\"reputation\": " << FmtFloat(hs.reputation) << ", "
           << "\"bountyGold\": " << FmtFloat(hs.bountyGold) << ", "
           << "\"guardPosture\": \"" << GetGuardPostureName(hs.guardPosture) << "\", "
           << "\"merchantPriceMult\": " << FmtFloat(hs.merchantPriceMult) << "}"
           << (i + 1u < kHoldCount ? "," : "") << "\n";
    }
    os << "    ],\n";
    os << "    \"factions\": [\n";
    for (size_t i = 0u; i < kFactionCount; ++i) {
        os << "      {\"name\": \"" << GetFactionName(static_cast<FactionId>(i)) << "\", "
           << "\"reputation\": " << FmtFloat(s.diplomacy.factions[i].reputation) << "}"
           << (i + 1u < kFactionCount ? "," : "") << "\n";
    }
    os << "    ]\n";
    os << "  },\n";

    os << "  \"ecosystem\": {\n";
    os << "    \"activeFireCells\": " << s.ecosystem.activeFireCells << ",\n";
    os << "    \"blackIceCells\": " << s.ecosystem.blackIceCells << ",\n";
    os << "    \"burnedAreaM2\": " << FmtFloat(s.ecosystem.burnedAreaM2) << ",\n";
    os << "    \"totalSnowVolumeM3\": " << FmtFloat(s.ecosystem.totalSnowVolumeM3) << ",\n";
    os << "    \"totalMeltRunoffM3\": " << FmtFloat(s.ecosystem.totalMeltRunoffM3) << ",\n";
    os << "    \"peakTempC\": " << FmtFloat(s.ecosystem.peakTempC) << "\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

}  // namespace raw::worldgen
