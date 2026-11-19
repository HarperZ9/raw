#pragma once
// Engine control (RAW engine runtime). Original work, MIT.

#include <d3d11.h>
#include <dxgi.h>

#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

// Settings by name from engine_rt_refs.tsv; writes go to the Setting's value field (+8).
bool SetSetting(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& value,
                std::string* before = nullptr);

// [EngineSet] in d3d11_proxy.ini: name=value pairs applied once at module start.
size_t ApplyStartupSettings(const Image& img, const std::vector<RefTarget>& refs, const std::string& ini,
                            const std::string& logPath);

// The go-file protocol: raw_engine_go.txt holds commands (set, wait, measure, capture); results go to
// <out>/control_results.jsonl and raw_engine_done.txt answers.
void ControlInit(const Image& img, const std::vector<RefTarget>* refs, const std::string& gameDir, const std::string& outDir);
void ControlOnPresent(ID3D11DeviceContext* ctx, IDXGISwapChain* sc);

// Papyrus: VirtualMachine::Update and UpdateTasklets (slots 4 and 5) timed, budget optionally replaced.
bool InstallPapyrusTiming(const Image& img, uint32_t vmVtable, float budgetOverrideMs);

}  // namespace raw::rt
