// ao_realtime_check: the engine's real-time AO behind the bridge, scored against
// the engine's reference GTAO at 16 slices by 16 steps on the same inputs, with
// its GPU cost at every step of the build-up.
//
// Inputs are a D24S8 depth buffer, the game's RGBA8 normal target and an RGBA8
// frame: a synthetic scene (default) or the buffers of a captured gameplay
// frame (--depth, --normal, --color, --proj). Everything goes through the real
// path: D3D11 textures, the bridge's captures, the pass on D3D12, and back.
//
// Checks (exit code 0 only when all pass):
//   1. the final configuration (2 slices x 16 steps), after 32 frames, is within 0.06 of the reference
//   2. its first frame (no history: every pixel just uncovered) is within its looser bound
//   3. CONTROL: twice the radius scores at least 1.5 times worse
//   4. a pan with the right motion vectors stays within the moving bound
//   5. CONTROL: the same pan with zero motion vectors and depth rejection off scores at
//      least 1.5 times worse where the reference is occluded (ghosting)
//   6. GPU timestamps read back through the RHI
// The pan is synthesised: each frame shows the buffers moved 8 pixels right.
//
// Original work, MIT.
#include "../bridge_roundtrip/fixtures.h"
#include "raw/renderer/post.hpp"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>

using namespace raw::bridge;

static int g_failed = 0;
static void Check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) ++g_failed;
}
#include "../engine_gtao/inputs.h"
#include "rig.h"

static bool g_quiet = false;
static void Log(const char* fmt, ...)
{
    if (g_quiet) return;
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}

struct Row
{
    const char* name;
    uint32_t slices, steps;
    bool noise, temporal, levels;
    int frames;
    float levelBias = 1.0f;
    Score score, dense;   // against the 16 x 16 reference, and against the dense one (--dense)
    Stats pass, stage[3], inputs, bridge;
};

