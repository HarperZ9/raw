// EnginePass: the Playground engine (superframe native/) behind the bridge's
// pass seam. The bridge hands over its D3D12 device, queue, fence and shared
// textures; the engine's RHI adopts them (raw/rhi/host.hpp), records the
// frame, and submits it only after D3D11 has signalled and flushed.
//
// The frame: the game's depth and normals -> the view G-buffer (GamePack.hlsl)
// -> the engine's checked GTAO (its post_gtao pass, unchanged) -> the game's
// frame times the visibility, into the shared output (AoComposite.hlsl).
// GPU timestamps come from the RHI.
//
// Original work, MIT.
#include "D3D12Bridge.h"

#include "raw/rhi/host.hpp"
#include "post_params.hpp"      // the engine's PostParamsGpu, params() and post dispatch rows

#include "ao_composite_fs.h"
#include "ao_composite_vs.h"
#include "game_pack.h"
#include "post_gtao.h"

#include <cstring>
#include <string>

namespace raw::bridge
{
namespace
{
using namespace raw::rhi;

struct PackParams
{
    uint32_t width, height, depthStride, normalStride;
    float    projA, projB, tanHalfFovY, aspect;
    uint32_t normalFormat, pad[3];
};
struct CompositeParams
{
    uint32_t width, height, mode, pad0;
    float    strength, pad1[3];
};

bool EngineFormat(DXGI_FORMAT f, TextureFormat* out)
{
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:     *out = TextureFormat::RGBA8Unorm; return true;
        case DXGI_FORMAT_B8G8R8A8_UNORM:     *out = TextureFormat::BGRA8Unorm; return true;
        case DXGI_FORMAT_R10G10B10A2_UNORM:  *out = TextureFormat::RGB10A2Unorm; return true;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: *out = TextureFormat::RGBA16Float; return true;
        case DXGI_FORMAT_R32_FLOAT:          *out = TextureFormat::R32Float; return true;
        default: return false;
    }
}

class EnginePass final : public IBridgePass
{
public:
    const char* Name() const override { return "engine-gtao"; }

