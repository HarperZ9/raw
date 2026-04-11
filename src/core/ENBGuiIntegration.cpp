#include "ENBGuiIntegration.h"
#include "ENBInterface.h"
#include "ShaderPreProcessor.h"
#include "ParameterBindingEngine.h"
#include "WeatherEditor.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" {
    __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);
    __declspec(dllimport) void* __stdcall GetProcAddress(void*, const char*);
}

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  AntTweakBar constants and types
// ═══════════════════════════════════════════════════════════════════════════

static constexpr int kTW_TYPE_BOOLCPP = 1;
static constexpr int kTW_TYPE_INT32   = 10;
static constexpr int kTW_TYPE_FLOAT   = 12;
static constexpr int kTW_TYPE_COLOR3F = 15;
static constexpr int kTW_TYPE_COLOR4F = 16;
static constexpr int kTW_TYPE_DIR3F   = 21;

struct TwStructMember
{
    const char* Name;
    int         Type;       // TwType
    size_t      Offset;
    const char* DefString;
};

// ═══════════════════════════════════════════════════════════════════════════
//  ATB function pointer types
// ═══════════════════════════════════════════════════════════════════════════

using FnTwNewBar              = void*(*)(const char*);
using FnTwDeleteBar           = int(*)(void*);
using FnTwAddVarRO            = int(*)(void*, const char*, int, const void*, const char*);
using FnTwAddVarRW            = int(*)(void*, const char*, int, void*, const char*);
using FnTwAddVarCB            = int(*)(void*, const char*, int,
                                       void(*)(const void*, void*),   // set callback
                                       void(*)(void*, void*),         // get callback
                                       void*, const char*);
using FnTwAddSeparator        = int(*)(void*, const char*, const char*);
using FnTwDefine              = int(*)(const char*);
using FnTwDefineStruct        = int(*)(const char*, const TwStructMember*, unsigned int,
                                       size_t, void(*)(char*, size_t, const void*, void*), void*);
using FnTwDefineEnumFromString = int(*)(const char*, const char*);
using FnTwRemoveAllVars       = int(*)(void*);
using FnTwRefreshBar          = int(*)(void*);
using FnTwGetBarByName        = void*(*)(const char*);
using FnTwGetBarCount         = int(*)();
using FnTwGetLastError        = const char*(*)();

// ═══════════════════════════════════════════════════════════════════════════
//  Resolved function pointers
// ═══════════════════════════════════════════════════════════════════════════

static FnTwNewBar              s_TwNewBar              = nullptr;
static FnTwDeleteBar           s_TwDeleteBar           = nullptr;
static FnTwAddVarRO            s_TwAddVarRO            = nullptr;
static FnTwAddVarRW            s_TwAddVarRW            = nullptr;
static FnTwAddVarCB            s_TwAddVarCB            = nullptr;
static FnTwAddSeparator        s_TwAddSeparator        = nullptr;
static FnTwDefine              s_TwDefine              = nullptr;
static FnTwDefineStruct        s_TwDefineStruct        = nullptr;
static FnTwDefineEnumFromString s_TwDefineEnumFromString = nullptr;
static FnTwRemoveAllVars       s_TwRemoveAllVars       = nullptr;
static FnTwRefreshBar          s_TwRefreshBar          = nullptr;
static FnTwGetBarByName        s_TwGetBarByName        = nullptr;
static FnTwGetBarCount         s_TwGetBarCount         = nullptr;
static FnTwGetLastError        s_TwGetLastError        = nullptr;

// ═══════════════════════════════════════════════════════════════════════════
//  File-scope data
// ═══════════════════════════════════════════════════════════════════════════

// Static AllData copy for ATB to read from (game-state bar)
static AllData s_guiData{};

// Annotated shader bar tracking
struct AnnotatedBarInfo
{
    void*       bar  = nullptr;
    std::string name;           // bar name (e.g., "SB_enbbloom")
};
static std::vector<AnnotatedBarInfo> s_annotatedBars;

// Callback context for annotated read-write params
struct ParamCBData
{
    std::string shader;         // shader file (e.g., "enbbloom.fx")
    std::string uiName;         // param UIName for ENBGet/SetParameter
    std::string varName;        // HLSL variable name (ATB identifier — guaranteed unique)
    std::string paramKey;       // unique key for ParameterBindingEngine lookups
    std::string barName;        // ATB bar name (for TwDefine visibility toggling)
    std::string hlslType;       // "float", "float3", "float4", etc.
    float       value[4] = {};  // cached backing store (ATB reads/writes here)
    int         enbSize  = 16;  // ENBParameter.Size for writes
    int         enbType  = 6;   // ENBParameter.Type for writes (COLOR4 default)
    bool        lastHidden = false;   // previous binding visibility state
};
static std::vector<std::unique_ptr<ParamCBData>> s_cbStorage;

// ═══════════════════════════════════════════════════════════════════════════
//  Param classification for smart widget selection
// ═══════════════════════════════════════════════════════════════════════════

enum class ParamCategory { Generic, Color, Direction };

