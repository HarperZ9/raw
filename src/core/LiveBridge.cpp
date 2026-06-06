// LiveBridge.cpp - see LiveBridge.h. Pure-CPU file bridge; never throws into
// the frame loop. Copyright (c) 2026 Zain D. Harper. All rights reserved.
#include "LiveBridge.h"

#include <fstream>
#include <sstream>
#include <string>

namespace SB
{
    LiveBridge& LiveBridge::Get()
    {
        static LiveBridge s;
        return s;
    }

    void LiveBridge::SetCadence(int metricsEveryN, int frameEveryN)
    {
        if (metricsEveryN > 0) m_metricsEveryN = metricsEveryN;
        if (frameEveryN > 0)   m_frameEveryN = frameEveryN;
    }

    void LiveBridge::Initialize(const std::filesystem::path& liveDir)
    {
        try
        {
            m_dir = liveDir;
            std::error_code ec;
            std::filesystem::create_directories(m_dir, ec);
            // start a fresh metrics stream each session
            std::ofstream(m_dir / "metrics.jsonl", std::ios::trunc).close();
            m_init = true;
        }
        catch (...) { m_init = false; }
    }

    static void WriteJsonLine(std::ostream& os, uint64_t frame, const LiveFrameStats& s)
    {
        os << "{\"frame\":" << frame
           << ",\"luma_mean\":" << s.lumaMean
           << ",\"luma_min\":" << s.lumaMin
           << ",\"luma_max\":" << s.lumaMax
           << ",\"gpu_ms\":" << s.gpuMs
           << ",\"fps\":" << s.fps
           << ",\"enable_flags\":" << s.enableFlags
           << ",\"depth_valid\":" << (s.depthValid ? "true" : "false")
           << ",\"nan\":" << (s.sawNaN ? "true" : "false")
           << "}";
    }

    void LiveBridge::EmitMetrics(const LiveFrameStats& s)
    {
        try
        {
            // latest.json: overwritten every frame (cheap, always-fresh snapshot)
            {
                std::ofstream f(m_dir / "latest.json", std::ios::trunc);
                if (f) { WriteJsonLine(f, m_frame, s); f << "\n"; }
            }
            // metrics.jsonl: appended at the metrics cadence (rolling history)
            if ((m_frame % static_cast<uint64_t>(m_metricsEveryN)) == 0)
            {
                std::ofstream f(m_dir / "metrics.jsonl", std::ios::app);
                if (f) { WriteJsonLine(f, m_frame, s); f << "\n"; }
            }
        }
        catch (...) { /* never disturb the frame loop */ }
    }

    bool LiveBridge::PollControl(LiveControl& out)
    {
        try
        {
            const auto path = m_dir / "control.ini";
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) return false;
            const auto mt = std::filesystem::last_write_time(path, ec);
            if (ec) return false;
            if (mt == m_ctrlMTime) return false;     // unchanged since last poll
            m_ctrlMTime = mt;

            std::ifstream f(path);
            if (!f) return false;
            out = LiveControl{};
            std::string line;
            while (std::getline(f, line))
            {
                // trim
                auto a = line.find_first_not_of(" \t\r\n");
                if (a == std::string::npos || line[a] == '#') continue;
                auto eq = line.find('=', a);
                if (eq == std::string::npos) continue;
                auto b = line.find_last_not_of(" \t\r\n", eq - 1);
                std::string key = line.substr(a, b - a + 1);
                auto vs = line.find_first_not_of(" \t\r\n", eq + 1);
                std::string val = (vs == std::string::npos) ? "" :
                    line.substr(vs, line.find_last_not_of(" \t\r\n") - vs + 1);
                if (key == "seq")    { out.seq = std::atoi(val.c_str()); }
                else if (key == "reload") { out.reload = (val == "1" || val == "true"); }
                else if (key == "reset")  { out.reset  = (val == "1" || val == "true"); }
                else out.pairs.emplace_back(key, val);
            }
            if (out.seq <= m_lastSeq) return false;  // already applied
            m_lastSeq = out.seq;
            return true;
        }
        catch (...) { return false; }
    }

    LiveControl LiveBridge::Tick(const LiveFrameStats& stats)
    {
        LiveControl cmd;
        if (!m_enabled || !m_init) return cmd;   // seq stays -1 => no command
        ++m_frame;
        EmitMetrics(stats);
        PollControl(cmd);
        return cmd;
    }
}
