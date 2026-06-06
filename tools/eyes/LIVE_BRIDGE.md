# Live Bridge — wiring guide (RAW side)

`src/core/LiveBridge.{h,cpp}` is the in-game half of the live "eyes". It is pure
CPU/file IO and never throws into the frame loop. It reuses existing RAW systems
for data; it does not re-measure anything. Opt-in (off by default).

## 1. Build
Add to `CMakeLists.txt` RAW sources:
```
src/core/LiveBridge.cpp
```

## 2. Initialize (once, at kDataLoaded, after systems are up)
```cpp
auto live = "Data/SKSE/Plugins/RAW/live";
SB::LiveBridge::Get().Initialize(live);
SB::LiveBridge::Get().SetEnabled(true);   // or gate behind an INI flag / hotkey
```

## 3. Tick (once per frame, after compositing — in the same place DebugGUI renders)
Gather stats from systems that already exist, then Tick and apply any command:
```cpp
SB::LiveFrameStats s;
// HOOK luminance: LuminanceHistogram already reads back avg/min/max (t17).
auto& lh = SB::LuminanceHistogram::Get();
s.lumaMean = lh.GetAverage();  s.lumaMin = lh.GetMin();  s.lumaMax = lh.GetMax();   // confirm getters
// HOOK timing: GPUProfiler total ms for the frame.
s.gpuMs = SB::GPUProfiler::Get().GetTotalMs();                                       // confirm getter
s.fps   = (s.gpuMs > 0.f) ? 1000.f / s.gpuMs : 0.f;
s.enableFlags = SB::SceneCompositor::Get().GetEnableFlags();                         // the compositor bitmask
s.depthValid  = D3D11Hook::GetGameDepthSRV() != nullptr;

SB::LiveControl cmd = SB::LiveBridge::Get().Tick(s);

// periodic downsampled frame for the host to *see*
if (SB::LiveBridge::Get().WantFrameDump()) {
    SB::TextureDump::Get().DumpSRV(/*sceneSRV*/, SB::LiveBridge::Get().FramePath(), /*maxDim=*/256);  // add a maxDim arg
}

// apply a control command (no alt-tab tuning)
if (cmd.seq >= 0) {
    for (auto& [k, v] : cmd.pairs) ApplyControlPair(k, v);   // see below
    if (cmd.reload) SB::ShaderReload::ReloadAllShaders();
    if (cmd.reset)  SB::ConfigManager::Get().ApplyToSystems();
}
```

## 4. ApplyControlPair — map flat keys to systems (small switch)
```cpp
void ApplyControlPair(const std::string& k, const std::string& v) {
    bool on = (v == "1" || v == "true");
    if      (k == "gtao")    SB::GTAORenderer::Get().SetEnabled(on);
    else if (k == "ssgi")    SB::SSGIRenderer::Get().SetEnabled(on);
    else if (k == "ssr")     SB::SSRRenderer::Get().SetEnabled(on);
    else if (k == "shadow")  SB::ContactShadowRenderer::Get().SetEnabled(on);
    else if (k == "sky")     SB::SkylightingRenderer::Get().SetEnabled(on);
    else if (k == "bloom")   SB::BloomRenderer::Get().SetEnabled(on);
    // params: forward "<effect>.<name>" straight into ConfigManager so the
    // existing apply path handles it (keeps this switch tiny).
    else if (k.find('.') != std::string::npos)
        SB::ConfigManager::Get().SetFloat(k, std::strtof(v.c_str(), nullptr));
}
```

## Notes
- Lines marked `HOOK`/`confirm` reference existing classes — verify the exact getter
  names against the headers at build (LuminanceHistogram, GPUProfiler, SceneCompositor,
  TextureDump). The bridge itself needs no changes for that.
- `TextureDump` currently writes full-res BMP; add an optional `maxDim` downsample so
  `frame.bmp` stays ~256px (fast, small, the host upscales-aware). Until then it will
  dump full-res and the host still reads it fine — just larger.
- Everything is throttled: latest.json every frame, metrics.jsonl ~10 Hz, frame.bmp ~1 Hz.
