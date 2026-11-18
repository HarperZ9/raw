// d3d11_translate_suite: shared declarations of the runner.
#pragma once
#include "scene_util.h"
#include "Translate.h"
#include <string>

namespace suite {
using namespace txtest;

enum class Backend { D3D11, Translate };

struct RunCfg {
    Backend be = Backend::D3D11;
    uint32_t faults = 0;       // raw::tx::Fault bits (Translate only)
    bool debug = true;         // debug layer of the back end
    bool hardware = false;     // default: WARP
    uint32_t draws = 2000, threads = 0, frames = 1;
};

struct RunOut {
    bool ok = false;           // the scene's own checks passed
    Image img;
    std::vector<uint64_t> values;
    std::vector<double> recordMs, totalMs;
    raw::tx::Stats stats;      // Translate only
    uint32_t d3d11Messages = 0;   // D3D11 debug-layer warnings and errors
    unsigned long leftRefs = 0;   // device references left after the scene
};

struct Opts {
    std::string scene, out = "out", json;
    bool hardware = false, bench = false, noDebug = false;
    uint32_t benchFrames = 24, benchThreads = 4;
};

struct Tally {
    int passed = 0, failed = 0;
    std::string json;          // result objects, comma separated
    void Add(bool ok, const char* name, const std::string& detail);
};

bool MakeDevice(const RunCfg& cfg, ComPtr<ID3D11Device>& dev, ComPtr<ID3D11DeviceContext>& ctx);
bool RunOnce(const Scene& s, const RunCfg& cfg, RunOut& out);
double Median(std::vector<double> v, size_t skip);

void RunScenes(const Opts& o, Tally& t);        // each scene on both back ends
void RunControls(const Opts& o, Tally& t);      // defects that must be caught
void RunReflection(Tally& t);                   // bytecode readers against D3DReflect
void RunSwapChain(const Opts& o, Tally& t);     // D3D12 swap chain, Present, resize
void RunReplay(const Opts& o, Tally& t);        // record on D3D11, replay on both
void RunBench(const Opts& o, Tally& t);         // many_draws timings

}  // namespace suite
