#ifndef RAW_MOD_SDK_ABI_H_INCLUDED
#define RAW_MOD_SDK_ABI_H_INCLUDED
/*
 * raw_mod_sdk_abi.h: Wave 86 Public SemVer v2.0 C11/C++23 Mod-Developer SDK ABI.
 *
 * Extends raw_engine_sdk.h (v1.0 192-byte RawUnifiedEngineSnapshot) with:
 *   - Fail-closed SemVer v2.0 ABI negotiation (RAW_MOD_SDK_ABI_VERSION = 0x00020000u)
 *   - Frame-graph render pass registration with DAG cycle rejection
 *   - Non-colliding live debug view registration (slots 100..255)
 *   - Capability-gated read-only G-buffer / actor / navmesh queries
 *   - Per-mod CPU microsecond & VRAM byte attribution with soft/hard budget enforcement
 *
 * Clean-room MIT, zero Windows or proprietary headers required.
 */

#include <stddef.h>
#include <stdint.h>

#include "raw_engine_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_MOD_SDK_ABI_VERSION_MAJOR 2u
#define RAW_MOD_SDK_ABI_VERSION_MINOR 0u
#define RAW_MOD_SDK_ABI_VERSION_PATCH 0u
#define RAW_MOD_SDK_ABI_VERSION       0x00020000u /* 2.0.0 */

/* Fail-closed status codes */
#define RAW_MOD_OK                    0
#define RAW_MOD_ERR_NULL_ARG         -1
#define RAW_MOD_ERR_ABI_MISMATCH     -2
#define RAW_MOD_ERR_STRUCT_SIZE      -3
#define RAW_MOD_ERR_CYCLE            -4
#define RAW_MOD_ERR_BUDGET_EXCEEDED  -5
#define RAW_MOD_ERR_DUPLICATE        -6
#define RAW_MOD_ERR_CAPABILITY_DENIED -7
#define RAW_MOD_ERR_INVALID_SLOT     -8
#define RAW_MOD_ERR_UNKNOWN_MOD      -9

/* Sandboxed effect / capability bitmask (mirrors sdk/raw_mod_sdk.bld) */
#define RAW_MOD_CAP_READ_GBUFFER     (1u << 0)
#define RAW_MOD_CAP_READ_ACTORS      (1u << 1)
#define RAW_MOD_CAP_READ_NAVMESH     (1u << 2)
#define RAW_MOD_CAP_RENDER_PASS      (1u << 3)
#define RAW_MOD_CAP_DEBUG_VIEW       (1u << 4)
#define RAW_MOD_CAP_BUILDLANG_FFI    (1u << 5)

#define RAW_MOD_NO_DEPENDENCY        0xFFFFFFFFu
#define RAW_MOD_DEBUG_SLOT_MIN       100u
#define RAW_MOD_DEBUG_SLOT_MAX       255u

/* Exact 64-byte frame-graph pass registration descriptor */
typedef struct RawModPassDesc {
    uint32_t structSize;       /* Must be >= 64 */
    uint32_t modId;
    uint32_t passId;
    uint32_t dependsOnPassId;  /* RAW_MOD_NO_DEPENDENCY if root pass */
    uint32_t requiredCaps;
    uint32_t flags;
    uint64_t callbackTag;
    char     passName[32];
} RawModPassDesc;

/* Exact 64-byte live debug view registration descriptor */
typedef struct RawModDebugViewDesc {
    uint32_t structSize;       /* Must be >= 64 */
    uint32_t modId;
    uint32_t viewSlot;         /* Must be in [100..255] so core views 0..8 stay reserved */
    uint32_t flags;
    char     viewName[48];
} RawModDebugViewDesc;