    bool Create(const HostResources& host) override
    {
        m_host = host;
        HostDevice hd;
        hd.device = host.device;
        hd.queue = host.queue;
        // How long a frame may wait for the one before. Generous on purpose: with
        // another process loading the GPU, the hand-offs between the two devices
        // were seen to take hundreds of milliseconds (docs/D3D12_BRIDGE.md).
        hd.retireTimeoutMs = host.retireTimeoutMs;
        m_dev = adoptDevice(hd, m_err);
        if (!m_dev) return Fail("adoptDevice");

        const UINT in = static_cast<UINT>(Shared::ColorIn), dp = static_cast<UINT>(Shared::Depth);
        const UINT nr = static_cast<UINT>(Shared::Normal), out = static_cast<UINT>(Shared::ColorOut);
        TextureFormat colour{}, normal{};
        if (!EngineFormat(host.format[in], &colour) || host.format[out] != host.format[in]) {
            m_err = "the frame format " + std::to_string(static_cast<int>(host.format[in])) + " has no engine format";
            return Fail("formats");
        }
        m_hasNormalTexture = host.tex[nr] != nullptr;
        if (m_hasNormalTexture && host.format[nr] != DXGI_FORMAT_R8G8B8A8_UNORM) {
            m_err = "the normal target format " + std::to_string(static_cast<int>(host.format[nr])) + " has no decoder";
            return Fail("formats");
        }
        normal = TextureFormat::RGBA8Unorm;
        m_colorIn = m_dev->importTexture({host.tex[in], colour, TextureUsage::Sampled, TextureState::Common}, m_err);
        m_depth = m_dev->importTexture({host.tex[dp], TextureFormat::R32Float, TextureUsage::None, TextureState::Common}, m_err);
        if (m_hasNormalTexture)
            m_normal = m_dev->importTexture({host.tex[nr], normal, TextureUsage::None, TextureState::Common}, m_err);
        m_colorOut = m_dev->importTexture({host.tex[out], colour, TextureUsage::RenderTarget, TextureState::Common}, m_err);
        if (!m_colorIn.valid() || !m_depth.valid() || !m_colorOut.valid() || (m_hasNormalTexture && !m_normal.valid()))
            return Fail("importTexture");

        static constexpr Binding packLayout[4] = {Binding::Uniform, Binding::StorageRead, Binding::StorageRead,
                                                  Binding::StorageReadWrite};
        static constexpr Binding gtaoLayout[3] = {Binding::Uniform, Binding::StorageRead, Binding::StorageReadWrite};
        static constexpr RasterBinding compLayout[3] = {RasterBinding::Uniform, RasterBinding::StorageRead,
                                                        RasterBinding::Texture};
        m_pack = m_dev->createComputePipeline(
            {"game_pack", {ShaderFormat::Dxil, kDxil_game_pack, sizeof kDxil_game_pack}, packLayout}, m_err);
        m_gtao = m_dev->createComputePipeline(
            {"post_gtao", {ShaderFormat::Dxil, kDxil_post_gtao, sizeof kDxil_post_gtao}, gtaoLayout}, m_err);
        RasterPipelineDesc rd;
        rd.label = "ao_composite";
        rd.vertex = {ShaderFormat::Dxil, kDxil_ao_composite_vs, sizeof kDxil_ao_composite_vs};
        rd.fragment = {ShaderFormat::Dxil, kDxil_ao_composite_fs, sizeof kDxil_ao_composite_fs};
        rd.layout = compLayout;
        rd.targets[0] = colour;
        m_comp = m_dev->createRasterPipeline(rd, m_err);
        if (!m_pack.valid() || !m_gtao.valid() || !m_comp.valid()) return Fail("pipelines");

        const uint64_t w = host.width, h = host.height, n = w * h;
        m_depthPitch = textureRowPitch(host.width, TextureFormat::R32Float);
        m_normalPitch = textureRowPitch(host.width, normal);
        m_uPack = m_dev->createBuffer({sizeof(PackParams), BufferUsage::Uniform, "pack params"}, m_err);
        m_uGtao = m_dev->createBuffer({sizeof(gpu_check::PostParamsGpu), BufferUsage::Uniform, "post params"}, m_err);
        m_uComp = m_dev->createBuffer({sizeof(CompositeParams), BufferUsage::Uniform, "composite params"}, m_err);
        m_bDepth = m_dev->createBuffer({uint64_t(m_depthPitch) * h, BufferUsage::Storage | BufferUsage::CopyDst, "game depth"}, m_err);
        m_bNormal = m_dev->createBuffer({uint64_t(m_normalPitch) * h, BufferUsage::Storage | BufferUsage::CopyDst, "game normal"}, m_err);
        m_bG = m_dev->createBuffer({n * 32, BufferUsage::Storage, "view gbuffer"}, m_err);
        m_bAo = m_dev->createBuffer({n * 4, BufferUsage::Storage | BufferUsage::CopySrc, "visibility"}, m_err);
        if (!m_uPack.valid() || !m_uGtao.valid() || !m_uComp.valid() || !m_bDepth.valid() || !m_bNormal.valid() ||
            !m_bG.valid() || !m_bAo.valid())
            return Fail("buffers");
        host.log("[EnginePass] engine RHI '%s' on the bridge's device: %ux%u, %.0f MB of pass buffers",
                 m_dev->backendName(), host.width, host.height,
                 (double(m_depthPitch) * h + double(m_normalPitch) * h + n * 36.0) / (1024.0 * 1024.0));
        return true;
    }

