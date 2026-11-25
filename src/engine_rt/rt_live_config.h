#pragma once
// rt_live_config.h: Wave 44 Live Hot-Reloading Configuration (raw_engine.ini) & F10 In-Game Runtime Diagnostics Dump.
// Zero-dependency INI serializer/parser with strict safe clamping, sub-microsecond mtime polling,
// and atomic structured live status dump (raw_engine_out\live_status_dump.txt).
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

struct LiveEngineConfig {
    // [Combat]
    float parryWindowMs          = 220.0f;  // [60 .. 450] ms
    float dodgeIFrameMs          = 260.0f;  // [80 .. 500] ms
    float dodgeStaminaCost       = 18.0f;   // [0 .. 60]
    bool  dodgeIFramesEnabled    = true;
    bool  hitstopEnabled         = true;
    float hitstopScale           = 1.0f;    // [0.0 .. 2.0]
    bool  nonSpongyDifficulty    = true;
    bool  tacticalSquadAi        = true;

    // [Physics]
    bool  skeletonXpbdEnabled    = true;
    float xpbdWindScale          = 1.0f;    // [0.0 .. 3.0]
    bool  fovBreathEnabled       = true;
    float fovBreathScale         = 1.0f;    // [0.0 .. 2.5]

    // [World]
    bool  tfcLodVoidGuard        = true;
    bool  livingEconomyEnabled   = true;
    bool  weatherStealthEnabled  = true;
    bool  thermalSurvivalEnabled = true;

    // [Audio]
    bool  liveAudioAmbience      = false;   // Opt-in live 48kHz WASAPI FDN-8 ambient soundscape (0/1, default 0)
    float liveAudioGain          = 0.22f;   // [0.0 .. 0.50] master ambient gain

    // [Stability]
    bool  savePapyrusGuardEnabled = true;   // .ESS save-file health auditor & Papyrus frame-hitch watchdog (0/1, default 1)
    bool  modLoadAuditEnabled     = true;   // TES4 master-dependency & ESL/ESP slot integrity auditor (0/1, default 1)

    // [WorldAI]
    bool  dynamicNpcBehaviorEnabled = true; // Weather/season/economy-driven NPC daily behavior & ecology (0/1, default 1)

    // [Diagnostics]
    uint32_t statusDumpHotkeyVK      = 121u;  // VK_F10 (0x79)
    uint32_t hotReloadIntervalFrames = 60u;   // [10 .. 600] frames
    bool     crashForensicsEnabled   = true;  // VEH fatal crash dump & 7,376-class RTTI symbolizer (0/1, default 1)
};

LiveEngineConfig DefaultLiveEngineConfig();
void             ClampLiveEngineConfig(LiveEngineConfig& cfg);
std::string      FormatDefaultLiveConfigIni(const LiveEngineConfig& cfg);
bool             ParseLiveConfigIni(const std::string& iniText, LiveEngineConfig& outCfg);
bool             EnsureDefaultLiveConfigFile(const std::string& iniPath,
                                             const LiveEngineConfig& defaults = DefaultLiveEngineConfig());
bool             LoadLiveConfigFromFile(const std::string& iniPath, LiveEngineConfig& outCfg);

// Deterministic mtime hot-reloader (compiles identically in pure tests & live raw_engine.dll).
class LiveConfigHotReloader {
public:
    bool Init(const std::string& iniPath, bool autoCreateDefault = true);
    bool PollReload(bool forceCheck = false);

    const LiveEngineConfig& Config() const { return cfg_; }
    const std::string&      Path() const { return iniPath_; }
    uint32_t                ReloadCount() const { return reloadCount_; }

private:
    bool QueryFileStamp(uint64_t& outWriteStamp, uint64_t& outSize) const;

    std::string      iniPath_;
    LiveEngineConfig cfg_{};
    uint64_t         lastWriteStamp_ = 0;
    uint64_t         lastFileSize_   = 0;
    uint32_t         frameCounter_   = 0;
    uint32_t         reloadCount_    = 0;
    bool             initialized_    = false;
};

struct LiveStatusDumpData {
    LiveEngineConfig config{};
    uint32_t         configReloads       = 0;
    std::string      iniPath;
    std::string      runtimeVersion      = "1.7.104.0";

    // Zydis binary offset verification
    uint32_t zydisPassed  = 41;
    uint32_t zydisChecked = 41;
    bool     zydisOk      = true;

