// test_world_serializer.cpp: Verification Suite for `raw::worldgen::RawWorldSerializer`.
// Exports `int raw::worldgen::RunWorldSerializerTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_WORLD_SERIALIZER_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawWorldSerializer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

bool FloatBitEq(float a, float b) noexcept {
    uint32_t ua = 0u, ub = 0u;
    std::memcpy(&ua, &a, sizeof(ua));
    std::memcpy(&ub, &b, sizeof(ub));
    return ua == ub;
}

bool SnapshotsBitEqual(const WorldSnapshotState& a, const WorldSnapshotState& b) {
    // 1. Weather
    if (!FloatBitEq(a.weather.dayOfYear, b.weather.dayOfYear) ||
        !FloatBitEq(a.weather.timeOfDayHours, b.weather.timeOfDayHours) ||
        !FloatBitEq(a.weather.timeScale, b.weather.timeScale) ||
        !FloatBitEq(a.weather.snowDepthM, b.weather.snowDepthM) ||
        !FloatBitEq(a.weather.surfaceTempC, b.weather.surfaceTempC) ||
        !FloatBitEq(a.weather.iceThicknessM, b.weather.iceThicknessM) ||
        !FloatBitEq(a.weather.beaufortWind, b.weather.beaufortWind) ||
        !FloatBitEq(a.weather.cloudCover01, b.weather.cloudCover01) ||
        !FloatBitEq(a.weather.barometricPressureHpa, b.weather.barometricPressureHpa) ||
        a.weather.activeRegime != b.weather.activeRegime ||
        a.weather.season != b.weather.season) {
        return false;
    }

    // 2. Settlement
    if (!FloatBitEq(a.settlement.simHoursElapsed, b.settlement.simHoursElapsed) ||
        !FloatBitEq(a.settlement.timeOfDayHours, b.settlement.timeOfDayHours) ||
        a.settlement.deliveriesCompleted != b.settlement.deliveriesCompleted ||
        a.settlement.caravans.size() != b.settlement.caravans.size()) {
        return false;
    }
    for (size_t i = 0u; i < kHoldCount; ++i) {
        if (!FloatBitEq(a.settlement.holds[i].treasuryGold, b.settlement.holds[i].treasuryGold) ||
            a.settlement.holds[i].marketOpen != b.settlement.holds[i].marketOpen ||
            a.settlement.holds[i].activeGuards != b.settlement.holds[i].activeGuards ||
            !FloatBitEq(a.settlement.holds[i].tavernOccupancy01, b.settlement.holds[i].tavernOccupancy01)) {
            return false;
        }
        for (size_t c = 0u; c < kCommodityCount; ++c) {
            if (!FloatBitEq(a.settlement.holds[i].stock[c], b.settlement.holds[i].stock[c]) ||
                !FloatBitEq(a.settlement.holds[i].price[c], b.settlement.holds[i].price[c])) {
                return false;
            }
        }
    }
    for (size_t i = 0u; i < a.settlement.caravans.size(); ++i) {
        const auto& ca = a.settlement.caravans[i];
        const auto& cb = b.settlement.caravans[i];
        if (ca.id != cb.id || ca.originHold != cb.originHold || ca.destHold != cb.destHold ||
            ca.commodity != cb.commodity || !FloatBitEq(ca.cargoUnits, cb.cargoUnits) ||
            !FloatBitEq(ca.traveledKm, cb.traveledKm) || ca.snowbound != cb.snowbound) {
            return false;
        }
    }

    // 3. Events
    if (a.events.totalSpawned != b.events.totalSpawned ||
        a.events.totalResolved != b.events.totalResolved ||
        a.events.totalExpired != b.events.totalExpired ||
        a.events.activeEvents.size() != b.events.activeEvents.size()) {
        return false;
    }
    for (size_t k = 0u; k < kWorldEventKindCount; ++k) {
        if (!FloatBitEq(a.events.cooldownRemaining[k], b.events.cooldownRemaining[k]) ||
            a.events.spawnedByKind[k] != b.events.spawnedByKind[k]) {
            return false;
        }
    }
    for (size_t i = 0u; i < a.events.activeEvents.size(); ++i) {
        const auto& ea = a.events.activeEvents[i];
        const auto& eb = b.events.activeEvents[i];
        if (ea.id != eb.id || ea.kind != eb.kind || ea.holdId != eb.holdId ||
            !FloatBitEq(ea.urgency01, eb.urgency01) || !FloatBitEq(ea.rewardGold, eb.rewardGold) ||
            !FloatBitEq(ea.ageHours, eb.ageHours)) {
            return false;
        }
    }

    // 4. Diplomacy
    for (size_t i = 0u; i < kHoldCount; ++i) {
        const auto& ha = a.diplomacy.holds[i];
        const auto& hb = b.diplomacy.holds[i];
        if (!FloatBitEq(ha.reputation, hb.reputation) || !FloatBitEq(ha.bountyGold, hb.bountyGold) ||
            ha.guardPosture != hb.guardPosture ||
            !FloatBitEq(ha.merchantPriceMult, hb.merchantPriceMult) ||
            !FloatBitEq(ha.bountyHunterRisk01, hb.bountyHunterRisk01)) {
            return false;
        }
    }
    for (size_t i = 0u; i < kFactionCount; ++i) {
        if (!FloatBitEq(a.diplomacy.factions[i].reputation, b.diplomacy.factions[i].reputation)) {
            return false;
        }
    }

    // 5. Ecosystem
    if (a.ecosystem.activeFireCells != b.ecosystem.activeFireCells ||
        a.ecosystem.blackIceCells != b.ecosystem.blackIceCells ||
        !FloatBitEq(a.ecosystem.burnedAreaM2, b.ecosystem.burnedAreaM2) ||
        !FloatBitEq(a.ecosystem.totalSnowVolumeM3, b.ecosystem.totalSnowVolumeM3) ||
        !FloatBitEq(a.ecosystem.totalMeltRunoffM3, b.ecosystem.totalMeltRunoffM3) ||
        !FloatBitEq(a.ecosystem.totalWaterMassM3, b.ecosystem.totalWaterMassM3) ||
        !FloatBitEq(a.ecosystem.peakTempC, b.ecosystem.peakTempC) ||
        !FloatBitEq(a.ecosystem.steamFlux, b.ecosystem.steamFlux)) {
        return false;
    }

    return true;
}

}  // namespace

int RunWorldSerializerTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // Build a rich coupled 5-pillar living world state once for all tests:
    RawWeatherSim weather;
    weather.SetDayOfYear(340.0f); // Winter
    weather.SetTimeOfDay(22.5f);
    weather.SetRegime(WeatherRegime::Blizzard, true);
    weather.Step(2.0f, 20.0f, -1.0f, {0.0f, 0.0f, 1200.0f});

    RawSettlementSim settle(5501u);
    SettlementWeatherMap wx{};
    wx.byHold[static_cast<size_t>(HoldId::Windhelm)].blizzard01   = 0.90f;
    wx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = 0.95f;
    settle.StepHours(36.0f, wx);

    RawWorldEcosystem eco{};
    const Vec2 firePos = eco.CellCenterWorldM(32u, 32u);
    eco.IgniteAt(firePos.x, firePos.y, 8.0f, 650.0f);
    EcoWeatherInput ecoWx{};
    ecoWx.ambientTempC = 18.0f;
    ecoWx.windBeaufort = 5.0f;
    eco.Step(1.5f, ecoWx);

    RawWorldEventDirector director{};
    WorldEventInputs evIn{};
    evIn.settlement     = &settle;
    evIn.ecosystem      = &eco;
    evIn.weatherSim     = &weather;
    evIn.timeOfDayHours = 22.5f;
    evIn.auroraKp       = 7.0f;
    evIn.cloudCover01   = 0.10f;
    evIn.blizzard01     = 0.15f;
    director.StepHours(1.0f, evIn);

    RawFactionDiplomacy diplomacy{};
    diplomacy.AdjustFactionReputation(FactionId::ImperialLegion, 42.0f);
    diplomacy.ApplyEventReward(WorldEventKind::BlizzardCaravanRescue, HoldId::Whiterun);
    diplomacy.RecordCrime(HoldId::Riften, CrimeSeverity::GrandTheft, true);

    const WorldSnapshotState originalSnap =
        RawWorldSerializer::CaptureFromSystems(&weather, &settle, &director, &diplomacy, &eco);

    std::vector<uint8_t> validBytes;
    const bool serialized = RawWorldSerializer::SerializeWorldBinary(originalSnap, validBytes);

    // =========================================================================
    // Test 1: Planted-failure controls (empty, bad magic, bad version, truncated, 1-bit CRC flip!)
    // =========================================================================
    {
        WorldSnapshotState dummy{};
        std::string err;

        const bool emptyRejected = !RawWorldSerializer::DeserializeWorldBinary(nullptr, 0u, dummy, &err);

        std::vector<uint8_t> badMagic = validBytes;
        badMagic[0] = 'B'; badMagic[1] = 'A'; badMagic[2] = 'D';
        const bool badMagicRejected = !RawWorldSerializer::DeserializeWorldBinary(
            badMagic.data(), badMagic.size(), dummy, &err);

        std::vector<uint8_t> badVer = validBytes;
        badVer[8] = 99u; badVer[9] = 0u; badVer[10] = 0u; badVer[11] = 0u;
        const bool badVerRejected = !RawWorldSerializer::DeserializeWorldBinary(
            badVer.data(), badVer.size(), dummy, &err);

        const bool truncRejected = !RawWorldSerializer::DeserializeWorldBinary(
            validBytes.data(), validBytes.size() - 5u, dummy, &err);

        // Flip literally ONE SINGLE BIT inside the payload -> IEEE 802.3 CRC-32 MUST catch it!
        std::vector<uint8_t> bitFlipped = validBytes;
        bitFlipped[validBytes.size() / 2u] ^= 0x01u;
        err.clear();
        const bool crcCaughtBitFlip = !RawWorldSerializer::DeserializeWorldBinary(
            bitFlipped.data(), bitFlipped.size(), dummy, &err) && !err.empty();

        check(serialized && emptyRejected && badMagicRejected && badVerRejected &&
                  truncRejected && crcCaughtBitFlip,
              "world_serializer_planted_failure_and_crc32_tamper_controls");
    }

    // =========================================================================
    // Test 2: Bit-Exact Binary Round-Trip + RestoreIntoSystems Proof
    // =========================================================================
    {
        WorldSnapshotState restoredSnap{};
        std::string err;
        const bool deserialized = RawWorldSerializer::DeserializeWorldBinary(
            validBytes.data(), validBytes.size(), restoredSnap, &err);

        const bool exactMatch = deserialized && SnapshotsBitEqual(originalSnap, restoredSnap);

        // Restore into fresh live simulation objects and verify state parity:
        RawWeatherSim weather2;
        RawSettlementSim settle2(9999u);
        RawWorldEventDirector dir2;
        RawFactionDiplomacy dip2;
        RawWorldSerializer::RestoreIntoSystems(restoredSnap, &weather2, &settle2, &dir2, &dip2);

        const bool liveRestoredOk =
            (std::abs(weather2.GetCalendarState().dayOfYear - originalSnap.weather.dayOfYear) < 1.0e-3f) &&
            FloatBitEq(weather2.GetCalendarState().timeOfDayHours, originalSnap.weather.timeOfDayHours) &&
            FloatBitEq(settle2.GetHold(HoldId::Whiterun)->treasuryGold,
                       originalSnap.settlement.holds[static_cast<size_t>(HoldId::Whiterun)].treasuryGold) &&
            FloatBitEq(dip2.GetHoldStanding(HoldId::Whiterun)->reputation,
                       originalSnap.diplomacy.holds[static_cast<size_t>(HoldId::Whiterun)].reputation);

        std::printf(
            "  [Binary Round-Trip] bytes=%zu caravans=%zu events=%zu fireCells=%u bitExact=%d liveRestore=%d\n",
            validBytes.size(), restoredSnap.settlement.caravans.size(),
            restoredSnap.events.activeEvents.size(), restoredSnap.ecosystem.activeFireCells,
            exactMatch ? 1 : 0, liveRestoredOk ? 1 : 0);

        check(exactMatch && liveRestoredOk && !restoredSnap.events.activeEvents.empty(),
              "world_serializer_bit_exact_round_trip_and_restore");
    }

    // =========================================================================
    // Test 3: Disk File Save/Load Round-Trip + Deterministic JSON Schema Verification
    // =========================================================================
    {
        const std::filesystem::path tmpPath =
            std::filesystem::temp_directory_path() / "raw_world_serializer_test_snapshot.rawworld";
        std::string err;
        const bool saved = RawWorldSerializer::SaveWorldToFile(originalSnap, tmpPath.string(), &err);

        WorldSnapshotState fromDisk{};
        const bool loaded = saved && RawWorldSerializer::LoadWorldFromFile(tmpPath.string(), fromDisk, &err);
        const bool diskExact = loaded && SnapshotsBitEqual(originalSnap, fromDisk);

        // Clean up temp file cleanly:
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);

        // Verify deterministic JSON output schema & zero NaN/Inf tokens:
        const std::string json = RawWorldSerializer::SerializeWorldJson(fromDisk);
        const bool jsonKeysOk =
            (json.find("\"format\": \"RAWWORLD\"") != std::string::npos) &&
            (json.find("\"weather\":") != std::string::npos) &&
            (json.find("\"settlement\":") != std::string::npos) &&
            (json.find("\"events\":") != std::string::npos) &&
            (json.find("\"diplomacy\":") != std::string::npos) &&
            (json.find("\"ecosystem\":") != std::string::npos) &&
            (json.find("Whiterun") != std::string::npos) &&
            (json.find("ImperialLegion") != std::string::npos) &&
            (json.find("nan") == std::string::npos) &&
            (json.find("NaN") == std::string::npos) &&
            (json.find("inf") == std::string::npos);

        std::printf("  [Disk + JSON] diskExact=%d jsonBytes=%zu keysOk=%d\n",
                    diskExact ? 1 : 0, json.size(), jsonKeysOk ? 1 : 0);

        check(saved && diskExact && jsonKeysOk,
              "world_serializer_disk_round_trip_and_json_export");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_WORLD_SERIALIZER_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunWorldSerializerTests(nullptr);
    std::printf("RawWorldSerializer standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
