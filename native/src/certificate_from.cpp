#include "raw/certificate.hpp"
#include "raw/reconcile.hpp"
#include "raw/arena.hpp"
#include <cstdio>
#include <string>
namespace raw {
static std::string f4(double v){ char b[64]; std::snprintf(b, sizeof b, "%.4f", v); return b; }
Certificate certificate_from_reconcile(const ReconcileResult& r, float tolerance){
    Verdict v = (r.pixels == 0) ? Verdict::Unverifiable
              : (r.withinTolerance ? Verdict::Verified : Verdict::Refuted);
    return Certificate{
        "screen-space AO matches ray-traced ground truth within tolerance",
        v, "raw-rt-ao-v1",
        { {"pixels", std::to_string(r.pixels)},
          {"rmse", f4(r.rmse)},
          {"maxError", f4(r.maxError)},
          {"tolerance", f4(tolerance)} }
    };
}
Certificate certificate_from_arena(const ArenaStats& s){
    Verdict v = (s.refusals == 0) ? Verdict::Verified : Verdict::Refuted;
    return Certificate{
        "arena stayed within its memory budget",
        v, "raw-arena-v1",
        { {"budget", std::to_string(s.budget)},
          {"used", std::to_string(s.used)},
          {"high_water", std::to_string(s.high_water)},
          {"allocations", std::to_string(s.allocations)},
          {"refusals", std::to_string(s.refusals)} }
    };
}
}
