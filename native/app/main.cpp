#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include "raw/arena.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <new>
#include <utility>
using namespace raw;
struct FrameOut { Buffer<Vec3> frame; Buffer<float> aoRT, aoSS; ReconcileResult rec; };
static FrameOut renderFrame(int W, int H, Arena* arena){
    Scene s = buildTestScene(W, H, arena);
    GBuffer g = rasterize(s, W, H, arena);
    LinearAccel accel; accel.build(s, arena);
    Buffer<float> aoRT = computeRTAO(g, accel, 64, 2.0f, arena);
    Buffer<float> aoSS = computeSSAO(g, 24, 2.0f, arena);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f, arena);
    Buffer<Vec3> frame = shade(g, aoRT, s, arena);
    return FrameOut{ std::move(frame), std::move(aoRT), std::move(aoSS), std::move(rec) };
}
int main(int argc, char** argv){
    std::string out = argc > 1 ? argv[1] : ".";
    const int W = 256, H = 256;

    // PASS 1 — MEASURE the footprint in a computed generous slab (no magic constant).
    const std::size_t PER_PIXEL_UPPER =
        sizeof(float) + 3*sizeof(Vec3) + sizeof(std::uint8_t)   // gbuffer: depth+nrm/pos/alb+mask
        + 3*sizeof(float) + sizeof(Vec3);                       // aoRT + aoSS + errorMap + frame
    std::size_t slabUB = (std::size_t)W*H*PER_PIXEL_UPPER*2 + (1u<<20);
    std::vector<std::uint8_t> slab1(slabUB);
    Arena measure(slab1.data(), slabUB);
    try { (void)renderFrame(W, H, &measure); }
    catch (const std::bad_alloc&){ std::printf("measure pass overflowed slab — raise PER_PIXEL_UPPER\n"); return 2; }
    std::size_t Hbytes = measure.stats().high_water;

    // PASS 2 — render within a budget of EXACTLY the measured footprint.
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena arena(slab2.data(), Hbytes);
    FrameOut o;
    try { o = renderFrame(W, H, &arena); }
    catch (const std::bad_alloc&){
        Certificate br = certificate_from_arena(arena.stats());
        std::ofstream(out + "/arena_certificate.json") << to_json(br);
        std::printf("arena: %s\n", to_json(br).c_str());     // BREACHED, fail-closed
        return 1;
    }
    writePPM(o.frame, out + "/frame.ppm");
    writePGM(o.aoRT,  out + "/ao_rt.pgm");
    writePGM(o.aoSS,  out + "/ao_ss.pgm");
    writePGM(o.rec.errorMap, out + "/ao_error.pgm");

    Certificate aoCert    = certificate_from_reconcile(o.rec, 0.12f);
    Certificate arenaCert = certificate_from_arena(arena.stats());
    std::ofstream(out + "/certificate.json")        << to_json(aoCert);
    std::ofstream(out + "/arena_certificate.json")  << to_json(arenaCert);

    std::printf("reconcile: pixels=%d rmse=%.4f maxError=%.4f verdict=%s\n",
        o.rec.pixels, o.rec.rmse, o.rec.maxError,
        o.rec.withinTolerance ? "WITHIN-TOLERANCE" : "DIVERGENT");
    std::printf("certificate: %s\n", to_json(aoCert).c_str());
    std::printf("arena: %s\n", arena_witness(arena.stats()).c_str());
    std::printf("arena-certificate: %s\n", to_json(arenaCert).c_str());
    return 0;
}
