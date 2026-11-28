// RawOverlayHost.cpp: Native DX12 Zero-Dependency <0.02ms GPU Compute Telemetry & Sparkline HUD Overlay.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawOverlayHost.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#if __has_include("raw_overlay_cs.h")
#define RAW_OVERLAY_HAS_EMBEDDED_DXIL 1
#include "raw_overlay_cs.h"
#else
#define RAW_OVERLAY_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawOverlayPass.hlsl byte-for-byte (256-byte aligned).
struct alignas(256) GpuOverlayCB {
    uint32_t panelRect[4];       // x = panelX, y = panelY, z = panelW, w = panelH
    uint32_t targetAndLayout[4]; // x = texW, y = texH, z = lineCount, w = glyphScale
    uint32_t sparkLayout[4];     // x = sparkCount, y = sparkStartY, z = sparkHeight, w = flags
    float    sparkRange[4];      // x = greenMaxMs, y = amberMaxMs, z = graphMaxMs, w = opacity
    uint32_t packedChars[512];   // 128 * uint4 = 8 lines x 64 chars (low8 = ASCII, bits8..15 = tag)
    float    sparkSamples[64];   // 16 * float4 = 64 frame-time samples (ms)
    float    lineColors[8][4];   // 8 * float4 = default per-line RGBA
    uint32_t _pad[16];           // 64 + 2048 + 256 + 128 + 64 = 2560 bytes exact
};
static_assert(sizeof(GpuOverlayCB) == 2560u, "GpuOverlayCB must be 2560 bytes");

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255ull) & ~255ull;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&out)));
}

DXGI_FORMAT ResolveUavFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:
        return fmt;
    }
}

void CopyUpperLine(char (&dst)[OverlayHudData::kMaxCols + 1u], const char* src)
{
    std::memset(dst, 0, sizeof(dst));
    if (!src) return;
    size_t i = 0u;
    for (; i < OverlayHudData::kMaxCols && src[i] != '\0'; ++i) {
        const unsigned char ch = static_cast<unsigned char>(src[i]);
        dst[i] = static_cast<char>(std::toupper(ch));
    }
    dst[i] = '\0';
}

void SanitizeUpperToken(char* dst, size_t dstCap, const char* src, const char* fallback)
{
    const char* s = (src && src[0] != '\0') ? src : fallback;
    size_t i = 0u;
    for (; i + 1u < dstCap && s[i] != '\0'; ++i) {
        dst[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    }
    dst[i] = '\0';
}

void ResolveLineColorRgba(uint8_t tag, float (&rgba)[4])
{
    switch (tag) {
    case kOverlayColorCyan:    rgba[0] = 0.46f; rgba[1] = 0.88f; rgba[2] = 1.00f; rgba[3] = 1.0f; break;
    case kOverlayColorGreen:   rgba[0] = 0.34f; rgba[1] = 0.94f; rgba[2] = 0.56f; rgba[3] = 1.0f; break;
    case kOverlayColorAmber:   rgba[0] = 1.00f; rgba[1] = 0.78f; rgba[2] = 0.28f; rgba[3] = 1.0f; break;
    case kOverlayColorCrimson: rgba[0] = 1.00f; rgba[1] = 0.36f; rgba[2] = 0.38f; rgba[3] = 1.0f; break;
    case kOverlayColorMuted:   rgba[0] = 0.70f; rgba[1] = 0.78f; rgba[2] = 0.88f; rgba[3] = 1.0f; break;
    default:                   rgba[0] = 0.94f; rgba[1] = 0.96f; rgba[2] = 0.99f; rgba[3] = 1.0f; break;
    }
}

} // namespace

