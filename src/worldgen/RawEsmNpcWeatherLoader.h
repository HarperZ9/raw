#pragma once
// RawEsmNpcWeatherLoader.h: Clean-Room Native `.ESM` World Metadata Parser (`raw::worldgen`).
// Streams top-level `WTHR` (Weather), `CLMT` (Climate), `NPC_` (Actors), `REGN` (Regions),
// and `QUST` (Quests) GRUPs directly from `Skyrim.esm` (< 250 ms via seeking past non-target GRUPs)
// using built-in clean-room RFC 1950/1951 zlib inflate, and maps authentic `WTHR` records directly
// into physical `RawWeatherSim` climate parameters!
// Zero Windows, D3D12, or Bethesda SDK dependencies. Original work, MIT.

#include "RawWeatherSim.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::worldgen {

struct EsmWeatherColorSet {
    Vec3 skyUpperDay{0.35f, 0.55f, 0.85f};
    Vec3 skyUpperNight{0.04f, 0.06f, 0.14f};
    Vec3 skyLowerDay{0.60f, 0.72f, 0.88f};
    Vec3 skyLowerNight{0.06f, 0.08f, 0.16f};
    Vec3 horizonDay{0.75f, 0.82f, 0.90f};
    Vec3 horizonNight{0.08f, 0.10f, 0.18f};
    Vec3 sunlightDay{1.00f, 0.96f, 0.88f};
    Vec3 sunlightNight{0.25f, 0.32f, 0.48f};
    Vec3 sunDay{1.00f, 0.98f, 0.92f};
    Vec3 sunNight{0.20f, 0.25f, 0.35f};
    Vec3 ambientDay{0.35f, 0.42f, 0.52f};
    Vec3 ambientNight{0.08f, 0.10f, 0.16f};
    Vec3 fogNearDay{0.68f, 0.75f, 0.84f};
    Vec3 fogNearNight{0.07f, 0.09f, 0.14f};
    Vec3 fogFarDay{0.72f, 0.79f, 0.88f};
    Vec3 fogFarNight{0.08f, 0.10f, 0.16f};
};

struct EsmWeatherRecord {
    uint32_t           formId                  = 0u;
    std::string        editorId;
    bool               hasData                 = false;
    bool               hasNam0                 = false;
    bool               hasFnam                 = false;
    uint8_t            windSpeedByte           = 0u;    // DATA[0] 0..255
    float              cloudSpeedMean01        = 0.0f;  // Mean RNAM/QNAM cloud layer speed [0..1]
    float              cloudAlphaDayMean01     = 0.0f;  // Mean JNAM day alpha over enabled cloud layers
    uint32_t           disabledCloudLayersMask = 0u;    // NAM1 bitmask of disabled layers
    uint8_t            transDelta              = 0u;    // DATA[3]
    uint8_t            sunGlare                = 0u;    // DATA[4]
    uint8_t            sunDamage               = 0u;    // DATA[5]
    uint8_t            precipBeginFadeIn       = 0u;    // DATA[6]
    uint8_t            precipEndFadeOut        = 0u;    // DATA[7]
    uint8_t            thunderFrequency        = 0u;    // DATA[10]
    uint8_t            precipFlags             = 0u;    // DATA[11]: 0x01 pleasant, 0x02 cloudy, 0x04 rainy, 0x08 snow
    float              windDirectionDeg        = 0.0f;  // DATA[17] scaled to [0..360)
    float              fogDayNear              = 0.0f;  // FNAM[0]
    float              fogDayFar               = 100000.0f; // FNAM[1]
    float              fogNightNear            = 0.0f;  // FNAM[2]
    float              fogNightFar             = 100000.0f; // FNAM[3]
    float              fogDayPow               = 1.0f;  // FNAM[4]
    float              fogNightPow             = 1.0f;  // FNAM[5]
    float              fogDayMax               = 1.0f;  // FNAM[6]
    float              fogNightMax             = 1.0f;  // FNAM[7]
    EsmWeatherColorSet colors{};

    bool IsPleasant() const noexcept { return (precipFlags & 0x01u) != 0u; }
    bool IsCloudy()   const noexcept { return (precipFlags & 0x02u) != 0u; }
    bool IsRainy()    const noexcept { return (precipFlags & 0x04u) != 0u; }
    bool IsSnowy()    const noexcept { return (precipFlags & 0x08u) != 0u; }
};

struct EsmWeatherChanceEntry {
    uint32_t weatherFormId = 0u;
    uint32_t chance        = 0u;
    uint32_t globalFormId  = 0u;
};

struct EsmClimateRecord {
    uint32_t                           formId              = 0u;
    std::string                        editorId;
    uint16_t                           sunriseBeginMin     = 360u;  // Minutes since midnight (TNAM[0]*10)
    uint16_t                           sunriseEndMin       = 480u;  // TNAM[1]*10
    uint16_t                           sunsetBeginMin      = 1080u; // TNAM[2]*10
    uint16_t                           sunsetEndMin        = 1200u; // TNAM[3]*10
    uint8_t                            volatility          = 0u;    // TNAM[4]
    uint8_t                            moonPhaseLengthDays = 0u;    // TNAM[5] & 0x3F
    bool                               hasMasser           = false; // (TNAM[5] & 0x80) != 0
    bool                               hasSecunda          = false; // (TNAM[5] & 0x40) != 0
    std::vector<EsmWeatherChanceEntry> weatherList;                 // WLST entries
};

