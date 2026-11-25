#pragma once
// RawUiLib.h: Clean-Room Zero-ImGui DX12 GPU-Native UI Library & Debug-Layer Legend System.
// Follows the RAW Design Canon (RawDesignTokens.h): calm dark ink ground (#101318), warm bone
// typography (#ede9e1, 15.4:1 contrast), CVD-safe verdict accents (#619ff9 / #eeb153 / #f47154),
// proportional-kerned 7x9 printable ASCII (32..126 including lowercase) with outline legibility,
// strict scissor clip rects, and single-pass <0.04ms compute execution.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace raw::ui
{

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct UiPixelRect {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
};

// Packed RGBA8 color (low byte = R, high byte = A; sRGB channels converted to linear in shader).
constexpr uint32_t PackRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) noexcept
{
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8u) |
           (static_cast<uint32_t>(b) << 16u) |
           (static_cast<uint32_t>(a) << 24u);
}

// Unified RAW Design Canon dark palette tokens (exact OKLCH->sRGB values from RawDesignTokens.h)
namespace tokens {
inline constexpr uint32_t kGround          = PackRgba8(0x10, 0x13, 0x18, 232); // #101318
inline constexpr uint32_t kSurface         = PackRgba8(0x16, 0x1A, 0x22, 240); // #161a22
inline constexpr uint32_t kSurfaceAlt      = PackRgba8(0x1B, 0x20, 0x2A, 244); // #1b202a
inline constexpr uint32_t kBorder          = PackRgba8(0x2A, 0x32, 0x40, 220); // #2a3240
inline constexpr uint32_t kBorderStrong    = PackRgba8(0x42, 0x4E, 0x62, 235); // #424e62
inline constexpr uint32_t kInkPrimary      = PackRgba8(0xED, 0xE9, 0xE1, 255); // #ede9e1 (15.4:1)
inline constexpr uint32_t kInkSecondary    = PackRgba8(0xBE, 0xB8, 0xAD, 255); // #beb8ad
inline constexpr uint32_t kInkMuted        = PackRgba8(0x92, 0x92, 0x92, 255); // #929292 (5.99:1)
inline constexpr uint32_t kTextOutline     = PackRgba8(0x06, 0x08, 0x0C, 210); // Crisp 1px halo
inline constexpr uint32_t kAccentPass      = PackRgba8(0x61, 0x9F, 0xF9, 255); // #619ff9 (6.93:1)
inline constexpr uint32_t kAccentLead      = PackRgba8(0xEE, 0xB1, 0x53, 255); // #eeb153 (9.82:1)
inline constexpr uint32_t kAccentFail      = PackRgba8(0xF4, 0x71, 0x54, 255); // #f47154 (6.51:1)
inline constexpr uint32_t kAccentNull      = PackRgba8(0x92, 0x92, 0x92, 255); // #929292
inline constexpr uint32_t kAccentEmerald   = PackRgba8(0x3E, 0xD2, 0x84, 255); // Crisp vegetation/OK
inline constexpr uint32_t kPillOnBg        = PackRgba8(0x1B, 0x2C, 0x45, 240);
inline constexpr uint32_t kPillOffBg       = PackRgba8(0x20, 0x24, 0x2D, 235);
inline constexpr uint32_t kSliderTrack     = PackRgba8(0x28, 0x30, 0x3D, 245);
inline constexpr uint32_t kGraphWell       = PackRgba8(0x0B, 0x0E, 0x13, 238);
} // namespace tokens

enum class UiCmdKind : uint32_t {
    Panel     = 0u,
    Rect      = 1u,
    Pill      = 2u,
    Toggle    = 3u,
    Slider    = 4u,
    Sparkline = 5u,
    Text      = 6u,
    Swatch    = 7u,
    Disc      = 8u,
};

struct UiBudgetSegment {
    const char* label     = "";
    float       valueMs   = 0.0f;
    uint32_t    colorRgba = tokens::kAccentPass;
};

// Exact 80-byte GPU command structure mirrored byte-for-byte in RawUiLibPass.hlsl.
struct GpuUiCmd {
    float    rect[4]     = {};
    float    clipRect[4] = {};
    uint32_t colorFill   = 0u;
    uint32_t colorBorder = 0u;
    uint32_t colorAccent = 0u;
    uint32_t kindAndMeta = 0u;
    float    params[4]   = {};
    float    extra[4]    = {};
};
static_assert(sizeof(GpuUiCmd) == 80u, "GpuUiCmd must be 80 bytes matching RawUiLibPass.hlsl");