void FormatEngineTelemetryLines(const OverlayTelemetrySnapshot& snap, OverlayHudData& outHud)
{
    char seasonBuf[24]{};
    char weatherBuf[24]{};
    char landmarkBuf[28]{};
    SanitizeUpperToken(seasonBuf, sizeof(seasonBuf), snap.seasonName, "AUTUMN");
    SanitizeUpperToken(weatherBuf, sizeof(weatherBuf), snap.weatherName, "CLEAR");
    SanitizeUpperToken(landmarkBuf, sizeof(landmarkBuf), snap.landmarkName, "TAMRIEL");

    const float safeFps     = (std::isfinite(snap.fps) && snap.fps >= 0.0f) ? snap.fps : 0.0f;
    const float safeFrameMs = (std::isfinite(snap.frameMs) && snap.frameMs >= 0.0f) ? snap.frameMs : 0.0f;
    const float safeFireMs  = (std::isfinite(snap.fireMs) && snap.fireMs >= 0.0f) ? snap.fireMs : 0.0f;
    const float safeWaterMs = (std::isfinite(snap.waterMs) && snap.waterMs >= 0.0f) ? snap.waterMs : 0.0f;
    const float safeGiMs    = (std::isfinite(snap.giMs) && snap.giMs >= 0.0f) ? snap.giMs : 0.0f;
    const float safeXpbdMs  = (std::isfinite(snap.xpbdMs) && snap.xpbdMs >= 0.0f) ? snap.xpbdMs : 0.0f;
    const float safeOvlMs   = (std::isfinite(snap.overlayMs) && snap.overlayMs >= 0.0f) ? snap.overlayMs : 0.0f;
    const float safeTempC   = std::isfinite(snap.tempC) ? snap.tempC : 0.0f;
    const float safeWind    = (std::isfinite(snap.windMps) && snap.windMps >= 0.0f) ? snap.windMps : 0.0f;
    const float safeSnow    = (std::isfinite(snap.snowPackM) && snap.snowPackM >= 0.0f) ? snap.snowPackM : 0.0f;
    const float safeLru     = std::clamp(std::isfinite(snap.lruHitRatePct) ? snap.lruHitRatePct : 0.0f, 0.0f, 100.0f);

    char tmp[128]{};

    // Line 0: Header banner
    CopyUpperLine(outHud.lines[0], "RAW ENGINE TELEMETRY | DX12 COMPUTE HUD");
    outHud.lineColorTag[0] = kOverlayColorCyan;

    // Line 1: FPS & frame pacing
    std::snprintf(tmp, sizeof(tmp), "FPS %6.1f | FRAME %5.2f MS | HUD %5.3f MS",
                  static_cast<double>(safeFps), static_cast<double>(safeFrameMs), static_cast<double>(safeOvlMs));
    CopyUpperLine(outHud.lines[1], tmp);
    if (safeFrameMs <= outHud.greenMaxMs) {
        outHud.lineColorTag[1] = kOverlayColorGreen;
    } else if (safeFrameMs <= outHud.amberMaxMs) {
        outHud.lineColorTag[1] = kOverlayColorAmber;
    } else {
        outHud.lineColorTag[1] = kOverlayColorCrimson;
    }

    // Line 2: GPU solver timings (FIRE | WATER | GI | XPBD)
    std::snprintf(tmp, sizeof(tmp), "GPU FIRE %4.2f | WATER %4.2f | GI %4.2f | XPBD %4.2f MS",
                  static_cast<double>(safeFireMs), static_cast<double>(safeWaterMs),
                  static_cast<double>(safeGiMs), static_cast<double>(safeXpbdMs));
    CopyUpperLine(outHud.lines[2], tmp);
    outHud.lineColorTag[2] = kOverlayColorDefault;

    // Line 3: Solver toggles
    std::snprintf(tmp, sizeof(tmp), "SOLVERS GI:%s | FLUID:%s | XPBD:%s",
                  snap.giEnabled ? "ON" : "OFF",
                  snap.fluidEnabled ? "ON" : "OFF",
                  snap.xpbdEnabled ? "ON" : "OFF");
    CopyUpperLine(outHud.lines[3], tmp);
    outHud.lineColorTag[3] = kOverlayColorMuted;

    // Line 4: Season, weather, temperature, wind, snowpack
    std::snprintf(tmp, sizeof(tmp), "ENV %s %s | %+5.1fC | WND %4.1fM/S | SNW %4.2fM",
                  seasonBuf, weatherBuf, static_cast<double>(safeTempC),
                  static_cast<double>(safeWind), static_cast<double>(safeSnow));
    CopyUpperLine(outHud.lines[4], tmp);
    outHud.lineColorTag[4] = kOverlayColorDefault;

    // Line 5: Active Tamriel cell, landmark, meshlets, LRU hit rate
    std::snprintf(tmp, sizeof(tmp), "CELL (%d,%d) %s | C:%u M:%u | LRU %4.1f%%",
                  snap.cellX, snap.cellY, landmarkBuf,
                  snap.activeCells, snap.activeMeshlets, static_cast<double>(safeLru));
    CopyUpperLine(outHud.lines[5], tmp);
    outHud.lineColorTag[5] = kOverlayColorDefault;

    // Line 6: Sparkline legend
    std::snprintf(tmp, sizeof(tmp), "FRAME SPARKLINE (64F) | <8.3MS GRN | <16.7MS AMB");
    CopyUpperLine(outHud.lines[6], tmp);
    outHud.lineColorTag[6] = kOverlayColorMuted;

    outHud.lineCount = 7u;
    outHud.lines[7][0] = '\0';
    outHud.lineColorTag[7] = kOverlayColorDefault;

    if (snap.historyFrameMs && snap.historyCount > 0u) {
        const uint32_t copyN = std::min(snap.historyCount, OverlayHudData::kMaxSparkCount);
        const uint32_t srcOff = snap.historyCount - copyN;
        outHud.sparkCount = copyN;
        for (uint32_t i = 0u; i < copyN; ++i) {
            const float s = snap.historyFrameMs[srcOff + i];
            outHud.sparkSamplesMs[i] = (std::isfinite(s) && s > 0.0f) ? s : 0.0f;
        }
        for (uint32_t i = copyN; i < OverlayHudData::kMaxSparkCount; ++i) {
            outHud.sparkSamplesMs[i] = 0.0f;
        }
    }
}

