// LightingPass: see LightingPass.h. The engine's RHI (superframe native/) adopts the bridge's
// device and reads the shared textures in place: the game's depth, normals, motion vectors,
// shadow mask and lit HDR colour, RAW's albedo and specular targets, and the cascade atlas.
// Original work, MIT.
#include "LightingPass.h"

#include "raw/renderer/ao_realtime.hpp"
#include "raw/rhi/host.hpp"

#include "ao_rt_level.h"
#include "ao_rt_prefilter.h"
#include "ao_rt_resolve.h"
#include "ao_rt_trace.h"
#include "ao_rt_vs.h"
#include "light_fog_fs.h"
#include "light_fs.h"
#include "light_vs.h"

#include <cmath>
#include <cstring>
#include <string>

namespace raw::bridge
{
namespace
{
using namespace raw::rhi;

// Mirrors cbuffer P in shaders/LightingCommon.hlsli, row for row.
struct LightUniform
{
    float dims[4], proj[4], clipToWorld[16], worldToClip[16], viewToWorld[12];
    float resScale[4], sunDir[4], sunColour[4], ambient[12], cascade[24], cascadeInfo[4];
    uint32_t flags[4];
    float cluster[4], tune[4], tune2[4];
    float lightPosR[kMaxSceneLights][4], lightColS[kMaxSceneLights][4];
    uint32_t clusters[kClusterCount][4];
};
static_assert(sizeof(LightUniform) == 30 * 16 + 2 * kMaxSceneLights * 16 + kClusterCount * 16, "layout of cbuffer P");
static_assert(sizeof(LightUniform) <= 65536, "one constant buffer view");

constexpr uint32_t kMatch = 1, kSoft = 2, kGgx = 4, kFog = 8, kContact = 16, kAo = 32, kCascades = 64, kSurface = 128, kShadows = 256;
constexpr RasterBinding U = RasterBinding::Uniform, T = RasterBinding::Texture;
constexpr RasterBinding kLightLayout[9] = {U, T, T, T, T, T, T, T, T};
constexpr RasterBinding kFogLayout[4] = {U, T, T, T};

class LightingPass final : public IBridgePass
{
public:
    const char* Name() const override { return "engine-lighting"; }