    // Skeleton / IK / XPBD
    bool     skeletonHookActive = false;
    uint32_t skeletonActors     = 0;
    uint32_t skeletonIkBones    = 0;
    uint32_t skeletonXpbdBones  = 0;
    uint32_t skeletonUpdated    = 0;
    uint64_t skeletonTotalRuns  = 0;

    // Combat / Parry / Dodge / Hitstop / FOV
    uint8_t  gameplayHooksMask = 0;
    uint32_t hitsProcessed     = 0;
    uint32_t timedParries      = 0;
    uint32_t guardBreaks       = 0;
    uint32_t counterHits       = 0;
    uint32_t dodgesTriggered   = 0;
    uint32_t evadedHits        = 0;
    bool     hitstopVerified   = false;
    uint32_t hitstopTriggers   = 0;
    uint32_t decalsActiveNow   = 0;
    uint32_t decalsTotal       = 0;
    float    baselineFovDeg    = 80.0f;
    float    appliedFovOffset  = 0.0f;

    // 9-Hold Economy Ledger
    std::string activeHoldName      = "Whiterun";
    uint8_t     marketTier          = 1;
    uint32_t    economyDaysSim      = 0;
    uint32_t    economyDayOfYear    = 228;
    uint32_t    economyTrades       = 0;
    float       economyBuyMult      = 1.0f;
    float       economySellMult     = 1.0f;
    float       economyScarcity01   = 0.0f;
    float       caravanDisruption01 = 0.0f;
    uint8_t     npcDirective        = 0;
    float       npcShelterUrgency01 = 0.0f;
    float       npcTorchNeed01      = 0.0f;

    // Weather Stealth / Thermal Survival / Acoustics
    bool     sneakHookActive   = false;
    uint32_t sneakChecks       = 0;
    uint32_t sneakMasked       = 0;
    float    coreWarmth        = 100.0f;
    float    wetness01         = 0.0f;
    float    windChillC        = 10.0f;
    float    staminaRegenMult  = 1.0f;
    uint8_t  acousticEnvKind   = 0;
    float    acousticRt60Sec   = 0.20f;

    // Live Skyrim Calendar
    float    calYear       = 201.0f;
    float    calMonth      = 7.0f;
    float    calDay        = 17.0f;
    float    calHour       = 12.0f;
    float    calDaysPassed = 0.0f;
    float    calTimeScale  = 20.0f;

    // Save Health & Papyrus Hitch Watchdog
    uint16_t savesAudited     = 0u;
    uint8_t  worstSaveHealth  = 0u;
    uint32_t worstSaveStrings = 0u;
    float    worstSaveSizeMb  = 0.0f;
    uint32_t hitchCount       = 0u;
    uint32_t hitchesLast60Sec = 0u;
    float    worstHitchMs     = 0.0f;
    float    p95FrameMs       = 0.0f;

    // NPC Behavior Ecology
    float    npcShelterPressure01   = 0.0f;
    float    npcTorchDeployRatio    = 0.0f;
    float    npcGuardAlertnessMult  = 1.0f;
    float    npcMarketActivity01    = 1.0f;
    uint8_t  npcPreferredBanditElem = 0u;
    uint8_t  npcPreferredMageElem   = 1u;

    // Mod Load-Order Integrity Audit
    uint8_t  modAuditSeverity       = 0u;
    uint16_t modFullPlugins         = 0u;
    uint16_t modLightEsls           = 0u;
    uint16_t modMissingMasters      = 0u;
    uint16_t modOutOfOrderMasters   = 0u;
    uint16_t modCorruptHeaders      = 0u;
    uint16_t modOversizedEsls       = 0u;
};

std::string FormatLiveStatusDump(const LiveStatusDumpData& d);
bool        WriteLiveStatusDumpToFile(const std::string& outPath, const LiveStatusDumpData& d);

// Thread-safe active config getter (works in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
LiveEngineConfig GetLiveConfig();
void             SetLiveConfigSnapshot(const LiveEngineConfig& cfg);

#ifndef RAW_SIM_PURE_TEST
std::string LiveConfigInit(const std::string& iniPath, const std::string& dumpOutPath);
bool        LiveConfigPollReload(std::string* logMsgOut = nullptr);
bool        LiveConfigWriteStatusDump(std::string* outPathWritten = nullptr);
void        LiveConfigOnPresent(void (*logFn)(const char* msg) = nullptr);
void        LiveConfigShutdown();
#endif

}  // namespace raw::rt