// Pure CPU proportional glyph metrics matching RawUiLibPass.hlsl kFont7x9 (ASCII 32..126).
uint32_t GetGlyphAdvancePx(char ch, uint32_t scale = 1u) noexcept;
uint32_t MeasureTextWidthPx(const char* text, uint32_t scale = 1u) noexcept;

// High-level CPU UI draw-list builder emitting crisp SDF panels, toggles, sliders, pills,
// sparklines, debug-layer legend swatches, and proportional outline text.
class UiDrawList {
public:
    static constexpr uint32_t kMaxCommands = 256u;
    static constexpr uint32_t kMaxGlyphs   = 4096u;
    static constexpr uint32_t kMaxSamples  = 512u;

    void Clear();

    void PushClipRect(float x, float y, float w, float h);
    void PopClipRect();

    void AddPanel(float x, float y, float w, float h,
                  float cornerRadius = 6.0f,
                  uint32_t fillRgba = tokens::kGround,
                  uint32_t borderRgba = tokens::kBorder,
                  uint32_t topAccentRgba = 0u,
                  float borderW = 1.0f);

    void AddRect(float x, float y, float w, float h, uint32_t fillRgba);

    void AddDivider(float x, float y, float w, uint32_t colorRgba = tokens::kBorder);

    void AddText(float x, float y, const char* text,
                 uint32_t colorRgba = tokens::kInkPrimary,
                 uint32_t scale = 1u,
                 uint32_t outlineRgba = tokens::kTextOutline);

    void AddSectionHeader(float x, float y, float w, const char* title,
                          uint32_t accentRgba = tokens::kAccentPass,
                          uint32_t scale = 1u,
                          bool collapsible = false,
                          bool expanded = true);

    void AddTabBar(float x, float y, float w,
                   const char* const* labels, uint32_t tabCount, uint32_t activeIndex,
                   uint32_t accentRgba = tokens::kAccentPass,
                   uint32_t scale = 1u);

    void AddBudgetBar(float x, float y, float w, float h,
                      const UiBudgetSegment* segments, uint32_t segmentCount,
                      float budgetTargetMs = 16.667f, float maxScaleMs = 20.0f,
                      uint32_t scale = 1u);

    void AddVerdictDot(float x, float y, float diameter,
                       uint32_t fillRgba,
                       uint32_t borderRgba = tokens::kBorderStrong);

    void AddEngineVarRow(float x, float y, float rowWidth,
                         const char* key, const char* valueText,
                         uint32_t verdictRgba = tokens::kAccentPass,
                         uint32_t scale = 1u);

    void AddEngineVarFloat(float x, float y, float rowWidth,
                           const char* key, float value, const char* unit = "",
                           uint32_t verdictRgba = tokens::kAccentPass,
                           uint32_t scale = 1u);

    void AddEngineVarBool(float x, float y, float rowWidth,
                          const char* key, bool value,
                          uint32_t scale = 1u);

    void AddPill(float x, float y, const char* label,
                 uint32_t bgRgba = tokens::kPillOnBg,
                 uint32_t textRgba = tokens::kInkPrimary,
                 uint32_t borderRgba = tokens::kBorderStrong,
                 uint32_t dotRgba = 0u,
                 uint32_t scale = 1u);

    void AddToggle(float x, float y, float rowWidth, const char* label, bool enabled,
                   uint32_t onAccentRgba = tokens::kAccentPass,
                   uint32_t scale = 1u);

    void AddSlider(float x, float y, float rowWidth, const char* label,
                   float value01, const char* valueText = nullptr,
                   uint32_t fillRgba = tokens::kAccentPass,
                   uint32_t scale = 1u);

    void AddSparkline(float x, float y, float w, float h,
                      const float* samples, uint32_t count,
                      float greenMax = 8.333f, float amberMax = 16.667f, float graphMax = 25.0f);

    void AddLegendSwatch(float x, float y, uint32_t swatchRgba, const char* label,
                         const char* badgeText = nullptr, uint32_t scale = 1u);

    UiPixelRect ComputeDirtyRect(uint32_t targetWidth, uint32_t targetHeight) const;