OverlayPanelRect ComputeOverlayPanelRect(const OverlayHudData& hud, uint32_t targetWidth, uint32_t targetHeight)
{
    OverlayPanelRect r{};
    if (!hud.enabled || targetWidth == 0u || targetHeight == 0u) {
        return r;
    }

    const uint32_t scale = std::clamp(hud.glyphScale, 1u, 2u);
    const uint32_t lines = std::min(hud.lineCount, OverlayHudData::kMaxLines);
    const uint32_t spark = std::min(hud.sparkCount, OverlayHudData::kMaxSparkCount);

    uint32_t maxChars = 0u;
    for (uint32_t li = 0u; li < lines; ++li) {
        const size_t len = std::strlen(hud.lines[li]);
        maxChars = std::max(maxChars, static_cast<uint32_t>(std::min<size_t>(len, OverlayHudData::kMaxCols)));
    }
    const uint32_t displayCols = std::clamp(maxChars, 44u, OverlayHudData::kMaxCols);

    const uint32_t padX   = 10u * scale;
    const uint32_t padY   = 9u * scale;
    const uint32_t cellW  = 6u * scale;
    const uint32_t cellH  = 10u * scale;
    const uint32_t sparkH = (spark > 0u) ? (38u * scale) : 0u;
    const uint32_t sparkGap = (spark > 0u && lines > 0u) ? (5u * scale) : 0u;

    const uint32_t desiredW = padX * 2u + displayCols * cellW;
    const uint32_t desiredH = padY * 2u + lines * cellH + sparkGap + sparkH;

    if (hud.originX >= targetWidth || hud.originY >= targetHeight) {
        return r;
    }
    r.x = hud.originX;
    r.y = hud.originY;
    r.w = std::min(desiredW, targetWidth - hud.originX);
    r.h = std::min(desiredH, targetHeight - hud.originY);
    return r;
}

