// test_save_papyrus_guard.cpp: Wave 48 Clean-Room `.ESS` Save-File Health Auditor &
// Papyrus Frame-Hitch Watchdog Tests.
// Linkable into ce_runtime_check via raw::rt::RunSavePapyrusGuardTests(checkFn),
// or buildable standalone with -DRAW_SAVE_PAPYRUS_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_live_config.h"
#include "../src/engine_rt/rt_save_papyrus_guard.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::rt {
namespace {

void PushU8(std::vector<uint8_t>& b, uint8_t v)   { b.push_back(v); }
void PushU16(std::vector<uint8_t>& b, uint16_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 2u); }
void PushU32(std::vector<uint8_t>& b, uint32_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 4u); }
void PushU64(std::vector<uint8_t>& b, uint64_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 8u); }
void PushF32(std::vector<uint8_t>& b, float v)    { const auto* p = reinterpret_cast<const uint8_t*>(&v); b.insert(b.end(), p, p + 4u); }
void PushWStr(std::vector<uint8_t>& b, const std::string& s) {
    PushU16(b, static_cast<uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

// Builds a spec-compliant synthetic uncompressed SSE v12 `.ESS` save file with configurable Papyrus string count.
std::vector<uint8_t> BuildSyntheticSseSave(const std::string& playerName,
                                           uint32_t level,
                                           const std::string& location,
                                           uint32_t saveNumber,
                                           uint32_t papyrusStringCount) {
    std::vector<uint8_t> hdr;
    PushU32(hdr, 12u);                  // version = 12 (SSE/AE)
    PushU32(hdr, saveNumber);
    PushWStr(hdr, playerName);
    PushU32(hdr, level);
    PushWStr(hdr, location);
    PushWStr(hdr, "004.12.30");         // gameDate
    PushWStr(hdr, "NordRace");          // race
    PushU16(hdr, 0u);                   // sex
    PushF32(hdr, 120.0f);               // curExp
    PushF32(hdr, 500.0f);               // lvlUpExp
    PushU64(hdr, 0x01DA960000000000ull);// FILETIME
    const uint32_t sw = 4u, sh = 4u;
    PushU32(hdr, sw);
    PushU32(hdr, sh);
    PushU16(hdr, 0u);                   // compressionType = 0 (uncompressed)

    std::vector<uint8_t> file;
    const char magic[13] = {'T','E','S','V','_','S','A','V','E','G','A','M','E'};
    file.insert(file.end(), magic, magic + 13);
    PushU32(file, static_cast<uint32_t>(hdr.size()));
    file.insert(file.end(), hdr.begin(), hdr.end());

    // 4x4 RGBA screenshot
    file.insert(file.end(), size_t(sw * sh * 4u), uint8_t(0x7Fu));
    const size_t bodyOff = file.size();

    // Body: formVersion (78), pluginInfoSize, FileLocationTable (10 uint32s + 15 padding uint32s), GlobalDataTable3
    PushU8(file, 78u);
    PushU32(file, 4u);                  // pluginInfoSize = 4 bytes dummy
    PushU32(file, 0u);

    const size_t fltPos = file.size();
    for (int i = 0; i < 25; ++i) PushU32(file, 0u);

    const uint32_t gdt3AbsOff = static_cast<uint32_t>(file.size());
    // Patch flt[5] = gdt3Off, flt[8] = gdt3Count (2), flt[9] = changeFormCount (420)
    std::memcpy(file.data() + fltPos + 5u * 4u, &gdt3AbsOff, 4u);
    const uint32_t gdt3Cnt = 2u;
    const uint32_t cfCnt   = 420u;
    std::memcpy(file.data() + fltPos + 8u * 4u, &gdt3Cnt, 4u);
    std::memcpy(file.data() + fltPos + 9u * 4u, &cfCnt, 4u);

    // GDT3 entry 0: type=1000, len=8
    PushU32(file, 1000u);
    PushU32(file, 8u);
    PushU64(file, 0ull);

    // GDT3 entry 1: type=1001 (Papyrus), len=16, version=6, stringCount=papyrusStringCount
    PushU32(file, 1001u);
    PushU32(file, 16u);
    PushU16(file, 6u);
    PushU32(file, papyrusStringCount);
    file.insert(file.end(), 10u, uint8_t(0u));
    (void)bodyOff;
    return file;
}

}  // namespace

int RunSavePapyrusGuardTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Planted-failure controls: truncated file, bad magic, corrupted header size -> Corrupt
    {
        const uint8_t tiny[8] = {'T','E','S','V','_','S','A','V'};
        const EssSaveAuditReport rTrunc = AuditEssSaveBytes(tiny, sizeof(tiny), "tiny.ess");

        std::vector<uint8_t> badMagic = BuildSyntheticSseSave("Dovahkiin", 42u, "Whiterun", 7u, 15000u);
        std::memcpy(badMagic.data(), "BAD_MAGIC_123", 13u);
        const EssSaveAuditReport rMagic = AuditEssSaveBytes(badMagic.data(), badMagic.size(), "bad_magic.ess");

        std::vector<uint8_t> badHdr = BuildSyntheticSseSave("Dovahkiin", 42u, "Whiterun", 7u, 15000u);
        const uint32_t bogusHdrSize = 0x00FFFFFFu;
        std::memcpy(badHdr.data() + 13u, &bogusHdrSize, 4u);
        const EssSaveAuditReport rHdr = AuditEssSaveBytes(badHdr.data(), badHdr.size(), "bad_hdr.ess");

        check(rTrunc.health == SaveHealthLevel::Corrupt &&
              rMagic.health == SaveHealthLevel::Corrupt &&
              rHdr.health   == SaveHealthLevel::Corrupt,
              "save_guard_planted_corrupt_magic_truncation_and_header_rejected");
    }

    // 2. Synthetic valid uncompressed SSE v12 save parsing (exact fields + Healthy)
    {
        const std::vector<uint8_t> save = BuildSyntheticSseSave("Dovahkiin", 42u, "Whiterun", 17u, 18500u);
        const EssSaveAuditReport r = AuditEssSaveBytes(save.data(), save.size(), "healthy.ess");
        const bool ok =
            r.health == SaveHealthLevel::Healthy &&
            r.version == 12u &&
            r.saveNumber == 17u &&
            r.playerName == "Dovahkiin" &&
            r.playerLevel == 42u &&
            r.playerLocation == "Whiterun" &&
            r.compressionType == 0u &&
            r.formVersion == 78u &&
            r.papyrusAudited &&
            r.papyrusVersion == 6u &&
            r.papyrusStringCount == 18500u &&
            r.changeFormCount == 420u;
        check(ok, "save_guard_synthetic_valid_sse_v12_healthy");
    }

    // 3. Papyrus string-table overflow thresholds: 52,000 -> Warning, 62,000 -> Critical, 65,536 -> Corrupt
    {
        const auto sWarn = BuildSyntheticSseSave("Mage", 55u, "Winterhold", 88u, 52000u);
        const auto sCrit = BuildSyntheticSseSave("Mage", 60u, "Winterhold", 89u, 62000u);
        const auto sOver = BuildSyntheticSseSave("Mage", 65u, "Winterhold", 90u, 65536u);

        const EssSaveAuditReport rWarn = AuditEssSaveBytes(sWarn.data(), sWarn.size(), "warn_52k.ess");
        const EssSaveAuditReport rCrit = AuditEssSaveBytes(sCrit.data(), sCrit.size(), "crit_62k.ess");
        const EssSaveAuditReport rOver = AuditEssSaveBytes(sOver.data(), sOver.size(), "overflow_65536.ess");

        check(rWarn.health == SaveHealthLevel::Warning && rWarn.papyrusStringCount == 52000u,
              "save_guard_papyrus_52000_strings_warning");
        check(rCrit.health == SaveHealthLevel::Critical && rCrit.papyrusStringCount == 62000u,
              "save_guard_papyrus_62000_strings_critical");
        check(rOver.health == SaveHealthLevel::Corrupt && rOver.papyrusStringCount == 65536u,
              "save_guard_papyrus_65536_strings_overflow_corrupt");
    }

    // 4. Real-time Frame/Papyrus Hitch Watchdog + RAW_GetSavePapyrusHealth C ABI + [Stability] INI toggle
    {
        // Save/restore live config cleanly
        const LiveEngineConfig prevCfg = GetLiveConfig();
        LiveEngineConfig cfg = DefaultLiveEngineConfig();
        cfg.savePapyrusGuardEnabled = true;
        SetLiveConfigSnapshot(cfg);

        SavePapyrusGuardInit("__nonexistent_test_saves_dir__");
        for (int i = 0; i < 60; ++i) {
            SavePapyrusGuardOnPresent(16.6);
        }

        RawSavePapyrusHealth hSteady{};
        const int okSteady = RAW_GetSavePapyrusHealth(&hSteady);
        const bool steadyOk =
            okSteady == 1 &&
            hSteady.guardEnabled == 1u &&
            hSteady.totalFrames == 60ull &&
            hSteady.hitchCount == 0u &&
            hSteady.hitchesLast60Sec == 0u &&
            std::fabs(hSteady.p95FrameMs - 16.6f) < 0.25f;
        check(steadyOk, "save_guard_watchdog_steady_60fps_zero_hitches");

        // Inject 85 ms and 120 ms hitches
        SavePapyrusGuardOnPresent(85.0);
        SavePapyrusGuardOnPresent(16.6);
        SavePapyrusGuardOnPresent(120.0);
        SavePapyrusGuardOnPresent(16.6);

        RawSavePapyrusHealth hSpiked{};
        const int okSpiked = RAW_GetSavePapyrusHealth(&hSpiked);
        const bool spikedOk =
            okSpiked == 1 &&
            hSpiked.totalFrames == 64ull &&
            hSpiked.hitchCount == 2u &&
            hSpiked.hitchesLast60Sec == 2u &&
            std::fabs(hSpiked.worstHitchMs - 120.0f) < 0.01f &&
            hSpiked.p95FrameMs >= 16.5f;
        check(spikedOk, "save_guard_watchdog_detects_85ms_and_120ms_hitches_and_c_abi");

        // Verify [Stability] SavePapyrusGuard INI round-trip & 0 disable path ignores new spikes
        const std::string defIni = FormatDefaultLiveConfigIni(DefaultLiveEngineConfig());
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(defIni, parsedDef);
        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[Stability]\nSavePapyrusGuard=0\n", parsedOff);

        SetLiveConfigSnapshot(parsedOff);
        SavePapyrusGuardOnPresent(250.0); // MUST be ignored while SavePapyrusGuard=0!
        RawSavePapyrusHealth hDisabled{};
        RAW_GetSavePapyrusHealth(&hDisabled);

        const bool iniToggleOk =
            defIni.find("[Stability]") != std::string::npos &&
            defIni.find("SavePapyrusGuard=1") != std::string::npos &&
            parsedDef.savePapyrusGuardEnabled &&
            !parsedOff.savePapyrusGuardEnabled &&
            hDisabled.guardEnabled == 0u &&
            hDisabled.hitchCount == 2u; // 250ms spike ignored!
        check(iniToggleOk, "save_guard_stability_ini_roundtrip_and_disable_gate");

        SavePapyrusGuardShutdown();
        SetLiveConfigSnapshot(prevCfg);
    }

    // 5. Optional live real Skyrim SE `.ESS` LZ4 save audit (if present in Documents\My Games\Skyrim Special Edition\Saves)
    {
        char* userProfileBuf = nullptr;
        size_t userProfileLen = 0u;
        std::string userProfile;
        if (_dupenv_s(&userProfileBuf, &userProfileLen, "USERPROFILE") == 0 && userProfileBuf) {
            userProfile = userProfileBuf;
            std::free(userProfileBuf);
        }
        if (!userProfile.empty()) {
            const std::string savesDir = userProfile + "\\Documents\\My Games\\Skyrim Special Edition\\Saves";
            const auto realSaves = AuditSkyrimSavesDirectory(savesDir, 3u);
            if (!realSaves.empty()) {
                bool allParsed = true;
                for (const auto& r : realSaves) {
                    if (!checkFn) {
                        std::printf("[RealESS] %s | ver=%u comp=%u health=%s strings=%u/65535 player=%s(L%u) loc=%s\n",
                                    r.fileName.c_str(), r.version, r.compressionType,
                                    SaveHealthLevelName(r.health), r.papyrusStringCount,
                                    r.playerName.c_str(), r.playerLevel, r.playerLocation.c_str());
                    }
                    if (r.version != 12u || r.health == SaveHealthLevel::Corrupt || !r.papyrusAudited || r.papyrusStringCount == 0u) {
                        allParsed = false;
                    }
                }
                check(allParsed, "save_guard_live_real_skyrim_lz4_ess_saves_audited");
            }
        }
    }

    if (!checkFn) {
        std::printf("SavePapyrusGuardTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_SAVE_PAPYRUS_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunSavePapyrusGuardTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
