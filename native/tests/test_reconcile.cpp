#include "raw/reconcile.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Buffer<float> a,b; Buffer<uint8_t> mask;
    a.resize(2,1); b.resize(2,1); mask.resize(2,1);
    mask.at(0,0)=1; mask.at(1,0)=1;
    a.at(0,0)=0.5f; b.at(0,0)=0.5f;     // identical
    a.at(1,0)=0.8f; b.at(1,0)=0.4f;     // diff 0.4
    ReconcileResult r = reconcile(a,b,mask,0.1f);
    CHECK_NEAR(r.errorMap.at(0,0), 0.0, 1e-6);
    CHECK_NEAR(r.errorMap.at(1,0), 0.4, 1e-6);
    CHECK_NEAR(r.maxError, 0.4, 1e-6);
    CHECK(r.pixels == 2);
    CHECK(!r.withinTolerance);                 // rmse ~0.283 > 0.1
    // identical buffers -> zero error, within tolerance
    ReconcileResult r2 = reconcile(a,a,mask,0.1f);
    CHECK_NEAR(r2.rmse, 0.0, 1e-6);
    CHECK(r2.withinTolerance);
    return raw_test_summary();
}
