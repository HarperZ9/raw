// gtao_precision.cpp: see gtao_precision.h. The pass is written once in gtao_precision.inc
// and compiled for six choices of types. Test code only. Original work, MIT.
#include "gtao_precision.h"
#include <algorithm>
#include <cmath>
#include <thread>

namespace precision
{
#define PREC_CAT2(a, b) a##b
#define PREC_CAT(a, b) PREC_CAT2(a, b)
#define PREC_NAME(f) PREC_CAT(f, PREC_TAG)

#define PREC_TAG ddd
#define D double
#define C double
#define R double
#include "gtao_precision.inc"
#undef PREC_TAG
#undef D
#undef C
#undef R
#define PREC_TAG fff
#define D float
#define C float
#define R float
#include "gtao_precision.inc"
#undef PREC_TAG
#undef D
#undef C
#undef R
#define PREC_TAG dff
#define D double
#define C float
#define R float
#include "gtao_precision.inc"
#undef PREC_TAG
#undef D
#undef C
#undef R
#define PREC_TAG fdf
#define D float
#define C double
#define R float
#include "gtao_precision.inc"
#undef PREC_TAG
#undef D
#undef C
#undef R
#define PREC_TAG ffd
#define D float
#define C float
#define R double
#include "gtao_precision.inc"
#undef PREC_TAG
#undef D
#undef C
#undef R
#define PREC_TAG ddf
#define D double
#define C double
#define R float
#include "gtao_precision.inc"
#undef PREC_TAG
#undef D
#undef C
#undef R
#define PREC_TAG aff
#define PREC_ANALYTIC 1
#define D float
#define C float
#define R float
#include "gtao_precision.inc"
#undef PREC_TAG
#undef PREC_ANALYTIC
#undef D
#undef C
#undef R

using RowFn = void (*)(const raw::post::ViewGBuffer&, double, int, int, int, std::vector<float>&);

static void Rows(RowFn fn, const raw::post::ViewGBuffer* v, double radius, int slices, int steps, int first, int stride,
                 std::vector<float>* out)
{
    for (int y = first; y < v->h; y += stride) fn(*v, radius, slices, steps, y, *out);
}

std::vector<float> Variant(const raw::post::ViewGBuffer& v, double radius, int slices, int steps, const char* stages)
{
    const int key = (stages[0] == 'd' ? 4 : 0) | (stages[1] == 'd' ? 2 : 0) | (stages[2] == 'd' ? 1 : 0);
    RowFn fn = Rowfff;
    if (stages[0] == 'a') fn = Rowaff;   // all float, analytic slice direction
    else if (key == 7) fn = Rowddd;
    if (key == 4) fn = Rowdff;
    if (key == 2) fn = Rowfdf;
    if (key == 1) fn = Rowffd;
    if (key == 6) fn = Rowddf;
    std::vector<float> out(size_t(v.w) * v.h, 1.0f);
    const int n = int(std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < n; ++t) pool.emplace_back(Rows, fn, &v, radius, slices, steps, t, n, &out);
    for (auto& th : pool) th.join();
    return out;
}

double Outside(const std::vector<float>& a, const std::vector<float>& b, const raw::post::ViewGBuffer& v)
{
    long px = 0, off = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (v.depth[i] < 0.0f) continue;
        ++px; off += std::fabs(double(a[i]) - double(b[i])) > 1e-3;
    }
    return px ? double(off) / double(px) : 0.0;
}
} // namespace precision
