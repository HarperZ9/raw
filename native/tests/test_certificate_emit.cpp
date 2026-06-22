// The real sight pipeline must produce a Certificate whose JSON the body can consume.
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/certificate.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main() {
    const int W = 256, H = 256;
    Scene s = buildTestScene(W, H);
    GBuffer g = rasterize(s, W, H);
    LinearAccel accel; accel.build(s);
    Buffer<float> aoRT = computeRTAO(g, accel, 64, 2.0f);
    Buffer<float> aoSS = computeSSAO(g, 24, 2.0f);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f);

    std::string j = to_json(certificate_from_reconcile(rec, 0.12f));
    // the built-in scene is deterministically DIVERGENT (approximation != ground truth)
    CHECK(rec.pixels > 0);
    CHECK(!rec.withinTolerance);
    CHECK(j.find("\"oracle\":\"raw-rt-ao-v1\"") != std::string::npos);
    CHECK(j.find("\"verdict\":\"refuted\"") != std::string::npos);
    CHECK(j.find("\"claim\":\"screen-space AO matches ray-traced ground truth within tolerance\"")
          != std::string::npos);
    return raw_test_summary();
}
