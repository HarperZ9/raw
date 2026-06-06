// RangeOracle.cpp -- Tier 1.3 per-pass numeric range oracle (GATED, default OFF).
//
// VERIFICATION STATUS (read before trusting any ranges.jsonl):
//   * Host reader (raw_eyes read_ranges) and the order-preserving sign-flip
//     reduction MATH are host-verified -- raw_eyes selftest mirrors this exact
//     bit-trick on synthetic floats (incl. negatives / NaN / Inf).
//   * This GPU dispatch/readback path is COMPILER-VERIFIED ONLY. It has NOT been
//     validated in-game (no game in the build environment). Gated behind
//     [Diagnostics] GpuReadback (RAW.ini), default false. Enable it and confirm
//     ranges.jsonl against a known-corrupt frame before trusting a verdict.
//   * KNOWN GAP (audit): s_cs / s_out are created once and never released or recreated
//     on device reset (resolution change / device-lost) -- after a reset they reference
//     dead-device resources. The operator-validation pass MUST include a resolution
//     change before trusting this under device churn; a proper fix needs a device-reset
//     hook (out of scope while the feature is gated off).
//
// A pass opts in by setting PassDef::post_execute to a lambda that forwards its
// own output SRV here; RenderPipeline::ExecuteStage invokes it when Enabled().

#include "RangeOracle.h"
#include "ComputeManager.h"

#include <d3d11.h>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstring>

using namespace SB;   // ConfigManager / ComputeManager / ComputeShaderID live in SB

namespace {

static bool g_rangeEnabled = false;   // [Diagnostics] GpuReadback, set at config apply

// cs_5_0 reduction. min/max use an order-preserving int reinterpretation of the
// float (cs_5_0 has no float atomics); NaN/Inf counted separately. GetDimensions
// avoids a constant buffer. Strided (downsampled) scan to bound per-pass cost.
static const char* kRangeReduceCS = R"HLSL(
Texture2D<float4> gSrc : register(t0);
RWStructuredBuffer<uint> gOut : register(u0); // [0]=minBits [1]=maxBits [2]=nan [3]=inf
groupshared uint sMin, sMax, sNan, sInf;
[numthreads(256,1,1)]
void main(uint3 dt : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0) { sMin = 0xFFFFFFFFu; sMax = 0u; sNan = 0u; sInf = 0u; }
    GroupMemoryBarrierWithGroupSync();
    uint W, H; gSrc.GetDimensions(W, H);
    uint stepX = max(1u, W / 64u);
    for (uint y = gi; y < H; y += 256u) {
        for (uint x = 0u; x < W; x += stepX) {
            float v = gSrc.Load(int3(int(x), int(y), 0)).r;
            if (isnan(v)) { InterlockedAdd(sNan, 1u); continue; }
            if (isinf(v)) { InterlockedAdd(sInf, 1u); continue; }
            uint b = asuint(v);
            uint o = (b & 0x80000000u) ? ~b : (b | 0x80000000u);
            InterlockedMin(sMin, o);
            InterlockedMax(sMax, o);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) { gOut[0] = sMin; gOut[1] = sMax; gOut[2] = sNan; gOut[3] = sInf; }
}
)HLSL";

// Inverse of the HLSL sign-flip: recover the float from the order-preserving uint.
static float UnflipToFloat(uint32_t o)
{
    uint32_t b = (o & 0x80000000u) ? (o & 0x7FFFFFFFu) : ~o;
    float f; std::memcpy(&f, &b, sizeof(f));
    return f;
}

static void EmitRanges(uint32_t frame, const char* pass, float fmin, float fmax,
                       uint32_t nanCount, uint32_t infCount)
{
    // Emit on corruption; otherwise throttle to bound JSONL growth (mirror Tier 1.1).
    if (nanCount == 0 && infCount == 0 && (frame % 64u) != 0u) return;
    try {
        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
        std::ofstream out("Data/SKSE/Plugins/RAW/live/ranges.jsonl", std::ios::app);
        if (!out.is_open()) return;
        const char Q = static_cast<char>(34);
        out << '{' << Q << "frame" << Q << ':' << frame
            << ',' << Q << "pass" << Q << ':' << Q << (pass ? pass : "?") << Q
            << ',' << Q << "output" << Q << ':' << Q << (pass ? pass : "?") << Q
            << ',' << Q << "min" << Q << ':' << fmin
            << ',' << Q << "max" << Q << ':' << fmax
            << ',' << Q << "nan" << Q << ':' << nanCount
            << ',' << Q << "inf" << Q << ':' << infCount
            << ',' << Q << "verdict" << Q << ':' << Q << ((nanCount || infCount) ? "CORRUPT" : "ok") << Q
            << '}' << static_cast<char>(10);
    } catch (...) {}
}

} // namespace

namespace RangeOracle {

bool Enabled() { return g_rangeEnabled; }
void SetEnabled(bool enabled) { g_rangeEnabled = enabled; }

void Inspect(uint32_t frame, const char* passName, ID3D11ShaderResourceView* srv)
{
    if (!Enabled() || !srv) return;
    auto& cm = ComputeManager::Get();
    if (!cm.IsInitialized()) return;

    static ComputeShaderID s_cs = 0;
    static bool s_failed = false;
    if (s_failed) return;
    if (s_cs == 0) {
        s_cs = cm.CompileShader("RangeReduce", kRangeReduceCS, "main", "cs_5_0");
        if (s_cs == 0) { s_failed = true; return; }
    }
    static ComputeManager::BufferResource s_out;
    if (!s_out.Valid()) {
        s_out = cm.CreateStructuredBuffer(4, 4, false, true, true, "RangeReduceOut");
        if (!s_out.Valid()) { s_failed = true; return; }
    }

    cm.SaveOMState();
    cm.SaveCSState();
    ID3D11ShaderResourceView* srvs[1] = { srv };
    ID3D11UnorderedAccessView* uavs[1] = { s_out.uav };
    cm.CSSetSRVs(0, 1, srvs);
    cm.CSSetUAVs(0, 1, uavs);
    cm.Dispatch(s_cs, 1, 1, 1);
    cm.CSClearUAVs(0, 1);
    cm.CSClearSRVs(0, 1);
    cm.RestoreCSState();
    cm.RestoreOMState();

    uint32_t res[4] = { 0xFFFFFFFFu, 0u, 0u, 0u };
    if (!cm.ReadbackBuffer(s_out.buffer, s_out.staging, res, sizeof(res))) return;
    float fmin = (res[0] == 0xFFFFFFFFu) ? 0.0f : UnflipToFloat(res[0]);
    float fmax = (res[1] == 0u) ? 0.0f : UnflipToFloat(res[1]);
    EmitRanges(frame, passName, fmin, fmax, res[2], res[3]);
}

} // namespace RangeOracle
