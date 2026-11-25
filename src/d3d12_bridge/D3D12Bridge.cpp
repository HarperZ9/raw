// D3D12Bridge setup: device on the game's adapter, shared textures, shared
// fence, descriptor heaps and timestamp queries. Original work, MIT.
#include "BridgeImpl.h"
#include "RawProceduralLib.h"
#include "StreamlineHost.h"
#include "RawTextureBridge.h"
#include "../materials/RawAssetForge.h"
#include "DepthCopy_CS.h"   // g_DepthCopyCS (fxc, cs_5_0)

namespace raw::bridge
{

static void NoLog(const char*, ...) {}

bool D3D12Bridge::Impl::CreateDevice12(ID3D11Device* dev)
{
    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter11;
    DXGI_ADAPTER_DESC ad{};
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxgiDev))) ||
        FAILED(dxgiDev->GetAdapter(&adapter11)) || FAILED(adapter11->GetDesc(&ad))) {
        log("[D3D12Bridge] cannot read the D3D11 adapter");
        return false;
    }

    // Loaded at runtime: the proxy must not import d3d12.dll or dxgi.dll.
    d3d12Dll = LoadLibraryW(L"d3d12.dll");
    HMODULE dxgiDll = LoadLibraryW(L"dxgi.dll");
    if (!d3d12Dll || !dxgiDll) { log("[D3D12Bridge] d3d12.dll or dxgi.dll missing"); return false; }
    auto createDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
        GetProcAddress(d3d12Dll, "D3D12CreateDevice"));
    auto getDebug = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(
        GetProcAddress(d3d12Dll, "D3D12GetDebugInterface"));
    using PFN_Factory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto createFactory = reinterpret_cast<PFN_Factory2>(GetProcAddress(dxgiDll, "CreateDXGIFactory2"));
    if (!createDevice || !createFactory) { log("[D3D12Bridge] entry points missing"); return false; }

    if (cfg.debugLayer && getDebug) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(getDebug(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
    }

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter12;
    if (FAILED(createFactory(0, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByLuid(ad.AdapterLuid, IID_PPV_ARGS(&adapter12)))) {
        log("[D3D12Bridge] no DXGI adapter with LUID %08X:%08X",
            ad.AdapterLuid.HighPart, ad.AdapterLuid.LowPart);
        return false;
    }
    HRESULT hr = createDevice(adapter12.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12));
    if (FAILED(hr)) { log("[D3D12Bridge] D3D12CreateDevice failed 0x%08X", (unsigned)hr); return false; }

    char name[128]{};
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
    log("[D3D12Bridge] D3D12 device on '%s' (LUID %08X:%08X, same adapter as D3D11)",
        name, ad.AdapterLuid.HighPart, ad.AdapterLuid.LowPart);

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return false;
    // One fence, created on D3D12, opened on D3D11.
    HANDLE h = nullptr;
    if (FAILED(dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12))) ||
        FAILED(dev12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &h))) {
        log("[D3D12Bridge] shared fence creation failed");
        return false;
    }
    hr = dev5->OpenSharedFence(h, IID_PPV_ARGS(&fence11));
    CloseHandle(h);
    if (FAILED(hr)) { log("[D3D12Bridge] OpenSharedFence failed 0x%08X", (unsigned)hr); return false; }
    cpuEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return cpuEvent != nullptr;
}

bool D3D12Bridge::Impl::CreateShared(SharedTex& t, DXGI_FORMAT fmt,
                                     D3D12_RESOURCE_FLAGS flags, const char* name, UINT texW, UINT texH)
{
    sharedFormat[&t - shared] = fmt;
    if (attached) return CreateAttached(t, fmt, (flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0, name, texW, texH);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = texW ? texW : cfg.width;
    rd.Height = texH ? texH : cfg.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = flags;
    HRESULT hr = dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t.res12));
    if (FAILED(hr)) { log("[D3D12Bridge] %s: CreateCommittedResource 0x%08X", name, (unsigned)hr); return false; }

    HANDLE h = nullptr;
    hr = dev12->CreateSharedHandle(t.res12.Get(), nullptr, GENERIC_ALL, nullptr, &h);
    if (FAILED(hr)) { log("[D3D12Bridge] %s: CreateSharedHandle 0x%08X", name, (unsigned)hr); return false; }
    hr = dev5->OpenSharedResource1(h, IID_PPV_ARGS(&t.tex11));
    CloseHandle(h);
    if (FAILED(hr)) { log("[D3D12Bridge] %s: OpenSharedResource1 0x%08X", name, (unsigned)hr); return false; }
    return true;
}

