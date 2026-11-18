// BridgeHook, lighting form: what a capture measures. The frame's geometry against the window
// and the monitor, the match of our lighting against the game's own lit buffer with a coverage
// check, and the dump of one frame for tools/lighting. Original work, MIT.
#include "BridgeHookState.h"
#include "GBufferComplete.h"
#include "ProxyLog.h"
#include "SceneLights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace SB::Proxy::hook
{
using raw::bridge::Shared;

namespace
{
float Half(uint16_t h)
{
    const int e = (h >> 10) & 31, m = h & 1023;
    const float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? 65504.0f : std::ldexp(float(m + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

// CIE L*a*b* of a linear HDR colour after the display map x / (1 + x) and the sRGB curve.
void Lab(const float* c, double* lab)
{
    double rgb[3];
    for (int k = 0; k < 3; ++k) {
        const double x = std::max(0.0, double(c[k]));
        const double v = x / (1.0 + x);   // tone map, then sRGB encode and decode cancel: linear in [0, 1)
        rgb[k] = v;
    }
    const double X = 0.4124 * rgb[0] + 0.3576 * rgb[1] + 0.1805 * rgb[2];
    const double Y = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    const double Z = 0.0193 * rgb[0] + 0.1192 * rgb[1] + 0.9505 * rgb[2];
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    const double fx = f(X / 0.95047), fy = f(Y), fz = f(Z / 1.08883);
    lab[0] = 116 * fy - 16; lab[1] = 500 * (fx - fy); lab[2] = 200 * (fy - fz);
}

} // namespace

// Every size the frame passes through, against the window and the monitor it is on: the swap
// chain's buffers, the backbuffer, the window's client area, the monitor, and the targets the
// bridge reads. A backbuffer larger than the monitor shows only part of the frame on screen
// (the game's iSize W/H above the display mode) while every capture stays whole.
void LightingLogGeometry(IDXGISwapChain* sc, const D3D11_TEXTURE2D_DESC& bb)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sc->GetDesc(&sd);
    RECT client{};
    GetClientRect(sd.OutputWindow, &client);
    MONITORINFO mi{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromWindow(sd.OutputWindow, MONITOR_DEFAULTTONEAREST), &mi);
    const LONG mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    const GBufferTextures g = GBuffer_Textures();
    D3D11_TEXTURE2D_DESC ad{}, cd{};
    if (g.albedo) g.albedo->GetDesc(&ad);
    if (ID3D11Texture2D* c = SceneLights_Cascades()) c->GetDesc(&cd);
    Log("[Lighting] geometry: swap chain %ux%u, backbuffer %ux%u, window client %ldx%ld, monitor %ldx%ld (%s), albedo %ux%u, cascades %ux%u",
        sd.BufferDesc.Width, sd.BufferDesc.Height, bb.Width, bb.Height, client.right, client.bottom, mw, mh,
        sd.Windowed ? "windowed" : "full screen", ad.Width, ad.Height, cd.Width, cd.Height);
    if (LONG(bb.Width) > mw || LONG(bb.Height) > mh)
        Log("[Lighting] WARNING: the backbuffer (%ux%u) is larger than the monitor (%ldx%ld): the screen shows only its top-left "
            "%.0f %%; captures are whole. Set iSize W/H in SkyrimPrefs.ini to the display mode to see all of it.",
            bb.Width, bb.Height, mw, mh, 100.0 * double(std::min<LONG>(mw, bb.Width)) * std::min<LONG>(mh, bb.Height) / (double(bb.Width) * bb.Height));
}

// Ours (colour out) against the game's own lit buffer (colour in) on the pixels we light.
void LightingStats(const char* step, int mode, std::string* json)
{
    raw::bridge::D3D12Bridge* b = Bridge();
    std::vector<uint8_t> gin, gout, spec, depth;
    UINT r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    if (mode == 0 || !b->ReadShared(Shared::ColorIn, &gin, &r0) || !b->ReadShared(Shared::ColorOut, &gout, &r1) ||
        !b->ReadShared(Shared::Specular, &spec, &r2) || !b->ReadShared(Shared::Depth, &depth, &r3)) return;
    const UINT w = b->Config().width, h = b->Config().height;
    double absSum = 0, refSum = 0, dE = 0;
    uint64_t n = 0, under2 = 0, under5 = 0, quad[4] = {}, changed[4] = {};
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            const uint8_t* s = spec.data() + size_t(y) * r2 + x * 4;
            const uint32_t cls = s[2], base = cls & 127u;
            float d;
            memcpy(&d, depth.data() + size_t(y) * r3 + x * 4, 4);
            if (base == 0 || base >= 8 || (cls & 128u) || d >= 1.0f) continue;
            float g[3], o[3];
            for (int k = 0; k < 3; ++k) {
                uint16_t hg, ho;
                memcpy(&hg, gin.data() + size_t(y) * r0 + x * 8 + k * 2, 2);
                memcpy(&ho, gout.data() + size_t(y) * r1 + x * 8 + k * 2, 2);
                g[k] = Half(hg); o[k] = Half(ho);
                absSum += std::fabs(double(o[k]) - g[k]); refSum += std::fabs(double(g[k]));
            }
            const int q = (x >= w / 2 ? 1 : 0) + (y >= h / 2 ? 2 : 0);
            ++quad[q];
            changed[q] += g[0] != o[0] || g[1] != o[1] || g[2] != o[2];
            double la[3], lb[3];
            Lab(g, la); Lab(o, lb);
            const double e = std::sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) + (la[2] - lb[2]) * (la[2] - lb[2]));
            dE += e; under2 += e < 2.0; under5 += e < 5.0; ++n;
        }
    if (!n) return;
    // Coverage: the pass must reach the whole frame. In each quadrant, the share of lit pixels our
    // output changed must be at least half the largest quadrant's share. A pass drawn with a half-size
    // viewport changes one quadrant only and fails (the View=9 control does exactly that).
    double share[4] = {}, most = 0;
    for (int k = 0; k < 4; ++k) { share[k] = quad[k] ? double(changed[k]) / quad[k] : 0; most = std::max(most, share[k]); }
    bool covered = most > 0;
    for (int k = 0; k < 4; ++k) covered &= quad[k] == 0 || share[k] >= 0.5 * most;
    Log("[Lighting] coverage %s: changed share by quadrant %.3f %.3f / %.3f %.3f (frame %ux%u)", covered ? "PASS" : "FAIL",
        share[0], share[1], share[2], share[3], w, h);
    char line[512];
    sprintf_s(line, "%s\t%s\t%u\t%llu\t%.5f\t%.4f\t%.4f\t%.4f\t%s", Cfg().lightScene.c_str(), step, h, (unsigned long long)n,
              absSum / refSum, dE / n, double(under2) / n, double(under5) / n, covered ? "covered" : "NOT-COVERED");
    Log("[Lighting] match %s: pixels %llu, relative mean abs difference %.4f, mean dE76 %.3f, dE<2 %.3f, dE<5 %.3f", step,
        (unsigned long long)n, absSum / refSum, dE / n, double(under2) / n, double(under5) / n);
    FILE* f = nullptr;
    if (fopen_s(&f, (Cfg().lightCaptureDir + "\\lighting_stats.tsv").c_str(), "a") == 0 && f) { fprintf(f, "%s\n", line); fclose(f); }
    if (json) {
        char j[512];
        sprintf_s(j, "\"pixels\":%llu,\"rel_mean_abs\":%.5f,\"mean_dE76\":%.4f,\"dE_under_2\":%.4f,\"dE_under_5\":%.4f,"
                     "\"covered\":%s,\"changed_by_quadrant\":[%.4f,%.4f,%.4f,%.4f]", (unsigned long long)n, absSum / refSum, dE / n,
                  double(under2) / n, double(under5) / n, covered ? "true" : "false", share[0], share[1], share[2], share[3]);
        *json = j;
    }
}

