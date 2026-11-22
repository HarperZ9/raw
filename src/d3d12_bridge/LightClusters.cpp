// Clustered light assignment for the lighting pass: the frame is cut into 16 x 9 screen tiles
// and 16 depth slices spaced evenly in log(view z), and every light gets a bit in each cluster
// its sphere can reach (Olsson, Billeter and Assarsson 2012, "Clustered deferred and forward
// shading"). The bounds are conservative: the screen rectangle of the sphere's bounding box,
// projected with the game's own world-to-clip matrix, and the view-z interval of the sphere.
// Original work, MIT.
#include "LightingPass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::bridge
{

void BuildClusters(const SceneLighting& s, float nearZ, float farZ, uint32_t (*out)[4])
{
    std::memset(out, 0, sizeof(uint32_t) * 4 * kClusterCount);
    const float scale = float(kSlices) / std::log(farZ / nearZ);
    const auto row = [&](int r, const float* p) {
        const float* m = s.worldToClip[r];
        return m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    };
    const auto slice = [&](float z) {
        const float v = std::log(std::max(z, nearZ) / nearZ) * scale;
        return std::clamp(int(v), 0, int(kSlices) - 1);
    };
    for (uint32_t i = 0; i < s.lightCount && i < kMaxSceneLights; ++i) {
        const SceneLight& l = s.lights[i];
        // Conservative 1.45x radius + 1-tile/1-slice guard band guarantees Lighting, FroxelPass, VolumetricPass & RtShadow
        // NEVER clip a light at 16x9x16 cluster tile boundaries (eliminates rectangular/horizontal tile-edge bands!).
        const float r = l.radius * 1.45f;
        const float zc = row(3, l.position);   // clip w is view z
        if (zc + r < nearZ * 0.5f) continue;   // wholly behind the camera
        float x0 = -1, x1 = 1, y0 = -1, y1 = 1;
        bool clipped = false;
        float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
        for (int c = 0; c < 8 && !clipped; ++c) {
            const float q[3] = {l.position[0] + ((c & 1) ? r : -r), l.position[1] + ((c & 2) ? r : -r), l.position[2] + ((c & 4) ? r : -r)};
            const float w = row(3, q);
            if (w <= nearZ * 0.5f) { clipped = true; break; }
            const float nx = row(0, q) / w, ny = row(1, q) / w;
            lo[0] = std::min(lo[0], nx); hi[0] = std::max(hi[0], nx);
            lo[1] = std::min(lo[1], ny); hi[1] = std::max(hi[1], ny);
        }
        if (!clipped) { x0 = lo[0]; x1 = hi[0]; y0 = lo[1]; y1 = hi[1]; }
        if (x1 < -1 || x0 > 1 || y1 < -1 || y0 > 1) continue;
        const int tx0 = std::clamp(int((x0 * 0.5f + 0.5f) * kTilesX) - 1, 0, int(kTilesX) - 1);
        const int tx1 = std::clamp(int((x1 * 0.5f + 0.5f) * kTilesX) + 1, 0, int(kTilesX) - 1);
        const int ty0 = std::clamp(int((0.5f - y1 * 0.5f) * kTilesY) - 1, 0, int(kTilesY) - 1);   // tiles count from the top
        const int ty1 = std::clamp(int((0.5f - y0 * 0.5f) * kTilesY) + 1, 0, int(kTilesY) - 1);
        const int s0 = std::max(slice(zc - r) - 1, 0), s1 = std::min(slice(zc + r) + 1, int(kSlices) - 1);
        for (int sl = s0; sl <= s1; ++sl)
            for (int ty = ty0; ty <= ty1; ++ty)
                for (int tx = tx0; tx <= tx1; ++tx)
                    out[(sl * kTilesY + ty) * kTilesX + tx][i >> 5] |= 1u << (i & 31);
    }
}

#ifndef RAW_ENGINE
std::unique_ptr<IBridgePass> MakeLightingPass() { return nullptr; }
bool LightingPassTune(IBridgePass*, const LightingTuning&) { return false; }
#endif

} // namespace raw::bridge
