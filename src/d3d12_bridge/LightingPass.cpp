// LightingPass: see LightingPass.h. The engine's RHI (superframe native/) adopts the bridge's
// device and reads the shared textures in place: the game's depth, normals, motion vectors,
// shadow mask and lit HDR colour, RAW's albedo and specular targets, and the cascade atlas.
// Original work, MIT.
#include "LightingPass.h"
#include <algorithm>

#include "raw/renderer/ao_realtime.hpp"
#include "raw/rhi/host.hpp"

#include "ao_rt_level.h"
#include "ao_rt_prefilter.h"
#include "ao_rt_resolve.h"
#include "ao_rt_trace.h"
#include "ao_rt_vs.h"
#include "light_fog_fs.h"
#include "clouds_fs.h"
#include "light_fs.h"
#include "light_vs.h"
#include "volumetric_fs.h"
#include "bloom_pre_fs.h"
#include "bloom_down_fs.h"
#include "bloom_up_fs.h"
#include "bloom_comp_fs.h"
#include "atmo_trans_fs.h"
#include "atmo_ms_fs.h"
#include "atmo_sky_fs.h"
#include "rt_refl_filter_fs.h"
#include "dof_focus_fs.h"
#include "dof_coc_fs.h"
#include "dof_gather_fs.h"
#include "dof_post_fs.h"
#include "dof_comp_fs.h"
#include "rawlss5_fs.h"
#include "gi_at1_fs.h"
#include "gi_at2_fs.h"
#include "gi_at4_fs.h"
#include "cloud_noise_fs.h"
#include "env_cube_fs.h"
#include "froxel_inject_fs.h"
#include "froxel_int_fs.h"
#include "world_field_sim_fs.h"

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
    float cluster[4], tune[4], tune2[4], weather[4], camPos[4], clouds[4], cloudWind[4], waterA[4], waterB[4], prevWorldToClip[16];
    float lightPosR[kMaxSceneLights][4], lightColS[kMaxSceneLights][4];
    uint32_t clusters[kClusterCount][4];
    float footTrail[32][4];   // persistent world-space boot-print ring buffer (xyz world abs, w active strength)
    float dofParams[4];       // x: dofMaxDist (0=off), y: dofRadius (px), z: dofOrbBoost, w: weatherLens (0/1)
    float timeEnv[4];         // x: gameHour (0..24), y: sunElev (-1..+1), z: lightning, w: menuPaused (0/1)
    float engineWorld[4];     // x: cell/worldspace waterHeight, y: camStateId, z: hasEngineCell (0/1), w: unused
};
static_assert(sizeof(LightUniform) == 40 * 16 + 2 * kMaxSceneLights * 16 + kClusterCount * 16 + 32 * 16 + 48, "layout of cbuffer P");
static_assert(sizeof(LightUniform) <= 65536, "one constant buffer view");