    bool Record(const FrameConstants& frame) override
    {
        if (!m_dev) return false;
        CommandList* c = m_dev->begin(m_err);   // retires the submission before, within the timeout
        if (!c) return Fail("begin");
        HarvestTimings();

        const uint32_t w = m_host.width, h = m_host.height, n = w * h;
        const bool ao = frame.camera.valid && frame.hasNormal && m_hasNormalTexture && frame.k.mode >= 1.0f;
        if (!ao && frame.k.mode >= 1.0f && m_skipLogs++ < 5)
            m_host.log("[EnginePass] frame passed through: camera %s, normals %s",
                       frame.camera.valid ? "valid" : "missing", frame.hasNormal ? "present" : "missing");
        m_stages = ao ? 3 : 1;
        c->timestamp(0);
        if (ao) {
            const CameraConstants& cam = frame.camera;
            PackParams pp{};
            pp.width = w; pp.height = h;
            pp.depthStride = m_depthPitch / 4; pp.normalStride = m_normalPitch / 4;
            // depth = A - B / viewZ, from the game's projection: rows 2 and 3 of proj.
            pp.projA = cam.proj[10]; pp.projB = -cam.proj[11];
            pp.tanHalfFovY = cam.tanHalfFovY; pp.aspect = cam.aspect;
            gpu_check::PostParamsGpu gp = gpu_check::params(int(w), int(h), cam.tanHalfFovY, cam.aspect, cam.nearZ, 0);
            if (m_tune.radius > 0) gp.view[3] = m_tune.radius;
            if (m_tune.slices > 0) gp.slices = m_tune.slices;
            if (m_tune.steps > 0) gp.steps = m_tune.steps;
            c->upload(m_uPack, 0, &pp, sizeof pp);
            c->upload(m_uGtao, 0, &gp, sizeof gp);
            const BufferBarrier a[2] = {{m_bDepth, m_copyState, Access::CopyDst}, {m_bNormal, m_copyState, Access::CopyDst}};
            c->barrier(a);
            c->copyTextureToBuffer(m_depth, m_bDepth);
            c->copyTextureToBuffer(m_normal, m_bNormal);
            const BufferBarrier b[3] = {{m_bDepth, Access::CopyDst, Access::StorageRead},
                                        {m_bNormal, Access::CopyDst, Access::StorageRead},
                                        {m_bG, m_gState, Access::StorageWrite}};
            c->barrier(b);
            const BufferHandle packBinds[4] = {m_uPack, m_bDepth, m_bNormal, m_bG};
            c->dispatch(m_pack, packBinds, gpu_check::postGroupsX(n), gpu_check::postGroupsY(n), 1);
            c->timestamp(1);
            const BufferBarrier d[2] = {{m_bG, Access::StorageWrite, Access::StorageRead},
                                        {m_bAo, m_aoState, Access::StorageWrite}};
            c->barrier(d);
            const BufferHandle gtaoBinds[3] = {m_uGtao, m_bG, m_bAo};
            c->dispatch(m_gtao, gtaoBinds, gpu_check::postGroupsX(n), gpu_check::postGroupsY(n), 1);
            c->timestamp(2);
            const BufferBarrier e[1] = {{m_bAo, Access::StorageWrite, Access::StorageRead}};
            c->barrier(e);
            m_copyState = Access::StorageRead; m_gState = Access::StorageRead; m_aoState = Access::StorageRead;
        }
        if (m_aoState == Access::Undefined) {
            // The composite binds the visibility buffer even when it passes the frame through.
            const BufferBarrier first[1] = {{m_bAo, Access::Undefined, Access::StorageRead}};
            c->barrier(first);
            m_aoState = Access::StorageRead;
        }
        CompositeParams cp{};
        cp.width = w; cp.height = h;
        cp.mode = !ao ? 0u : (frame.k.mode >= 2.0f ? 2u : 1u);
        cp.strength = m_tune.strength;
        c->upload(m_uComp, 0, &cp, sizeof cp);
        RenderPassDesc rp;
        rp.color[0].texture = m_colorOut;
        c->beginRenderPass(rp);
        const RasterBind draw[3] = {{m_uComp, {}, {}}, {m_bAo, {}, {}}, {{}, m_colorIn, {}}};
        c->draw(m_comp, draw, 3);
        c->endRenderPass();
        c->timestamp(ao ? 3 : 1);
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
        m_pendingStages = m_stages;
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

    void Tune(const EngineTuning& t) { m_tune = t; }
    // Test access: the visibility buffer of the last retired frame, one float a pixel.
    bool ReadVisibility(std::vector<float>* out)
    {
        if (!m_dev || m_dev->retire(120000, m_err) != WaitResult::Done) return false;
        const uint64_t bytes = uint64_t(m_host.width) * m_host.height * 4;
        const BufferHandle rb = m_dev->createBuffer({bytes, BufferUsage::MapRead | BufferUsage::CopyDst, "visibility readback"}, m_err);
        CommandList* c = rb.valid() ? m_dev->begin(m_err) : nullptr;
        if (!c) return Fail("visibility readback");
        const BufferBarrier a[2] = {{m_bAo, m_aoState, Access::CopySrc}, {rb, Access::Undefined, Access::CopyDst}};
        c->barrier(a);
        c->copyBuffer(m_bAo, 0, rb, 0, bytes);
        const BufferBarrier b[1] = {{m_bAo, Access::CopySrc, m_aoState}};
        c->barrier(b);
        bool ok = m_dev->submitAndWait(m_err);
        const void* p = ok ? m_dev->mapRead(rb, bytes, m_err) : nullptr;
        if (p) {
            out->resize(bytes / 4);
            memcpy(out->data(), p, bytes);
            m_dev->unmap(rb);
        }
        m_dev->destroyBuffer(rb);
        return p != nullptr || Fail("visibility map");
    }

private:
    bool Fail(const char* where)
    {
        if (m_host.log) m_host.log("[EnginePass] %s: %s", where, m_err.c_str());
        return false;
    }

    // The RHI resolves a submission's timestamps when it records the end of
    // the list; they are readable once that submission has retired.
    void HarvestTimings()
    {
        if (!m_havePending) return;
        m_havePending = false;
        std::vector<double> t;
        std::string err;
        if (!m_dev->timestampSeconds(m_pendingStages + 1, t, err)) return;
        static const char* kNames[3] = {"pack", "gtao", "composite"};
        m_timings = {};
        m_timings.submission = m_pendingSubmission;
        m_timings.totalMs = (t.back() - t.front()) * 1000.0;
        for (uint32_t k = 0; k < m_pendingStages; ++k) {
            m_timings.stageMs[k] = (t[k + 1] - t[k]) * 1000.0;
            m_timings.stageName[k] = m_pendingStages == 1 ? "composite" : kNames[k];
        }
        m_haveTimings = true;
    }

    HostResources m_host;
    std::unique_ptr<Device> m_dev;
    std::string m_err;
    TextureHandle m_colorIn, m_depth, m_normal, m_colorOut;
    PipelineHandle m_pack, m_gtao;
    RasterPipelineHandle m_comp;
    BufferHandle m_uPack, m_uGtao, m_uComp, m_bDepth, m_bNormal, m_bG, m_bAo;
    Access m_copyState = Access::Undefined, m_gState = Access::Undefined, m_aoState = Access::Undefined;
    uint32_t m_depthPitch = 0, m_normalPitch = 0, m_stages = 1, m_pendingStages = 1;
    uint64_t m_submissions = 0, m_pendingSubmission = 0;
    bool m_hasNormalTexture = false, m_havePending = false, m_haveTimings = false;
    int m_skipLogs = 0;
    PassTimings m_timings;
    EngineTuning m_tune;
};
} // namespace

std::unique_ptr<IBridgePass> MakeEnginePass() { return std::make_unique<EnginePass>(); }

bool EnginePassTune(IBridgePass* pass, const EngineTuning& t)
{
    auto* e = dynamic_cast<EnginePass*>(pass);
    if (e) e->Tune(t);
    return e != nullptr;
}

bool EnginePassReadVisibility(IBridgePass* pass, std::vector<float>* out)
{
    auto* e = dynamic_cast<EnginePass*>(pass);
    return e && e->ReadVisibility(out);
}

} // namespace raw::bridge
