#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <new>
using namespace raw;
// Render the whole frame through `arena`; returns the shaded frame.
static Buffer<Vec3> renderFrame(int W, int H, Arena* arena){
    Scene s = buildTestScene(W, H, arena);
    GBuffer g = rasterize(s, W, H, arena);
    LinearAccel accel; accel.build(s, arena);
    Buffer<float> aoRT = computeRTAO(g, accel, 16, 2.0f, arena);
    Buffer<float> aoSS = computeSSAO(g, 12, 2.0f, arena);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f, arena);
    return shade(g, aoRT, s, arena);
}
int main() {
    const int W = 64, H = 64;

    // (a) MEASURED-BUDGET BOUNDED: measure H, then render at exactly H.
    std::vector<std::uint8_t> slab1(8u << 20);
    Arena measure(slab1.data(), slab1.size());
    renderFrame(W, H, &measure);
    std::size_t Hbytes = measure.stats().high_water;
    CHECK(Hbytes > 0);
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena tight(slab2.data(), Hbytes);
    renderFrame(W, H, &tight);
    CHECK(tight.stats().refusals == 0);
    CHECK(tight.stats().used == Hbytes);
    CHECK(tight.stats().high_water == Hbytes);
    CHECK(to_json(certificate_from_arena(tight.stats())).find("\"verdict\":\"verified\"") != std::string::npos);

    // (b) FAIL-CLOSED BREACH: a tiny budget throws and is witnessed BREACHED.
    std::vector<std::uint8_t> tinyslab(4096);
    Arena tiny(tinyslab.data(), tinyslab.size());
    bool threw = false;
    try { renderFrame(W, H, &tiny); } catch (const std::bad_alloc&) { threw = true; }
    CHECK(threw);
    CHECK(tiny.stats().refusals > 0);
    CHECK(to_json(certificate_from_arena(tiny.stats())).find("\"verdict\":\"refuted\"") != std::string::npos);

    // (c) PIXEL-IDENTICAL: arena frame == heap frame.
    Buffer<Vec3> heapFrame = renderFrame(W, H, nullptr);
    std::vector<std::uint8_t> slab3(8u << 20);
    Arena a3(slab3.data(), slab3.size());
    Buffer<Vec3> arenaFrame = renderFrame(W, H, &a3);
    bool identical = true;
    for (int y = 0; y < H && identical; ++y) for (int x = 0; x < W; ++x){
        Vec3 p = heapFrame.at(x,y), q = arenaFrame.at(x,y);
        if (p.x != q.x || p.y != q.y || p.z != q.z){ identical = false; break; }
    }
    CHECK(identical);
    return raw_test_summary();
}
