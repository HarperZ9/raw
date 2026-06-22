#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <vector>
#include <cstdint>
using namespace raw;
int main() {
    const int W = 64, H = 64;
    // heap render
    Scene sh = buildTestScene(W, H);
    GBuffer gh = rasterize(sh, W, H);
    LinearAccel ah; ah.build(sh);
    Buffer<float> aoRTh = computeRTAO(gh, ah, 16, 2.0f);
    Buffer<float> aoSSh = computeSSAO(gh, 12, 2.0f);
    ReconcileResult rh = reconcile(aoSSh, aoRTh, gh.mask, 0.12f);
    Buffer<Vec3> fh = shade(gh, aoRTh, sh);

    // arena render (8 MB heap-backed slab is plenty for 64x64)
    std::vector<std::uint8_t> slab(8u << 20);
    Arena a(slab.data(), slab.size());
    Scene sa = buildTestScene(W, H, &a);
    GBuffer ga = rasterize(sa, W, H, &a);
    LinearAccel aa; aa.build(sa, &a);
    Buffer<float> aoRTa = computeRTAO(ga, aa, 16, 2.0f, &a);
    Buffer<float> aoSSa = computeSSAO(ga, 12, 2.0f, &a);
    ReconcileResult ra = reconcile(aoSSa, aoRTa, ga.mask, 0.12f, &a);
    Buffer<Vec3> fa = shade(ga, aoRTa, sa, &a);
    CHECK(a.stats().refusals == 0);

    // PIXEL-IDENTICAL: the arena moves memory, it does not change the render
    bool identical = true;
    for (int y = 0; y < H && identical; ++y) for (int x = 0; x < W; ++x){
        Vec3 p = fh.at(x,y), q = fa.at(x,y);
        if (p.x != q.x || p.y != q.y || p.z != q.z){ identical = false; break; }
    }
    CHECK(identical);
    CHECK(ra.rmse == rh.rmse);
    CHECK(ra.pixels == rh.pixels);
    return raw_test_summary();
}