struct EsmNpcRecord {
    uint32_t    formId           = 0u;
    std::string editorId;
    std::string fullName;          // Inline FULL string (empty when localized lstring ID)
    uint32_t    fullNameStringId = 0u; // Localized FULL string-table ID (when TES4 localized flag set)
    uint32_t    acbsFlags        = 0u;
    uint16_t    level            = 1u;
    uint16_t    calcMinLevel     = 0u;
    uint16_t    calcMaxLevel     = 0u;
    uint16_t    speedMult        = 100u;
    uint32_t    raceFormId       = 0u;
    uint32_t    factionCount     = 0u; // Number of SNAM faction records
};

struct EsmRegionRecord {
    uint32_t                           formId = 0u;
    std::string                        editorId;
    std::vector<EsmWeatherChanceEntry> weatherTable; // RDWT weather probability entries
};

struct EsmQuestRecord {
    uint32_t    formId           = 0u;
    std::string editorId;
    std::string fullName;
    uint32_t    fullNameStringId = 0u;
    uint16_t    flags            = 0u; // DNAM flags
    uint8_t     priority         = 0u; // DNAM priority
    uint32_t    questType        = 0u; // DNAM type
};

struct EsmPhysicalWeatherParams {
    WeatherRegime suggestedRegime    = WeatherRegime::ClearAlpine;
    PrecipPhase   precipPhase        = PrecipPhase::None;
    float         beaufortWindScale  = 1.0f;   // [0..12]
    float         windSpeedMps       = 2.0f;   // [0..35] m/s
    float         windDirectionRad   = 0.0f;   // Radians
    float         cloudCover01       = 0.15f;  // [0..1]
    float         rainRate01         = 0.0f;   // [0..1]
    float         snowRate01         = 0.0f;   // [0..1]
    float         fogDensity         = 0.0f;   // [0..1] optical extinction proxy
    float         thunderIntensity01 = 0.0f;   // [0..1]
    Vec3          skyTintRgb{0.45f, 0.65f, 0.90f};
    Vec3          horizonTintRgb{0.72f, 0.80f, 0.90f};
    Vec3          sunTintRgb{1.0f, 0.96f, 0.88f};
    Vec3          ambientTintRgb{0.35f, 0.42f, 0.52f};
};

struct EsmWorldMetaIndex {
    std::string                    esmPath;
    bool                           isLocalized    = false;
    uint32_t                       compressedSeen = 0u;
    double                         loadTimeMs     = 0.0;

    std::vector<EsmWeatherRecord>  weathers;
    std::vector<EsmClimateRecord>  climates;
    std::vector<EsmNpcRecord>      npcs;
    std::vector<EsmRegionRecord>   regions;
    std::vector<EsmQuestRecord>    quests;

    std::unordered_map<uint32_t, size_t>    weatherByFormId;
    std::unordered_map<std::string, size_t> weatherByEdidLower;
    std::unordered_map<uint32_t, size_t>    climateByFormId;
    std::unordered_map<std::string, size_t> climateByEdidLower;
    std::unordered_map<uint32_t, size_t>    npcByFormId;
    std::unordered_map<std::string, size_t> npcByEdidLower;
    std::unordered_map<uint32_t, size_t>    regionByFormId;
    std::unordered_map<std::string, size_t> regionByEdidLower;
    std::unordered_map<uint32_t, size_t>    questByFormId;
    std::unordered_map<std::string, size_t> questByEdidLower;

    const EsmWeatherRecord* FindWeatherByFormId(uint32_t formId) const;
    const EsmWeatherRecord* FindWeatherByEdid(const std::string& edid) const;
    const EsmClimateRecord* FindClimateByFormId(uint32_t formId) const;
    const EsmClimateRecord* FindClimateByEdid(const std::string& edid) const;
    const EsmNpcRecord*     FindNpcByFormId(uint32_t formId) const;
    const EsmNpcRecord*     FindNpcByEdid(const std::string& edid) const;
    const EsmRegionRecord*  FindRegionByFormId(uint32_t formId) const;
    const EsmRegionRecord*  FindRegionByEdid(const std::string& edid) const;
    const EsmQuestRecord*   FindQuestByFormId(uint32_t formId) const;
    const EsmQuestRecord*   FindQuestByEdid(const std::string& edid) const;
};

// Fast streaming parser (< 250 ms on 250 MB Skyrim.esm): loads all WTHR, CLMT, NPC_, REGN, QUST records.
bool LoadEsmWorldMeta(const std::string& esmPath, EsmWorldMetaIndex* outIndex, std::string* outErr = nullptr);
bool LoadEsmWorldMetaFromMemory(const uint8_t* bytes, size_t size, const std::string& label,
                                EsmWorldMetaIndex* outIndex, std::string* outErr = nullptr);

// Converts any authentic Skyrim.esm WTHR record into physical RawWeatherSim climate parameters.
EsmPhysicalWeatherParams MapEsmWeatherToPhysicalParams(const EsmWeatherRecord& wthr,
                                                       float timeOfDayHours = 13.0f);

// Applies a mapped Skyrim.esm WTHR record directly to a live RawWeatherSim instance.
EsmPhysicalWeatherParams ApplyEsmWeatherToSim(const EsmWeatherRecord& wthr,
                                              RawWeatherSim& sim,
                                              float timeOfDayHours = 13.0f,
                                              bool immediate = false);

}  // namespace raw::worldgen
