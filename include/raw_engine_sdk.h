#ifndef RAW_ENGINE_SDK_H_INCLUDED
#define RAW_ENGINE_SDK_H_INCLUDED
/*
 * raw_engine_sdk.h: Wave 55 Public Versioned C11/C++20 ABI Header for raw_engine.dll.
 *
 * Self-contained standard C/C++ header (<stdint.h>, <stddef.h> only — ZERO Windows or internal
 * engine headers required). Any third-party SKSE plugin, overlay, profiler, or external tool can
 * `#include "raw_engine_sdk.h"` and query live RAW engine telemetry via GetProcAddress:
 *
 *   HMODULE rawMod = GetModuleHandleA("raw_engine.dll");
 *   auto fnVer  = (PFN_RAW_GetSdkVersion)GetProcAddress(rawMod, "RAW_GetSdkVersion");
 *   auto fnSnap = (PFN_RAW_GetUnifiedEngineSnapshot)GetProcAddress(rawMod, "RAW_GetUnifiedEngineSnapshot");
 *   RawUnifiedEngineSnapshot snap = {0};
 *   snap.structSize = (uint32_t)sizeof(RawUnifiedEngineSnapshot);
 *   if (fnSnap && fnSnap(&snap)) { ... }
 *
 * Original work, MIT.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_ENGINE_SDK_VERSION_MAJOR 1u
#define RAW_ENGINE_SDK_VERSION_MINOR 0u
#define RAW_ENGINE_SDK_VERSION_PATCH 0u
#define RAW_ENGINE_SDK_VERSION       0x00010000u /* 1.0.0 */

#if defined(RAW_SDK_EXPORTS) && defined(_WIN32)
#define RAW_SDK_API __declspec(dllexport)
#else
#define RAW_SDK_API
#endif

/* Bitmask constants for RawUnifiedEngineSnapshot::activeSubsystemMask (17 runtime subsystems). */
#define RAW_SUBSYS_ENGINE_HEALTH         (1u << 0)
#define RAW_SUBSYS_FRAME_GOVERNOR        (1u << 1)
#define RAW_SUBSYS_TELEMETRY_RECORDER    (1u << 2)
#define RAW_SUBSYS_AUDIO_DIRECTOR        (1u << 3)
#define RAW_SUBSYS_NPC_BEHAVIOR          (1u << 4)
#define RAW_SUBSYS_SAVE_PAPYRUS_GUARD    (1u << 5)
#define RAW_SUBSYS_MOD_LOAD_AUDIT        (1u << 6)
#define RAW_SUBSYS_MAGIC_PHYSICS         (1u << 7)
#define RAW_SUBSYS_PROGRESSION_ECONOMY   (1u << 8)
#define RAW_SUBSYS_AI_PACKAGES           (1u << 9)
#define RAW_SUBSYS_WORLD_RULES_GMST      (1u << 10)
#define RAW_SUBSYS_PHYSICS_CAMERA_GMST   (1u << 11)
#define RAW_SUBSYS_STEALTH_SOCIAL_GMST   (1u << 12)
#define RAW_SUBSYS_LOCOMOTION_MELEE_GMST (1u << 13)
#define RAW_SUBSYS_ALCHEMY_ENCHANT_GMST  (1u << 14)
#define RAW_SUBSYS_WORLD_SIM             (1u << 15)
#define RAW_SUBSYS_MEMORY_GUARDS         (1u << 16)
#define RAW_SUBSYS_ALL_MASK              0x0001FFFFu

/*
 * Fixed-width, naturally aligned ABI POD uniting all 17 live raw_engine.dll subsystems.
 * Caller MUST initialize `structSize = (uint32_t)sizeof(RawUnifiedEngineSnapshot)` before calling
 * RAW_GetUnifiedEngineSnapshot(). Calls with nullptr or undersized structSize fail closed (return 0).
 */