static ParamCategory ClassifyParam(const char* name)
{
    // Direction detection (check first — "SB_Sun_Direction" contains "Sun" too)
    if (strstr(name, "Direction") || strstr(name, "_DirDir"))
        return ParamCategory::Direction;

    // Color detection — params where .rgb represents a color
    if (strstr(name, "Color") || strstr(name, "Tint") || strstr(name, "Diffuse"))
        return ParamCategory::Color;
    if (strstr(name, "SkyUpper") || strstr(name, "SkyLower") || strstr(name, "Horizon"))
        return ParamCategory::Color;
    if (strstr(name, "Sunlight") || strstr(name, "EffectLight"))
        return ParamCategory::Color;
    if (strstr(name, "AmbientSpec") || strstr(name, "DirAmbient"))
        return ParamCategory::Color;

    // Careful: "SB_Interior_Ambient" and "SB_Shadow_Ambient" are colors,
    // but "SB_Audio_Ambient" is NOT. Check for known color domains.
    if (strstr(name, "Ambient")) {
        if (strstr(name, "Atmos_") || strstr(name, "Interior_") || strstr(name, "Shadow_"))
            return ParamCategory::Color;
    }

    return ParamCategory::Generic;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Summary callbacks for custom struct types
// ═══════════════════════════════════════════════════════════════════════════

static void Float4Summary(char* summary, size_t maxLen,
                           const void* value, void* /*clientData*/)
{
    const auto* f = static_cast<const float*>(value);
    snprintf(summary, maxLen, "{%.3f, %.3f, %.3f, %.3f}", f[0], f[1], f[2], f[3]);
}

static void Color4Summary(char* summary, size_t maxLen,
                            const void* value, void* /*clientData*/)
{
    const auto* f = static_cast<const float*>(value);
    snprintf(summary, maxLen, "(%.2f, %.2f, %.2f) a=%.2f", f[0], f[1], f[2], f[3]);
}

static void Dir4Summary(char* summary, size_t maxLen,
                          const void* value, void* /*clientData*/)
{
    const auto* f = static_cast<const float*>(value);
    snprintf(summary, maxLen, "<%.3f, %.3f, %.3f> w=%.3f", f[0], f[1], f[2], f[3]);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ATB callbacks for annotated shader params (read-write)
// ═══════════════════════════════════════════════════════════════════════════

// Get callback: ATB calls this to display the current value
static void CB_GetValue(void* value, void* clientData)
{
    auto* ctx = static_cast<ParamCBData*>(clientData);
    std::memcpy(value, ctx->value, static_cast<size_t>(ctx->enbSize));
}

// Set callback: ATB calls this when the user modifies the value
static void CB_SetValue(const void* value, void* clientData)
{
    auto* ctx = static_cast<ParamCBData*>(clientData);

    // Merge modified bytes into cached value (preserves untouched components)
    std::memcpy(ctx->value, value, static_cast<size_t>(ctx->enbSize));

    // Write to ENB — shader name already UPPERCASE (stored that way in cb->shader)
    if (ENBInterface::SetParameter) {
        ENBInterface::ENBParameter param;
        std::memcpy(param.Data, ctx->value, 16);
        param.Size = static_cast<unsigned long>(ctx->enbSize);
        param.Type = static_cast<ENBInterface::ENBParameterType>(ctx->enbType);
        ENBInterface::SetParameter(nullptr, ctx->shader.c_str(), ctx->uiName.c_str(), &param);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Editable param system — live game-engine write-back from ATB
//
//  Some AllData params can be edited in the ENB GUI and written back to
//  the game engine in real time (like KreatE). Each editable param has an
//  apply function that writes to the relevant RE:: singleton. Overrides
//  are re-applied every frame since the engine recomputes these values.
// ═══════════════════════════════════════════════════════════════════════════

// ── Apply functions ─────────────────────────────────────────────────────

static void Apply_CameraParams(const float* v)
{
    auto* cam = RE::PlayerCamera::GetSingleton();
    if (cam) cam->worldFOV = v[0] * (180.0f / 3.14159265f);  // .x = FOV radians → degrees
}

static void Apply_FogNear(const float* v)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->currentWeather) return;
    sky->currentWeather->fogData.dayNear   = v[3];  // .a = near distance
    sky->currentWeather->fogData.nightNear = v[3];
}

static void Apply_FogFar(const float* v)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->currentWeather) return;
    sky->currentWeather->fogData.dayFar   = v[3];
    sky->currentWeather->fogData.nightFar = v[3];
}

static void Apply_FogDensity(const float* v)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->currentWeather) return;
    sky->currentWeather->fogData.dayPower   = v[0];  // .x = power
    sky->currentWeather->fogData.nightPower = v[0];
    sky->currentWeather->fogData.dayMax     = v[1];  // .y = max opacity
    sky->currentWeather->fogData.nightMax   = v[1];
}

static void Apply_IS_HDR(const float* v)
{
    auto* ism = RE::ImageSpaceManager::GetSingleton();
    if (!ism) return;
    ism->data.baseData.hdr.eyeAdaptSpeed  = v[0];
    ism->data.baseData.hdr.bloomScale     = v[1];
    ism->data.baseData.hdr.bloomThreshold = v[2];
    ism->data.baseData.hdr.sunlightScale  = v[3];
}

static void Apply_IS_Cinematic(const float* v)
{
    auto* ism = RE::ImageSpaceManager::GetSingleton();
    if (!ism) return;
    ism->data.baseData.cinematic.saturation = v[0];
    ism->data.baseData.cinematic.brightness = v[1];
    ism->data.baseData.cinematic.contrast   = v[2];
    ism->data.baseData.tint.amount          = v[3];  // .w = tint amount
}

static void Apply_IS_CineTint(const float* v)
{
    auto* ism = RE::ImageSpaceManager::GetSingleton();
    if (!ism) return;
    // Tint color stored as uint8 [0-255], AllData uses float [0-1]
    auto to8 = [](float f) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(f * 255.0f, 0.0f, 255.0f));
    };
    ism->data.baseData.tint.color.red   = to8(v[0]);
    ism->data.baseData.tint.color.green = to8(v[1]);
    ism->data.baseData.tint.color.blue  = to8(v[2]);
}

static void Apply_IS_DOF(const float* v)
{
    auto* ism = RE::ImageSpaceManager::GetSingleton();
    if (!ism) return;
    ism->data.baseData.depthOfField.strength = v[0];
    ism->data.baseData.depthOfField.distance = v[1];
    ism->data.baseData.depthOfField.range    = v[2];
}

static void Apply_SunlightColor(const float* v)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->sun || !sky->sun->light.get()) return;
    auto* niLight = reinterpret_cast<RE::NiLight*>(sky->sun->light.get());
    auto& diffuse = niLight->GetLightRuntimeData().diffuse;
    diffuse.red   = v[0];
    diffuse.green = v[1];
    diffuse.blue  = v[2];
}

static void Apply_Timescale(const float* v)
{
    auto* cal = RE::Calendar::GetSingleton();
    if (cal && cal->timeScale)
        cal->timeScale->value = v[0];
}

// ── Editable param registry ────────────────────────────────────────────

struct EditableParam {
    const char*  paramName;          // matches kParamTable name
    void       (*apply)(const float*);
    size_t       offset;             // byte offset into AllData (resolved at registration)
    bool         active;             // override is live
    float        value[4];           // current override value
};

static EditableParam s_editableParams[] = {
    { "SB_Camera_Params",       Apply_CameraParams,   0, false, {} },
    { "SB_Fog_NearColor",       Apply_FogNear,        0, false, {} },
    { "SB_Fog_FarColor",        Apply_FogFar,         0, false, {} },
    { "SB_Fog_Density",         Apply_FogDensity,     0, false, {} },
    { "SB_IS_HDR",              Apply_IS_HDR,         0, false, {} },
    { "SB_IS_Cinematic",        Apply_IS_Cinematic,   0, false, {} },
    { "SB_IS_CineTint",         Apply_IS_CineTint,    0, false, {} },
    { "SB_IS_DOF",              Apply_IS_DOF,         0, false, {} },
    { "SB_Atmos_SunlightColor", Apply_SunlightColor,  0, false, {} },
    { "SB_Time_TimeData",       Apply_Timescale,      0, false, {} },
};

static constexpr size_t kEditableCount = sizeof(s_editableParams) / sizeof(s_editableParams[0]);

static EditableParam* FindEditable(const char* name)
{
    for (size_t i = 0; i < kEditableCount; ++i) {
        if (strcmp(name, s_editableParams[i].paramName) == 0)
            return &s_editableParams[i];
    }
    return nullptr;
}

// ── ATB callbacks for editable params ──────────────────────────────────