RawOverlayHost::~RawOverlayHost()
{
    Shutdown();
}

void RawOverlayHost::Shutdown()
{
    m_uiLib.Shutdown();
    m_debugDrawList.Clear();
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_pso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev = nullptr;
    m_ready = false;
    m_frameIdx = 0u;
    m_lastOverlayMs = 0.0f;
    m_hasValidTiming = false;
}

bool RawOverlayHost::Init(ID3D12Device* dev, std::string* outErr, D3D12_SHADER_BYTECODE customShader)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawOverlayHost: null ID3D12Device";
        return false;
    }

    D3D12_SHADER_BYTECODE cs = customShader;
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0) {
#if RAW_OVERLAY_HAS_EMBEDDED_DXIL
        cs = {kDxil_raw_overlay, sizeof kDxil_raw_overlay};
#else
        if (outErr) *outErr = "RawOverlayHost: no embedded raw_overlay_cs.h and no customShader provided";
        return false;
#endif
    }

    m_dev = dev;

    // 1) Shader-visible descriptor heap (kRingCount dynamic UAV slots)
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kRingCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawOverlayHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2) Minimal root signature: rp[0] = CBV b0, rp[1] = UAV descriptor table u0
    {
        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0, 0};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &uavRange};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawOverlayHost: CreateRootSignature failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = cs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawOverlayHost: CreateComputePipelineState failed";
        return false;
    }

    // 3) 4-frame persistently mapped upload CBV ring
    const uint64_t cbBytes = static_cast<uint64_t>(kRingCount) * sizeof(GpuOverlayCB);
    if (!CreateBuffer(dev, cbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawOverlayHost: CBV upload ring allocation failed";
        return false;
    }

    D3D12_RANGE rr{0, 0};
    void* ptr = nullptr;
    if (FAILED(m_cbBuf->Map(0, &rr, &ptr))) {
        if (outErr) *outErr = "RawOverlayHost: failed to map CBV ring";
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(ptr);
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes));

    // 4) 4-frame GPU timestamp query heap + readback buffer (2 queries/frame)
    {
        constexpr uint32_t kTotalQueries = kRingCount * 2u;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTotalQueries;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap))) ||
            !CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                          D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback)) {
            m_tsHeap.Reset();
            m_tsReadback.Reset();
        }
    }

    // Initialize embedded RawUiLibHost for Debug-Layer Control & Legend HUD (optional if DXIL absent)
    std::string uiErr;
    if (m_uiLib.Init(dev, &uiErr) && m_tsFreqHz > 0u) {
        m_uiLib.SetTimestampFrequency(m_tsFreqHz);
    }

    m_ready = true;
    return true;
}

void RawOverlayHost::RecordUiDrawList(ID3D12GraphicsCommandList* cmd,
                                      ID3D12Resource* targetTex,
                                      D3D12_RESOURCE_STATES currentState,
                                      uint32_t width,
                                      uint32_t height,
                                      const raw::ui::UiDrawList& drawList,
                                      bool srgbOutput)
{
    if (!m_uiLib.Ready()) return;
    m_uiLib.RecordDrawList(cmd, targetTex, currentState, width, height, drawList, srgbOutput);
}