// Every input and both outputs of one frame, for the offline model check (tools/lighting/).
void LightingDump()
{
    raw::bridge::D3D12Bridge* b = Bridge();
    static const char* kNames[raw::bridge::kSharedCount] = {"color_in", "depth", "normal", "color_out", "motion",
                                                             "albedo", "specular", "shadow_mask", "shadow_atlas"};
    const std::string base = Cfg().lightCaptureDir + "\\" + Cfg().lightScene + "_";
    for (UINT k = 0; k < raw::bridge::kSharedCount; ++k) {
        std::vector<uint8_t> bytes;
        UINT row = 0;
        if (!b->ReadShared(static_cast<Shared>(k), &bytes, &row) || !row) continue;
        FILE* f = nullptr;
        if (fopen_s(&f, (base + kNames[k] + ".bin").c_str(), "wb") != 0 || !f) continue;
        const uint32_t hdr[3] = {0, uint32_t(bytes.size() / row), row};
        fwrite(hdr, 4, 3, f);
        fwrite(bytes.data(), 1, bytes.size(), f);
        fclose(f);
    }
    raw::bridge::SceneLighting l = SceneLights_Frame();
    FILE* f = nullptr;
    if (fopen_s(&f, (base + "lighting.bin").c_str(), "wb") == 0 && f) { fwrite(&l, sizeof l, 1, f); fclose(f); }
    Log("[Lighting] dumped the frame's inputs and outputs (%u bytes of lighting)", unsigned(sizeof l));
}

} // namespace SB::Proxy::hook
