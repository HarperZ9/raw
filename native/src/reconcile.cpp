#include "raw/reconcile.hpp"
#include <cmath>
namespace raw {
ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth,
                          const Buffer<uint8_t>& mask, float tolerance){
    ReconcileResult r; r.errorMap.resize(approx.w, approx.h);
    double sumSq=0; int n=0;
    for (int y=0;y<approx.h;++y) for (int x=0;x<approx.w;++x){
        if (!mask.at(x,y)){ r.errorMap.at(x,y)=0; continue; }
        float e = std::fabs(approx.at(x,y) - truth.at(x,y));
        r.errorMap.at(x,y) = e;
        if (e > r.maxError) r.maxError = e;
        sumSq += (double)e*e; ++n;
    }
    r.pixels = n;
    r.rmse = n>0 ? (float)std::sqrt(sumSq/n) : 0.0f;
    r.withinTolerance = r.rmse <= tolerance;
    return r;
}
}