void RawOverlayHost::RecordDebugLayerHud(ID3D12GraphicsCommandList* cmd,
                                         ID3D12Resource* targetTex,
                                         D3D12_RESOURCE_STATES currentState,
                                         uint32_t width,
                                         uint32_t height,
                                         const raw::ui::DebugLayerHudSnapshot& snap,
                                         bool srgbOutput)
{
    if (!m_uiLib.Ready()) return;
    raw::ui::FormatDebugLayerHud(snap, m_debugDrawList);

    // Wave 66: Attach Pipeline Health Matrix & Vector Compass sidecar whenever
    // the debug drawer is open or any Lighting/Water Debug View is active.
    if ((snap.drawerOpen || snap.activeDebugView != 0u || snap.activeWaterDebugView != 0u) &&
        (snap.hudEnabled || snap.drawerOpen)) {
        const uint32_t s = std::clamp(snap.scale, 1u, 2u);
        const float sf = static_cast<float>(s);
        const float mainW = (snap.drawerOpen ? 344.0f : 268.0f) * sf;
        const float sideW = 312.0f * sf;
        const float sidePad = 8.0f * sf;
        const float sideX = static_cast<float>(snap.originX) + mainW + 8.0f * sf;
        const float sideY = static_cast<float>(snap.originY);

        if (sideX + sideW <= static_cast<float>(width) + 2.0f) {
            using raw::ui::PipelineHealthState;
            using raw::ui::PipelinePassStatus;
            using raw::ui::VectorCompassData;

            const auto safeMs = [](float v) { return (std::isfinite(v) && v >= 0.0f) ? v : 0.0f; };
            const auto healthFor = [](float ms, float warnMs, float failMs, bool enabled) {
                if (!enabled) return PipelineHealthState::Off;
                if (ms >= failMs) return PipelineHealthState::Fail;
                if (ms >= warnMs) return PipelineHealthState::Warn;
                return PipelineHealthState::Pass;
            };

            const float lightMs = safeMs(snap.lightingMs);
            const float waterMs = safeMs(snap.waterMs);
            const float fireMs  = safeMs(snap.fireMs);
            const float giMs    = safeMs(snap.giMs);
            const float postMs  = safeMs(snap.postMs);
            const float uiMs    = safeMs(snap.uiMs);
            const bool mvOk = (!snap.motionTelemetry) ||
                              (snap.motionTelemetry->mvSignValid && snap.motionTelemetry->mvReprojRmse <= 0.085f);

            const PipelinePassStatus defaultPasses[8] = {
                {"GBuffer",     lightMs * 0.25f, healthFor(lightMs * 0.25f, 0.80f, 1.50f, true)},
                {"RT Shadows",  lightMs * 0.75f, healthFor(lightMs * 0.75f, 1.20f, 2.20f, snap.rtActive)},
                {"Water",       waterMs,         healthFor(waterMs,         0.60f, 1.20f, snap.fluidActive)},
                {"Flora/Grass", giMs * 0.50f,    healthFor(giMs * 0.50f,    0.50f, 1.00f, snap.grassWindActive)},
                {"Fire/Fluid",  fireMs,          healthFor(fireMs,          0.60f, 1.20f, snap.fluidActive)},
                {"Motion Vec",  uiMs,            mvOk ? PipelineHealthState::Pass : PipelineHealthState::Fail},
                {"Tonemap",     postMs,          healthFor(postMs,          0.40f, 0.90f, true)},
                {"GameComp",    0.02f,           PipelineHealthState::Pass},
            };
            const PipelinePassStatus* passes = (snap.pipelinePasses && snap.pipelinePassCount > 0u)
                                                   ? snap.pipelinePasses : defaultPasses;
            const uint32_t passCount = (snap.pipelinePasses && snap.pipelinePassCount > 0u)
                                           ? snap.pipelinePassCount : 8u;

            VectorCompassData defaultCompass{};
            if (snap.motionTelemetry) {
                defaultCompass.yawDeg         = std::fmod(std::max(0.0f, snap.motionTelemetry->yawRateDegPerSec * 4.0f), 360.0f);
                defaultCompass.pitchDeg       = 0.0f;
                defaultCompass.mvX            = snap.motionTelemetry->yawRateDegPerSec * 0.35f;
                defaultCompass.mvY            = snap.motionTelemetry->cameraSpeedUps * 0.02f;
                defaultCompass.mvDivPx        = snap.motionTelemetry->mvReprojRmse * 18.0f;
                defaultCompass.camSweepActive = snap.motionTelemetry->camSweepActive;
            }
            const VectorCompassData& compass = snap.vectorCompass ? *snap.vectorCompass : defaultCompass;

            const uint32_t rows = (std::min(passCount, 12u) + 1u) / 2u;
            const float matH = static_cast<float>(rows) * 18.0f * sf +
                               static_cast<float>(rows > 0u ? rows - 1u : 0u) * 3.0f * sf;
            const float sideH = sidePad * 2.0f + 16.0f * sf + matH + 8.0f * sf + 68.0f * sf;
            const float innerW = sideW - sidePad * 2.0f;
            const float sx = sideX + sidePad;
            float sy = sideY + sidePad;

            m_debugDrawList.AddPanel(sideX, sideY, sideW, sideH, 6.0f * sf,
                                     raw::ui::tokens::kGround, raw::ui::tokens::kBorderStrong,
                                     compass.camSweepActive ? raw::ui::tokens::kAccentEmerald
                                                            : raw::ui::tokens::kAccentPass, 1.0f);
            m_debugDrawList.AddSectionHeader(sx, sy, innerW, "Pipeline Health",
                                             raw::ui::tokens::kAccentPass, s, true, true);
            sy += 16.0f * sf;
            m_debugDrawList.AddPipelineHealthMatrix(sx, sy, innerW, passes, passCount, 2u, s);
            sy += matH + 8.0f * sf;
            m_debugDrawList.AddVectorCompass(sx, sy, innerW, compass, s);
        }
    }

    if (m_debugDrawList.Empty()) return;
    m_uiLib.RecordDrawList(cmd, targetTex, currentState, width, height, m_debugDrawList, srgbOutput);
}