// Attached to raw::tx: the layer's device and queue, and a fence of our own for the pass to retire
// its frames by. Nothing is shared across devices.
bool D3D12Bridge::Impl::AttachLayer()
{
    dev12 = cfg.layer.device;
    queue = cfg.layer.queue;
    if (FAILED(dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence12)))) return false;
    cpuEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    log("[D3D12Bridge] attached to raw::tx: the layer's D3D12 device and queue, no fence crossing");
    return cpuEvent != nullptr;
}

// A texture of the layer, made through its D3D11 face; the pass reads its D3D12 resource.
bool D3D12Bridge::Impl::CreateAttached(SharedTex& t, DXGI_FORMAT fmt, bool uav, const char* name, UINT w, UINT h)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w ? w : cfg.width; d.Height = h ? h : cfg.height; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
    HRESULT hr = dev11->CreateTexture2D(&d, nullptr, &t.tex11);
    if (FAILED(hr)) { log("[D3D12Bridge] %s: layer CreateTexture2D 0x%08X", name, (unsigned)hr); return false; }
    t.res12 = cfg.layer.native(t.tex11.Get());
    if (!t.res12) { log("[D3D12Bridge] %s: no D3D12 resource behind the layer's texture", name); return false; }
    return true;
}

bool D3D12Bridge::Impl::HandToHost(const std::vector<ID3D11Resource*>& extra)
{
    std::vector<ID3D11Resource*> list(extra);
    for (auto& t : shared) if (t.tex11) list.push_back(t.tex11.Get());
    return SUCCEEDED(cfg.layer.prepare(ctx11.Get(), list.data(), UINT(list.size()), D3D12_RESOURCE_STATE_COMMON));
}

