// Contract between the D3D11 test scenes and the runners. A scene is plain D3D11
// code: it gets a device and its immediate context, draws, and returns the final
// image. The same scene runs on the system D3D11 runtime and on the D3D11-to-D3D12
// translation layer; the runner compares the two images.
//
// Rules for a scene (so both back ends can run it):
//   - only ID3D11Device and ID3D11DeviceContext (no Device1+, no DXGI queries)
//   - shaders compiled at run time with D3DCompile, targets vs_5_0/ps_5_0/cs_5_0
//   - output read through a staging texture: CopyResource, then Map(READ)
//   - no swap chain; the runner owns presentation
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <vector>

namespace txtest {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4, row-major, top row first
};

struct SceneCtx {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    uint32_t width = 256, height = 256;
    // many_draws parameters
    uint32_t drawCount = 2000;
    uint32_t threads = 0;        // 0: immediate context only; N: N deferred contexts
    uint32_t frames = 1;         // frames to render (timing runs use more)
    // outputs
    std::vector<uint64_t> values;   // exact scalars to compare (query results)
    std::vector<double> recordMs;   // per frame: first call until Flush returned
    std::vector<double> totalMs;    // per frame: first call until the GPU finished
};

struct Scene {
    const char* name;
    bool (*run)(SceneCtx& c, Image& out);   // false: the scene itself failed
    int tolerance;                           // max per-channel difference allowed
};

// All scenes, in suite order. Defined in scenes/registry.cpp.
const std::vector<Scene>& AllScenes();
const Scene* FindScene(const char* name);

}  // namespace txtest