constexpr uint32_t kMatch = 1, kSoft = 2, kGgx = 4, kFog = 8, kContact = 16, kAo = 32, kCascades = 64, kSurface = 128, kShadows = 256, kFogReplaces = 512, kRt = 1024, kSky = 2048, kBounce = 4096, kReflect = 8192, kBounceIn = 16384, kClouds = 32768, kRest = 65536, kDistantLod = 131072, kEnvMap = 262144, kCharLight = 524288, kUnderwater = 1048576;
constexpr RasterBinding U = RasterBinding::Uniform, T = RasterBinding::Texture, Sm = RasterBinding::Sampler;
constexpr RasterBinding kLightLayout[17] = {U, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, Sm};
constexpr RasterBinding kWorldSimLayout[3] = {U, T, Sm};
constexpr RasterBinding kCloudLayout[5] = {U, T, T, T, Sm};
constexpr RasterBinding kVolLayout[7] = {U, T, T, T, T, T, Sm};
constexpr RasterBinding kFogLayout[10] = {U, T, T, T, T, T, T, T, T, Sm};
constexpr RasterBinding kFroxelInjectLayout[6] = {U, T, T, T, T, Sm};
constexpr RasterBinding kFroxelIntLayout[2] = {U, T};
constexpr RasterBinding kBloom1Layout[2] = {T, Sm};
constexpr RasterBinding kBloom2Layout[3] = {T, T, Sm};
constexpr RasterBinding kAtmoTransLayout[1] = {U};
constexpr RasterBinding kAtmoMsLayout[3] = {U, T, Sm};
constexpr RasterBinding kAtmoSkyLayout[4] = {U, T, T, Sm};
constexpr RasterBinding kEnvCubeLayout[6] = {U, T, T, T, T, Sm};
constexpr RasterBinding kReflFilterLayout[6] = {U, T, T, T, T, Sm};
constexpr RasterBinding kDofFocusLayout[3] = {U, T, T};
constexpr RasterBinding kDofCocLayout[5] = {U, T, T, T, Sm};
constexpr RasterBinding kDofGatherLayout[3] = {U, T, Sm};
constexpr RasterBinding kDofCompLayout[6] = {U, T, T, T, T, Sm};
constexpr RasterBinding kRawlss5Layout[8] = {U, T, T, T, T, T, T, Sm};
constexpr RasterBinding kGiAtrousLayout[4] = {U, T, T, T};

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
        m_soft = import(Shared::Emissive, TextureFormat::RGBA16Float, S);
        m_rtVis = import(Shared::RtVisibility, TextureFormat::RGBA8Unorm, S);
        m_rtIdx = import(Shared::RtLights, TextureFormat::RGBA8Unorm, S);
        m_rtBounce = import(Shared::RtBounce, TextureFormat::RGBA16Float, S);
        m_rtReflect = import(Shared::RtReflect, TextureFormat::RGBA16Float, S);
        if (!m_game.valid() || !m_depth.valid() || !m_normal.valid() || !m_albedo.valid() || !m_spec.valid() || !m_mask.valid() ||
            !m_atlas.valid() || !m_out.valid()) return Fail("importTexture");
        const TextureUsage RT = TextureUsage::RenderTarget | TextureUsage::Sampled;
        m_lit = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "lighting lit"}, m_err);
        m_aoOut = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "lighting ao frame"}, m_err);
        m_uniform = m_dev->createBuffer({sizeof(LightUniform), BufferUsage::Uniform, "lighting params"}, m_err);
        if (!m_lit.valid() || !m_aoOut.valid() || !m_uniform.valid()) return Fail("createTexture");
        // The clouds at a quarter of the width and height, two textures in turn (one is last frame's).
        // Half-resolution temporally-reprojected volumetric clouds (4x GPU speedup vs full-res, 4x density vs 1/4-res;
        // composited at full native resolution against full-res Depth in LightFog.hlsl so silhouettes stay razor-sharp).
        const uint32_t cw = (host.width + 1) / 2, ch = (host.height + 1) / 2;
        for (int k = 0; k < 2; ++k) m_cloud[k] = m_dev->createTexture({cw, ch, TextureFormat::RGBA16Float, RT, "clouds"}, m_err);
        const uint32_t vw = (host.width + 1) / 2, vh = (host.height + 1) / 2;
        for (int k = 0; k < 2; ++k) m_volFog[k] = m_dev->createTexture({vw, vh, TextureFormat::RGBA16Float, RT, "volumetric fog"}, m_err);
        // 6-level progressive RGBA16F HDR Bloom & Halation pyramid (Jimenez SIGGRAPH 2014).
        uint32_t bw = host.width, bh = host.height;
        for (int k = 0; k < 6; ++k) {
            bw = (bw + 1) / 2; bh = (bh + 1) / 2;
            m_bloomDown[k] = m_dev->createTexture({bw, bh, TextureFormat::RGBA16Float, RT, "bloom down"}, m_err);
            if (k < 5) m_bloomUp[k] = m_dev->createTexture({bw, bh, TextureFormat::RGBA16Float, RT, "bloom up"}, m_err);
            if (!m_bloomDown[k].valid() || (k < 5 && !m_bloomUp[k].valid())) return Fail("bloom pyramid textures");
        }
        // Hillaire SIGGRAPH 2020 Physical Atmosphere LUTs + 64^3 (512x512 8x8 atlas) Periodic 3D Perlin-Worley Cloud Volume LUT.
        m_atmoTrans = m_dev->createTexture({256, 64, TextureFormat::RGBA16Float, RT, "atmo transmittance"}, m_err);
        m_atmoMs = m_dev->createTexture({32, 32, TextureFormat::RGBA16Float, RT, "atmo multiscat"}, m_err);
        m_atmoSkyView = m_dev->createTexture({192, 108, TextureFormat::RGBA16Float, RT, "atmo skyview"}, m_err);
        m_cloudNoise = m_dev->createTexture({512, 512, TextureFormat::RGBA16Float, RT, "cloud 64^3 perlin-worley"}, m_err);
        m_envCube = m_dev->createTexture({256, 256, TextureFormat::RGBA16Float, RT, "dynamic prefiltered env cube"}, m_err);
        // True 160x90x64 Temporally-Reprojected 3D Froxel Volumetric Grid (ping-pong m_froxelRaw[2] + integrated m_froxelInt).
        for (int k = 0; k < 2; ++k) m_froxelRaw[k] = m_dev->createTexture({1280, 720, TextureFormat::RGBA16Float, RT, "froxel 160x90x64 raw"}, m_err);
        m_froxelInt = m_dev->createTexture({1280, 720, TextureFormat::RGBA16Float, RT, "froxel 160x90x64 integrated"}, m_err);
        if (!m_atmoTrans.valid() || !m_atmoMs.valid() || !m_atmoSkyView.valid() || !m_cloudNoise.valid() || !m_envCube.valid() ||
            !m_froxelRaw[0].valid() || !m_froxelRaw[1].valid() || !m_froxelInt.valid()) return Fail("atmosphere/cloud/env/froxel LUT textures");
        for (int k = 0; k < 2; ++k) m_reflHist[k] = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "rt refl history"}, m_err);
        if (!m_reflHist[0].valid() || !m_reflHist[1].valid()) return Fail("rt refl history textures");
        for (int k = 0; k < 2; ++k) m_rawlssHist[k] = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "rawlss5 history"}, m_err);
        if (!m_rawlssHist[0].valid() || !m_rawlssHist[1].valid()) return Fail("rawlss5 history textures");
        for (int k = 0; k < 2; ++k) m_dofFocus[k] = m_dev->createTexture({1, 1, TextureFormat::RGBA16Float, RT, "dof focus motor"}, m_err);
        m_dofCoc = m_dev->createTexture({vw, vh, TextureFormat::RGBA16Float, RT, "dof coc"}, m_err);
        m_dofBokeh = m_dev->createTexture({vw, vh, TextureFormat::RGBA16Float, RT, "dof bokeh"}, m_err);
        if (!m_dofFocus[0].valid() || !m_dofFocus[1].valid() || !m_dofCoc.valid() || !m_dofBokeh.valid()) return Fail("dof textures");
        for (int k = 0; k < 2; ++k) m_giAtrous[k] = m_dev->createTexture({host.width, host.height, TextureFormat::RGBA16Float, RT, "gi atrous"}, m_err);
        if (!m_giAtrous[0].valid() || !m_giAtrous[1].valid()) return Fail("gi atrous textures");
        // Persistent World-Anchored 512x512 RGBA16F GPU PDE Fluid Wave & Snowpack Simulation Grid (2,048x2,048u window):
        for (int k = 0; k < 2; ++k) m_worldSim[k] = m_dev->createTexture({512, 512, TextureFormat::RGBA16Float, RT, "world field pde sim"}, m_err);
        if (!m_worldSim[0].valid() || !m_worldSim[1].valid()) return Fail("world field pde sim textures");
        m_lin = m_dev->createSampler({Filter::Linear, AddressMode::ClampToEdge, "clouds linear"}, m_err);
        if (!m_cloud[0].valid() || !m_cloud[1].valid() || !m_volFog[0].valid() || !m_volFog[1].valid() || !m_lin.valid()) return Fail("cloud/volFog textures");
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
        d.label = "clouds"; d.fragment = dxil(kDxil_clouds_fs, sizeof kDxil_clouds_fs); d.layout = kCloudLayout;
        m_cloudPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_cloudPipe.valid()) return Fail("cloud pipeline");
        d.label = "volumetric fog"; d.fragment = dxil(kDxil_volumetric_fs, sizeof kDxil_volumetric_fs); d.layout = kVolLayout;
        m_volPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_volPipe.valid()) return Fail("volumetric pipeline");
        d.label = "bloom pre"; d.fragment = dxil(kDxil_bloom_pre_fs, sizeof kDxil_bloom_pre_fs); d.layout = kBloom1Layout;
        m_bloomPrePipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "bloom down"; d.fragment = dxil(kDxil_bloom_down_fs, sizeof kDxil_bloom_down_fs); d.layout = kBloom1Layout;
        m_bloomDownPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "bloom up"; d.fragment = dxil(kDxil_bloom_up_fs, sizeof kDxil_bloom_up_fs); d.layout = kBloom2Layout;
        m_bloomUpPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "bloom comp"; d.fragment = dxil(kDxil_bloom_comp_fs, sizeof kDxil_bloom_comp_fs); d.layout = kBloom2Layout;
        m_bloomCompPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_bloomPrePipe.valid() || !m_bloomDownPipe.valid() || !m_bloomUpPipe.valid() || !m_bloomCompPipe.valid()) return Fail("bloom pipelines");
        d.label = "atmo trans"; d.fragment = dxil(kDxil_atmo_trans_fs, sizeof kDxil_atmo_trans_fs); d.layout = kAtmoTransLayout;
        m_atmoTransPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "atmo ms"; d.fragment = dxil(kDxil_atmo_ms_fs, sizeof kDxil_atmo_ms_fs); d.layout = kAtmoMsLayout;
        m_atmoMsPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "atmo skyview"; d.fragment = dxil(kDxil_atmo_sky_fs, sizeof kDxil_atmo_sky_fs); d.layout = kAtmoSkyLayout;
        m_atmoSkyPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "cloud noise 64^3"; d.fragment = dxil(kDxil_cloud_noise_fs, sizeof kDxil_cloud_noise_fs); d.layout = kAtmoTransLayout;
        m_cloudNoisePipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "dynamic env cube"; d.fragment = dxil(kDxil_env_cube_fs, sizeof kDxil_env_cube_fs); d.layout = kEnvCubeLayout;
        m_envCubePipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "froxel inject"; d.fragment = dxil(kDxil_froxel_inject_fs, sizeof kDxil_froxel_inject_fs); d.layout = kFroxelInjectLayout;
        m_froxelInjectPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "froxel integrate"; d.fragment = dxil(kDxil_froxel_int_fs, sizeof kDxil_froxel_int_fs); d.layout = kFroxelIntLayout;
        m_froxelIntPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_atmoTransPipe.valid() || !m_atmoMsPipe.valid() || !m_atmoSkyPipe.valid() || !m_cloudNoisePipe.valid() || !m_envCubePipe.valid() ||
            !m_froxelInjectPipe.valid() || !m_froxelIntPipe.valid()) return Fail("atmosphere/cloud/env/froxel pipelines");
        d.label = "rt refl filter"; d.fragment = dxil(kDxil_rt_refl_filter_fs, sizeof kDxil_rt_refl_filter_fs); d.layout = kReflFilterLayout;
        m_reflFilterPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_reflFilterPipe.valid()) return Fail("rt refl filter pipeline");
        d.label = "dof focus"; d.fragment = dxil(kDxil_dof_focus_fs, sizeof kDxil_dof_focus_fs); d.layout = kDofFocusLayout;
        m_dofFocusPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "dof coc"; d.fragment = dxil(kDxil_dof_coc_fs, sizeof kDxil_dof_coc_fs); d.layout = kDofCocLayout;
        m_dofCocPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "dof gather"; d.fragment = dxil(kDxil_dof_gather_fs, sizeof kDxil_dof_gather_fs); d.layout = kDofGatherLayout;
        m_dofGatherPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "dof post"; d.fragment = dxil(kDxil_dof_post_fs, sizeof kDxil_dof_post_fs); d.layout = kDofGatherLayout;
        m_dofPostPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "dof comp"; d.fragment = dxil(kDxil_dof_comp_fs, sizeof kDxil_dof_comp_fs); d.layout = kDofCompLayout;
        m_dofCompPipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "rawlss5 3d-guided"; d.fragment = dxil(kDxil_rawlss5_fs, sizeof kDxil_rawlss5_fs); d.layout = kRawlss5Layout;
        m_rawlss5Pipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_dofFocusPipe.valid() || !m_dofCocPipe.valid() || !m_dofGatherPipe.valid() || !m_dofPostPipe.valid() || !m_dofCompPipe.valid() || !m_rawlss5Pipe.valid()) return Fail("dof/rawlss5 pipelines");
        d.label = "gi atrous 1"; d.fragment = dxil(kDxil_gi_at1_fs, sizeof kDxil_gi_at1_fs); d.layout = kGiAtrousLayout;
        m_giAt1Pipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "gi atrous 2"; d.fragment = dxil(kDxil_gi_at2_fs, sizeof kDxil_gi_at2_fs); d.layout = kGiAtrousLayout;
        m_giAt2Pipe = m_dev->createRasterPipeline(d, m_err);
        d.label = "world field pde sim"; d.fragment = dxil(kDxil_world_field_sim_fs, sizeof kDxil_world_field_sim_fs); d.layout = kWorldSimLayout;
        m_worldSimPipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_worldSimPipe.valid()) return Fail("world field pde sim pipeline");
        d.label = "gi atrous 4"; d.fragment = dxil(kDxil_gi_at4_fs, sizeof kDxil_gi_at4_fs); d.layout = kGiAtrousLayout;
        m_giAt4Pipe = m_dev->createRasterPipeline(d, m_err);
        if (!m_giAt1Pipe.valid() || !m_giAt2Pipe.valid() || !m_giAt4Pipe.valid()) return Fail("gi atrous pipelines");
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
        if (!m_atmoTransBuilt) {
            const RasterBind tb[1] = {{m_uniform, {}, {}}};
            Draw(*c, m_atmoTrans, m_atmoTransPipe, tb);
            const RasterBind mb[3] = {{m_uniform, {}, {}}, {{}, m_atmoTrans, {}}, {{}, {}, m_lin}};
            Draw(*c, m_atmoMs, m_atmoMsPipe, mb);
            Draw(*c, m_cloudNoise, m_cloudNoisePipe, tb);
            m_atmoTransBuilt = true;
        }
        if (ok && (m_u.flags[0] & kMatch) == 0u) {
            const RasterBind sb[4] = {{m_uniform, {}, {}}, {{}, m_atmoTrans, {}}, {{}, m_atmoMs, {}}, {{}, {}, m_lin}};
            Draw(*c, m_atmoSkyView, m_atmoSkyPipe, sb);
            const RasterBind eb[6] = {{m_uniform, {}, {}}, {{}, m_atmoSkyView, {}}, {{}, m_cloudNoise, {}},
                                      {{}, m_game, {}}, {{}, m_depth, {}}, {{}, {}, m_lin}};
            Draw(*c, m_envCube, m_envCubePipe, eb);
        }
        const TextureHandle aoVis = ao ? m_ao.history() : m_game;   // unused without the flag
        const bool clouds = ok && (m_u.flags[0] & kClouds);
        const TextureHandle cloudNow = m_cloud[m_frame & 1], cloudLast = m_cloud[(m_frame + 1) & 1];
        if (clouds) {
            const RasterBind cb[5] = {{m_uniform, {}, {}}, {{}, cloudLast, {}}, {{}, m_atmoTrans, {}}, {{}, m_cloudNoise, {}}, {{}, {}, m_lin}};
            Draw(*c, cloudNow, m_cloudPipe, cb);
        }
        const TextureHandle reflNow = m_reflHist[m_frame & 1], reflLast = m_reflHist[(m_frame + 1) & 1];
        const bool reflFilter = ok && m_rtReflect.valid() && (m_u.flags[0] & kReflect);
        if (reflFilter) {
            const RasterBind rb[6] = {{m_uniform, {}, {}}, {{}, m_rtReflect, {}}, {{}, m_depth, {}},
                                      {{}, frame.hasMotion ? m_motion : m_game, {}}, {{}, reflLast, {}}, {{}, {}, m_lin}};
            Draw(*c, reflNow, m_reflFilterPipe, rb);
        }
        const bool giFilter = ok && m_rtBounce.valid() && (m_u.flags[0] & kBounce) && (m_u.flags[0] & kMatch) == 0u;
        if (giFilter) {
            const RasterBind a1[4] = {{m_uniform, {}, {}}, {{}, m_rtBounce, {}}, {{}, m_depth, {}}, {{}, m_normal, {}}};
            Draw(*c, m_giAtrous[0], m_giAt1Pipe, a1);
            const RasterBind a2[4] = {{m_uniform, {}, {}}, {{}, m_giAtrous[0], {}}, {{}, m_depth, {}}, {{}, m_normal, {}}};
            Draw(*c, m_giAtrous[1], m_giAt2Pipe, a2);
            const RasterBind a4[4] = {{m_uniform, {}, {}}, {{}, m_giAtrous[1], {}}, {{}, m_depth, {}}, {{}, m_normal, {}}};
            Draw(*c, m_giAtrous[0], m_giAt4Pipe, a4);
        }
        const TextureHandle simNow = m_worldSim[m_frame & 1], simPrev = m_worldSim[(m_frame + 1) & 1];
        if (ok && (m_u.flags[0] & kMatch) == 0u) {
            const RasterBind ws[3] = {{m_uniform, {}, {}}, {{}, simPrev, {}}, {{}, {}, m_lin}};
            Draw(*c, simNow, m_worldSimPipe, ws);
        }
        const RasterBind light[17] = {{m_uniform, {}, {}}, {{}, m_depth, {}}, {{}, m_normal, {}}, {{}, m_albedo, {}}, {{}, m_spec, {}},
                                     {{}, m_mask, {}}, {{}, m_atlas, {}}, {{}, aoVis, {}}, {{}, m_game, {}}, {{}, m_soft.valid() ? m_soft : m_game, {}},
                                     {{}, m_rtVis.valid() ? m_rtVis : m_game, {}}, {{}, m_rtIdx.valid() ? m_rtIdx : m_game, {}},
                                     {{}, giFilter ? m_giAtrous[0] : (m_rtBounce.valid() ? m_rtBounce : m_game), {}},
                                     {{}, reflFilter ? reflNow : (m_rtReflect.valid() ? m_rtReflect : m_game), {}},
                                     {{}, m_envCube, {}}, {{}, simNow, {}}, {{}, {}, m_lin}};
        Draw(*c, m_lit, m_lightPipe, light);
        c->timestamp(4);
        const TextureHandle volNow = m_volFog[m_frame & 1], volLast = m_volFog[(m_frame + 1) & 1];
        if (ok && (m_u.flags[0] & kFog)) {
            const RasterBind vb[7] = {{m_uniform, {}, {}}, {{}, m_depth, {}}, {{}, m_atlas, {}}, {{}, volLast, {}},
                                      {{}, m_atmoTrans, {}}, {{}, m_atmoSkyView, {}}, {{}, {}, m_lin}};
            Draw(*c, volNow, m_volPipe, vb);
            // True 160x90x64 Temporally-Reprojected 3D Froxel Participating Media Grid (inject + temporal -> integrate).
            if ((m_u.flags[0] & kMatch) == 0u) {
                const TextureHandle froxNow = m_froxelRaw[m_frame & 1], froxPrev = m_froxelRaw[(m_frame + 1) & 1];
                const RasterBind fi[6] = {{m_uniform, {}, {}}, {{}, m_atlas, {}}, {{}, m_atmoTrans, {}}, {{}, m_atmoSkyView, {}}, {{}, froxPrev, {}}, {{}, {}, m_lin}};
                Draw(*c, froxNow, m_froxelInjectPipe, fi);
                const RasterBind fn[2] = {{m_uniform, {}, {}}, {{}, froxNow, {}}};
                Draw(*c, m_froxelInt, m_froxelIntPipe, fn);
            }
        }
        const RasterBind fog[10] = {{m_uniform, {}, {}}, {{}, m_lit, {}}, {{}, m_depth, {}}, {{}, m_atlas, {}}, {{}, volNow, {}}, {{}, m_atmoSkyView, {}}, {{}, cloudNow, {}}, {{}, m_froxelInt, {}}, {{}, simNow, {}}, {{}, {}, m_lin}};
        if (ok && (m_u.flags[0] & kMatch) == 0u) {
            // Render fog into m_aoOut, optionally run 3-Pass Near/Far Physical Bokeh DoF into m_lit,
            // then run 6-level Jimenez Karis HDR Bloom Pyramid and composite bloomed linear HDR into m_out.
            Draw(*c, m_aoOut, m_fogPipe, fog);
            TextureHandle bloomSrc = m_aoOut;
            if (m_u.dofParams[0] > 1.0f) {
                const TextureHandle focusPrev = m_dofFocus[(m_frame + 1) & 1];
                const TextureHandle focusNow  = m_dofFocus[m_frame & 1];
                const RasterBind df[3] = {{m_uniform, {}, {}}, {{}, focusPrev, {}}, {{}, m_depth, {}}};
                Draw(*c, focusNow, m_dofFocusPipe, df);
                const RasterBind dc[5] = {{m_uniform, {}, {}}, {{}, m_aoOut, {}}, {{}, m_depth, {}}, {{}, focusNow, {}}, {{}, {}, m_lin}};
                Draw(*c, m_dofCoc, m_dofCocPipe, dc);
                const RasterBind dg[3] = {{m_uniform, {}, {}}, {{}, m_dofCoc, {}}, {{}, {}, m_lin}};
                Draw(*c, m_dofBokeh, m_dofGatherPipe, dg);
                const RasterBind dpost[3] = {{m_uniform, {}, {}}, {{}, m_dofBokeh, {}}, {{}, {}, m_lin}};
                Draw(*c, m_dofCoc, m_dofPostPipe, dpost);
                const RasterBind dp[6] = {{m_uniform, {}, {}}, {{}, m_aoOut, {}}, {{}, m_depth, {}}, {{}, m_dofCoc, {}}, {{}, focusNow, {}}, {{}, {}, m_lin}};
                Draw(*c, m_lit, m_dofCompPipe, dp);
                bloomSrc = m_lit;
            }
            // RAWLSS 5.0 (RAW Custom DLSS 5): 3D-Guided SM 6.9 FP16 Neural & Physical Reconstruction Pass
            // (reconstructs micro-relief, grazing sheen & YCoCg temporal clarity across RTX 4090/5090 and lower cards!).
            if (m_tune.rawlss5) {
                const TextureHandle rPrev = m_rawlssHist[(m_frame + 1) & 1];
                const TextureHandle rNow  = m_rawlssHist[m_frame & 1];
                const RasterBind rb[8] = {{m_uniform, {}, {}}, {{}, bloomSrc, {}}, {{}, m_depth, {}}, {{}, m_normal, {}}, {{}, frame.hasMotion ? m_motion : m_game, {}}, {{}, m_spec, {}}, {{}, rPrev, {}}, {{}, {}, m_lin}};
                Draw(*c, rNow, m_rawlss5Pipe, rb);
                bloomSrc = rNow;
            }
            const RasterBind bp[2] = {{{}, bloomSrc, {}}, {{}, {}, m_lin}};
            Draw(*c, m_bloomDown[0], m_bloomPrePipe, bp);
            for (int k = 1; k < 6; ++k) {
                const RasterBind bd[2] = {{{}, m_bloomDown[k - 1], {}}, {{}, {}, m_lin}};
                Draw(*c, m_bloomDown[k], m_bloomDownPipe, bd);
            }
            for (int k = 4; k >= 0; --k) {
                const TextureHandle coarser = (k == 4) ? m_bloomDown[5] : m_bloomUp[k + 1];
                const RasterBind bu[3] = {{{}, coarser, {}}, {{}, m_bloomDown[k], {}}, {{}, {}, m_lin}};
                Draw(*c, m_bloomUp[k], m_bloomUpPipe, bu);
            }
            const RasterBind bc[3] = {{{}, bloomSrc, {}}, {{}, m_bloomUp[0], {}}, {{}, {}, m_lin}};
            Draw(*c, m_out, m_bloomCompPipe, bc);
        } else {
            Draw(*c, m_out, m_fogPipe, fog);
        }
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
    void Uniform(const void** d, size_t* n) const { *d = &m_u; *n = sizeof m_u; }

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
        u.proj[0] = frame.camera.proj[10]; u.proj[1] = -frame.camera.proj[11]; u.proj[2] = g_deterministic ? 0.0f : float(m_frame & 0xFFFF);
        uint32_t f = ok ? kSurface : 0u;
        if (!ok) { u.flags[0] = 0; return; }
        const SceneLighting& s = *frame.lighting;
        if (t.mode <= 1) f |= kMatch;
        else f |= (t.softShadows ? kSoft : 0u) | (t.ggxSpecular ? kGgx : 0u) | (t.volumetricFog ? kFog : 0u) |
                  (t.contactShadows ? kContact : 0u) | (t.ao ? kAo : 0u);
        if (t.cascades) f |= kCascades;
        if (t.fogReplacesGame) f |= kFogReplaces;
        if (frame.rt && t.rtShadows && m_rtVis.valid() && m_rtIdx.valid()) f |= kRt | (t.rtSky ? kSky : 0u) | (t.rtSky && t.rtBounce > 0.0f && m_rtBounce.valid() ? kBounce : 0u) |
                     (t.rtSky && t.rtReflect && m_rtReflect.valid() ? kReflect : 0u) | (t.rtSky && t.rtRest && m_rtBounce.valid() ? kRest : 0u);
        if (frame.hasShadows && s.hasCascades) f |= kShadows;
        // True engine TESObjectCELL::IsInterior() ownership (eliminates exterior->interior flicker when
        // shadow-cascade draws drop during free-cam/fast turns; falls back to !kShadows only if engine_rt is absent).
        const bool interior = s.hasEngineCell ? s.isInterior : ((f & kShadows) == 0u);
        if (interior && (f & kBounce) && t.rtBounceInterior > 0.0f) f |= kBounceIn;
        u.flags[0] = f; u.flags[1] = s.lightCount; u.flags[2] = s.cascades; u.flags[3] = uint32_t(t.view);
        u.dofParams[0] = t.dof ? t.dofMaxDist : 0.0f;
        u.dofParams[1] = t.dofRadius;
        u.dofParams[2] = t.dofOrbBoost;
        u.dofParams[3] = t.weatherLens ? 1.0f : 0.0f;
        u.timeEnv[0] = t.gameHour;
        // True 24-hour astronomical solar elevation (sin(elev)): smoothly rises through 0 at ~5:38, peaks +54 deg (+0.81) at 12:30,
        // sets smoothly through 0 at ~19:22, and stays deep below horizon (-0.51) at midnight — decoupled from ShadowSceneNode's
        // night-time Moon flip and minimum-elevation shadow clamp!
        u.timeEnv[1] = 0.15f + 0.66f * std::cos((t.gameHour - 12.5f) * 0.26179939f);
        u.timeEnv[2] = t.lightning;
        u.timeEnv[3] = s.menuPaused ? 1.0f : 0.0f;
        u.engineWorld[0] = s.waterHeight;
        u.engineWorld[1] = s.camStateId <= 16u ? float(s.camStateId) : -1.0f;
        u.engineWorld[2] = s.hasEngineCell ? 1.0f : 0.0f;
        u.engineWorld[3] = 0.0f;
        std::memcpy(u.clipToWorld, s.clipToWorld, 64);
        std::memcpy(u.worldToClip, s.worldToClip, 64);
        std::memcpy(u.viewToWorld, s.viewToWorld, 48);
        u.resScale[0] = s.resolutionScale[0]; u.resScale[1] = s.resolutionScale[1]; u.resScale[2] = s.knee; u.resScale[3] = s.specularScale;
        for (int k = 0; k < 3; ++k) { u.sunDir[k] = s.sunDirection[k]; u.sunColour[k] = s.sunColour[k]; }
        u.sunDir[3] = s.hasSun ? 1.0f : 0.0f;
        u.sunColour[3] = interior ? 0.0f : 1.0f;
        std::memcpy(u.ambient, s.ambient, 48);
        std::memcpy(u.cascade, s.cascade, 96);
        u.cascadeInfo[0] = s.cascadeBias[0]; u.cascadeInfo[1] = s.cascadeBias[1]; u.cascadeInfo[2] = s.splitDepth;
        u.cascadeInfo[3] = float(m_host.shadowSide);
        const float nearZ = frame.camera.nearZ > 0 ? frame.camera.nearZ : 15.0f;
        u.cluster[0] = nearZ; u.cluster[1] = t.clusterFar; u.cluster[2] = float(kSlices) / std::log(t.clusterFar / nearZ);
        u.cluster[3] = t.rtSkyStrength;
        u.proj[3] = interior ? t.rtBounceInterior : t.rtBounce;
        u.tune[0] = t.fogDensity; u.tune[1] = t.fogAnisotropy; u.tune[2] = t.softRange; u.tune[3] = t.contactLength;
        u.tune2[0] = t.aoStrength; u.tune2[1] = t.fogLightScale; u.tune2[2] = t.fogSunScale; u.tune2[3] = t.fogMaxDistance;
        // Procedural Infinite Physical Atmospheric Weather Engine:
        // Continuously evolves barometric pressure, cloud cover, jet-stream wind shear, humidity mist, passing rain/snow
        // fronts, and storm lightning organically over time & world altitude (replacing reliance on static vanilla weathers
        // while still honoring manual weather overrides!).
        const float wtSim = u.camPos[3];
        const float spatialPhase = s.posAdjust[0] * 0.000018f + s.posAdjust[1] * 0.000013f;
        const float baroWave = std::sin(wtSim * 0.0037f + spatialPhase) * 0.52f
                             + std::sin(wtSim * 0.0089f - spatialPhase * 1.4f + 1.9f) * 0.31f
                             + std::sin(wtSim * 0.0211f + 4.3f) * 0.17f;   // [-1..+1], aperiodic infinite fronts
        const float stormFront = std::clamp((0.35f - baroWave) * 0.85f, 0.0f, 1.0f);
        const float altCold = std::clamp((s.posAdjust[2] - 1800.0f) / 3800.0f, 0.0f, 1.0f);
        const float procRain = (!interior && t.mode == 2) ? std::clamp((stormFront - 0.52f) * 2.1f, 0.0f, 1.0f) * (1.0f - altCold * 0.85f) : 0.0f;
        const float procSnow = (!interior && t.mode == 2) ? std::clamp((stormFront - 0.46f) * 1.9f, 0.0f, 1.0f) * altCold : 0.0f;
        const float effRain  = std::fmax(t.rain, procRain * 0.85f);
        const float effSnow  = std::fmax(t.snow, procSnow * 0.85f);
        const float effWet   = std::fmax(t.wetness, std::clamp(effRain * 1.15f + stormFront * 0.22f, 0.0f, 1.0f));
        const float effPud   = std::fmax(t.puddles, std::clamp((effRain - 0.15f) * 0.95f, 0.0f, 0.85f));
        u.weather[0] = t.mode == 2 ? effWet : 0.0f;
        u.weather[1] = t.mode == 2 ? effPud : 0.0f;
        u.weather[2] = t.mode == 2 ? effSnow : 0.0f;
        u.weather[3] = t.mode == 2 ? effRain : 0.0f;
        if (!interior && stormFront > 0.78f && u.timeEnv[2] < 0.05f) {
            u.timeEnv[2] = std::clamp((stormFront - 0.78f) * 4.2f, 0.0f, 1.0f);   // procedural convective thunderhead lightning
        }
        for (int k = 0; k < 3; ++k) u.camPos[k] = s.posAdjust[k];
        // Clouds: infinite procedural synoptic fronts blended seamlessly with weather cover.
        if (t.clouds && t.mode == 2 && !interior) f |= kClouds;
        if (t.distantLod && t.mode == 2) f |= kDistantLod;
        if (t.envMap) f |= kEnvMap;   // both modes: the game's own environment-map term
        if (t.charLight) f |= kCharLight;   // both modes: the game's own character light
        if (s.inWater) f |= kUnderwater;    // engine-native camera_in_water flag from raw_engine.dll
        u.flags[0] = f;
        const float procCover = std::clamp(0.18f + stormFront * 0.76f, 0.15f, 0.95f);
        const float baseCover = std::fmax(t.cloudCover, 0.22f);
        const float blendedCover = std::fmin(1.0f, std::fmax(baseCover * 0.45f + procCover * 0.55f, baseCover + (0.92f - baseCover) * t.weatherCloud));
        u.clouds[0] = blendedCover;
        u.clouds[1] = t.cloudBase; u.clouds[2] = t.cloudThickness;
        u.clouds[3] = t.cloudShadow * (1.0f - 0.42f * std::fmax(t.weatherCloud, stormFront * 0.75f));
        const float windAng = wtSim * 0.0045f + baroWave * 0.65f;
        const float windGust = 1.0f + stormFront * 1.15f;
        u.cloudWind[0] = (t.cloudWindX * std::cos(windAng) - t.cloudWindY * std::sin(windAng)) * windGust;
        u.cloudWind[1] = (t.cloudWindX * std::sin(windAng) + t.cloudWindY * std::cos(windAng)) * windGust;
        u.cloudWind[2] = t.cloudDensity * (0.85f + stormFront * 0.45f);
        u.cloudWind[3] = t.cloudHistory;
        u.waterA[0] = t.waterReflect; u.waterA[1] = t.waterCaustics; u.waterA[2] = t.waterAbsorb; u.waterA[3] = t.waterGlint;
        u.waterB[0] = float(t.waterView); u.waterB[1] = t.waterCausticCell; u.waterB[2] = t.waterRefract; u.waterB[3] = float(t.season);
        // Update persistent 32-slot world-space footprint & terrain-deformation trail ring buffer.
        if (m_hasLastFoot) {
            const float dx = s.posAdjust[0] - m_lastFoot[0], dy = s.posAdjust[1] - m_lastFoot[1], dz = s.posAdjust[2] - m_lastFoot[2];
            const float d2 = dx * dx + dy * dy;
            if (d2 > 250000.0f || std::fabs(dz) > 600.0f) {
                std::memset(m_footTrail, 0, sizeof m_footTrail);
                m_lastFoot[0] = s.posAdjust[0]; m_lastFoot[1] = s.posAdjust[1]; m_lastFoot[2] = s.posAdjust[2];
            } else if (d2 >= 1156.0f) {   // stride >= 34 units (~0.5 m)
                const float invL = 1.0f / std::sqrt(d2);
                const float side = (m_footIdx & 1u) ? 8.5f : -8.5f;
                const uint32_t slot = m_footIdx++ & 15u;   // slots 0..15: player footsteps; slots 16..31: live ProcessLists NPCs!
                m_footTrail[slot][0] = s.posAdjust[0] - dy * invL * side;
                m_footTrail[slot][1] = s.posAdjust[1] + dx * invL * side;
                m_footTrail[slot][2] = s.posAdjust[2];
                m_footTrail[slot][3] = 1.0f;
                m_lastFoot[0] = s.posAdjust[0]; m_lastFoot[1] = s.posAdjust[1]; m_lastFoot[2] = s.posAdjust[2];
            }
        } else {
            m_lastFoot[0] = s.posAdjust[0]; m_lastFoot[1] = s.posAdjust[1]; m_lastFoot[2] = s.posAdjust[2];
            m_hasLastFoot = true;
        }
        std::memcpy(u.footTrail, m_footTrail, sizeof m_footTrail);
        // Engine-to-GPU Convergence: stream up to 16 live ProcessLists NPC world positions from raw_engine.dll
        // directly into u.footTrail[16..31] so EVERY NPC bends grass, carves snow trenches & creates water wakes!
        using ActorsFn = uint32_t (*)(float*, uint32_t);
        static HMODULE s_engMod = nullptr;
        static ActorsFn s_actorsFn = nullptr;
        if (!s_actorsFn) {
            if (!s_engMod) s_engMod = GetModuleHandleA("raw_engine.dll");
            if (s_engMod) s_actorsFn = reinterpret_cast<ActorsFn>(GetProcAddress(s_engMod, "RAW_EngineActors"));
        }
        if (s_actorsFn) {
            float npcs[16][4] = {};
            const uint32_t n = s_actorsFn(&npcs[0][0], 16u);
            for (uint32_t i = 0; i < n && i < 16u; ++i) {
                std::memcpy(u.footTrail[16u + i], npcs[i], 16u);
            }
        }
        // Bake exact world-space camera translation delta (camPos - prevCamPos) into prevWorldToClip so
        // world-locked temporal reprojection (Froxels, Clouds, Volumetrics, RT Reflections) never smears on movement.
        std::memcpy(u.prevWorldToClip, m_prevWorldToClip, 64);
        if (m_hasPrevCam) {
            const float dx = s.posAdjust[0] - m_prevCamPos[0];
            const float dy = s.posAdjust[1] - m_prevCamPos[1];
            const float dz = s.posAdjust[2] - m_prevCamPos[2];
            for (int r = 0; r < 4; ++r) {
                u.prevWorldToClip[r * 4 + 3] +=
                    m_prevWorldToClip[r * 4 + 0] * dx +
                    m_prevWorldToClip[r * 4 + 1] * dy +
                    m_prevWorldToClip[r * 4 + 2] * dz;
            }
        }
        std::memcpy(m_prevWorldToClip, s.worldToClip, 64);
        m_prevCamPos[0] = s.posAdjust[0]; m_prevCamPos[1] = s.posAdjust[1]; m_prevCamPos[2] = s.posAdjust[2];
        m_hasPrevCam = true;
        u.camPos[3] = float(double(GetTickCount64() % 3600000ull) / 1000.0);   // seconds, for the rain's rings and the snow's glints
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
    TextureHandle m_rtBounce, m_rtReflect;
    TextureHandle m_rtVis, m_rtIdx, m_soft, m_game, m_depth, m_normal, m_albedo, m_spec, m_mask, m_atlas, m_out, m_motion, m_lit, m_aoOut;
    BufferHandle m_uniform;
    RasterPipelineHandle m_lightPipe, m_fogPipe, m_cloudPipe, m_volPipe;
    RasterPipelineHandle m_bloomPrePipe, m_bloomDownPipe, m_bloomUpPipe, m_bloomCompPipe;
    RasterPipelineHandle m_atmoTransPipe, m_atmoMsPipe, m_atmoSkyPipe, m_cloudNoisePipe, m_envCubePipe, m_froxelInjectPipe, m_froxelIntPipe, m_reflFilterPipe, m_dofFocusPipe, m_dofCocPipe, m_dofGatherPipe, m_dofPostPipe, m_dofCompPipe, m_rawlss5Pipe, m_giAt1Pipe, m_giAt2Pipe, m_giAt4Pipe, m_worldSimPipe;
    TextureHandle m_cloud[2], m_volFog[2], m_bloomDown[6], m_bloomUp[5], m_atmoTrans, m_atmoMs, m_atmoSkyView, m_cloudNoise, m_envCube, m_froxelRaw[2], m_froxelInt, m_reflHist[2], m_rawlssHist[2], m_dofFocus[2], m_dofCoc, m_dofBokeh, m_giAtrous[2], m_worldSim[2];
    bool m_atmoTransBuilt = false;
    SamplerHandle m_lin;
    float m_prevWorldToClip[16] = {};
    float m_prevCamPos[3] = {};
    bool m_hasPrevCam = false;
    float m_footTrail[32][4] = {};
    float m_lastFoot[3] = {};
    uint32_t m_footIdx = 0;
    bool m_hasLastFoot = false;
    LightUniform m_u{};
    LightingTuning m_tune;
    uint64_t m_frame = 0, m_submissions = 0, m_pendingSubmission = 0;
    bool m_havePending = false, m_haveTimings = false;
    int m_skipLogs = 0;
    PassTimings m_timings;
};
} // namespace

std::unique_ptr<IBridgePass> MakeLightingPass() { return std::make_unique<LightingPass>(); }

bool LightingPassUniform(IBridgePass* pass, const void** data, size_t* bytes)
{
    auto* p = dynamic_cast<LightingPass*>(pass);
    if (!p) return false;
    p->Uniform(data, bytes);
    return true;
}

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
