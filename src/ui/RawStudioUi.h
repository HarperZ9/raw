#pragma once
// RawStudioUi.h: Architectural Design-Canon In-Game Studio UI (DESIGN-RULES.md + colour-tokens.json).
// Replaces legacy GDI text-list overlay with an anti-aliased vector canvas supporting:
//   - Dark (#101318 / #ede9e1) and Light (#f9f5ed / #1b1e24) themes with WCAG AAA contrast
//   - CVD-safe verdict tokens paired with geometric shape glyphs
//   - Full Mouse (click, hover, drag sliders, color wheel click), Keyboard, and XInput Controller navigation
//   - 10-domain Studio sidebar covering RT, Runtime Geometry Uplift, 3D Snow Sim, Skeletal IK/Physics, World AI/Combat
// Original work, MIT.

#include "RawDesignTokens.h"
#include "RawInputEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw::ui {

// Live runtime simulation & engine-upgrade tuning shared between Studio UI, DX12 bridge, and raw_engine.dll (zero SKSE).
struct WorldSimTuning {
    // Runtime GPU geometry uplift (Wave 32 paired Z-prepass + color-pass lockstep, zero DEPTH_EQUAL holes)
    int   geomSubdivLevel   = 1;      // 0=Off (Stock), 1=2x Curved PN-Tri (4x tris), 2=Adaptive 4x->16x
    float geomCurvature     = 0.62f;  // Vlachos cubic Bezier normal projection strength (0..1.25)
    float geomDisplaceScale = 0.00f;  // Micro-displacement amplitude (0..2.0; 0 preserves exact collision manifold)
    bool  geomWireframe     = false;  // Live meshlet/subdivision wireframe inspection

    // 3D Snowpack Tuning (shader-side normal/albedo snow accumulation; never lifts collision vertices)
    bool  snowVolumetric    = false;  // Disabled on vertex lift to prevent ground/collision detachment
    float snowAccumRate     = 1.00f;  // Storm buildup rate multiplier (0..3.0)
    float snowMaxDepthU     = 28.0f;  // Max volumetric snowpack buildup thickness in world units (4..48)
    float snowAvalancheAng  = 0.42f;  // Slope cosine threshold for snow shedding/cornices (0.20..0.75)

    // Skeletal IK, Softbody & Whole-World Physics (raw_engine.dll, zero SKSE)
    bool  ikFootEnabled     = true;   // 2-bone terrain-conforming foot planting & pelvis drop
    bool  ikGazeEnabled     = true;   // Upper-body spine/head gaze & velocity lean
    bool  softbodyEnabled   = true;   // 240 Hz sub-stepped XPBD cloth/hair/tissue physics
    float physicsStiffness  = 1.00f;  // Global XPBD spring stiffness multiplier (0.4..2.0)

    // Custom Navmesh, Tactical Squad Combat AI & Modern Action Combat (raw_engine.dll, zero SKSE)
    bool  navCustomEnabled  = true;   // Clean-room CeNavMesh + RawNavGraph dynamic obstacle avoidance
    bool  combatAiEnabled   = true;   // Squad role coordination (vanguard/flanker/skirmisher)
    bool  timedParryEnabled = true;   // Precision timed parry & posture break
    float parryWindowMs     = 160.0f; // Timed parry window in ms (80..280)
    float hitStopMs         = 45.0f;  // Melee impact hit-stop duration in ms (0..90)
    float poiseScale        = 1.00f;  // Global poise/stagger threshold multiplier (0.5..2.0)
    bool  livingWorldAi     = true;   // Weather-reactive shelter seeking & dynamic torch routines

    // UI Preferences (DESIGN-RULES.md compliance)
    bool  darkTheme         = true;   // true = #101318 dark ground, false = #f9f5ed light paper ground
    bool  reducedMotion     = false;  // Respects reduced-motion preference (static settled visuals)
};

enum class RowKind : uint8_t {
    Toggle = 0,
    Slider = 1,
    Cycle  = 2,
    Action = 3,
};

struct StudioRow {
    std::string label;
    std::string valueText;
    RowKind     kind = RowKind::Toggle;
    bool        boolVal = false;
    float       normVal = 0.0f;    // [0..1] for Slider rendering & mouse drag
    VerdictGlyph glyph = VerdictGlyph::HollowRing;
    bool        activeAccent = false;
};

struct StudioTelemetry {
    float       fps = 60.0f;
    float       frameMs = 16.6f;
    bool        bridgeReady = false;
    bool        sm69Active = true;
    bool        rawlssActive = true;
    bool        rtActive = true;
    int         hwTier = 4;
    std::string statusMessage;

    // Live instrument bay values
    float colorTempK = 6500.0f;
    float oklabSat = 1.0f;
    float dofFocalMm = 35.0f;
    float dofFStop = 4.0f;
    int   dofBokehShape = 2;
    float exposureBias = 0.0f;
    float highlightKnee = 0.68f;
    float shadowLift = 0.0f;
    int   activeNavNodes = 0;
    int   activeAiAgents = 0;
    int   activeIkActors = 0;
    float snowDepthAvgU = 0.0f;
};

struct StudioHitResult {
    int   clickedTab = -1;       // 0..9 if user clicked a domain rail tab
    int   hoveredTab = -1;
    int   clickedRow = -1;       // row index if clicked
    int   hoveredRow = -1;
    bool  sliderDragged = false;
    float sliderNorm = 0.0f;     // [0..1] when dragging a row slider
    bool  wheelClicked = false;
    float wheelTempK = 6500.0f;  // when clicking/dragging the Oklab color wheel
    float wheelSat = 1.0f;
    bool  toggleThemeClicked = false;
    bool  toggleMotionClicked = false;
    bool  saveClicked = false;
    bool  exportClicked = false;
};

class RawStudioUi {
public:
    static constexpr int kDomainCount = 10;
    static const char* DomainName(int idx);
    static const char* DomainCode(int idx);

    // Hit-tests mouse coordinates against the stable Studio layout and returns interactive events.
    StudioHitResult HitTest(int32_t panelOriginX, int32_t panelOriginY,
                            uint32_t panelW, uint32_t panelH,
                            const InputNavFrame& input,
                            int rowCount, int scrollOffset) const;

    // Renders the complete anti-aliased RAW Studio UI into an RGBA/BGRA 32-bit pixel buffer.
    // Returns true on success with outW/outH set.
    bool RenderStudio(uint32_t maxW, uint32_t maxH,
                      int activeTab, int selectedRow, int hoveredRow, int hoveredTab, int scrollOffset,
                      const std::vector<StudioRow>& rows,
                      const StudioTelemetry& tel,
                      const WorldSimTuning& sim,
                      const InputNavFrame& input,
                      int32_t panelOriginX, int32_t panelOriginY,
                      const char* footerHint,
                      std::vector<uint8_t>& outBgra, uint32_t& outW, uint32_t& outH) const;

    // Renders a clean compact error/status banner using the same design tokens.
    bool RenderBanner(const std::string& text, uint32_t maxW, bool darkTheme,
                      std::vector<uint8_t>& outBgra, uint32_t& outW, uint32_t& outH) const;
};

// Global thread-safe accessors for WorldSimTuning (shared with raw_engine.dll & LightingPass).
WorldSimTuning& GetWorldSimTuning();

}  // namespace raw::ui
