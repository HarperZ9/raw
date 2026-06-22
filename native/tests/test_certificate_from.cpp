#include "raw/certificate.hpp"
#include "raw/reconcile.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main() {
    // DIVERGENT reconcile -> refuted, with witnessed numbers
    ReconcileResult d; d.rmse = 0.1294f; d.maxError = 0.6094f; d.pixels = 37996; d.withinTolerance = false;
    std::string jd = to_json(certificate_from_reconcile(d, 0.12f));
    CHECK(jd.find("\"oracle\":\"raw-rt-ao-v1\"") != std::string::npos);
    CHECK(jd.find("\"verdict\":\"refuted\"") != std::string::npos);
    CHECK(jd.find("[\"pixels\",\"37996\"]") != std::string::npos);
    CHECK(jd.find("[\"rmse\",\"0.1294\"]") != std::string::npos);
    CHECK(jd.find("[\"tolerance\",\"0.1200\"]") != std::string::npos);

    // within tolerance -> verified
    ReconcileResult w; w.rmse = 0.01f; w.maxError = 0.05f; w.pixels = 1000; w.withinTolerance = true;
    CHECK(to_json(certificate_from_reconcile(w, 0.12f)).find("\"verdict\":\"verified\"") != std::string::npos);

    // nothing to compare -> unverifiable (never confabulate)
    ReconcileResult z; z.pixels = 0; z.withinTolerance = false;
    CHECK(to_json(certificate_from_reconcile(z, 0.12f)).find("\"verdict\":\"unverifiable\"") != std::string::npos);

    // arena within budget -> verified (BOUNDED)
    ArenaStats ok{256, 128, 200, 3, 0};
    std::string ja = to_json(certificate_from_arena(ok));
    CHECK(ja.find("\"oracle\":\"raw-arena-v1\"") != std::string::npos);
    CHECK(ja.find("\"verdict\":\"verified\"") != std::string::npos);
    CHECK(ja.find("[\"budget\",\"256\"]") != std::string::npos);
    CHECK(ja.find("[\"refusals\",\"0\"]") != std::string::npos);

    // arena over budget -> refuted (BREACHED)
    ArenaStats bad{256, 0, 0, 0, 2};
    CHECK(to_json(certificate_from_arena(bad)).find("\"verdict\":\"refuted\"") != std::string::npos);
    return raw_test_summary();
}