    bool Create(const HostResources& host) override
    {
        m_host = host;
        HostDevice hd;
        hd.device = host.device; hd.queue = host.queue; hd.retireTimeoutMs = host.retireTimeoutMs;
        m_dev = adoptDevice(hd, m_err);
        if (!m_dev) return Fail("adoptDevice");
        const auto at = [](Shared s) { return static_cast<UINT>(s); };
        if (host.format[at(Shared::ColorIn)] != DXGI_FORMAT_R16G16B16A16_FLOAT) { m_err = "the lighting pass runs on the HDR scene colour"; return Fail("formats"); }
        const auto import = [&](Shared s, TextureFormat f, TextureUsage u) {
            return host.tex[at(s)] ? m_dev->importTexture({host.tex[at(s)], f, u, TextureState::Common}, m_err) : TextureHandle{};
        };
        const TextureUsage S = TextureUsage::Sampled;
        m_game = import(Shared::ColorIn, TextureFormat::RGBA16Float, S);
        m_depth = import(Shared::Depth, TextureFormat::R32Float, S);
        m_normal = import(Shared::Normal, TextureFormat::RGBA8Unorm, S);
        m_albedo = import(Shared::Albedo, TextureFormat::RGBA8Unorm, S);
        m_spec = import(Shared::Specular, TextureFormat::RGBA8Unorm, S);
        m_mask = import(Shared::ShadowMask, TextureFormat::RGBA8Unorm, S);
        m_atlas = import(Shared::ShadowAtlas, TextureFormat::R32Float, S);
        m_out = import(Shared::ColorOut, TextureFormat::RGBA16Float, TextureUsage::RenderTarget);
        m_motion = import(Shared::Motion, TextureFormat::RG16Float, S);
        if (!m_game.valid() || !m_depth.valid() || !m_normal.valid() || !m_albedo.valid() || !m_spec.valid() || !m_mask.valid() ||
            !m_atlas.valid() || !m_out.valid()) return Fail("importTexture");
        const TextureUsage RT = TextureUsage::RenderTarget | TextureUsage::Sampled;
        m_lit = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "lighting lit"}, m_err);
        m_aoOut = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "lighting ao frame"}, m_err);
        m_uniform = m_dev->createBuffer({sizeof(LightUniform), BufferUsage::Uniform, "lighting params"}, m_err);
        if (!m_lit.valid() || !m_aoOut.valid() || !m_uniform.valid()) return Fail("createTexture");
        const auto dxil = [](const void* p, size_t n) { return ShaderCode{ShaderFormat::Dxil, p, n}; };
        RasterPipelineDesc d;
        d.vertex = dxil(kDxil_light_vs, sizeof kDxil_light_vs);
        d.targets[0] = TextureFormat::RGBA16Float;
        d.label = "lighting"; d.fragment = dxil(kDxil_light_fs, sizeof kDxil_light_fs); d.layout = kLightLayout;
        m_lightPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_lightPipe.valid()) return Fail("lighting pipeline");
        d.label = "lighting fog"; d.fragment = dxil(kDxil_light_fog_fs, sizeof kDxil_light_fog_fs); d.layout = kFogLayout;
        m_fogPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_fogPipe.valid()) return Fail("fog pipeline");
        const ao_rt::Shaders sh{dxil(kDxil_ao_rt_vs, sizeof kDxil_ao_rt_vs), dxil(kDxil_ao_rt_prefilter, sizeof kDxil_ao_rt_prefilter),
                                dxil(kDxil_ao_rt_level, sizeof kDxil_ao_rt_level), dxil(kDxil_ao_rt_trace, sizeof kDxil_ao_rt_trace),
                                dxil(kDxil_ao_rt_resolve, sizeof kDxil_ao_rt_resolve)};
        if (!m_ao.create(*m_dev, host.width, host.height, TextureFormat::RGBA16Float, sh, m_err)) return Fail("ao_rt::Pass::create");
        host.log("[Lighting] engine RHI '%s' on the bridge's device: %ux%u, %u lights at most, %u clusters, uniform %u bytes",
                 m_dev->backendName(), host.width, host.height, kMaxSceneLights, kClusterCount, unsigned(sizeof(LightUniform)));
        return true;
    }

    bool Record(const FrameConstants& frame) override
    {
        if (!m_dev) return false;
        CommandList* c = m_dev->begin(m_err);
        if (!c) return Fail("begin");
        HarvestTimings();
        const SceneLighting* l = frame.lighting;
        const bool ok = l && l->valid && frame.camera.valid && frame.hasNormal && frame.hasSurface;
        if (!ok && m_skipLogs++ < 5)
            m_host.log("[Lighting] frame passed through: lights %d, camera %d, normals %d, surface %d", l && l->valid,
                       frame.camera.valid, frame.hasNormal, frame.hasSurface);
        Fill(frame, ok);
        c->upload(m_uniform, 0, &m_u, sizeof m_u);
        c->timestamp(0);
        const bool ao = ok && m_u.flags[0] & kAo;
        if (ao) {
            ao_rt::Params p;
            p.projA = frame.camera.proj[10]; p.projB = -frame.camera.proj[11];
            p.tanHalf = frame.camera.tanHalfFovY; p.aspect = frame.camera.aspect;
            p.radius = 35.0f; p.spheremap = true; p.srgb = false; p.mode = 1;
            ao_rt::Inputs in{m_depth, m_normal, frame.hasMotion ? m_motion : TextureHandle{}, m_game, m_aoOut};
            m_ao.record(*c, in, p, 0);
        } else {
            m_ao.resetHistory();
            for (uint32_t k = 1; k <= 3; ++k) c->timestamp(k);
        }
        const TextureHandle aoVis = ao ? m_ao.history() : m_game;   // unused without the flag
        const RasterBind light[9] = {{m_uniform, {}, {}}, {{}, m_depth, {}}, {{}, m_normal, {}}, {{}, m_albedo, {}}, {{}, m_spec, {}},
                                     {{}, m_mask, {}}, {{}, m_atlas, {}}, {{}, aoVis, {}}, {{}, m_game, {}}};
        Draw(*c, m_lit, m_lightPipe, light);
        c->timestamp(4);
        const RasterBind fog[4] = {{m_uniform, {}, {}}, {{}, m_lit, {}}, {{}, m_depth, {}}, {{}, m_atlas, {}}};
        Draw(*c, m_out, m_fogPipe, fog);
        c->timestamp(5);
        if (!m_dev->endRecording(m_err)) return Fail("endRecording");
        ++m_frame;
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
        if (!m_dev || m_dev->retire(timeoutMs, m_err) != WaitResult::Done) return false;
        HarvestTimings();
        return true;
    }

    bool LastTimings(PassTimings* out) override
    {
        if (!m_haveTimings) return false;
        *out = m_timings;
        return true;
    }

    void Tune(const LightingTuning& t) { m_tune = t; }

    // Hot reload: new DXIL for the lighting and fog pipelines. Both are made before either is
    // swapped in, so a failure keeps the frame on the old pair. The old pipelines stay alive
    // (a frame in flight may still use them); a reload makes two small objects.
    bool Reload(const std::vector<uint8_t>& vs, const std::vector<uint8_t>& fs, const std::vector<uint8_t>& fogFs, std::string& err)
    {
        if (!m_dev) { err = "no device"; return false; }
        const auto dxil = [](const std::vector<uint8_t>& b) { return ShaderCode{ShaderFormat::Dxil, b.data(), b.size()}; };
        RasterPipelineDesc d;
        d.vertex = dxil(vs);
        d.targets[0] = TextureFormat::RGBA16Float;
        d.label = "lighting (reloaded)"; d.fragment = dxil(fs); d.layout = kLightLayout;
        RasterPipelineHandle light = m_dev->createRasterPipeline(d, err);
        if (!light.valid()) return false;
        d.label = "lighting fog (reloaded)"; d.fragment = dxil(fogFs); d.layout = kFogLayout;
        RasterPipelineHandle fog = m_dev->createRasterPipeline(d, err);
        if (!fog.valid()) return false;
        m_lightPipe = light;
        m_fogPipe = fog;
        return true;
    }

