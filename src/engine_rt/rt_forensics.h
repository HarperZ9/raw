#pragma once
// Deep Engine Crash Forensics, Flight Recorder & Proactive RAM-Pressure Guard (RAW engine runtime).
// Surpasses static post-mortem crash loggers by capturing live memory pressure, VirtualQuery fault
// classification (NULL_DEREF / UNINIT_TRUNCATED_PTR / RAM_EXHAUSTION_OOM / FREED_MEMORY), active
// cell/camera EngineView, and a lock-free pre-crash event ring buffer into raw_forensics_crash.json.
// Original work, MIT.
#include <cstdint>
#include <string>

namespace raw::rt {

void ForensicsInit(const std::string& outDir);
void ForensicsShutdown();
void ForensicsOnPresent();
void ForensicsRecord(const char* tag, uint64_t a = 0, uint64_t b = 0);
std::string ForensicsStatusJson();

}  // namespace raw::rt
