#pragma once
// HardwareProfile.h: Architectural GPU & CPU capability profiler for RTX 3060 through RTX 5090
// (plus AMD RDNA2-RDNA4 & Intel Arc). Probes DXGI adapter description/VRAM, D3D12 feature tiers
// (Shader Model 6.6-6.9, DXR 1.1/1.2, OMM, SER, Mesh Shaders, Work Graphs, FP16), and logical CPU cores
// to select calibrated ray/cloud/froxel budgets and parallel raw::tx command-list worker counts.
// Original work, MIT.
#include <d3d12.h>
#include <cstdint>
#include <string>

namespace raw::bridge
{

struct HardwareProfile
{
    std::string gpuName;
    uint32_t    vendorId = 0;        // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
    uint32_t    vramMB = 0;
    uint32_t    logicalCores = 4;
    uint32_t    shaderModel = 0x66;  // 0x66 = SM 6.6 .. 0x69 = SM 6.9
    uint32_t    dxrTier = 0;         // 10=1.0, 11=1.1, 12=1.2
    uint32_t    meshTier = 0;
    uint32_t    workGraphsTier = 0;
    bool        fp16 = false;
    bool        enhancedBarriers = false;
    bool        omm = false;
    bool        ser = false;
    // 0: Raster/Fallback (<6GB or no DXR 1.1)
    // 1: Entry RT (RTX 2060/3050/3060, RX 6600-6700, Arc A750)
    // 2: Mid-High RT (RTX 3070/3080/4060/4070, RX 6800/7700/7800)
    // 3: Flagship RT (RTX 3090/4080/4090, RX 7900 XTX / 9070 XT)
    // 4: Next-Gen SM 6.9 Flagship (RTX 5080/5090+ with DXR 1.2 + SER)
    int         tier = 3;
    int         recommendedQuality = 4;   // [Lighting] Quality 0..4
    uint32_t    recommendedTxWorkers = 2; // [Translate] parallel command-list translation workers
};

// Probes the primary hardware adapter (and optional live D3D12 device) and returns its HardwareProfile.
HardwareProfile DetectHardwareProfile(ID3D12Device* dev = nullptr);

// Recommends parallel raw::tx command-list worker count from physical/logical CPU concurrency.
uint32_t RecommendTxWorkers();

} // namespace raw::bridge