private:
    static void Draw(CommandList& c, TextureHandle target, RasterPipelineHandle pipe, std::span<const RasterBind> binds)
    {
        RenderPassDesc rp;
        rp.color[0].texture = target;
        c.beginRenderPass(rp);
        c.draw(pipe, binds, 3);
        c.endRenderPass();
    }

    void Fill(const FrameConstants& frame, bool ok)
    {
        LightUniform& u = m_u;
        const LightingTuning& t = m_tune;
        u.dims[0] = float(m_host.width); u.dims[1] = float(m_host.height); u.dims[2] = 1.0f / u.dims[0]; u.dims[3] = 1.0f / u.dims[1];
        u.proj[0] = frame.camera.proj[10]; u.proj[1] = -frame.camera.proj[11]; u.proj[2] = float(m_frame & 0xFFFF);
        uint32_t f = ok ? kSurface : 0u;
        if (!ok) { u.flags[0] = 0; return; }
        const SceneLighting& s = *frame.lighting;
        if (t.mode <= 1) f |= kMatch;
        else f |= (t.softShadows ? kSoft : 0u) | (t.ggxSpecular ? kGgx : 0u) | (t.volumetricFog ? kFog : 0u) |
                  (t.contactShadows ? kContact : 0u) | (t.ao ? kAo : 0u);
        if (t.cascades) f |= kCascades;
        if (frame.hasShadows && s.hasCascades) f |= kShadows;
        u.flags[0] = f; u.flags[1] = s.lightCount; u.flags[2] = s.cascades; u.flags[3] = uint32_t(t.view);
        std::memcpy(u.clipToWorld, s.clipToWorld, 64);
        std::memcpy(u.worldToClip, s.worldToClip, 64);
        std::memcpy(u.viewToWorld, s.viewToWorld, 48);
        u.resScale[0] = s.resolutionScale[0]; u.resScale[1] = s.resolutionScale[1]; u.resScale[2] = s.knee; u.resScale[3] = s.specularScale;
        for (int k = 0; k < 3; ++k) { u.sunDir[k] = s.sunDirection[k]; u.sunColour[k] = s.sunColour[k]; }
        u.sunDir[3] = s.hasSun ? 1.0f : 0.0f;
        u.sunColour[3] = (f & kShadows) ? 1.0f : 0.0f;
        std::memcpy(u.ambient, s.ambient, 48);
        std::memcpy(u.cascade, s.cascade, 96);
        u.cascadeInfo[0] = s.cascadeBias[0]; u.cascadeInfo[1] = s.cascadeBias[1]; u.cascadeInfo[2] = s.splitDepth;
        u.cascadeInfo[3] = float(m_host.shadowSide);
        const float nearZ = frame.camera.nearZ > 0 ? frame.camera.nearZ : 15.0f;
        u.cluster[0] = nearZ; u.cluster[1] = t.clusterFar; u.cluster[2] = float(kSlices) / std::log(t.clusterFar / nearZ);
        u.tune[0] = t.fogDensity; u.tune[1] = t.fogAnisotropy; u.tune[2] = t.softRange; u.tune[3] = t.contactLength;
        u.tune2[0] = t.aoStrength; u.tune2[1] = t.fogLightScale; u.tune2[2] = t.fogSunScale; u.tune2[3] = t.fogMaxDistance;
        for (uint32_t i = 0; i < s.lightCount; ++i) {
            const SceneLight& l = s.lights[i];
            for (int k = 0; k < 3; ++k) { u.lightPosR[i][k] = l.position[k]; u.lightColS[i][k] = l.colour[k]; }
            u.lightPosR[i][3] = l.radius;
            u.lightColS[i][3] = float(l.shadowChannel);
        }
        BuildClusters(s, nearZ, t.clusterFar, u.clusters);
    }

    bool Fail(const char* where)
    {
        if (m_host.log) m_host.log("[Lighting] %s: %s", where, m_err.c_str());
        return false;
    }

    void HarvestTimings()
    {
        if (!m_havePending) return;
        m_havePending = false;
        std::vector<double> t;
        std::string err;
        if (!m_dev->timestampSeconds(6, t, err)) return;
        static const char* kNames[3] = {"ao", "lighting", "fog"};
        const double ms[3] = {(t[3] - t[0]) * 1000.0, (t[4] - t[3]) * 1000.0, (t[5] - t[4]) * 1000.0};
        m_timings = {};
        m_timings.submission = m_pendingSubmission;
        m_timings.totalMs = (t[5] - t[0]) * 1000.0;
        for (uint32_t k = 0; k < 3; ++k) { m_timings.stageMs[k] = ms[k]; m_timings.stageName[k] = kNames[k]; }
        m_haveTimings = true;
    }

    HostResources m_host;
    std::unique_ptr<Device> m_dev;
    std::string m_err;
    ao_rt::Pass m_ao;
    TextureHandle m_game, m_depth, m_normal, m_albedo, m_spec, m_mask, m_atlas, m_out, m_motion, m_lit, m_aoOut;
    BufferHandle m_uniform;
    RasterPipelineHandle m_lightPipe, m_fogPipe;
    LightUniform m_u{};
    LightingTuning m_tune;
    uint64_t m_frame = 0, m_submissions = 0, m_pendingSubmission = 0;
    bool m_havePending = false, m_haveTimings = false;
    int m_skipLogs = 0;
    PassTimings m_timings;
};
} // namespace

std::unique_ptr<IBridgePass> MakeLightingPass() { return std::make_unique<LightingPass>(); }

bool LightingPassReload(IBridgePass* pass, const std::vector<uint8_t>& vs, const std::vector<uint8_t>& fs, const std::vector<uint8_t>& fogFs,
                        std::string& err)
{
    auto* p = dynamic_cast<LightingPass*>(pass);
    if (!p) { err = "the active pass is not the lighting pass"; return false; }
    return p->Reload(vs, fs, fogFs, err);
}

bool LightingPassTune(IBridgePass* pass, const LightingTuning& t)
{
    auto* p = dynamic_cast<LightingPass*>(pass);
    if (p) p->Tune(t);
    return p != nullptr;
}

} // namespace raw::bridge