// The ray-tracing scene on the layer's device; its two outputs join the pass's inputs.
bool D3D12Bridge::Impl::CreateRt()
{
    rt = std::make_unique<RtScene>();
    std::string err;
    if (!rt->Create(dev12.Get(), cfg.width, cfg.height, log, err)) {
        log("[RT] off: %s", err.c_str());
        rt.reset();
        return true;   // the lighting pass runs without it
    }
    rt->Tune(rtTune);
    shared[static_cast<UINT>(Shared::RtVisibility)].res12 = rt->Visibility();
    shared[static_cast<UINT>(Shared::RtLights)].res12 = rt->Lights();
    sharedFormat[static_cast<UINT>(Shared::RtVisibility)] = DXGI_FORMAT_R8G8B8A8_UNORM;
    sharedFormat[static_cast<UINT>(Shared::RtLights)] = DXGI_FORMAT_R8G8B8A8_UNORM;
    shared[static_cast<UINT>(Shared::RtReflect)].res12 = rt->Reflection();
    sharedFormat[static_cast<UINT>(Shared::RtReflect)] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (rt->Bounce()) {
        shared[static_cast<UINT>(Shared::RtBounce)].res12 = rt->Bounce();
        sharedFormat[static_cast<UINT>(Shared::RtBounce)] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    return true;
}

bool D3D12Bridge::Impl::CreatePass()
{
    HostResources h;
    h.device = dev12.Get();
    h.queue = queue.Get();
    h.fence = fence12.Get();
    for (UINT k = 0; k < kSharedCount; ++k) {
        h.tex[k] = shared[k].res12.Get();
        h.format[k] = sharedFormat[k];
    }
    h.width = cfg.width;
    h.height = cfg.height;
    h.retireTimeoutMs = cfg.passRetireMs;
    h.shadowSide = cfg.shadowMapSize;
    h.log = log;
    pass = cfg.lightingPass ? MakeLightingPass()
         : !cfg.enginePass ? MakeDepthFogPass() : cfg.realtimeAo ? MakeRealtimeAoPass() : MakeEnginePass();
    if (!pass) { log("[D3D12Bridge] this build carries no engine pass (RAW_ENGINE=OFF)"); return false; }
    return pass->Create(h);
}

bool D3D12Bridge::Impl::CreateQueries()
{
    D3D11_QUERY_DESC dq{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC tq{D3D11_QUERY_TIMESTAMP, 0};
    for (auto& s : d11) {
        if (FAILED(dev11->CreateQuery(&dq, &s.disjoint))) return false;
        for (auto& t : s.ts)
            if (FAILED(dev11->CreateQuery(&tq, &t))) return false;
    }
    return true;
}

// D3D11 opened these D3D12 textures only when they were render-target
// capable; a no-flag texture and a depth-stencil texture both returned
// E_INVALIDARG from OpenSharedResource1 (RTX 4090, 2026-11-17). So depth
// crosses as R32_FLOAT written by a D3D11 compute pass. Simultaneous access
// lets both queues use the textures with the shared fence as the only ordering.
static constexpr D3D12_RESOURCE_FLAGS kSharedColour =
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
static constexpr D3D12_RESOURCE_FLAGS kSharedDepth =
    kSharedColour | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

bool D3D12Bridge::Impl::CreateDepthCopy()
{
    HRESULT hr = dev11->CreateComputeShader(g_DepthCopyCS, sizeof(g_DepthCopyCS), nullptr, &depthCS);
    if (FAILED(hr)) { log("[D3D12Bridge] depth copy shader 0x%08X", (unsigned)hr); return false; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = kSharedDepthFormat;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    hr = dev11->CreateUnorderedAccessView(depthIn.tex11.Get(), &ud, &depthUAV);
    if (FAILED(hr)) { log("[D3D12Bridge] depth UAV 0x%08X", (unsigned)hr); return false; }
    return true;
}

D3D12Bridge::D3D12Bridge() : m(std::make_unique<Impl>()) {}
D3D12Bridge::~D3D12Bridge() { Shutdown(); }

bool D3D12Bridge::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                             const BridgeConfig& cfg, LogFn log)
{
    Shutdown();
    m->cfg = cfg;
    m->log = log ? log : NoLog;
    if (!dev || !ctx || !cfg.width || !cfg.height) return false;
    // Always work on the device's own immediate context. A process can hold
    // several D3D11 devices (SKSE plugins create their own); a context from
    // another device makes Signal fail with E_INVALIDARG.
    ComPtr<ID3D11DeviceContext> own;
    dev->GetImmediateContext(&own);
    if (own.Get() != ctx)
        m->log("[D3D12Bridge] the context passed in belongs to another device; using the device's own");
    m->dev11 = dev;
    m->ctx11 = own;
    m->attached = cfg.layer.device && cfg.layer.queue && cfg.layer.native && cfg.layer.prepare;
    if (!m->attached) {
        HRESULT hrDev = dev->QueryInterface(IID_PPV_ARGS(&m->dev5));
        HRESULT hrCtx = own->QueryInterface(IID_PPV_ARGS(&m->ctx4));
        if (FAILED(hrDev) || FAILED(hrCtx)) {
            m->log("[D3D12Bridge] needs ID3D11Device5 (0x%08X) and ID3D11DeviceContext4 (0x%08X)",
                   (unsigned)hrDev, (unsigned)hrCtx);
            return false;
        }
    }
    const bool probe = cfg.probeStages;
    auto stage = [&](const char* where) { if (probe) ProbeFenceSupport(dev, ctx, m->log, where); return true; };
    const char* failed = "the device or the shared textures";   // named in the failure line
    auto step = [&](bool r, const char* what) { if (!r) failed = what; return r; };
    bool ok = stage("bridge: before D3D12 device") && (m->attached ? m->AttachLayer() : m->CreateDevice12(dev)) && stage("bridge: after D3D12 device")
        && m->CreateShared(m->colorIn, cfg.colorFormat, kSharedColour, "colour-in")
        && m->CreateShared(m->depthIn, kSharedDepthFormat, kSharedDepth, "depth-in")
        && m->CreateShared(m->colorOut, cfg.colorFormat,
                           cfg.lightingPass ? (kSharedColour | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) : kSharedColour,
                           "colour-out")
        && (cfg.normalFormat == DXGI_FORMAT_UNKNOWN
            || m->CreateShared(m->normalIn, cfg.normalFormat, kSharedColour, "normal-in"))
        && (cfg.motionFormat == DXGI_FORMAT_UNKNOWN
            || m->CreateShared(m->motionIn, cfg.motionFormat, kSharedColour, "motion-in"))
        && (!cfg.lightingPass || m->CreateLightingShared())
        && stage("bridge: after shared textures")
        && step(m->CreateDepthCopy(), "depth copy") && step(m->CreateQueries(), "queries") && step(m->CreateProbe(), "probe")
        && stage("bridge: after views and queries")
        && step(!m->attached || !cfg.lightingPass || m->CreateRt(), "ray tracing")
        && step(m->CreatePass(), "engine pass");
    if (ok && m->dev12) {
        // Log live DX12 / Agility SDK 1.620.1 hardware feature tiers (SM, DXR, Work Graphs, Enhanced Barriers, Mesh Shaders, Sampler Feedback).
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_9};
        if (FAILED(m->dev12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) {
            sm.HighestShaderModel = D3D_SHADER_MODEL_6_6;
            m->dev12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS21 o21{};
        m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
        m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7));
        m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &o12, sizeof(o12));
        m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &o21, sizeof(o21));
        D3D12_FEATURE_DATA_D3D12_OPTIONS o0{}; m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof o0);
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{}; m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof o1);
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{}; m->dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &o4, sizeof o4);
        m->log("[D3D12Bridge] HW tiers: SM=0x%X, DXR=%d, Mesh=%d, SamplerFB=%d, EnhancedBarriers=%s, WorkGraphs=%d, FP16=%s, BindlessTier=%d, Wave=%u",
               unsigned(sm.HighestShaderModel), int(o5.RaytracingTier), int(o7.MeshShaderTier),
               int(o7.SamplerFeedbackTier), o12.EnhancedBarriersSupported ? "YES" : "no", int(o21.WorkGraphsTier),
               o4.Native16BitShaderOpsSupported ? "YES" : "no", int(o0.ResourceBindingTier), o1.WaveLaneCountMin);
        StreamlineHost::Get().Initialize(m->dev12.Get(), m->dev12->GetAdapterLuid(), m->log);
        // Initialize RAW-LUMEN Bindless Heap, RAW-MESH AS+MS Host, Split-Submit Async Compute & ExecuteIndirect Batcher:
        const bool bindOk = m->bindless.Init(m->dev12.Get());
        std::string meshErr;
        const bool meshInitOk = m->meshHost.Init(m->dev12.Get(), &meshErr);
        const bool meshOk = m->meshHost.Ready();
        const bool terrainOk = m->meshHost.TerrainReady();
        const bool asyncOk = m->asyncCompute.Init(m->dev12.Get());
        const bool indOk = m->indirect.Init(m->dev12.Get(), meshOk);
        const bool fluidOk = m->InitFluidSim();
        m->log("[D3D12Bridge] Metal subsystems: RAW-LUMEN Bindless65K=%s, RAW-MESH(as_6_6/ms_6_6)=%s, RawTerrainWorld=%s (%s)%s, AsyncCompute=%s, ExecuteIndirect=%s (IndirectMesh=%s), FluidSim=%s",
               bindOk ? "READY" : "off", meshOk ? "READY" : "off",
               terrainOk ? "READY" : "off",
               m->meshHost.TerrainUsesMeshShaders() ? "AS+MS+PS" : "VS+PS fallback",
               (!meshInitOk && !meshErr.empty()) ? (" (" + meshErr + ")").c_str() : "",
               asyncOk ? "READY" : "off", indOk ? "READY" : "off",
               m->indirect.SupportsIndirectMesh() ? "YES" : "no",
               fluidOk ? "READY" : "off");
        // Monumental Remaster: Canonical 32-material physically-measured registry + 6-biome frontier WorldShowcase + 9 zero-external-asset hero GPU AS+MS assets:
        {
            const auto& matReg = raw::materials::DefaultMaterialRegistry();
            m->materialGpuRecords = matReg.BuildGpuBuffer();
            raw::worldgen::StreamConfig wcfg{};
            wcfg.vramBudgetBytes = 128ull * 1024ull * 1024ull;
            wcfg.lod0RadiusChunks = 2;
            wcfg.lod1RadiusChunks = 3;
            wcfg.lod2RadiusChunks = 4;
            wcfg.lod3RadiusChunks = 6;
            wcfg.maxLoadsPerFrame = 4u;
            wcfg.maxMeshletTris = 126u; // Native RawTerrainWorld.hlsl 64v/126t meshlets
            wcfg.showcaseMode = false;  // auto-activates near/beyond Tamriel borders
            wcfg.frontierActivationMarginU = 24576.0f;
            wcfg.terrain.border.enabled = true;
            wcfg.terrain.border.halfExtentX = 148000.0f;
            wcfg.terrain.border.halfExtentY = 132000.0f;
            wcfg.terrain.border.blendApronU = 16384.0f;
            wcfg.terrain.border.tamrielEdgeBaseZ = -1800.0f;
            wcfg.useCustomBorderBox = true;
            wcfg.borderMinX = -148000.0f;
            wcfg.borderMaxX =  148000.0f;
            wcfg.borderMinY = -118000.0f;
            wcfg.borderMaxY =  132000.0f;
            wcfg.asyncStreaming = true;
            const bool worldOk = m->worldShowcase.Init(wcfg);
            m->log("[D3D12Bridge] Physical Materials & Frontier WorldGen: %zu measured 64B GPU materials (8 families), WorldShowcase=%s (128MB cap, async=ON, Tamriel box [-148k..148k]x[-118k..132k], margin 24.5k)",
                   m->materialGpuRecords.size(), worldOk ? "READY" : "off");
        }
        if (meshOk) {
            RawMeshAsset builtIn[9]{};
            const bool genOk[9] = {
                GenerateNordicSpruceTree(1337u, 220.0f, 36.0f, 0.25f, &builtIn[0]),
                GenerateNordicBoulder(2026u, 84.0f, 0.28f, 10u, &builtIn[1]),
                GenerateAlpineGrassClump(4096u, 12u, 42.0f, &builtIn[2]),
                GenerateOceanTileMesh(16u, 1200.0f, &builtIn[3]),
                GenerateGlacialIceArch(7001u, 320.0f, 180.0f, 44.0f, &builtIn[4]),
                GenerateNordicSnowDrift(7002u, 260.0f, 140.0f, 38.0f, &builtIn[5]),
                GenerateBasaltColumnCluster(7003u, 7u, 22.0f, 150.0f, &builtIn[6]),
                GenerateVolcanicVent(7004u, 95.0f, 65.0f, &builtIn[7]),
                GenerateCrystalMonolith(7005u, 32.0f, 160.0f, 5u, &builtIn[8]),
            };
            uint32_t totalMeshlets = 0;
            for (int k = 0; k < 9; ++k) {
                if (!genOk[k]) continue;
                RawMeshGpuAsset gpu{};
                if (m->meshHost.UploadAsset(builtIn[k], &gpu)) {
                    totalMeshlets += gpu.meshletCount;
                    m->proceduralMeshes.push_back(std::move(gpu));
                }
            }
            uint32_t texCount = 0;
            uint64_t texBytes = 0;
            const ProceduralTexKind kinds[10] = {
                ProceduralTexKind::BarkFurrow, ProceduralTexKind::PineNeedles,
                ProceduralTexKind::GraniteStrata, ProceduralTexKind::SnowSastrugi,
                ProceduralTexKind::CellularFoam, ProceduralTexKind::GlacialIceFracture,
                ProceduralTexKind::VolcanicBasaltMagma, ProceduralTexKind::HexagonalBasalt,
                ProceduralTexKind::CrystalQuartzFacet, ProceduralTexKind::MossyGraniteCobble,
            };
            for (ProceduralTexKind tk : kinds) {
                RawTextureAsset tx{};
                if (SynthesizeProceduralPbrTexture(tk, 128u, 128u, 777u, &tx)) {
                    ++texCount;
                    texBytes += tx.rgba8.size();
                }
            }
            m->log("[D3D12Bridge] RAW Procedural Library (0 external assets): %zu GPU AS+MS assets (%u meshlets), %u synthesized PBR textures (%llu KB)",
                   m->proceduralMeshes.size(), totalMeshlets, texCount,
                   static_cast<unsigned long long>(texBytes / 1024ull));
        }
        char exeBuf[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, exeBuf, MAX_PATH) > 0) {
            std::string gDir(exeBuf);
            const size_t sl = gDir.find_last_of("\\/");
            if (sl != std::string::npos) gDir.resize(sl + 1);
            m->telemetryOutDir = gDir + "raw_engine_out";
            std::string assetLog;
            const auto summary = ScanRawAssetsFolder(gDir, &assetLog);
            if (!assetLog.empty() && (summary.meshFiles || summary.textureFiles)) m->log("%s", assetLog.c_str());
            std::string ovLog;
            if (m->overrides.ScanDirectory(gDir + "raw_assets\\overrides", meshOk ? &m->meshHost : nullptr, &ovLog) > 0 && !ovLog.empty()) {
                m->log("%s", ovLog.c_str());
            }
            std::string customErr;
            const std::string customDir = gDir + "raw_assets\\overrides";
            const uint32_t scannedCustom = m->customOverrides.ScanMirrorDirectory(customDir, &customErr);
            m->customOverrides.LoadManifestFile(customDir + "\\manifest.json", nullptr);
            if (scannedCustom > 0u) {
                m->log("[D3D12Bridge] Non-NIF/Non-DDS Custom Asset Override Registry: %u handcrafted overrides active", scannedCustom);
            }
        }
    }
    if (!ok) { m->log("[D3D12Bridge] initialisation failed at %s; bridge stays off", failed); Shutdown(); return false; }
    if (!m->attached && !m->SelfTest()) {
        m->log("[D3D12Bridge] fence self-test failed; bridge stays off");
        Shutdown();
        return false;
    }
    m->ready = true;
    m->log("[D3D12Bridge] ready: %ux%u colour fmt %d, depth as R32F, normals fmt %d, pass '%s'",
           cfg.width, cfg.height, (int)cfg.colorFormat, (int)cfg.normalFormat, m->pass->Name());
    return true;
}

