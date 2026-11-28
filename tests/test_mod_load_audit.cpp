// test_mod_load_audit.cpp: Wave 50 Clean-Room TES4 Plugin Load-Order, Master-Dependency &
// ESL/ESP Slot Integrity Auditor Tests.
// Linkable into ce_runtime_check via raw::rt::RunModLoadAuditTests(checkFn),
// or buildable standalone with -DRAW_MOD_LOAD_AUDIT_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_live_config.h"
#include "../src/engine_rt/rt_mod_load_audit.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::rt {
namespace {

void PushU16(std::vector<uint8_t>& b, uint16_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 2u); }
void PushU32(std::vector<uint8_t>& b, uint32_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 4u); }
void PushF32(std::vector<uint8_t>& b, float v)    { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 4u); }
void Push4CC(std::vector<uint8_t>& b, const char cc[4]) { b.insert(b.end(), cc, cc + 4); }

void PushSubrecord(std::vector<uint8_t>& b, const char cc[4], const void* payload, uint16_t len) {
    Push4CC(b, cc);
    PushU16(b, len);
    const auto* p = static_cast<const uint8_t*>(payload);
    b.insert(b.end(), p, p + len);
}

std::vector<uint8_t> BuildSyntheticTes4(uint32_t flags,
                                        uint32_t numRecords,
                                        const std::string& author,
                                        const std::vector<std::string>& masters) {
    std::vector<uint8_t> sub;
    // HEDR (12 bytes): float version=1.70, uint32 numRecords, uint32 nextObjectId
    std::vector<uint8_t> hedr;
    PushF32(hedr, 1.70f);
    PushU32(hedr, numRecords);
    PushU32(hedr, 0x00000800u);
    PushSubrecord(sub, "HEDR", hedr.data(), static_cast<uint16_t>(hedr.size()));

    if (!author.empty()) {
        PushSubrecord(sub, "CNAM", author.c_str(), static_cast<uint16_t>(author.size() + 1u));
    }
    for (const std::string& m : masters) {
        PushSubrecord(sub, "MAST", m.c_str(), static_cast<uint16_t>(m.size() + 1u));
        const uint64_t zeroData = 0ull;
        PushSubrecord(sub, "DATA", &zeroData, 8u);
    }

    std::vector<uint8_t> rec;
    Push4CC(rec, "TES4");
    PushU32(rec, static_cast<uint32_t>(sub.size()));
    PushU32(rec, flags);
    PushU32(rec, 0u); // formId
    PushU32(rec, 0u); // timestamp/vc
    PushU16(rec, 44u); // SSE formVersion
    PushU16(rec, 0u);
    rec.insert(rec.end(), sub.begin(), sub.end());
    return rec;
}

Tes4PluginHeader MakeParsedPlugin(const std::string& name,
                                  uint32_t flags,
                                  uint32_t numRecords,
                                  const std::vector<std::string>& masters) {
    const auto bytes = BuildSyntheticTes4(flags, numRecords, "RAW", masters);
    return ParseTes4HeaderBytes(bytes.data(), bytes.size(), name);
}

}  // namespace

int RunModLoadAuditTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Planted corrupt TES4 header & truncated file rejection
    {
        const uint8_t tiny[10] = {'T','E','S','4',0,0,0,0,0,0};
        const Tes4PluginHeader rTiny = ParseTes4HeaderBytes(tiny, sizeof(tiny), "tiny.esp");

        auto badMagic = BuildSyntheticTes4(0u, 10u, "RAW", {});
        std::memcpy(badMagic.data(), "BAD4", 4u);
        const Tes4PluginHeader rMagic = ParseTes4HeaderBytes(badMagic.data(), badMagic.size(), "bad_magic.esp");

        auto truncSub = BuildSyntheticTes4(0u, 10u, "RAW", {"Skyrim.esm"});
        truncSub.resize(30u);
        const Tes4PluginHeader rTrunc = ParseTes4HeaderBytes(truncSub.data(), truncSub.size(), "trunc.esp");

        check(!rTiny.valid && !rMagic.valid && !rTrunc.valid,
              "mod_audit_planted_corrupt_and_truncated_tes4_rejected");
    }

    // 2. Synthetic clean load order: Skyrim.esm -> Update.esm -> MyMod.esp -> LightPatch.esl
    {
        std::vector<Tes4PluginHeader> order = {
            MakeParsedPlugin("Skyrim.esm",     0x00000001u, 100000u, {}),
            MakeParsedPlugin("Update.esm",     0x00000001u,   4500u, {"Skyrim.esm"}),
            MakeParsedPlugin("MyMod.esp",      0x00000000u,    320u, {"Skyrim.esm", "Update.esm"}),
            MakeParsedPlugin("LightPatch.esl", 0x00000201u,    180u, {"Skyrim.esm", "MyMod.esp"}),
        };
        const ModLoadAuditReport rep = AuditModLoadOrder(order);
        const bool cleanOk =
            rep.severity == ModAuditSeverity::Clean &&
            rep.totalPlugins == 4u &&
            rep.fullPluginCount == 3u &&
            rep.lightEslCount == 1u &&
            rep.missingMasterCount == 0u &&
            rep.outOfOrderMasterCount == 0u &&
            rep.corruptHeaderCount == 0u &&
            rep.oversizedEslCount == 0u &&
            !rep.fullSlotOverflow && !rep.lightSlotOverflow;
        check(cleanOk, "mod_audit_clean_load_order_exact_full_and_esl_counts");

        // Verify plugins.txt parser ignores comments & disabled lines and deduplicates '*' lines
        const auto enabled = ParsePluginsTxt("# comment\nDisabled.esp\n*MyMod.esp\n*LightPatch.esl\n*mymod.esp\n");
        check(enabled.size() == 2u && enabled[0] == "MyMod.esp" && enabled[1] == "LightPatch.esl",
              "mod_audit_plugins_txt_parser_handles_comments_and_dedup");
    }

    // 3. Synthetic Missing Master, Out-of-Order Master, Oversized ESL, and 255-Full-Slot Overflow
    {
        // Missing master
        const ModLoadAuditReport rMiss = AuditModLoadOrder({
            MakeParsedPlugin("Skyrim.esm", 0x01u, 1000u, {}),
            MakeParsedPlugin("Broken.esp", 0x00u,  120u, {"Skyrim.esm", "MissingDep.esm"}),
        });
        check(rMiss.severity == ModAuditSeverity::Critical && rMiss.missingMasterCount == 1u,
              "mod_audit_detects_missing_master_critical");

        // Out-of-order master (Child loads BEFORE Parent)
        const ModLoadAuditReport rOrder = AuditModLoadOrder({
            MakeParsedPlugin("Skyrim.esm", 0x01u, 1000u, {}),
            MakeParsedPlugin("Child.esp",  0x00u,   50u, {"Skyrim.esm", "Parent.esp"}),
            MakeParsedPlugin("Parent.esp", 0x00u,  100u, {"Skyrim.esm"}),
        });
        check(rOrder.severity == ModAuditSeverity::Critical && rOrder.outOfOrderMasterCount == 1u,
              "mod_audit_detects_out_of_order_master_critical");

        // Oversized ESL warning (> 4,096 records in ESL slot)
        const ModLoadAuditReport rBigEsl = AuditModLoadOrder({
            MakeParsedPlugin("Skyrim.esm", 0x01u, 1000u, {}),
            MakeParsedPlugin("Huge.esl",   0x201u, 5200u, {"Skyrim.esm"}),
        });
        check(rBigEsl.severity == ModAuditSeverity::Warning && rBigEsl.oversizedEslCount == 1u,
              "mod_audit_detects_oversized_esl_warning");

        // 255-Full-Slot Overflow (> 254 non-ESL plugins)
        std::vector<Tes4PluginHeader> overflow;
        overflow.reserve(255u);
        overflow.push_back(MakeParsedPlugin("Skyrim.esm", 0x01u, 1000u, {}));
        for (uint32_t i = 1u; i < 255u; ++i) {
            overflow.push_back(MakeParsedPlugin("Mod_" + std::to_string(i) + ".esp", 0x00u, 10u, {"Skyrim.esm"}));
        }
        const ModLoadAuditReport rOver = AuditModLoadOrder(overflow);
        check(rOver.severity == ModAuditSeverity::Critical &&
              rOver.fullSlotOverflow && rOver.fullPluginCount == 255u,
              "mod_audit_detects_255_full_slot_overflow_critical");
    }

    // 4. Live Disk Proof on REAL Skyrim Special Edition\Data\ + RAW_GetModAuditSummary C ABI + [Stability] ModLoadAudit=0 gate
    {
        const LiveEngineConfig prevCfg = GetLiveConfig();
        LiveEngineConfig onCfg = DefaultLiveEngineConfig();
        onCfg.modLoadAuditEnabled = true;
        SetLiveConfigSnapshot(onCfg);

        const std::string liveGameDir = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition";
        std::error_code ec;
        if (std::filesystem::exists(liveGameDir + "\\Data\\Skyrim.esm", ec)) {
            const std::string logLine = ModLoadAuditInit(liveGameDir);
            RawModAuditSummary sum{};
            const int got = RAW_GetModAuditSummary(&sum);
            if (!checkFn) {
                std::printf("%s\n", logLine.c_str());
            }
            const bool liveOk =
                got == 1 &&
                sum.auditEnabled == 1u &&
                sum.totalPlugins >= 5u && // Skyrim + Update + Dawnguard + HearthFires + Dragonborn (+ CC)
                sum.corruptHeaderCount == 0u &&
                sum.missingMasterCount == 0u &&
                sum.outOfOrderMasterCount == 0u &&
                sum.auditDurationMs < 2000.0f;
            check(liveOk, "mod_audit_live_skyrim_se_data_directory_clean_and_fast");
        } else {
            // Fallback C ABI check on headless machine without Skyrim installed
            ModLoadAuditInit("__nonexistent_game_dir__");
            RawModAuditSummary sum{};
            check(RAW_GetModAuditSummary(&sum) == 1 && sum.auditEnabled == 1u,
                  "mod_audit_c_abi_exports_summary");
        }

        // Verify [Stability] ModLoadAudit INI round-trip & 0 disable gate
        const std::string defIni = FormatDefaultLiveConfigIni(DefaultLiveEngineConfig());
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(defIni, parsedDef);
        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[Stability]\nModLoadAudit=0\n", parsedOff);

        SetLiveConfigSnapshot(parsedOff);
        const std::string offLog = ModLoadAuditInit(liveGameDir);
        RawModAuditSummary sumOff{};
        RAW_GetModAuditSummary(&sumOff);

        const bool iniOffOk =
            defIni.find("ModLoadAudit=1") != std::string::npos &&
            parsedDef.modLoadAuditEnabled &&
            !parsedOff.modLoadAuditEnabled &&
            sumOff.auditEnabled == 0u &&
            sumOff.totalPlugins == 0u &&
            offLog.find("disabled") != std::string::npos;
        check(iniOffOk, "mod_audit_stability_ini_roundtrip_and_disable_gate");

        ModLoadAuditShutdown();
        SetLiveConfigSnapshot(prevCfg);
    }

    if (!checkFn) {
        std::printf("ModLoadAuditTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_MOD_LOAD_AUDIT_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunModLoadAuditTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
