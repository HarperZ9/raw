#pragma once
// rt_sdk_bridge.h: Wave 55 17-Subsystem Unified Public C ABI Aggregator & JSON Snapshot Exporter (`raw::rt`).
// Unifies all 17 live raw_engine.dll runtime subsystems behind RAW_GetSdkVersion() /
// RAW_GetUnifiedEngineSnapshot() and flushes `raw_engine_out\unified_sdk_snapshot.json` on clean stop.
// Original work, MIT.

#ifndef RAW_SDK_EXPORTS
#define RAW_SDK_EXPORTS 1
#endif
#include "../../include/raw_engine_sdk.h"

#include <string>

namespace raw::rt {

// Initializes the SDK bridge output directory for clean-stop JSON flushing.
std::string SdkBridgeInit(const std::string& outDir);

// Formats a RawUnifiedEngineSnapshot as deterministic, well-formed JSON.
std::string SdkBridgeFormatSnapshotJson(const RawUnifiedEngineSnapshot& snap);

// Captures a live snapshot and atomically exports it as JSON to `path` (or `<outDir>\unified_sdk_snapshot.json`
// if `path == nullptr || path[0] == '\0'`). Returns true on success.
bool SdkBridgeExportSnapshotJson(const char* path);

// Flushes final `<outDir>\unified_sdk_snapshot.json` and resets runtime state.
void SdkBridgeShutdown();

}  // namespace raw::rt
