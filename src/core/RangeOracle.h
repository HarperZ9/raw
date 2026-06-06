#pragma once
#include <cstdint>

struct ID3D11ShaderResourceView;

// Tier 1.3 per-pass numeric range oracle. GATED behind [Diagnostics] GpuReadback
// in RAW.ini (default OFF). The GPU dispatch/readback path is COMPILER-VERIFIED
// ONLY -- it has not been validated in-game (no game in the build environment);
// see RangeOracle.cpp. The host side (raw_eyes read_ranges + the sign-flip
// reduction math) IS host-verified.
namespace RangeOracle
{
    // Reads [Diagnostics] GpuReadback once (default false); cached.
    bool Enabled();
    // Set from ConfigManager::ApplyToSystems ([Diagnostics] GpuReadback, default false).
    void SetEnabled(bool enabled);
    // GPU min/max/NaN/Inf reduction over `srv` (pre-clamp) -> live/ranges.jsonl.
    // No-op if disabled / srv null / ComputeManager uninitialised.
    void Inspect(uint32_t frame, const char* passName, ID3D11ShaderResourceView* srv);
}
