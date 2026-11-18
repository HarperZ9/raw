// RealtimeAoPass: the Playground engine's real-time AO (superframe native/,
// raw/renderer/ao_realtime.hpp) behind the bridge's pass seam. The engine's RHI
// adopts the bridge's device and reads the shared textures directly: the game's
// depth, its normal target in the game's own encoding, its motion vectors and
// the frame. No buffer copies and no pack step.
//
// One frame: depth -> half-resolution view depth and three coarser levels ->
// the trace at half resolution (two noisy slices of 16 steps a side) -> the resolve at full
// resolution (depth-aware upsample, accumulation along the motion vectors with
// rejection of uncovered pixels, and the frame times the visibility).
//
// Original work, MIT.
#include "D3D12Bridge.h"

#include "raw/renderer/ao_realtime.hpp"
#include "raw/rhi/host.hpp"

#include "ao_rt_level.h"
#include "ao_rt_prefilter.h"
#include "ao_rt_resolve.h"
#include "ao_rt_trace.h"
#include "ao_rt_vs.h"

#include <cmath>
#include <cstring>
#include <string>

namespace raw::bridge
{
namespace
{
using namespace raw::rhi;

float HalfToFloat(uint16_t h)
{
    const int e = (h >> 10) & 31, m = h & 1023;
    const float v = e == 0 ? std::ldexp(float(m), -24) : std::ldexp(float(m + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

class RealtimeAoPass final : public IBridgePass
{
public:
    const char* Name() const override { return "engine-ao-realtime"; }

    bool Create(const HostResources& host) override
    {
        m_host = host;
        HostDevice hd;
        hd.device = host.device;
        hd.queue = host.queue;
        hd.retireTimeoutMs = host.retireTimeoutMs;
        m_dev = adoptDevice(hd, m_err);
        if (!m_dev) return Fail("adoptDevice");
        const auto at = [&](Shared s) { return static_cast<UINT>(s); };
        TextureFormat colour{};
        switch (host.format[at(Shared::ColorIn)]) {
            case DXGI_FORMAT_R8G8B8A8_UNORM:     colour = TextureFormat::RGBA8Unorm; break;
            case DXGI_FORMAT_B8G8R8A8_UNORM:     colour = TextureFormat::BGRA8Unorm; break;
            case DXGI_FORMAT_R10G10B10A2_UNORM:  colour = TextureFormat::RGB10A2Unorm; break;
            case DXGI_FORMAT_R16G16B16A16_FLOAT: colour = TextureFormat::RGBA16Float; break;
            default: m_err = "the frame format has no engine format"; return Fail("formats");
        }
        // The frame is display-referred when it is 8 or 10 bits: multiply in linear light.
        m_srgb = colour != TextureFormat::RGBA16Float;
        if (!host.tex[at(Shared::Normal)] || host.format[at(Shared::Normal)] != DXGI_FORMAT_R8G8B8A8_UNORM) {
            m_err = "the pass needs the game's RGBA8 normal target";
            return Fail("formats");
        }
        const auto import = [&](Shared s, TextureFormat f, TextureUsage u) {
            return m_dev->importTexture({host.tex[at(s)], f, u, TextureState::Common}, m_err);
        };
        m_in.frame = import(Shared::ColorIn, colour, TextureUsage::Sampled);
        m_in.depth = import(Shared::Depth, TextureFormat::R32Float, TextureUsage::Sampled);
        m_in.normal = import(Shared::Normal, TextureFormat::RGBA8Unorm, TextureUsage::Sampled);
        m_in.target = import(Shared::ColorOut, colour, TextureUsage::RenderTarget);
        if (!m_in.frame.valid() || !m_in.depth.valid() || !m_in.normal.valid() || !m_in.target.valid()) return Fail("importTexture");
        if (host.tex[at(Shared::Motion)] && host.format[at(Shared::Motion)] == DXGI_FORMAT_R16G16_FLOAT) {
            m_motion = import(Shared::Motion, TextureFormat::RG16Float, TextureUsage::Sampled);
            if (!m_motion.valid()) return Fail("importTexture (motion)");
        }
        const auto dxil = [](const void* p, size_t n) { return ShaderCode{ShaderFormat::Dxil, p, n}; };
        const ao_rt::Shaders sh{dxil(kDxil_ao_rt_vs, sizeof kDxil_ao_rt_vs), dxil(kDxil_ao_rt_prefilter, sizeof kDxil_ao_rt_prefilter),
                                dxil(kDxil_ao_rt_level, sizeof kDxil_ao_rt_level), dxil(kDxil_ao_rt_trace, sizeof kDxil_ao_rt_trace),
                                dxil(kDxil_ao_rt_resolve, sizeof kDxil_ao_rt_resolve)};
        if (!m_pass.create(*m_dev, host.width, host.height, colour, sh, m_err)) return Fail("ao_rt::Pass::create");
        const double mb = double(host.width) * host.height * (2.0 * 8 + 0.25 * 4 * 2.33) / (1024.0 * 1024.0);
        host.log("[RealtimeAo] engine RHI '%s' on the bridge's device: %ux%u, motion vectors %s, %.0f MB of pass textures",
                 m_dev->backendName(), host.width, host.height, m_motion.valid() ? "shared" : "absent", mb);
        return true;
    }

    bool Record(const FrameConstants& frame) override
    {
        if (!m_dev) return false;
        CommandList* c = m_dev->begin(m_err);   // retires the submission before, within the timeout
        if (!c) return Fail("begin");
        HarvestTimings();
        const bool ao = frame.camera.valid && frame.hasNormal && frame.k.mode >= 1.0f;
        if (!ao && frame.k.mode >= 1.0f && m_skipLogs++ < 5)
            m_host.log("[RealtimeAo] frame passed through: camera %s, normals %s",
                       frame.camera.valid ? "valid" : "missing", frame.hasNormal ? "present" : "missing");
        if (!ao || m_reset) { m_pass.resetHistory(); m_reset = false; }
        ao_rt::Params p;
        const CameraConstants& cam = frame.camera;
        // depth = A - B / viewZ, from the game's projection: rows 2 and 3 of proj.
        p.projA = cam.proj[10]; p.projB = -cam.proj[11];
        p.tanHalf = cam.tanHalfFovY; p.aspect = cam.aspect;
        p.radius = m_tune.radius > 0 ? m_tune.radius : 35.0f;
        p.strength = m_tune.strength;
        p.slices = m_tune.slices ? m_tune.slices : 2;
        p.steps = m_tune.steps ? m_tune.steps : 16;
        p.noise = m_tune.noise; p.temporal = m_tune.temporal; p.levels = m_tune.levels;
        p.spheremap = true;   // the game's normal target (docs/FRAME_BUFFERS.md)
        p.srgb = m_srgb;
        p.mode = !ao ? 0u : (frame.k.mode >= 2.0f ? 2u : 1u);
        p.maxHistory = m_tune.maxHistory;
        p.levelBias = m_tune.levelBias;
        p.zTol = m_tune.depthTolerance;
        p.mvScale[0] = m_tune.motionScale[0]; p.mvScale[1] = m_tune.motionScale[1];
        ao_rt::Inputs in = m_in;
        in.motion = frame.hasMotion ? m_motion : TextureHandle{};
        c->timestamp(0);
        m_pass.record(*c, in, p, 0);
        if (!m_dev->endRecording(m_err)) return Fail("endRecording");
        return true;
    }

    bool Submit(uint64_t waitValue, uint64_t signalValue) override
    {
        FenceSync sync;
        sync.fence = m_host.fence;
        sync.wait = true; sync.waitValue = waitValue;
        sync.signal = true; sync.signalValue = signalValue;
        if (!m_dev->submit(sync, m_err)) return Fail("submit");
        m_pendingSubmission = m_submissions++;
        m_havePending = true;
        return true;
    }

    bool Retire(DWORD timeoutMs) override
    {
        if (!m_dev) return false;
        if (m_dev->retire(timeoutMs, m_err) != WaitResult::Done) return false;
        HarvestTimings();
        return true;
    }

    bool LastTimings(PassTimings* out) override
    {
        if (!m_haveTimings) return false;
        *out = m_timings;
        return true;
    }

    void Tune(const EngineTuning& t) { m_tune = t; m_reset = m_reset || t.resetHistory; }

    // Test access: the accumulated visibility of the last retired frame, one float a pixel.
    bool ReadVisibility(std::vector<float>* out)
    {
        if (!m_dev || m_dev->retire(120000, m_err) != WaitResult::Done) return false;
        const uint32_t w = m_host.width, h = m_host.height, pitch = textureRowPitch(w, TextureFormat::RGBA16Float);
        const uint64_t bytes = uint64_t(pitch) * h;
        const BufferHandle rb = m_dev->createBuffer({bytes, BufferUsage::MapRead | BufferUsage::CopyDst, "visibility readback"}, m_err);
        CommandList* c = rb.valid() ? m_dev->begin(m_err) : nullptr;
        if (!c) return Fail("visibility readback");
        const BufferBarrier a[1] = {{rb, Access::Undefined, Access::CopyDst}};
        c->barrier(a);
        c->copyTextureToBuffer(m_pass.history(), rb);
        const bool ok = m_dev->submitAndWait(m_err);
        const auto* p = ok ? static_cast<const uint8_t*>(m_dev->mapRead(rb, bytes, m_err)) : nullptr;
        if (p) {
            out->resize(size_t(w) * h);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    uint16_t v;
                    memcpy(&v, p + size_t(y) * pitch + size_t(x) * 8, 2);
                    (*out)[size_t(y) * w + x] = HalfToFloat(v);
                }
            m_dev->unmap(rb);
        }
        m_dev->destroyBuffer(rb);
        return p != nullptr || Fail("visibility map");
    }

private:
    bool Fail(const char* where)
    {
        if (m_host.log) m_host.log("[RealtimeAo] %s: %s", where, m_err.c_str());
        return false;
    }

    void HarvestTimings()
    {
        if (!m_havePending) return;
        m_havePending = false;
        std::vector<double> t;
        std::string err;
        if (!m_dev->timestampSeconds(4, t, err)) return;
        static const char* kNames[3] = {"depth levels", "trace", "resolve"};
        m_timings = {};
        m_timings.submission = m_pendingSubmission;
        m_timings.totalMs = (t[3] - t[0]) * 1000.0;
        for (uint32_t k = 0; k < 3; ++k) {
            m_timings.stageMs[k] = (t[k + 1] - t[k]) * 1000.0;
            m_timings.stageName[k] = kNames[k];
        }
        m_haveTimings = true;
    }

    HostResources m_host;
    std::unique_ptr<Device> m_dev;
    std::string m_err;
    ao_rt::Pass m_pass;
    ao_rt::Inputs m_in;
    TextureHandle m_motion;
    uint64_t m_submissions = 0, m_pendingSubmission = 0;
    bool m_srgb = true, m_havePending = false, m_haveTimings = false, m_reset = false;
    int m_skipLogs = 0;
    PassTimings m_timings;
    EngineTuning m_tune;
};
} // namespace

std::unique_ptr<IBridgePass> MakeRealtimeAoPass() { return std::make_unique<RealtimeAoPass>(); }

bool RealtimeAoTune(IBridgePass* pass, const EngineTuning& t)
{
    auto* e = dynamic_cast<RealtimeAoPass*>(pass);
    if (e) e->Tune(t);
    return e != nullptr;
}

bool RealtimeAoReadVisibility(IBridgePass* pass, std::vector<float>* out)
{
    auto* e = dynamic_cast<RealtimeAoPass*>(pass);
    return e && e->ReadVisibility(out);
}

} // namespace raw::bridge