bool D3D12Bridge::Impl::InitFluidSim()
{
    ShutdownFluidSim();
    if (!dev12) return false;
    std::string fluidErr;
    if (!fluidSim.Init(dev12.Get(), &fluidErr)) {
        log("[FluidSim] off: %s", fluidErr.c_str());
        return false;
    }
    for (UINT i = 0; i < kRing; ++i) {
        if (FAILED(dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&fluidAlloc[i])))) {
            ShutdownFluidSim();
            return false;
        }
    }
    if (FAILED(dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, fluidAlloc[0].Get(), nullptr, IID_PPV_ARGS(&fluidList))) ||
        FAILED(fluidList->Close()) ||
        FAILED(dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fluidFence)))) {
        ShutdownFluidSim();
        return false;
    }
    fluidEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fluidEvent) {
        ShutdownFluidSim();
        return false;
    }
    std::string compErr;
    const bool compOk = fluidComposite.Init(dev12.Get(), &compErr);
    fluidReady = true;
    log("[FluidSim] live GPU 64^3 NS Fire + 256^2 SWE Water ready (FireComposite=%s%s)",
        compOk ? "READY" : "off", (!compOk && !compErr.empty()) ? (": " + compErr).c_str() : "");
    return true;
}

void D3D12Bridge::Impl::ShutdownFluidSim()
{
    fluidComposite.Shutdown();
    if (fluidFence && fluidEvent && fluidFenceVal > 0) {
        if (fluidFence->GetCompletedValue() < fluidFenceVal) {
            fluidFence->SetEventOnCompletion(fluidFenceVal, fluidEvent);
            WaitForSingleObject(fluidEvent, 3000);
        }
    }
    fluidList.Reset();
    for (UINT i = 0; i < kRing; ++i) {
        fluidAlloc[i].Reset();
        fluidSlotVal[i] = 0;
    }
    fluidFence.Reset();
    if (fluidEvent) {
        CloseHandle(fluidEvent);
        fluidEvent = nullptr;
    }
    fluidFenceVal = 0;
    fluidSlot = 0;
    fluidStepFrames = 0;
    fluidLastMs = 0;
    fluidReady = false;
}

void D3D12Bridge::Shutdown()
{
    if (!m) return;
    if (m->ready) {
        m->CpuWait(m->lastD12, m->cfg.cpuWaitTimeout);
        if (m->ctx11) m->ctx11->Flush();
    }
    m->asyncCompute.Shutdown();
    m->ShutdownFluidSim();
    m->worldShowcase.WaitForAsyncIdle();
    m->worldShowcase.Reset();
    m->worldShowcase.SetAsyncEnabled(false);
    m->streamedTerrainGpu.clear();
    m->meshHost.Shutdown();
    m->pass.reset();   // before the device and the textures it borrows
    m->rt.reset();
    LogFn log = m->log;
    if (m->cpuEvent) CloseHandle(m->cpuEvent);
    m = std::make_unique<Impl>();
    m->log = log;
}

bool D3D12Bridge::Ready() const { return m->ready; }
const BridgeConfig& D3D12Bridge::Config() const { return m->cfg; }
bool D3D12Bridge::Matches(UINT w, UINT h, DXGI_FORMAT c) const
{
    return m->ready && m->cfg.width == w && m->cfg.height == h && m->cfg.colorFormat == c;
}

} // namespace raw::bridge
