// many_draws timings. Per frame the scene measures wall time from the first
// draw-related call until: the last D3D11 call that records a draw returned and
// nothing is submitted yet (call_ms); Flush returned (record_ms); the GPU
// finished (total_ms). The median over the frames after a warm-up is reported.
#include "suite.h"

namespace suite {

void RunBench(const Opts& o, Tally& t)
{
    const Scene* s = FindScene("many_draws");
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    printf("many_draws: %u frames per run, first 4 dropped, %u logical processors, %s\n",
           o.benchFrames, unsigned(si.dwNumberOfProcessors), o.hardware ? "hardware adapter" : "WARP");
    const struct { const char* name; Backend be; uint32_t threads; } k[] = {
        {"d3d11_immediate", Backend::D3D11, 0},
        {"d3d11_deferred", Backend::D3D11, o.benchThreads},
        {"d3d12_translated_immediate", Backend::Translate, 0},
        {"d3d12_translated_deferred", Backend::Translate, o.benchThreads},
    };
    for (uint32_t draws : {10000u, 50000u}) {
        Image ref;
        for (const auto& c : k) {
            RunCfg cfg;
            cfg.be = c.be; cfg.threads = c.threads; cfg.draws = draws; cfg.frames = o.benchFrames;
            cfg.debug = false; cfg.hardware = o.hardware;
            RunOut r;
            RunOnce(*s, cfg, r);
            if (ref.rgba.empty()) ref = r.img;
            int diff = MaxDiff(ref, r.img);
            double call = Median(r.callMs, 4), rec = Median(r.recordMs, 4), tot = Median(r.totalMs, 4);
            char name[96], d[256];
            snprintf(name, sizeof(name), "%s_%u", c.name, draws);
            snprintf(d, sizeof(d), "draws %u, threads %u, call_ms %.3f, record_ms %.3f, total_ms %.3f, call_us_per_draw %.3f, max diff to D3D11 immediate %d",
                     draws, c.threads, call, rec, tot, call * 1000.0 / draws, diff);
            t.Add(r.ok && diff >= 0 && diff <= s->tolerance, name, d);
        }
    }
}

}  // namespace suite