    const std::vector<GpuUiCmd>& Commands() const noexcept { return m_cmds; }
    const std::vector<uint32_t>& Glyphs()   const noexcept { return m_glyphs; }
    const std::vector<float>&    Samples()  const noexcept { return m_samples; }
    bool                         Empty()    const noexcept { return m_cmds.empty(); }

private:
    UiRect CurrentClip() const noexcept;
    void   PushCommand(GpuUiCmd cmd);

    std::vector<GpuUiCmd> m_cmds;
    std::vector<uint32_t> m_glyphs;
    std::vector<float>    m_samples;
    std::vector<UiRect>   m_clipStack;
};

struct DebugLegendSwatch {
    uint32_t    rgba  = 0u;
    const char* label = "";
    const char* code  = "";
};

struct DebugViewDescriptor {
    uint32_t                       viewId     = 0u;
    const char*                    shortName  = "Final";
    const char*                    title      = "Final Lit Scene";
    const char*                    purpose    = "";
    uint32_t                       accentRgba = tokens::kAccentPass;
    std::vector<DebugLegendSwatch> legend;
};

struct DebugLayerHudSnapshot {
    uint32_t     activeDebugView = 0u;
    bool         drawerOpen      = true;
    bool         hudEnabled      = true;
    bool         isInterior      = false;
    float        fps             = 0.0f;
    float        frameMs         = 0.0f;
    float        lightingMs      = 0.0f;
    float        waterMs         = 0.0f;
    float        fireMs          = 0.0f;
    float        giMs            = 0.0f;
    float        cloudsMs        = 0.0f;
    float        postMs          = 0.0f;
    float        uiMs            = 0.0f;
    bool         rtActive        = true;
    bool         fluidActive     = true;
    bool         snowDeformActive = true;
    bool         grassWindActive  = true;
    uint32_t     originX         = 16u;
    uint32_t     originY         = 16u;
    uint32_t     scale           = 1u;
    const float* historyFrameMs  = nullptr;
    uint32_t     historyCount    = 0u;
    uint32_t     activeWaterDebugView = 0u; // 0=Off, 1..7=Water Debug Views (W1..W7)
    uint32_t     activeTabIndex       = 0u; // 0=Layers, 1=Perf, 2=Vars
};

// Returns full metadata & color legend swatches for ALL 37 Lighting DebugView IDs (0..36).
DebugViewDescriptor GetDebugViewDescriptor(uint32_t debugViewId);

// Returns full metadata & color legend swatches for Water DebugView IDs (0 + 1..7, incl. W7:FlowDiag).
DebugViewDescriptor GetWaterDebugViewDescriptor(uint32_t waterDebugViewId);

// Populates a complete, crisp, high-contrast Debug-Layer Control & Legend Overlay into outList.
void FormatDebugLayerHud(const DebugLayerHudSnapshot& snap, UiDrawList& outList);

// Native DX12 GPU Compute Host executing any UiDrawList in a single <0.04ms compute pass.
class RawUiLibHost {
public:
    static constexpr uint32_t kRingCount = 4u;

    RawUiLibHost() = default;
    ~RawUiLibHost();

    RawUiLibHost(const RawUiLibHost&) = delete;
    RawUiLibHost& operator=(const RawUiLibHost&) = delete;

    bool Init(ID3D12Device* dev, std::string* outErr = nullptr, D3D12_SHADER_BYTECODE customShader = {});
    bool Ready() const noexcept { return m_ready; }

    // Records single cs_uilib_draw dispatch strictly over drawList.ComputeDirtyRect(width, height).
    // If drawList is empty, emits ZERO GPU commands and leaves targetTex bit-exact untouched.
    void RecordDrawList(ID3D12GraphicsCommandList* cmd,
                        ID3D12Resource* targetTex,
                        D3D12_RESOURCE_STATES currentState,
                        uint32_t width,
                        uint32_t height,
                        const UiDrawList& drawList,
                        bool srgbOutput = false);

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastUiMs() const noexcept { return m_lastUiMs; }
    bool  HasValidTiming() const noexcept { return m_hasValidTiming; }

    void Shutdown();

private:
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev   = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_pso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cmdBuf;
    uint8_t*                                     m_cmdMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_glyphBuf;
    uint8_t*                                     m_glyphMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_sampleBuf;
    uint8_t*                                     m_sampleMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz       = 0u;
    uint32_t                                     m_frameIdx       = 0u;
    float                                        m_lastUiMs       = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::ui