static void CB_EditableGet(void* value, void* clientData)
{
    auto* ep = static_cast<EditableParam*>(clientData);
    // Read from s_guiData — patched with overrides each frame in Update()
    std::memcpy(value, reinterpret_cast<const char*>(&s_guiData) + ep->offset, 16);
}

static void CB_EditableSet(const void* value, void* clientData)
{
    auto* ep = static_cast<EditableParam*>(clientData);
    std::memcpy(ep->value, value, 16);
    ep->active = true;
    ep->apply(ep->value);  // immediate write to game engine
}

// Re-apply all active overrides each frame (engine recomputes these values)
static void ApplyOverrides()
{
    for (size_t i = 0; i < kEditableCount; ++i) {
        auto& ep = s_editableParams[i];
        if (!ep.active) continue;
        ep.apply(ep.value);
        // Patch s_guiData so ATB get-callback shows the override value
        std::memcpy(reinterpret_cast<char*>(&s_guiData) + ep.offset, ep.value, 16);
    }
}

// Clear all active overrides (game returns to natural values)
static void ClearOverrides()
{
    for (size_t i = 0; i < kEditableCount; ++i)
        s_editableParams[i].active = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Domain name extraction from param name
// ═══════════════════════════════════════════════════════════════════════════

// "SB_Celestial_TimeData" → domain="Celestial", param="TimeData"
static bool ParseDomainParam(const char* name,
                              std::string& domain, std::string& param)
{
    if (strncmp(name, "SB_", 3) != 0) return false;
    const char* p = name + 3;
    const char* sep = strchr(p, '_');
    if (!sep) return false;
    domain.assign(p, static_cast<size_t>(sep - p));
    param = sep + 1;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Category bar system — split AllData across 6 themed bars
// ═══════════════════════════════════════════════════════════════════════════

struct CategoryBarDef {
    const char* barName;
    const char* label;
    const char* color;
    const char* size;
};

static constexpr int kCatBarCount = 6;

static const CategoryBarDef kCatDefs[kCatBarCount] = {
    { "SB_Env",    "SB: Environment",        "25 50 75",   "340 500" },
    { "SB_Player", "SB: Player & Character",  "70 42 22",  "340 380" },
    { "SB_Camera", "SB: Camera & Render",     "40 50 62",  "340 440" },
    { "SB_World",  "SB: World",               "30 55 40",  "340 380" },
    { "SB_UI",     "SB: Interaction",          "55 38 60",  "340 280" },
    { "SB_Diag",   "SB: Scene & Diagnostics", "58 48 32",  "340 520" },
};

// Split camelCase: "SkyUpper" → "Sky Upper", "PosRad" → "Pos Rad"
static std::string HumanizeLabel(const std::string& raw)
{
    std::string result;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (i > 0) {
            bool prevLower = islower(static_cast<unsigned char>(raw[i-1])) != 0;
            bool currUpper = isupper(static_cast<unsigned char>(c)) != 0;
            bool currDigit = isdigit(static_cast<unsigned char>(c)) != 0;
            bool prevAlpha = isalpha(static_cast<unsigned char>(raw[i-1])) != 0;
            if ((prevLower && currUpper) || (prevAlpha && currDigit))
                result += ' ';
        }
        result += c;
    }
    return result;
}

// Route a param name to a bar index (0-5), group name, and display label.
// Diagnostics/Scene checks come FIRST to prevent ambiguity (e.g., SB_Sun_Glare vs SB_Sun_Direction).
static int RouteParam(const char* name, const char*& outGroup, std::string& outLabel)
{
    // ── Diagnostics bar (5) — Scene, Engine, Water, Effect, Feedback, Perf ──
    if (strncmp(name, "SB_Scene_", 9) == 0) {
        outGroup = "Scene Composition"; outLabel = name + 9; return 5;
    }
    if (strncmp(name, "SB_Engine_", 10) == 0) {
        outGroup = "Engine State"; outLabel = name + 10; return 5;
    }
    if (strncmp(name, "SB_DirAmbient_", 14) == 0) {
        outGroup = "Directional Ambient"; outLabel = name + 14; return 5;
    }
    if (strcmp(name, "SB_Sun_Glare") == 0) {
        outGroup = "Scene Composition"; outLabel = "Sun Glare"; return 5;
    }
    if (strncmp(name, "SB_Water_", 9) == 0) {
        outGroup = "Water Shader"; outLabel = name + 9; return 5;
    }
    if (strncmp(name, "SB_Effect_", 10) == 0) {
        outGroup = "Effect Shader"; outLabel = name + 10; return 5;
    }
    if (strncmp(name, "SB_Computed_", 12) == 0) {
        outGroup = "GPU Feedback"; outLabel = name + 12; return 5;
    }
    if (strncmp(name, "SB_ENB_", 7) == 0) {
        outGroup = "ENB Readback"; outLabel = name + 7; return 5;
    }
    if (strncmp(name, "SB_Perf_", 8) == 0) {
        outGroup = "Performance"; outLabel = name + 8; return 5;
    }
    if (strncmp(name, "SB_Theme_", 9) == 0) {
        outGroup = "Theme"; outLabel = name + 9; return 5;
    }

    // ── Environment bar (0) — Celestial, Atmosphere, Fog, Weather ──
    if (strncmp(name, "SB_Sun_", 7) == 0) {
        outGroup = "Celestial"; outLabel = std::string("Sun ") + (name + 7); return 0;
    }
    if (strncmp(name, "SB_Masser_", 10) == 0) {
        outGroup = "Celestial"; outLabel = std::string("Masser ") + (name + 10); return 0;
    }
    if (strncmp(name, "SB_Secunda_", 11) == 0) {
        outGroup = "Celestial"; outLabel = std::string("Secunda ") + (name + 11); return 0;
    }
    if (strncmp(name, "SB_Time", 7) == 0) {
        outGroup = "Celestial";
        outLabel = (name[7] == '_') ? std::string("Time ") + (name + 8) : "Time";
        return 0;
    }
    if (strncmp(name, "SB_Atmos_", 9) == 0) {
        outGroup = "Atmosphere"; outLabel = name + 9; return 0;
    }
    if (strncmp(name, "SB_Fog_", 7) == 0) {
        outGroup = "Fog"; outLabel = name + 7; return 0;
    }
    if (strcmp(name, "SB_Wind") == 0) {
        outGroup = "Weather"; outLabel = "Wind"; return 0;
    }
    if (strcmp(name, "SB_Precipitation") == 0) {
        outGroup = "Weather"; outLabel = "Precipitation"; return 0;
    }
    if (strcmp(name, "SB_Lightning") == 0) {
        outGroup = "Weather"; outLabel = "Lightning"; return 0;
    }
    if (strncmp(name, "SB_Weather_", 11) == 0) {
        outGroup = "Weather"; outLabel = name + 11; return 0;
    }
    if (strncmp(name, "SB_Precip_", 10) == 0) {
        outGroup = "Weather"; outLabel = std::string("Precip ") + (name + 10); return 0;
    }
    if (strncmp(name, "SB_Wind_", 8) == 0) {
        outGroup = "Weather"; outLabel = std::string("Wind ") + (name + 8); return 0;
    }
    if (strncmp(name, "SB_Cloud_", 9) == 0) {
        outGroup = "Weather"; outLabel = std::string("Cloud ") + (name + 9); return 0;
    }
    if (strncmp(name, "SB_Aurora_", 10) == 0) {
        outGroup = "Weather"; outLabel = std::string("Aurora ") + (name + 10); return 0;
    }

    // ── Player bar (1) — Player, Equipment, Skills, NPC ──
    if (strncmp(name, "SB_Player_", 10) == 0) {
        outGroup = "Player"; outLabel = name + 10; return 1;
    }
    if (strncmp(name, "SB_Equip_", 9) == 0) {
        outGroup = "Equipment"; outLabel = name + 9; return 1;
    }
    if (strncmp(name, "SB_AV_", 6) == 0) {
        outGroup = "Skills & Stats"; outLabel = name + 6; return 1;
    }
    if (strncmp(name, "SB_NPC_", 7) == 0) {
        outGroup = "NPC Detection"; outLabel = name + 7; return 1;
    }

    // ── Camera bar (2) — Camera, View, Render, ImageSpace, Shadow, Effects ──
    if (strncmp(name, "SB_Camera_", 10) == 0) {
        outGroup = "Camera"; outLabel = name + 10; return 2;
    }
    if (strncmp(name, "SB_View_", 8) == 0) {
        outGroup = "View Matrices"; outLabel = name + 8; return 2;
    }
    if (strncmp(name, "SB_PrevCamera_", 14) == 0) {
        outGroup = "Previous Frame"; outLabel = std::string("Pos ") + (name + 14); return 2;
    }
    if (strncmp(name, "SB_PrevView_", 12) == 0) {
        outGroup = "Previous Frame"; outLabel = name + 12; return 2;
    }
    if (strncmp(name, "SB_Render_", 10) == 0) {
        outGroup = "Rendering"; outLabel = name + 10; return 2;
    }
    if (strncmp(name, "SB_IS_", 6) == 0) {
        outGroup = "Image Space"; outLabel = name + 6; return 2;
    }
    if (strncmp(name, "SB_Shadow_", 10) == 0) {
        outGroup = "Shadow & Light"; outLabel = name + 10; return 2;
    }
    if (strncmp(name, "SB_FX_", 6) == 0) {
        outGroup = "Active Effects"; outLabel = name + 6; return 2;
    }

    // ── World bar (3) — Interior, Lights, Region, Audio ──
    if (strncmp(name, "SB_Interior_", 12) == 0) {
        outGroup = "Interior"; outLabel = name + 12; return 3;
    }
    if (strncmp(name, "SB_Light", 8) == 0) {
        outGroup = "Nearby Lights";
        if (name[8] >= '0' && name[8] <= '9') {
            outLabel = std::string("Light ") + name[8];
            if (name[9] == '_') outLabel += std::string(" ") + (name + 10);
        } else if (name[8] == '_') {
            outLabel = name + 9;
        } else {
            outLabel = name + 3;
        }
        return 3;
    }
    if (strncmp(name, "SB_Region_", 10) == 0) {
        outGroup = "Region"; outLabel = name + 10; return 3;
    }
    if (strncmp(name, "SB_Audio_", 9) == 0) {
        outGroup = "Audio"; outLabel = name + 9; return 3;
    }

    // ── Interaction bar (4) — Crosshair, Quest, UI ──
    if (strncmp(name, "SB_XHair_", 9) == 0) {
        outGroup = "Crosshair"; outLabel = name + 9; return 4;
    }
    if (strncmp(name, "SB_Quest_", 9) == 0) {
        outGroup = "Quests"; outLabel = name + 9; return 4;
    }
    if (strncmp(name, "SB_UI_", 6) == 0) {
        outGroup = "UI State"; outLabel = name + 6; return 4;
    }

    // Default: Environment, no group
    outGroup = nullptr;
    outLabel = name;
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Singleton
// ═══════════════════════════════════════════════════════════════════════════

ENBGuiIntegration& ENBGuiIntegration::Get()
{
    static ENBGuiIntegration inst;
    return inst;
}

int ENBGuiIntegration::GetAnnotatedBarCount() const
{
    return static_cast<int>(s_annotatedBars.size());
}

int ENBGuiIntegration::GetCallbackCount() const
{
    return static_cast<int>(s_cbStorage.size());
}

// ═══════════════════════════════════════════════════════════════════════════
//  Init: resolve ATB functions from d3d11.dll
// ═══════════════════════════════════════════════════════════════════════════

bool ENBGuiIntegration::Init()
{
    void* enbModule = GetModuleHandleW(L"d3d11.dll");
    if (!enbModule) {
        enbModule = GetModuleHandleW(L"d3d11_enb.dll");
    }
    if (!enbModule) return false;

    #define RESOLVE_ATB(exportName, var) \
        var = reinterpret_cast<decltype(var)>(GetProcAddress(enbModule, exportName))

    RESOLVE_ATB("TwNewBar",              s_TwNewBar);
    RESOLVE_ATB("TwDeleteBar",           s_TwDeleteBar);
    RESOLVE_ATB("TwAddVarRO",            s_TwAddVarRO);
    RESOLVE_ATB("TwAddVarRW",            s_TwAddVarRW);
    RESOLVE_ATB("TwAddVarCB",            s_TwAddVarCB);
    RESOLVE_ATB("TwAddSeparator",        s_TwAddSeparator);
    RESOLVE_ATB("TwDefine",              s_TwDefine);
    RESOLVE_ATB("TwDefineStruct",        s_TwDefineStruct);
    RESOLVE_ATB("TwDefineEnumFromString", s_TwDefineEnumFromString);
    RESOLVE_ATB("TwRemoveAllVars",       s_TwRemoveAllVars);
    RESOLVE_ATB("TwRefreshBar",          s_TwRefreshBar);
    RESOLVE_ATB("TwGetBarByName",        s_TwGetBarByName);
    RESOLVE_ATB("TwGetBarCount",         s_TwGetBarCount);
    RESOLVE_ATB("TwGetLastError",        s_TwGetLastError);

    #undef RESOLVE_ATB

    if (!s_TwNewBar || !s_TwAddVarRO || !s_TwDefine) {
        SKSE::log::info("ENBGuiIntegration: critical ATB exports missing from d3d11.dll");
        return false;
    }

    m_available = true;
    int resolved =
        (s_TwNewBar?1:0) + (s_TwDeleteBar?1:0) + (s_TwAddVarRO?1:0) + (s_TwAddVarRW?1:0) +
        (s_TwAddVarCB?1:0) + (s_TwAddSeparator?1:0) + (s_TwDefine?1:0) + (s_TwDefineStruct?1:0) +
        (s_TwDefineEnumFromString?1:0) + (s_TwRemoveAllVars?1:0) + (s_TwRefreshBar?1:0) +
        (s_TwGetBarByName?1:0) + (s_TwGetBarCount?1:0) + (s_TwGetLastError?1:0);
    SKSE::log::info("ENBGuiIntegration: {} ATB exports resolved (TwAddVarCB={}, TwDefineEnum={})",
        resolved, s_TwAddVarCB ? "yes" : "no", s_TwDefineEnumFromString ? "yes" : "no");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  EnsureBar: create the AllData bar once ATB is initialized
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::EnsureBar()
{
    if (!m_available || m_barCreated) return;

    ++m_retryCount;

    // Test ATB readiness by creating the first category bar
    int barCountBefore = s_TwGetBarCount ? s_TwGetBarCount() : -1;

    void* testBar = s_TwNewBar(kCatDefs[0].barName);
    if (!testBar) {
        if (m_retryCount == 1 || m_retryCount % 600 == 0) {
            const char* err = s_TwGetLastError ? s_TwGetLastError() : "unknown";
            SKSE::log::info("ENBGuiIntegration: TwNewBar failed, retry {} (barCount={}, err='{}')",
                m_retryCount, barCountBefore, err);
        }
        return;
    }

    // ATB is ready — store first bar and create the rest
    m_categoryBars[0] = testBar;

    for (int i = 1; i < kCategoryBarCount; ++i) {
        m_categoryBars[i] = s_TwNewBar(kCatDefs[i].barName);
        if (!m_categoryBars[i]) {
            SKSE::log::warn("ENBGuiIntegration: failed to create bar '{}' ({})",
                kCatDefs[i].barName, kCatDefs[i].label);
        }
    }

    // Backward compat
    m_bar = m_categoryBars[0];

    // Style each bar — all start iconified, staggered positions
    for (int i = 0; i < kCategoryBarCount; ++i) {
        if (!m_categoryBars[i]) continue;
        char style[384];
        snprintf(style, sizeof(style),
            " %s label='%s' "
            " color='%s' alpha=220 "
            " size='%s' position='%d 16' "
            " valueswidth=140 "
            " iconified=true ",
            kCatDefs[i].barName, kCatDefs[i].label,
            kCatDefs[i].color,
            kCatDefs[i].size,
            16 + i * 20);
        s_TwDefine(style);
    }

    RegisterParams();

    m_barCreated = true;
    int barCountAfter = s_TwGetBarCount ? s_TwGetBarCount() : -1;
    SKSE::log::info("ENBGuiIntegration: {} category bars created, {} params registered "
        "(barCount {} -> {}, retry {}, color4Type={}, dir4Type={})",
        kCategoryBarCount, kParamCount, barCountBefore, barCountAfter,
        m_retryCount, m_color4Type, m_dir4Type);
}

// ═══════════════════════════════════════════════════════════════════════════
//  RegisterParams: add AllData params to the bar with smart widget types
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::RegisterParams()
{
    // Need at least one bar
    if (!m_categoryBars[0]) return;

    // ── Define custom struct types (once) ───────────────────────────────

    if (s_TwDefineStruct) {
        TwStructMember float4Members[] = {
            { "x", kTW_TYPE_FLOAT,  0, " step=0.001 precision=4 " },
            { "y", kTW_TYPE_FLOAT,  4, " step=0.001 precision=4 " },
            { "z", kTW_TYPE_FLOAT,  8, " step=0.001 precision=4 " },
            { "w", kTW_TYPE_FLOAT, 12, " step=0.001 precision=4 " },
        };
        m_float4Type = s_TwDefineStruct(
            "SB_Float4", float4Members, 4, 16, Float4Summary, nullptr);

        TwStructMember color4Members[] = {
            { "rgb", kTW_TYPE_COLOR3F,  0, "" },
            { "a",   kTW_TYPE_FLOAT,   12, " step=0.01 precision=3 " },
        };
        m_color4Type = s_TwDefineStruct(
            "SB_Color4", color4Members, 2, 16, Color4Summary, nullptr);

        TwStructMember dir4Members[] = {
            { "dir", kTW_TYPE_DIR3F,  0, "" },
            { "w",   kTW_TYPE_FLOAT, 12, " step=0.001 precision=4 " },
        };
        m_dir4Type = s_TwDefineStruct(
            "SB_Dir4", dir4Members, 2, 16, Dir4Summary, nullptr);
    }

    // ── Route each param to its category bar with proper grouping ───────

    // Track which groups have been added per bar (for collapse-once logic)
    std::vector<std::string> groupsSeen[kCategoryBarCount];

    for (size_t i = 0; i < kParamCount; ++i) {
        const auto& entry = kParamTable[i];
        const void* ptr = reinterpret_cast<const char*>(&s_guiData) + entry.offset;

        // Route to bar index, group, and raw label
        const char* group = nullptr;
        std::string rawLabel;
        int barIdx = RouteParam(entry.name, group, rawLabel);

        // Get target bar (fallback to first)
        void* targetBar = (barIdx >= 0 && barIdx < kCategoryBarCount && m_categoryBars[barIdx])
            ? m_categoryBars[barIdx] : m_categoryBars[0];
        if (!targetBar) continue;

        int actualBarIdx = (targetBar == m_categoryBars[barIdx]) ? barIdx : 0;

        // Humanize the label: "SkyUpper" → "Sky Upper"
        std::string label = HumanizeLabel(rawLabel);

        // Classify for smart widget selection
        ParamCategory cat = ClassifyParam(entry.name);
        int varType;
        switch (cat) {
            case ParamCategory::Color:
                varType = (m_color4Type != 0) ? m_color4Type : kTW_TYPE_COLOR4F;
                break;
            case ParamCategory::Direction:
                varType = (m_dir4Type != 0) ? m_dir4Type
                        : ((m_float4Type != 0) ? m_float4Type : kTW_TYPE_COLOR4F);
                break;
            default:
                varType = (m_float4Type != 0) ? m_float4Type : kTW_TYPE_COLOR4F;
                break;
        }

        // Build definition string
        char def[256];
        if (group) {
            snprintf(def, sizeof(def), " group='%s' label='%s' ", group, label.c_str());
        } else {
            snprintf(def, sizeof(def), " label='%s' ", label.c_str());
        }

        // Editable params use TwAddVarCB for live write-back; others are read-only
        EditableParam* editable = FindEditable(entry.name);
        if (editable && s_TwAddVarCB) {
            editable->offset = entry.offset;
            s_TwAddVarCB(targetBar, entry.name, varType,
                CB_EditableSet, CB_EditableGet, editable, def);
        } else {
            s_TwAddVarRO(targetBar, entry.name, varType, ptr, def);
        }

        // Collapse group on first encounter per bar
        if (group && actualBarIdx >= 0 && actualBarIdx < kCategoryBarCount) {
            std::string groupStr(group);
            auto& seen = groupsSeen[actualBarIdx];
            bool found = false;
            for (auto& g : seen) {
                if (g == groupStr) { found = true; break; }
            }
            if (!found) {
                seen.push_back(groupStr);
                char groupDef[256];
                snprintf(groupDef, sizeof(groupDef),
                    " %s/'%s' opened=false ",
                    kCatDefs[actualBarIdx].barName, group);
                s_TwDefine(groupDef);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  BuildAnnotatedBars: per-shader bars from AnnotationDatabase
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::BuildAnnotatedBars()
{
    if (!m_available || !s_TwNewBar) return;

    auto& db = AnnotationDatabase::Get();
    auto shaders = db.GetAllShaderNames();

    if (shaders.empty()) return;

    auto& binding = ParameterBindingEngine::Get();

    for (auto& shader : shaders) {
        auto params = db.GetParametersForShader(shader);
        if (params.empty()) continue;

        // Skip string-only params (group markers)
        bool hasReal = false;
        for (auto* p : params) {
            if (p->hlslType != "string" && !p->isSeparator &&
                !p->isGroupBegin && !p->isGroupEnd) {
                hasReal = true;
                break;
            }
        }
        if (!hasReal) continue;

        // Generate bar name: "SB_enbbloom" from "enbbloom.fx"
        std::string barName = "SB_" + shader;
        auto dotPos = barName.rfind('.');
        if (dotPos != std::string::npos)
            barName.erase(dotPos);
        // Replace characters ATB doesn't like in bar names
        for (auto& c : barName) {
            if (c == ' ' || c == '/' || c == '\\') c = '_';
        }

        void* bar = s_TwNewBar(barName.c_str());
        if (!bar) {
            SKSE::log::warn("ENBGuiIntegration: failed to create bar '{}'", barName);
            continue;
        }

        // Style the bar — hidden by default, user opens from ENB bar list
        {
            // Human-readable label from shader name
            std::string label = shader;
            if (label.size() > 3 && label.substr(label.size()-3) == ".fx")
                label.erase(label.size()-3);

            char style[256];
            snprintf(style, sizeof(style),
                " %s label='SB: %s' "
                " color='40 30 60' alpha=210 "
                " size='320 400' position='370 16' "
                " valueswidth=130 "
                " visible=false ",
                barName.c_str(), label.c_str());
            s_TwDefine(style);
        }

        // ── Add parameters ──────────────────────────────────────────────

        std::string lastGroup;
        int sepIdx = 0;

        for (auto* meta : params) {
            // Skip internal markers
            if (meta->hlslType == "string" && !meta->isSeparator)
                continue;
            if (meta->isGroupBegin || meta->isGroupEnd)
                continue;
            if (meta->uiHidden)
                continue;

            // Separator
            if (meta->isSeparator) {
                if (s_TwAddSeparator) {
                    char sepName[32];
                    snprintf(sepName, sizeof(sepName), "sep%d", sepIdx++);
                    char sepDef[128] = "";
                    if (!meta->uiGroup.empty())
                        snprintf(sepDef, sizeof(sepDef), " group='%s' ", meta->uiGroup.c_str());
                    s_TwAddSeparator(bar, sepName, sepDef);
                }
                continue;
            }

            // Determine ATB identifier (varName = unique per shader) and display label
            const char* atbName = meta->varName.c_str();
            const char* displayName = meta->uiName.empty()
                ? meta->varName.c_str() : meta->uiName.c_str();

            // Check initial UIBinding visibility
            std::string paramKey = meta->GetUniqueKey();
            bool isHidden = binding.IsHidden(paramKey);

            // Determine ATB type from UIWidget + hlslType
            int twType = kTW_TYPE_FLOAT;    // default for scalars
            int enbSize = 4;
            int enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_FLOAT);

            if (meta->uiWidget == "Color") {
                if (meta->hlslType == "float3") {
                    twType = kTW_TYPE_COLOR3F;
                    enbSize = 12;
                    enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_COLOR3);
                } else {
                    twType = kTW_TYPE_COLOR4F;
                    enbSize = 16;
                    enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_COLOR4);
                }
            } else if (meta->hlslType == "float4") {
                twType = (m_float4Type != 0) ? m_float4Type : kTW_TYPE_COLOR4F;
                enbSize = 16;
                enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_COLOR4);
            } else if (meta->hlslType == "float3") {
                twType = kTW_TYPE_COLOR3F;  // default 3-component display
                enbSize = 12;
                enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_VECTOR3);
            } else if (meta->hlslType == "float2") {
                // No native float2 in ATB — use Float4 struct type
                twType = (m_float4Type != 0) ? m_float4Type : kTW_TYPE_COLOR4F;
                enbSize = 16;
                enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_COLOR4);
            } else if (meta->hlslType == "int") {
                twType = kTW_TYPE_INT32;
                enbSize = 4;
                enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_INT);
            } else if (meta->hlslType == "bool") {
                twType = kTW_TYPE_BOOLCPP;
                enbSize = 4;
                enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_BOOL);
            }

            // Dropdown: create enum type if UIList is available
            if (meta->uiWidget == "Dropdown" && !meta->uiList.empty() &&
                s_TwDefineEnumFromString)
            {
                std::string enumName = barName + "_" + meta->varName + "_enum";
                int enumType = s_TwDefineEnumFromString(enumName.c_str(), meta->uiList.c_str());
                if (enumType != 0) {
                    twType = enumType;
                    enbSize = 4;
                    enbType = static_cast<int>(ENBInterface::ENBParameterType::ENBParam_INT);
                }
            }

            // Build definition string
            char def[384];
            int defLen = 0;

            // Label (displayName shown to user, atbName is internal identifier)
            defLen += snprintf(def + defLen, sizeof(def) - defLen,
                " label='%s' ", displayName);

            // Group hierarchy from UIGroup
            if (!meta->uiGroup.empty()) {
                defLen += snprintf(def + defLen, sizeof(def) - defLen,
                    " group='%s' ", meta->uiGroup.c_str());
            }

            // Min/max/step for float spinners
            if (twType == kTW_TYPE_FLOAT) {
                defLen += snprintf(def + defLen, sizeof(def) - defLen,
                    " min=%.4f max=%.4f step=%.4f precision=4 ",
                    meta->uiMin, meta->uiMax,
                    (meta->uiMax - meta->uiMin) > 0 ? (meta->uiMax - meta->uiMin) / 100.f : 0.01f);
            } else if (twType == kTW_TYPE_INT32) {
                defLen += snprintf(def + defLen, sizeof(def) - defLen,
                    " min=%d max=%d ",
                    static_cast<int>(meta->uiMin), static_cast<int>(meta->uiMax));
            }

            // Start hidden if UIBinding says so
            if (isHidden) {
                defLen += snprintf(def + defLen, sizeof(def) - defLen, " visible=false ");
            }

            // Readonly from UIBinding or annotation
            bool isReadOnly = binding.IsReadOnly(paramKey) ||
                              meta->uiReadOnly ||
                              meta->varName.rfind("SB_", 0) == 0;

            // Create callback context
            // Store shader name in UPPERCASE — ENB's internal lookup is case-sensitive
            auto cb = std::make_unique<ParamCBData>();
            cb->shader     = shader;
            for (auto& c : cb->shader) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            cb->uiName     = meta->uiName.empty() ? meta->varName : meta->uiName;
            cb->varName    = meta->varName;
            cb->paramKey   = paramKey;
            cb->barName    = barName;
            cb->hlslType   = meta->hlslType;
            cb->enbSize    = enbSize;
            cb->enbType    = enbType;
            cb->lastHidden = isHidden;

            // Read initial value from ENB (cb->shader is already uppercased)
            if (ENBInterface::GetParameter) {
                ENBInterface::ENBParameter outParam;
                if (ENBInterface::GetParameter(nullptr, cb->shader.c_str(),
                    cb->uiName.c_str(), &outParam) && outParam.Size > 0) {
                    int sz = (outParam.Size <= 16) ? static_cast<int>(outParam.Size) : 16;
                    std::memcpy(cb->value, outParam.Data, sz);
                }
            }

            // Register with ATB — use varName as identifier (unique per shader)
            ParamCBData* rawCb = cb.get();
            if (isReadOnly || !s_TwAddVarCB) {
                s_TwAddVarRO(bar, atbName, twType, rawCb->value, def);
            } else {
                s_TwAddVarCB(bar, atbName, twType,
                    CB_SetValue, CB_GetValue, rawCb, def);
            }

            s_cbStorage.push_back(std::move(cb));
        }

        // Collapse UIGroup groups by default
        for (auto* meta : params) {
            if (!meta->uiGroup.empty() && meta->uiGroup != lastGroup) {
                char groupDef[256];
                snprintf(groupDef, sizeof(groupDef),
                    " %s/'%s' opened=false ",
                    barName.c_str(), meta->uiGroup.c_str());
                s_TwDefine(groupDef);
                lastGroup = meta->uiGroup;
            }
        }

        s_annotatedBars.push_back({bar, barName});
    }

    if (!s_annotatedBars.empty()) {
        SKSE::log::info("ENBGuiIntegration: built {} annotated shader bars ({} params)",
            s_annotatedBars.size(), s_cbStorage.size());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  RefreshBindingState: toggle visibility via TwDefine when bindings change
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::RefreshBindingState()
{
    if (s_cbStorage.empty() || !s_TwDefine) return;

    auto& binding = ParameterBindingEngine::Get();

    for (auto& cb : s_cbStorage) {
        bool hidden = binding.IsHidden(cb->paramKey);
        if (hidden != cb->lastHidden) {
            // Toggle visibility: " barName/varName visible=true/false "
            char cmd[256];
            snprintf(cmd, sizeof(cmd), " %s/%s visible=%s ",
                cb->barName.c_str(), cb->varName.c_str(),
                hidden ? "false" : "true");
            s_TwDefine(cmd);
            cb->lastHidden = hidden;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  RebuildAnnotatedBarsIfNeeded: detect annotation changes via generation
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::RebuildAnnotatedBarsIfNeeded()
{
    auto& db = AnnotationDatabase::Get();
    int gen = db.GetGeneration();
    if (gen == m_lastGeneration)
        return;

    int paramCount = db.GetParameterCount();
    if (paramCount == 0) {
        m_lastGeneration = gen;
        return;
    }

    // Annotations changed — rebuild
    SKSE::log::info("ENBGuiIntegration: annotations changed (gen {} -> {}), rebuilding bars",
        m_lastGeneration, gen);

    DestroyAnnotatedBars();
    BuildAnnotatedBars();
    m_lastGeneration = gen;
}

// ═══════════════════════════════════════════════════════════════════════════
//  DestroyAnnotatedBars
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::DestroyAnnotatedBars()
{
    // Delete bars FIRST (ATB stops reading from callback pointers)
    for (auto& ab : s_annotatedBars) {
        if (ab.bar && s_TwDeleteBar)
            s_TwDeleteBar(ab.bar);
    }
    s_annotatedBars.clear();
    // Now safe to free callback data
    s_cbStorage.clear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Update: sync data + manage bars (with try/catch safety)
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::Update(const AllData& data)
{
    if (!m_available) return;

    try {
        // Copy latest game state for AllData bar
        std::memcpy(&s_guiData, &data, sizeof(AllData));

        // Re-apply active overrides to game engine and patch s_guiData
        ApplyOverrides();

        // Create AllData bar when ATB is ready
        if (!m_barCreated) {
            EnsureBar();
            return;  // Don't do annotated work until base bar exists
        }

        // Create weather editor bar (once ATB is ready)
        if (!m_weatherBarCreated)
            EnsureWeatherEditorBar();
        else
            UpdateWeatherEditorBarLabel();

        // Check if AnnotationDatabase changed (shaders compiled/recompiled)
        RebuildAnnotatedBarsIfNeeded();

        // Only sync annotated params when ENB editor is open.
        // Uses ENBGetState(IsEditorActive) from SDK v1001+.
        bool editorActive = ENBInterface::IsEditorOpen();
        if (!editorActive)
            return;

        // Sync annotated param cached values from ENB
        if (ENBInterface::GetParameter) {
            for (auto& cb : s_cbStorage) {
                ENBInterface::ENBParameter outParam;
                if (ENBInterface::GetParameter(nullptr, cb->shader.c_str(),
                    cb->uiName.c_str(), &outParam) && outParam.Size > 0) {
                    int sz = (outParam.Size <= 16) ? static_cast<int>(outParam.Size) : 16;
                    std::memcpy(cb->value, outParam.Data, sz);
                }
            }
        }

        // Refresh UIBinding visibility (hidden/visible toggling)
        RefreshBindingState();

    } catch (const std::exception& e) {
        SKSE::log::error("ENBGuiIntegration::Update exception: {}", e.what());
    } catch (...) {
        SKSE::log::error("ENBGuiIntegration::Update unknown exception");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Weather Editor ATB Bar — read-write access to weather snapshot values
// ═══════════════════════════════════════════════════════════════════════════

// Callback context: pointer to a float inside WeatherEditor::GetSnapshot()
struct WECBFloat { float* ptr; };
static std::vector<std::unique_ptr<WECBFloat>> s_weCbStorage;

static void WE_GetFloat(void* value, void* clientData)
{
    *static_cast<float*>(value) = *static_cast<WECBFloat*>(clientData)->ptr;
}

static void WE_SetFloat(const void* value, void* clientData)
{
    *static_cast<WECBFloat*>(clientData)->ptr = *static_cast<const float*>(value);
    WeatherEditor::Get().MarkDirty();
}

static void WE_GetColor3(void* value, void* clientData)
{
    std::memcpy(value, static_cast<WECBFloat*>(clientData)->ptr, 12);
}

static void WE_SetColor3(const void* value, void* clientData)
{
    std::memcpy(static_cast<WECBFloat*>(clientData)->ptr, value, 12);
    WeatherEditor::Get().MarkDirty();
}

void ENBGuiIntegration::EnsureWeatherEditorBar()
{
    if (!m_available || !m_barCreated || m_weatherBarCreated) return;
    if (!s_TwNewBar || !s_TwAddVarCB) return;

    m_weatherBar = s_TwNewBar("SB_WeatherEditor");
    if (!m_weatherBar) return;

    s_TwDefine(" SB_WeatherEditor label='SB: Weather Editor' "
               " color='40 55 35' alpha=220 "
               " size='320 520' position='370 16' "
               " valueswidth=130 "
               " visible=false ");

    auto& snap = WeatherEditor::Get().GetSnapshot();

    // Helper lambdas to register params with persistent callback data
    auto addFloat = [&](const char* name, float* ptr, const char* def) {
        auto cb = std::make_unique<WECBFloat>();
        cb->ptr = ptr;
        s_TwAddVarCB(m_weatherBar, name, kTW_TYPE_FLOAT,
            WE_SetFloat, WE_GetFloat, cb.get(), def);
        s_weCbStorage.push_back(std::move(cb));
    };

    auto addColor3 = [&](const char* name, float* ptr, const char* def) {
        auto cb = std::make_unique<WECBFloat>();
        cb->ptr = ptr;
        s_TwAddVarCB(m_weatherBar, name, kTW_TYPE_COLOR3F,
            WE_SetColor3, WE_GetColor3, cb.get(), def);
        s_weCbStorage.push_back(std::move(cb));
    };

    // ── Fog group ──────────────────────────────────────────────────────
    addFloat("fogDayNear",   &snap.fogDayNear,   " group='Fog' label='Day Near'   min=0 max=100000 step=10 ");
    addFloat("fogDayFar",    &snap.fogDayFar,    " group='Fog' label='Day Far'    min=0 max=500000 step=50 ");
    addFloat("fogDayPower",  &snap.fogDayPower,  " group='Fog' label='Day Power'  min=0 max=10 step=0.01 ");
    addFloat("fogDayMax",    &snap.fogDayMax,    " group='Fog' label='Day Max'    min=0 max=1 step=0.01 ");
    addFloat("fogNightNear", &snap.fogNightNear, " group='Fog' label='Night Near' min=0 max=100000 step=10 ");
    addFloat("fogNightFar",  &snap.fogNightFar,  " group='Fog' label='Night Far'  min=0 max=500000 step=50 ");
    addFloat("fogNightPow",  &snap.fogNightPower," group='Fog' label='Night Power' min=0 max=10 step=0.01 ");
    addFloat("fogNightMax",  &snap.fogNightMax,  " group='Fog' label='Night Max'  min=0 max=1 step=0.01 ");

    // ── Weather group ──────────────────────────────────────────────────
    addFloat("windSpeed",    &snap.windSpeed,     " group='Weather' label='Wind Speed'  min=0 max=1 step=0.01 ");
    addFloat("windDir",      &snap.windDirection, " group='Weather' label='Wind Dir'    min=0 max=1 step=0.01 ");
    addFloat("transDelta",   &snap.transDelta,    " group='Weather' label='Trans Delta' min=0 max=1 step=0.01 ");
    addFloat("sunGlareVal",  &snap.sunGlare,      " group='Weather' label='Sun Glare'   min=0 max=1 step=0.01 ");
    addFloat("sunDmg",       &snap.sunDamage,     " group='Weather' label='Sun Damage'  min=0 max=1 step=0.01 ");
    addFloat("precipIn",     &snap.precipBeginFadeIn,  " group='Weather' label='Precip In'  min=0 max=1 step=0.01 ");
    addFloat("precipOut",    &snap.precipEndFadeOut,   " group='Weather' label='Precip Out' min=0 max=1 step=0.01 ");
    addFloat("thunderFreq",  &snap.thunderFrequency,   " group='Weather' label='Thunder Freq' min=0 max=1 step=0.01 ");

    // ── Day Colors group (ToD index 1 = Day) ───────────────────────────
    addColor3("skyUpperDay", snap.colors[kSkyUpper][kDay].Ptr(),
              " group='Day Colors' label='Sky Upper' ");
    addColor3("skyLowerDay", snap.colors[kSkyLower][kDay].Ptr(),
              " group='Day Colors' label='Sky Lower' ");
    addColor3("horizonDay",  snap.colors[kHorizon][kDay].Ptr(),
              " group='Day Colors' label='Horizon' ");
    addColor3("sunlightDay", snap.colors[kSunlight][kDay].Ptr(),
              " group='Day Colors' label='Sunlight' ");
    addColor3("ambientDay",  snap.colors[kAmbient][kDay].Ptr(),
              " group='Day Colors' label='Ambient' ");
    addColor3("fogNearDay",  snap.colors[kFogNear][kDay].Ptr(),
              " group='Day Colors' label='Fog Near' ");
    addColor3("fogFarDay",   snap.colors[kFogFar][kDay].Ptr(),
              " group='Day Colors' label='Fog Far' ");

    // Collapse groups by default
    s_TwDefine(" SB_WeatherEditor/'Fog'         opened=true ");
    s_TwDefine(" SB_WeatherEditor/'Weather'     opened=false ");
    s_TwDefine(" SB_WeatherEditor/'Day Colors'  opened=false ");

    m_weatherBarCreated = true;
    SKSE::log::info("ENBGuiIntegration: weather editor bar created ({} params)",
        s_weCbStorage.size());
}

void ENBGuiIntegration::UpdateWeatherEditorBarLabel()
{
    if (!m_weatherBar || !s_TwDefine) return;

    auto& editor = WeatherEditor::Get();
    if (!editor.IsActive()) return;

    auto formID = editor.GetSnapshot().formID;
    if (formID == m_lastWeatherBarID) return;
    m_lastWeatherBarID = formID;

    char style[256];
    snprintf(style, sizeof(style),
        " SB_WeatherEditor label='SB: Weather — %s' ",
        editor.GetSnapshot().editorID.c_str());
    s_TwDefine(style);
}

void ENBGuiIntegration::DestroyWeatherEditorBar()
{
    if (m_weatherBar && s_TwDeleteBar) {
        s_TwDeleteBar(m_weatherBar);
        m_weatherBar = nullptr;
    }
    s_weCbStorage.clear();
    m_weatherBarCreated = false;
    m_lastWeatherBarID = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void ENBGuiIntegration::Shutdown()
{
    DestroyWeatherEditorBar();
    DestroyAnnotatedBars();

    for (int i = 0; i < kCategoryBarCount; ++i) {
        if (m_categoryBars[i] && s_TwDeleteBar) {
            s_TwDeleteBar(m_categoryBars[i]);
            m_categoryBars[i] = nullptr;
        }
    }
    m_bar = nullptr;
    m_barCreated = false;
}

} // namespace SB