bool RawOverlayHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    const D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0, &rr, &ptr))) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ptr) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const D3D12_RANGE wr{0, 0};
    m_tsReadback->Unmap(0, &wr);
    if (t1 < t0) return false;
    const double ms = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz);
    if (!std::isfinite(ms) || ms < 0.0 || ms > 5000.0) return false;
    outMs = static_cast<float>(ms);
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawOverlayHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawOverlayHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

void RawOverlayHost::RecordOverlay(ID3D12GraphicsCommandList* cmd,
                                   ID3D12Resource* targetTex,
                                   D3D12_RESOURCE_STATES currentState,
                                   uint32_t width,
                                   uint32_t height,
                                   const OverlayHudData& hud)
{
    // Strict zero-touch guarantee when disabled or invalid
    if (!hud.enabled || !m_ready || !cmd || !targetTex || !m_cbMapped || width == 0u || height == 0u) {
        return;
    }

    const D3D12_RESOURCE_DESC texDesc = targetTex->GetDesc();
    const uint32_t safeW = std::min(width, static_cast<uint32_t>(texDesc.Width));
    const uint32_t safeH = std::min(height, texDesc.Height);
    if (safeW == 0u || safeH == 0u) {
        return;
    }

    const OverlayPanelRect rect = ComputeOverlayPanelRect(hud, safeW, safeH);
    if (rect.w == 0u || rect.h == 0u) {
        return;
    }

    // Harvest 3-frames-old retired timestamp slot (non-blocking)
    if (m_frameIdx >= (kRingCount - 1u)) {
        const uint32_t retired = (m_frameIdx - (kRingCount - 1u)) % kRingCount;
        float ms = 0.0f;
        if (HarvestTimestampPair(retired * 2u, ms)) {
            m_lastOverlayMs = ms;
            m_hasValidTiming = true;
        }
    }

    const uint32_t scale      = std::clamp(hud.glyphScale, 1u, 2u);
    const uint32_t lineCount  = std::min(hud.lineCount, OverlayHudData::kMaxLines);
    const uint32_t sparkCount = std::min(hud.sparkCount, OverlayHudData::kMaxSparkCount);
    const uint32_t padY       = 9u * scale;
    const uint32_t cellH      = 10u * scale;
    const uint32_t sparkGap   = (sparkCount > 0u && lineCount > 0u) ? (5u * scale) : 0u;
    const uint32_t sparkStartY = padY + lineCount * cellH + sparkGap;
    const uint32_t sparkAvailH = (rect.h > sparkStartY + padY) ? (rect.h - sparkStartY - padY) : 0u;
    const uint32_t sparkH      = (sparkCount > 0u) ? std::min(38u * scale, sparkAvailH) : 0u;

    GpuOverlayCB cb{};
    cb.panelRect[0] = rect.x;
    cb.panelRect[1] = rect.y;
    cb.panelRect[2] = rect.w;
    cb.panelRect[3] = rect.h;

    cb.targetAndLayout[0] = safeW;
    cb.targetAndLayout[1] = safeH;
    cb.targetAndLayout[2] = lineCount;
    cb.targetAndLayout[3] = scale;

    cb.sparkLayout[0] = (sparkH >= 8u) ? sparkCount : 0u;
    cb.sparkLayout[1] = sparkStartY;
    cb.sparkLayout[2] = sparkH;
    cb.sparkLayout[3] = hud.srgbOutput ? 1u : 0u;

    cb.sparkRange[0] = std::max(hud.greenMaxMs, 0.1f);
    cb.sparkRange[1] = std::max(hud.amberMaxMs, cb.sparkRange[0] + 0.1f);
    cb.sparkRange[2] = std::max(hud.graphMaxMs, cb.sparkRange[1] + 0.5f);
    cb.sparkRange[3] = std::clamp(hud.opacity, 0.0f, 1.0f);

    for (uint32_t li = 0u; li < lineCount; ++li) {
        ResolveLineColorRgba(hud.lineColorTag[li], cb.lineColors[li]);
        const char* s = hud.lines[li];
        const uint32_t tagBits = static_cast<uint32_t>(hud.lineColorTag[li]) << 8u;
        for (uint32_t ci = 0u; ci < OverlayHudData::kMaxCols; ++ci) {
            const unsigned char ch = static_cast<unsigned char>(s[ci]);
            if (ch == '\0') break;
            cb.packedChars[li * OverlayHudData::kMaxCols + ci] = static_cast<uint32_t>(ch) | tagBits;
        }
    }
    for (uint32_t li = lineCount; li < OverlayHudData::kMaxLines; ++li) {
        ResolveLineColorRgba(kOverlayColorDefault, cb.lineColors[li]);
    }
    for (uint32_t si = 0u; si < sparkCount; ++si) {
        const float v = hud.sparkSamplesMs[si];
        cb.sparkSamples[si] = (std::isfinite(v) && v > 0.0f) ? v : 0.0f;
    }

    const uint32_t ring = m_frameIdx % kRingCount;
    const size_t byteOff = static_cast<size_t>(ring) * sizeof(GpuOverlayCB);
    std::memcpy(m_cbMapped + byteOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_cbBuf->GetGPUVirtualAddress() + byteOff;

    // Create transient UAV descriptor in this frame's ring slot
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = ResolveUavFormat(texDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0u;
    uavDesc.Texture2D.PlaneSlice = 0u;
    m_dev->CreateUnorderedAccessView(targetTex, nullptr, &uavDesc, CpuAt(ring));

    // Transition target -> UAV if needed
    if (currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = targetTex;
        b.Transition.StateBefore = currentState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    }

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(ring));

    // Dispatch strictly over the HUD card bounding box (8x8 threads/group)
    const uint32_t gx = (rect.w + 7u) / 8u;
    const uint32_t gy = (rect.h + 7u) / 8u;
    cmd->Dispatch(gx, gy, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ring * 2u) * sizeof(uint64_t));
    }

    // Restore caller's resource state cleanly
    if (currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = targetTex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter = currentState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    } else {
        D3D12_RESOURCE_BARRIER uavB{};
        uavB.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavB.UAV.pResource = targetTex;
        cmd->ResourceBarrier(1, &uavB);
    }

    ++m_frameIdx;
}

} // namespace raw::bridge