/* Exact 64-byte read-only G-buffer / actor / navmesh query snapshot */
typedef struct RawModReadOnlyWorldQuery {
    uint32_t structSize;       /* Must be >= 64 */
    uint32_t activeActorCount;
    uint32_t navClusterCount;
    uint32_t gbufferWidth;
    uint32_t gbufferHeight;
    uint32_t activeDebugView;
    uint64_t frameIndex;
    float    cameraPos[3];
    float    cameraYaw;
    uint64_t reserved[2];
} RawModReadOnlyWorldQuery;

/* Exact 64-byte per-mod CPU-microsecond + VRAM-byte attribution record */
typedef struct RawModBudgetRecord {
    uint32_t modId;
    uint32_t softCpuBreaches;
    uint32_t hardCpuBreaches;
    uint32_t hardVramBreaches;
    uint64_t cpuTimeUs;
    uint64_t softCpuCapUs;
    uint64_t hardCpuCapUs;
    uint64_t vramBytes;
    uint64_t softVramCapBytes;
    uint64_t hardVramCapBytes;
} RawModBudgetRecord;

/* Function pointer table for SemVer v2.0 Mod SDK consumers */
typedef int (*PFN_RAW_ModRegisterPass)(void* hostCtx, const RawModPassDesc* desc);
typedef int (*PFN_RAW_ModRegisterDebugView)(void* hostCtx, const RawModDebugViewDesc* desc);
typedef int (*PFN_RAW_ModQueryWorld)(void* hostCtx, uint32_t modId, uint32_t reqCaps, RawModReadOnlyWorldQuery* outQuery);
typedef int (*PFN_RAW_ModRecordUsage)(void* hostCtx, uint32_t modId, uint64_t cpuUs, uint64_t vramBytes);
typedef int (*PFN_RAW_ModGetBudget)(void* hostCtx, uint32_t modId, RawModBudgetRecord* outBudget);

typedef struct RawModSdkApiV2 {
    uint32_t                          structSize;
    uint32_t                          abiVersion;
    void*                             hostCtx;
    PFN_RAW_GetUnifiedEngineSnapshot  GetUnifiedSnapshot;
    PFN_RAW_ModRegisterPass           RegisterRenderPass;
    PFN_RAW_ModRegisterDebugView      RegisterDebugView;
    PFN_RAW_ModQueryWorld             QueryReadOnlyWorld;
    PFN_RAW_ModRecordUsage            RecordModUsage;
    PFN_RAW_ModGetBudget              GetModBudget;
} RawModSdkApiV2;

#if defined(__cplusplus)
static_assert(sizeof(RawModPassDesc) == 64u, "RawModPassDesc ABI must be 64 bytes");
static_assert(sizeof(RawModDebugViewDesc) == 64u, "RawModDebugViewDesc ABI must be 64 bytes");
static_assert(sizeof(RawModReadOnlyWorldQuery) == 64u, "RawModReadOnlyWorldQuery ABI must be 64 bytes");
static_assert(sizeof(RawModBudgetRecord) == 64u, "RawModBudgetRecord ABI must be 64 bytes");
static_assert(offsetof(RawModBudgetRecord, cpuTimeUs) == 16u, "RawModBudgetRecord::cpuTimeUs offset mismatch");
static_assert(offsetof(RawModBudgetRecord, vramBytes) == 40u, "RawModBudgetRecord::vramBytes offset mismatch");
static_assert(sizeof(RawModSdkApiV2) == 64u, "RawModSdkApiV2 ABI must be 64 bytes on x64");
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(RawModPassDesc) == 64u, "RawModPassDesc ABI must be 64 bytes");
_Static_assert(sizeof(RawModDebugViewDesc) == 64u, "RawModDebugViewDesc ABI must be 64 bytes");
_Static_assert(sizeof(RawModReadOnlyWorldQuery) == 64u, "RawModReadOnlyWorldQuery ABI must be 64 bytes");
_Static_assert(sizeof(RawModBudgetRecord) == 64u, "RawModBudgetRecord ABI must be 64 bytes");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAW_MOD_SDK_ABI_H_INCLUDED */