typedef struct RawUnifiedEngineSnapshot {
    /* Header & Identity (bytes 0..15) */
    uint32_t structSize;          /* Caller-provided sizeof(RawUnifiedEngineSnapshot) >= 192 */
    uint32_t sdkVersion;          /* RAW_ENGINE_SDK_VERSION (0x00010000u) */
    uint32_t activeSubsystemMask; /* Bitmask of RAW_SUBSYS_* subsystems active/queried */
    uint32_t verifiedGmstModules; /* Count of GMST modules active/verified (0..8) */

    /* Engine Health, Handles, Saves & Mod Audit (bytes 16..39) */
    int32_t  healthScore;         /* Composite 0..100 engine health score */
    int32_t  healthGrade;         /* 0=Optimal, 1=Healthy, 2=Degraded, 3=Critical */
    uint32_t activeHandles;       /* Estimated active Creation Engine ref-handles (/ 1,048,576) */
    uint32_t saveMaxStrings;      /* Worst .ESS Papyrus string-table count (/ 65,535) */
    uint32_t modPluginCount;      /* Total TES4 plugins audited */
    uint32_t modIssueCount;       /* Sum of missing/out-of-order/corrupt/oversized-ESL issues */

    /* Frame Pacing & Quality Scaling Governor (bytes 40..71) */
    float    frameP95Ms;          /* Rolling p95 frame time (ms) */
    float    frameMeanMs;         /* Rolling mean frame time (ms) */
    float    renderScale;         /* PI governor render resolution scale [0.75 .. 1.00] */
    float    volumetricQuality01; /* PI governor volumetric fog/cloud scale [0.50 .. 1.00] */
    float    shadowDistanceScale; /* PI governor shadow cascade distance scale [0.65 .. 1.00] */
    float    floraLodScale;       /* PI governor flora LOD scale [0.60 .. 1.00] */
    float    giRayBudget01;       /* PI governor GI ray budget [0.50 .. 1.00] */
    uint32_t governorRegime;      /* 0=DeadbandHold, 1=ScalingDown, 2=RecoveringUp, 3=DisabledUnity */

    /* World Simulation & NPC Ecology (bytes 72..95) */
    float    surfaceTempC;        /* Live ambient surface temperature (Celsius) */
    float    snowpackM;           /* Live accumulated snowpack depth (meters) */
    float    windBeaufort;        /* Physical Beaufort wind scale [0 .. 12] */
    float    shelterPressure01;   /* NPC shelter urgency [0 .. 1] */
    float    torchDeployment01;   /* NPC dusk/night torch deployment ratio [0 .. 1] */
    float    guardAlertnessMult;  /* NPC guard alertness multiplier */

    /* Elemental Magic Weather/Water Synergy (bytes 96..115) */
    float    fireDamageMult;      /* [0.78 .. 1.18] */
    float    frostDamageMult;     /* [1.00 .. 1.45] */
    float    shockDamageMult;     /* [1.00 .. 1.55] */
    uint32_t magicSteamBurst;     /* 1 when fire meets rain/water */
    uint32_t magicWaterArc;       /* 1 when wet/submerged amplifies shock */

    /* Adaptive Audio Director (bytes 116..135) */
    int32_t  audioMood;           /* 0=ExploreDay .. 7=UnderwaterMuffle */
    float    audioLpfCutoffHz;    /* Active acoustic low-pass cutoff (Hz) */
    float    audioReverbWetMult;  /* Active reverb wet multiplier */
    float    audioDuckGain;       /* Dialogue/menu ducking gain [0.32 .. 1.00] */
    uint32_t audioTransitions;    /* Total hysteresis-approved mood transitions */

    /* Telemetry & Binary-Verified GMST Runtime Summary (bytes 136..175) */
    uint32_t hitchCount50ms;      /* Frame hitches exceeding 50ms */
    uint32_t modAuditSeverity;    /* 0=Clean, 1=Warning, 2=Critical */
    uint32_t worstSaveHealth;     /* 0=Healthy, 1=Warning, 2=Critical, 3=Corrupt */
    uint32_t gmstTotalVerified;   /* Total verified GMSTs reported across queried modules */
    uint32_t gmstTotalApplied;    /* Total live-applied GMSTs reported across queried modules */
    float    craftingAsymptote;   /* Anti-exploit crafting asymptote cap */
    float    aiViewConeDeg;       /* Tactical AI effective view cone (deg) */
    float    crimeAlarmRadiusU;   /* Effective crime witness alarm radius (Skyrim units) */
    float    meleeDeathForceCap;  /* Havok melee ragdoll impulse cap */
    float    sneakMaxDistanceU;   /* Stealth max detection distance (Skyrim units) */

    /* 64-bit Frame Counter & Process Working Set (bytes 176..191) */
    uint64_t totalRecordedFrames; /* Total frames recorded by flight recorder */
    uint32_t workingSetMB;        /* Process working set (MB) */
    uint32_t reserved0;           /* Reserved for forward ABI compatibility (always 0) */
} RawUnifiedEngineSnapshot;

#if defined(__cplusplus)
static_assert(sizeof(RawUnifiedEngineSnapshot) == 192u,
              "RawUnifiedEngineSnapshot ABI size must remain exactly 192 bytes");
static_assert(alignof(RawUnifiedEngineSnapshot) == 8u,
              "RawUnifiedEngineSnapshot ABI alignment must remain 8 bytes");
static_assert(offsetof(RawUnifiedEngineSnapshot, totalRecordedFrames) == 176u,
              "RawUnifiedEngineSnapshot ABI offset for totalRecordedFrames mismatch");
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(RawUnifiedEngineSnapshot) == 192u,
               "RawUnifiedEngineSnapshot ABI size must remain exactly 192 bytes");
_Static_assert(offsetof(RawUnifiedEngineSnapshot, totalRecordedFrames) == 176u,
               "RawUnifiedEngineSnapshot ABI offset for totalRecordedFrames mismatch");
#endif

/* Function pointer typedefs for dynamic GetProcAddress() consumers. */
typedef uint32_t (*PFN_RAW_GetSdkVersion)(void);
typedef int      (*PFN_RAW_GetUnifiedEngineSnapshot)(RawUnifiedEngineSnapshot* out);

/* Exported C ABI entry points implemented in raw_engine.dll (rt_sdk_bridge.cpp). */
RAW_SDK_API uint32_t RAW_GetSdkVersion(void);
RAW_SDK_API int      RAW_GetUnifiedEngineSnapshot(RawUnifiedEngineSnapshot* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAW_ENGINE_SDK_H_INCLUDED */
