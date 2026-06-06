// LiveBridge.h - live telemetry + remote-control bridge for the "eyes" host tool.
//
// While RAW runs, LiveBridge emits per-frame metrics and a periodic downsampled
// frame to a `live/` folder, and polls `live/control.ini` so shaders, effect
// toggles and parameters can be driven from outside the game (no alt-tab).
// Pairs with tools/eyes/raw_eyes.py (watch / control). Opt-in; fully defensive.
//
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
#pragma once

#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include <utility>

namespace SB
{
    struct LiveFrameStats   // filled by the caller from existing systems each frame
    {
        float lumaMean = 0.f, lumaMin = 0.f, lumaMax = 0.f;
        float gpuMs = 0.f;
        float fps = 0.f;
        uint32_t enableFlags = 0;   // compositor enableFlags (which effects are on)
        bool depthValid = false;
        bool sawNaN = false;
    };

    // A control command parsed from live/control.ini. Flat key=value schema:
    //   <effect>=0|1 toggles, <effect>.<param>=<float> sets, seq/reload/reset.
    struct LiveControl
    {
        int seq = -1;
        bool reload = false;
        bool reset = false;
        // raw parsed pairs; the dispatcher in main maps known keys to systems.
        std::vector<std::pair<std::string, std::string>> pairs;
    };

    class LiveBridge
    {
    public:
        static LiveBridge& Get();

        // dir e.g. Data/SKSE/Plugins/RAW/live  (created if missing)
        void Initialize(const std::filesystem::path& liveDir);

        // Call once per frame, after compositing. `stats` is gathered by the
        // caller from LuminanceHistogram / GPUProfiler / SceneCompositor.
        // Returns a control command if control.ini changed this frame (else seq<0).
        LiveControl Tick(const LiveFrameStats& stats);

        // Dump a downsampled BGRA frame (host reads it as live/frame.bmp).
        // Wire to TextureDump with the scene/back-buffer SRV; throttled internally.
        bool WantFrameDump() const { return m_enabled && (m_frame % m_frameEveryN) == 0; }
        std::filesystem::path FramePath() const { return m_dir / "frame.bmp"; }

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool e) { m_enabled = e; }
        void SetCadence(int metricsEveryN, int frameEveryN);

    private:
        void EmitMetrics(const LiveFrameStats& s);
        bool PollControl(LiveControl& out);

        bool m_enabled = false;
        bool m_init = false;
        uint64_t m_frame = 0;
        int m_metricsEveryN = 6;    // ~10 Hz at 60 fps
        int m_frameEveryN = 60;     // ~1 Hz
        int m_lastSeq = -1;
        std::filesystem::path m_dir;
        std::filesystem::file_time_type m_ctrlMTime{};
    };
}