int main(int argc, char** argv)
{
    bool warp = false;
    UINT size = 640;
    int timed = 30, dense = 0;
    const int kSlide = 8, kPan = 12;
    std::string depthPath, normalPath, colourPath, outPath, load = "not stated";
    std::vector<std::string> extra;   // --row slices,steps,levels,levelBias,frames: one more row of the curve
    Inputs in;
    float radius = 35.0f;
    bool haveProj = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--warp") warp = true;
        else if (a == "--size") size = UINT(atoi(next().c_str()));
        else if (a == "--frames") timed = atoi(next().c_str());
        else if (a == "--depth") depthPath = next();
        else if (a == "--normal") normalPath = next();
        else if (a == "--color") colourPath = next();
        else if (a == "--out") outPath = next();
        else if (a == "--load") load = next();
        else if (a == "--dense") dense = atoi(next().c_str());
        else if (a == "--row") extra.push_back(next());
        else if (a == "--radius") radius = float(atof(next().c_str()));
        else if (a == "--proj") {
            in.A = float(atof(next().c_str())); in.B = float(atof(next().c_str()));
            in.sx = float(atof(next().c_str())); in.sy = float(atof(next().c_str()));
            haveProj = true;
        }
    }
    const bool captured = !depthPath.empty();
    if (captured) {
        UINT w2 = 0, h2 = 0;
        const bool ok = haveProj && ReadRawTex(depthPath, 4, &in.w, &in.h, &in.depth) &&
                        ReadRawTex(normalPath, 4, &w2, &h2, &in.normal) && w2 == in.w && h2 == in.h &&
                        ReadRawTex(colourPath, 4, &w2, &h2, &in.colour) && w2 == in.w && h2 == in.h;
        if (!ok) { printf("cannot read the captured buffers (need --depth --normal --color --proj A B sx sy)\n"); return 2; }
    } else {
        in = Synthetic(size, size * 9 / 16);
    }
    printf("inputs: %s, %ux%u, radius %.1f; GPU load before the run: %s\n", captured ? "captured gameplay frame" : "synthetic scene",
           in.w, in.h, radius, load.c_str());

    Rig rig;
    Check(rig.Create(in, warp, Log), "D3D11 device and input textures");
    if (g_failed) return 1;

    // The reference: the engine's GTAO at 16 x 16, still and at the end of the pan.
    EngineTuning ref16;
    ref16.radius = radius; ref16.slices = 16; ref16.steps = 16;
    std::vector<float> ref, refMoved, vis;
    const Inputs moved = Shifted(in, kSlide * (kPan - 1));
    bool ok = rig.Bridge(false, !captured) && rig.Run(ref16, 1, &ref);
    std::vector<float> refDense;
    Score refVsDense;
    if (ok && dense > 16) {
        // A denser reference shows how far the 16 x 16 reference itself is from its own limit.
        EngineTuning d = ref16;
        d.slices = d.steps = uint32_t(dense);
        ok = rig.Run(d, 1, &refDense);
        if (ok) refVsDense = ScoreAgainst(ref, refDense, in);
        printf("       the 16 x 16 reference against %d x %d: mean |d| %.4f (signed %+.4f), over 0.05: %.2f %%\n", dense, dense,
               refVsDense.meanAbs, refVsDense.meanSigned, 100.0 * double(refVsDense.over005) / double(std::max(refVsDense.pixels, 1L)));
    }
    rig.Upload(moved);
    ok = ok && rig.Run(ref16, 1, &refMoved);
    rig.Upload(in);
    Check(ok, "reference visibility: the engine's GTAO at 16 x 16, on the frame and on the frame at the end of the pan");
    if (g_failed) return 1;
    g_quiet = true;

    Check(rig.Bridge(true, !captured), "bridge initialised with the real-time AO pass");
    if (g_failed) return 1;
    std::vector<Row> rows = {
        {"half resolution, 2 fixed slices x 16 steps, one frame", 2, 16, false, false, false, 1},
        {"+ blue noise, 1 slice x 16, one frame (no history)", 1, 16, true, false, false, 1},
        {"+ blue noise, 2 slices x 16, one frame (no history)", 2, 16, true, false, false, 1},
        {"+ accumulation, 2 slices x 16, 4 frames", 2, 16, true, true, false, 4},
        {"+ accumulation, 2 slices x 16, 32 frames", 2, 16, true, true, false, 32},
        {"+ depth levels, 2 slices x 16, 32 frames (final)", 2, 16, true, true, true, 32},
        {"final, first frame (no history)", 2, 16, true, true, true, 1},
        {"final, 64 frames", 2, 16, true, true, true, 64},
        {"1 slice x 6, 32 frames", 1, 6, true, true, true, 32},
        {"1 slice x 16, 32 frames", 1, 16, true, true, true, 32},
        {"1 slice x 24, 32 frames", 1, 24, true, true, true, 32},
        {"1 slice x 32, 32 frames", 1, 32, true, true, true, 32},
        {"2 slices x 24, 32 frames", 2, 24, true, true, true, 32},
    };
    for (std::string& e : extra) {
        Row r{};
        int levels = 1;
        if (sscanf_s(e.c_str(), "%u,%u,%d,%f,%d", &r.slices, &r.steps, &levels, &r.levelBias, &r.frames) != 5) continue;
        r.noise = r.temporal = true; r.levels = levels != 0;
        e = "extra: " + e;
        r.name = e.c_str();
        rows.push_back(r);
    }
    const auto tuning = [&](const Row& r) {
        EngineTuning t;
        t.radius = radius; t.slices = r.slices; t.steps = r.steps; t.noise = r.noise; t.temporal = r.temporal; t.levels = r.levels; t.levelBias = r.levelBias;
        return t;
    };
    for (Row& r : rows) {
        if (!rig.Run(tuning(r), r.frames, &vis)) { Check(false, r.name); continue; }
        r.score = ScoreAgainst(vis, ref, in);
        if (!refDense.empty()) r.dense = ScoreAgainst(vis, refDense, in);
        rig.Time(timed, &r.pass, r.stage, &r.inputs, &r.bridge);
        printf("       %-52s mean |d| %.4f (signed %+.4f), over 0.05: %5.2f %%, pass %.3f ms (p10 %.3f, p90 %.3f; levels %.3f, trace %.3f, resolve %.3f), "
               "input copies %.3f ms; against dense %.4f (%+.4f)\n", r.name, r.score.meanAbs, r.score.meanSigned, 100.0 * double(r.score.over005) / double(std::max(r.score.pixels, 1L)),
               r.pass.median, r.pass.p10, r.pass.p90, r.stage[0].median, r.stage[1].median, r.stage[2].median, r.inputs.median, r.dense.meanAbs, r.dense.meanSigned);
    }
    const Row& fin = rows[5];
    const Row& first = rows[6];
    // Bounds for any input. On game content the 16 x 16 reference is itself about 0.06 from a
    // 48 x 48 one (--dense 48), which is the floor for a mean difference to it.
    const double kConverged = 0.06, kFirst = 0.12, kMoving = 0.12;
    Check(fin.score.pixels > 0 && fin.score.meanAbs < kConverged, "final configuration, 32 frames: mean difference to the 16 x 16 reference under 0.06");
    Check(first.score.pixels > 0 && first.score.meanAbs < kFirst, "final configuration, first frame: mean difference under 0.12");
    Check(fin.pass.n > 0 && fin.pass.median > 0.0, "GPU timestamps read back through the RHI");

    // Control: the wrong radius.
    EngineTuning wide = tuning(fin);
    wide.radius = radius * 2.0f;
    const Score wrongRadius = rig.Run(wide, 32, &vis) ? ScoreAgainst(vis, ref, in) : Score{};
    printf("       control, radius x 2: mean |d| %.4f (final: %.4f)\n", wrongRadius.meanAbs, fin.score.meanAbs);
    Check(wrongRadius.meanAbs > 1.5 * fin.score.meanAbs, "CONTROL: twice the radius scores at least 1.5 times worse");

    // The pan: right vectors, then no vectors with and without the depth rejection.
    struct Pan { const char* name; float mv; float tolerance; Score score; };
    Pan pans[] = {{"right motion vectors", -float(kSlide), 0.1f, {}},
                  {"zero motion vectors, depth rejection on", 0.0f, 0.1f, {}},
                  {"zero motion vectors, depth rejection off", 0.0f, 1.0e6f, {}}};
    for (Pan& p : pans) {
        EngineTuning t = tuning(fin);
        t.depthTolerance = p.tolerance;
        t.resetHistory = true;
        rig.SetMotion(p.mv / float(in.w), 0.0f);
        bool run = true;
        for (int f = 0; run && f < kPan; ++f) {
            rig.Upload(Shifted(in, kSlide * f));
            rig.bridge->TuneEngine(t);
            t.resetHistory = false;
            run = rig.Frame();
        }
        if (run && rig.bridge->ReadEngineVisibility(&vis)) p.score = ScoreAgainst(vis, refMoved, moved);
        printf("       pan of %d frames, %d px a frame, %-42s mean |d| %.4f, where the reference is occluded %.4f (%ld px)\n", kPan, kSlide,
               p.name, p.score.meanAbs, p.score.occludedMeanAbs, p.score.occludedPixels);
    }
    rig.Upload(in);
    rig.SetMotion(0, 0);
    Check(pans[0].score.pixels > 0 && pans[0].score.meanAbs < kMoving, "pan with the right motion vectors: mean difference under 0.12");
    Check(pans[2].score.occludedMeanAbs > 1.5 * pans[0].score.occludedMeanAbs,
          "CONTROL: zero motion vectors with depth rejection off ghost: at least 1.5 times worse where the reference is occluded");

    if (!outPath.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, outPath.c_str(), "w") == 0 && f) {
            fprintf(f, "{\"inputs\":\"%s\",\"width\":%u,\"height\":%u,\"adapter\":\"%s\",\"radius\":%.3f,\"gpu_load_before\":\"%s\",\n"
                       " \"reference\":\"engine GTAO, 16 slices x 16 steps\",\"scored_pixels\":%ld,\"timed_frames\":%d,\n"
                       " \"dense_reference\":{\"slices_and_steps\":%d,\"reference_16x16_mean_abs\":%.5f,\"reference_16x16_over_0.05\":%.5f},\n \"rows\":[\n",
                    captured ? "captured" : "synthetic", in.w, in.h, warp ? "WARP" : "hardware", radius, load.c_str(), fin.score.pixels, timed,
                    dense, refVsDense.meanAbs, double(refVsDense.over005) / double(std::max(refVsDense.pixels, 1L)));
            for (const Row& r : rows)
                fprintf(f, "  {\"step\":\"%s\",\"slices\":%u,\"steps\":%u,\"noise\":%d,\"temporal\":%d,\"levels\":%d,\"frames\":%d,"
                           "\"mean_abs\":%.5f,\"mean_signed\":%.5f,\"over_0.05\":%.5f,\"dense_mean_abs\":%.5f,\"pass_ms\":{\"median\":%.4f,\"p10\":%.4f,\"p90\":%.4f},"
                           "\"stage_ms\":{\"levels\":%.4f,\"trace\":%.4f,\"resolve\":%.4f},\"input_copies_ms\":%.4f,\"bridge_cost_ms\":%.4f}%s\n",
                        r.name, r.slices, r.steps, r.noise, r.temporal, r.levels, r.frames, r.score.meanAbs, r.score.meanSigned,
                        double(r.score.over005) / double(std::max(r.score.pixels, 1L)), r.dense.meanAbs, r.pass.median, r.pass.p10, r.pass.p90,
                        r.stage[0].median, r.stage[1].median, r.stage[2].median, r.inputs.median, r.bridge.median, &r == &rows.back() ? "" : ",");
            fprintf(f, " ],\n \"control_radius_x2\":{\"mean_abs\":%.5f},\n \"pan\":{\"frames\":%d,\"pixels_a_frame\":%d,\"occluded_pixels\":%ld,\n", wrongRadius.meanAbs,
                    kPan, kSlide, pans[0].score.occludedPixels);
            for (const Pan& p : pans)
                fprintf(f, "  \"%s\":{\"mean_abs\":%.5f,\"occluded_mean_abs\":%.5f}%s\n", p.name, p.score.meanAbs, p.score.occludedMeanAbs,
                        &p == &pans[2] ? "}," : ",");
            fprintf(f, " \"bounds\":{\"converged\":%.3f,\"first_frame\":%.3f,\"pan\":%.3f},\"failed_checks\":%d}\n", kConverged, kFirst, kMoving, g_failed);
            fclose(f);
        }
    }
    printf("%s: %d check(s) failed\n", g_failed ? "FAILED" : "ALL CHECKS PASSED", g_failed);
    return g_failed ? 1 : 0;
}
