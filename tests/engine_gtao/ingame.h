#pragma once
// engine_gtao_check --ingame: the visibility the pass computed in the game against the CPU
// reference, and which stage of float arithmetic separates the two. Included by main.cpp after
// inputs.h. Original work, MIT.

// The capture set the proxy writes in game (BridgeHookCapture.cpp): three uint32
// (width, height, row bytes), then tight rows.
template <class T>
static bool ReadRows(const std::string& path, UINT* w, UINT* h, std::vector<T>* out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    uint32_t hdr[3];
    bool ok = fread(hdr, 4, 3, f) == 3 && hdr[2] == hdr[0] * 4 && sizeof(T) == 4;
    if (ok) {
        *w = hdr[0]; *h = hdr[1];
        out->resize(size_t(hdr[0]) * hdr[1]);
        ok = fread(out->data(), 4, out->size(), f) == out->size();
    }
    fclose(f);
    return ok;
}

// The visibility the pass computed in the game, against the CPU reference on
// the depth and normals that crossed the bridge in that same frame.
static int InGame(const std::string& dir, Inputs in, const EngineTuning& tune, const std::string& outPath)
{
    std::vector<float> depth, vis;
    UINT w2 = 0, h2 = 0;
    const bool ok = ReadRows(dir + "/d3d12_depth.bin", &in.w, &in.h, &depth) &&
                    ReadRows(dir + "/d3d12_normal.bin", &w2, &h2, &in.normal) && w2 == in.w && h2 == in.h &&
                    ReadRows(dir + "/engine_visibility.bin", &w2, &h2, &vis) && w2 == in.w && h2 == in.h;
    if (!ok) { printf("cannot read the in-game capture set in %s\n", dir.c_str()); return 2; }
    in.depth.resize(depth.size());
    for (size_t i = 0; i < depth.size(); ++i) in.depth[i] = uint32_t(std::lround(double(depth[i]) * 16777215.0));
    printf("inputs: in-game capture set, %ux%u, projection A=%.6f B=%.5f sx=%.5f sy=%.5f, radius %.1f, slices %u, steps %u\n",
           in.w, in.h, in.A, in.B, in.sx, in.sy, tune.radius, tune.slices, tune.steps);
    const raw::post::ViewGBuffer v = Pack(in);
    raw::post::GtaoParams gp;
    gp.radius = tune.radius;
    if (tune.slices) gp.slices = int(tune.slices);
    if (tune.steps) gp.steps = int(tune.steps);
    const Parity p = Compare(vis, raw::post::gtao(v, gp), v);
    const double frac = p.pixels ? double(p.outside) / double(p.pixels) : 1.0;
    printf("       gtao computed in game: %ld drawn pixels, %ld outside 1e-3 (%.5f %%), worst %.3e\n", p.pixels, p.outside, frac * 100.0, p.worst);
    Check(p.pixels > 0 && frac <= 0.001, "the visibility computed in the game matches raw::post::gtao within 1e-3 on 99.9 % of drawn pixels");
    {
        // Which stage's float arithmetic separates the GPU from the reference (gtao_precision.h).
        using namespace precision;
        const int sl = gp.slices, st = gp.steps;
        const std::vector<float> ddd = Variant(v, gp.radius, sl, st, "ddd");
        const std::vector<float> fff = Variant(v, gp.radius, sl, st, "fff");
        const std::vector<float> dff = Variant(v, gp.radius, sl, st, "dff");
        const std::vector<float> fdf = Variant(v, gp.radius, sl, st, "fdf");
        const std::vector<float> ffd = Variant(v, gp.radius, sl, st, "ffd");
        const std::vector<float> ddf = Variant(v, gp.radius, sl, st, "ddf");
        printf("       precision: stages are (slice direction, sample coordinates, the rest); outside 1e-3, percent\n");
        printf("       all double vs raw::post::gtao        %.5f   (the port equals the reference)\n", 100 * p.outside / double(p.pixels) - 100 * Outside(vis, ddd, v));
        printf("       GPU vs all float                     %.5f   (the port in float equals the GPU)\n", 100 * Outside(vis, fff, v));
        printf("       all float vs all double              %.5f\n", 100 * Outside(fff, ddd, v));
        printf("       double direction only vs all double  %.5f\n", 100 * Outside(dff, ddd, v));
        printf("       double coordinates only vs all double %.5f\n", 100 * Outside(fdf, ddd, v));
        printf("       double rest only vs all double       %.5f\n", 100 * Outside(ffd, ddd, v));
        printf("       double direction and coordinates     %.5f\n", 100 * Outside(ddf, ddd, v));
        printf("       all float, derivative direction      %.5f   (the fix)\n", 100 * Outside(Variant(v, gp.radius, sl, st, "aff"), ddd, v));
    }
    raw::post::GtaoParams wrong = gp;
    wrong.radius = gp.radius * 1.25;
    const Parity pw = Compare(vis, raw::post::gtao(v, wrong), v);
    printf("       control: against a reference with radius x1.25, %ld of %ld outside\n", pw.outside, pw.pixels);
    Check(pw.pixels > 0 && double(pw.outside) / double(pw.pixels) > 0.001, "CONTROL: a reference with another radius is reported as a mismatch");
    if (!outPath.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, outPath.c_str(), "w") == 0 && f) {
            fprintf(f, "{\"inputs\":\"in-game capture set\",\"width\":%u,\"height\":%u,\"radius\":%.3f,\"slices\":%d,\"steps\":%d,\n"
                       " \"gtao\":{\"pixels\":%ld,\"outside_1e-3\":%ld,\"worst\":%.4e,\"bound_fraction\":0.001},\n"
                       " \"control_wrong_radius\":{\"pixels\":%ld,\"outside_1e-3\":%ld},\"failed_checks\":%d}\n",
                    in.w, in.h, gp.radius, gp.slices, gp.steps, p.pixels, p.outside, p.worst, pw.pixels, pw.outside, g_failed);
            fclose(f);
        }
    }
    printf("%s: %d check(s) failed\n", g_failed ? "FAILED" : "ALL CHECKS PASSED", g_failed);
    return g_failed ? 1 : 0;
}
