#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include <string>
#include <cstdio>
#include <fstream>
using namespace raw;
int main(int argc, char** argv){
    std::string out = argc>1 ? argv[1] : ".";
    const int W=256, H=256;
    Scene s = buildTestScene(W,H);
    GBuffer g = rasterize(s, W, H);
    LinearAccel accel; accel.build(s);
    Buffer<float> aoRT = computeRTAO(g, accel, 64, 2.0f);
    Buffer<float> aoSS = computeSSAO(g, 24, 2.0f);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f);
    Certificate cert = certificate_from_reconcile(rec, 0.12f);
    std::string certJson = to_json(cert);
    std::ofstream(out + "/certificate.json") << certJson;
    Buffer<Vec3> frame = shade(g, aoRT, s);
    writePPM(frame, out + "/frame.ppm");
    writePGM(aoRT,  out + "/ao_rt.pgm");
    writePGM(aoSS,  out + "/ao_ss.pgm");
    writePGM(rec.errorMap, out + "/ao_error.pgm");
    std::printf("reconcile: pixels=%d rmse=%.4f maxError=%.4f verdict=%s\n",
        rec.pixels, rec.rmse, rec.maxError,
        rec.withinTolerance ? "WITHIN-TOLERANCE" : "DIVERGENT");
    std::printf("certificate: %s\n", certJson.c_str());
    return 0;
}
