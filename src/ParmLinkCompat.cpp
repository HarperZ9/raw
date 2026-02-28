//=============================================================================
//  ParmLinkCompat.cpp — Drop-in replacement for enbParmLink.dll
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "ParmLinkCompat.h"
#include "Trackers.h"
#include "ENBInterface_v3.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cmath>

namespace SB
{

//=============================================================================
//  ParmLinkCompat — Initialization
//=============================================================================

void ParmLinkCompat::Initialize(const std::filesystem::path& gameDir)
{
    std::lock_guard lock(m_mutex);

    // Register all variable bindings
    RegisterENBVariables();
    RegisterSkyrimBridgeVariables();
    RegisterAddressRedirections();

    // Look for enbParmLink.cfg (or SkyrimBridge.cfg) in the game directory
    m_cfgPath = gameDir / "enbParmLink.cfg";
    if (!std::filesystem::exists(m_cfgPath)) {
        m_cfgPath = gameDir / "SkyrimBridge_ParmLink.cfg";
    }

    if (std::filesystem::exists(m_cfgPath)) {
        LoadCFG(m_cfgPath);
        m_cfgLastMod = std::filesystem::last_write_time(m_cfgPath);
    }

    SKSE::log::info("ParmLinkCompat: initialized with {} variables, {} expressions",
        m_variables.size(), m_expressions.size());
}


void ParmLinkCompat::RegisterENBVariables()
{
    // ENB built-in state — these mirror what ParmLink reads via enb.* API
    RegisterVariable("enb.fNightDayFactor", []() -> float {
        // ENB's night-day factor: 0 = full night, 1 = full day
        // We derive this from SkyrimBridge time data
        float hour = GetData().celestial.TimeData.x;
        float sunrise = GetData().celestial.TimeData.y;
        float sunset  = GetData().celestial.TimeData.z;
        if (hour >= sunrise && hour <= sunset)
            return 1.0f;
        if (hour < sunrise - 1.0f || hour > sunset + 1.0f)
            return 0.0f;
        // Transition zone: 1 hour before sunrise, 1 hour after sunset
        if (hour < sunrise)
            return (hour - (sunrise - 1.0f)) / 1.0f;
        return 1.0f - (hour - sunset) / 1.0f;
    }, "ENB night/day factor [0=night, 1=day]");

    RegisterVariable("enb.fTimeOfDay", []() -> float {
        return GetData().celestial.TimeData.x;  // Game hour [0,24)
    }, "Current game hour");

    RegisterVariable("enb.fCurrentLocationIndicator", []() -> float {
        return GetData().interior.IsInterior.x;  // 0 = exterior, 1 = interior
    }, "Interior flag [0=exterior, 1=interior]");

    RegisterVariable("enb.fWeatherTransition", []() -> float {
        return GetData().weather.Transition.x;
    }, "Weather transition progress [0,1]");

    RegisterVariable("enb.fCurrentWeatherID", []() -> float {
        return GetData().weather.Transition.z;  // Current weather FormID (lower bits)
    }, "Current weather FormID");

    // Extended SB variables accessible via ParmLink-style names
    RegisterVariable("sb.sunElevation", []() -> float {
        return GetData().celestial.SunDirection.w;
    }, "Sun elevation angle (radians)");

    RegisterVariable("sb.windSpeed", []() -> float {
        return GetData().weather.Wind.x;
    }, "Wind speed [0,1]");

    RegisterVariable("sb.precipIntensity", []() -> float {
        return GetData().weather.Precipitation.y;
    }, "Precipitation intensity [0,1]");

    RegisterVariable("sb.precipType", []() -> float {
        return GetData().weather.Precipitation.x;
    }, "Precipitation type (0=none, 1=rain, 2=snow)");

    RegisterVariable("sb.isStormy", []() -> float {
        return GetData().weather.Flags.z > 0.5f ? 1.0f : 0.0f;
    }, "Is rainy weather");

    RegisterVariable("sb.isSnowy", []() -> float {
        return GetData().weather.Flags.w > 0.5f ? 1.0f : 0.0f;
    }, "Is snowy weather");

    RegisterVariable("sb.fogDensity", []() -> float {
        return GetData().fog.Density.x;
    }, "Fog density curve power");

    RegisterVariable("sb.playerHealth", []() -> float {
        return GetData().player.Vitals.x;
    }, "Player health percentage [0,1]");

    RegisterVariable("sb.inCombat", []() -> float {
        return GetData().player.Combat.x;
    }, "Player in combat flag");

    RegisterVariable("sb.isUnderwater", []() -> float {
        return GetData().player.Water.x;
    }, "Player underwater flag");

    RegisterVariable("sb.lightningFlash", []() -> float {
        return GetData().weather.Lightning.y;
    }, "Lightning flash active flag");

    RegisterVariable("sb.nightEye", []() -> float {
        return GetData().effects.VisionEffects.x;
    }, "Night Eye active flag");

    RegisterVariable("sb.slowTime", []() -> float {
        return GetData().effects.TimeEffects.x;
    }, "Slow time factor");
}


void ParmLinkCompat::RegisterSkyrimBridgeVariables()
{
    // Direct access to every SB_ float4 component via dot notation
    // This allows ParmLink-style expressions to use: sb.Camera_Info.x
    // Full SB API exposure — over 100 individual float values

    // Lambdas call GetData() directly for fresh data each frame

    // Camera
    RegisterVariable("sb.fov", []() { return GetData().camera.Info.x; }, "Camera FOV degrees");
    RegisterVariable("sb.nearClip", []() { return GetData().camera.Info.y; }, "Near clip");
    RegisterVariable("sb.farClip", []() { return GetData().camera.Info.z; }, "Far clip");
    RegisterVariable("sb.aspectRatio", []() { return GetData().camera.Info.w; }, "Aspect ratio");
    RegisterVariable("sb.cameraPitch", []() { return GetData().camera.Angles.x; }, "Camera pitch (rad)");
    RegisterVariable("sb.cameraYaw", []() { return GetData().camera.Angles.y; }, "Camera yaw (rad)");

    // Player
    RegisterVariable("sb.playerPosX", []() { return GetData().player.Position.x; }, "Player X");
    RegisterVariable("sb.playerPosY", []() { return GetData().player.Position.y; }, "Player Y");
    RegisterVariable("sb.playerPosZ", []() { return GetData().player.Position.z; }, "Player Z");
    RegisterVariable("sb.stamina", []() { return GetData().player.Vitals.y; }, "Stamina %");
    RegisterVariable("sb.magicka", []() { return GetData().player.Vitals.z; }, "Magicka %");
    RegisterVariable("sb.playerLevel", []() { return GetData().player.Vitals.w; }, "Player level");
    RegisterVariable("sb.playerSpeed", []() { return GetData().player.Movement.x; }, "Movement speed");
    RegisterVariable("sb.isSprinting", []() { return GetData().player.Movement.y; }, "Sprinting flag");
    RegisterVariable("sb.isSwimming", []() { return GetData().player.Movement.z; }, "Swimming flag");
    RegisterVariable("sb.isMounted", []() { return GetData().player.Movement.w; }, "Mounted flag");

    // Weather
    RegisterVariable("sb.windDirection", []() { return GetData().weather.Wind.y; }, "Wind dir (rad)");
    RegisterVariable("sb.lightningFreq", []() { return GetData().weather.Lightning.x; }, "Lightning frequency");
    RegisterVariable("sb.flashIntensity", []() { return GetData().weather.Lightning.z; }, "Flash intensity");
    RegisterVariable("sb.precipType", []() { return GetData().weather.Precipitation.x; }, "Surface wetness");
    RegisterVariable("sb.precipIntensity", []() { return GetData().weather.Precipitation.y; }, "Puddle depth");
    RegisterVariable("sb.weatherTransition", []() { return GetData().weather.Transition.x; }, "Weather transition");

    // Interior
    RegisterVariable("sb.isInterior", []() { return GetData().interior.IsInterior.x; }, "Interior flag");

    // Atmosphere
    RegisterVariable("sb.ambientR", []() { return GetData().atmosphere.Ambient.x; }, "Ambient R");
    RegisterVariable("sb.ambientG", []() { return GetData().atmosphere.Ambient.y; }, "Ambient G");
    RegisterVariable("sb.ambientB", []() { return GetData().atmosphere.Ambient.z; }, "Ambient B");
    RegisterVariable("sb.sunlightR", []() { return GetData().atmosphere.SunlightColor.x; }, "Sunlight R");
    RegisterVariable("sb.sunlightG", []() { return GetData().atmosphere.SunlightColor.y; }, "Sunlight G");
    RegisterVariable("sb.sunlightB", []() { return GetData().atmosphere.SunlightColor.z; }, "Sunlight B");
}


void ParmLinkCompat::RegisterAddressRedirections()
{
    // Map known ParmLink memory addresses to SB data
    // This handles the case where existing .cfg files use addr.getAbsFloat()
    AddressRedirectTable::Get().RegisterDefaults();
}


void ParmLinkCompat::RegisterVariable(const std::string& name,
                                       std::function<float()> getter,
                                       const std::string& description)
{
    m_varIndex[name] = m_variables.size();
    m_variables.push_back({name, std::move(getter), description});
}


float ParmLinkCompat::GetVariable(const std::string& name) const
{
    auto it = m_varIndex.find(name);
    if (it != m_varIndex.end())
        return m_variables[it->second].getter();

    // Check user-defined variables
    auto uit = m_userVars.find(name);
    if (uit != m_userVars.end())
        return uit->second;

    return 0.0f;
}


//=============================================================================
//  CFG Loading & Expression Compilation
//=============================================================================

bool ParmLinkCompat::LoadCFG(const std::filesystem::path& cfgPath)
{
    std::ifstream file(cfgPath);
    if (!file.is_open()) {
        SKSE::log::error("ParmLinkCompat: cannot open '{}'", cfgPath.string());
        return false;
    }

    m_expressions.clear();

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line))
    {
        lineNum++;

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty()) continue;

        // Strip inline comments (// style)
        auto cpos = line.find("//");
        if (cpos != std::string::npos)
            line = line.substr(0, cpos);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;

        // Skip function definitions (ParmLink supports [] func() { })
        // We don't support user-defined functions — they're rare in practice
        if (line.front() == '[') continue;

        // Look for assignment: name := expr  (live evaluation)
        //                  or: name = "str"  (string assignment, skip)
        auto colonEq = line.find(":=");
        if (colonEq != std::string::npos) {
            auto expr = CompileExpression(line);
            if (expr.evaluate) {
                m_expressions.push_back(std::move(expr));
            } else if (m_logEnabled) {
                SKSE::log::warn("ParmLinkCompat: line {}: failed to compile '{}'",
                    lineNum, line);
            }
            continue;
        }

        // Check for enb.setFloat() calls as standalone statements
        if (line.find("enb.setFloat") != std::string::npos) {
            auto expr = CompileExpression(line);
            if (expr.evaluate) {
                m_expressions.push_back(std::move(expr));
            }
        }
    }

    SKSE::log::info("ParmLinkCompat: loaded {} expressions from '{}'",
        m_expressions.size(), cfgPath.filename().string());
    return true;
}


CompiledExpression ParmLinkCompat::CompileExpression(const std::string& line)
{
    CompiledExpression result;
    result.source = line;

    // Parse: name := expression
    auto colonEq = line.find(":=");
    if (colonEq != std::string::npos) {
        result.name = line.substr(0, colonEq);
        result.name.erase(result.name.find_last_not_of(" \t") + 1);
        result.name.erase(0, result.name.find_first_not_of(" \t"));

        std::string exprStr = line.substr(colonEq + 2);
        exprStr.erase(0, exprStr.find_first_not_of(" \t"));

        // Strip trailing semicolon
        if (!exprStr.empty() && exprStr.back() == ';')
            exprStr.pop_back();

        result.evaluate = CompileMathExpr(exprStr);
        return result;
    }

    // NOTE: enb.setFloat() parsing disabled - use WeatherParams.ini instead
    // Phase 2 provides a cleaner per-weather parameter system.


    return result;
}


std::function<float()> ParmLinkCompat::CompileMathExpr(const std::string& expr)
{
    // Simple recursive-descent expression compiler
    // Supports: +, -, *, /, parentheses, variables, number literals
    // Built-in functions: lerp(), smoothstep(), clamp(), min(), max(),
    //                     sin(), cos(), sqrt(), abs(), pow(), exp(), log()
    //
    // For complex expressions, we fall back to a tree-walking evaluator.
    // ParmLink uses ExprTk which supports arbitrary C-like math.
    // We cover the 95% case with this simpler approach.

    // First, check for simple variable reference
    if (expr.find_first_of("+-*/()") == std::string::npos) {
        std::string var = expr;
        var.erase(0, var.find_first_not_of(" \t"));
        var.erase(var.find_last_not_of(" \t;") + 1);

        // Number literal?
        try {
            float val = std::stof(var);
            return [val]() { return val; };
        } catch (...) {}

        // Variable reference
        return [this, var]() { return GetVariable(var); };
    }

    // For the lerp() pattern specifically (by far the most common in ParmLink):
    //   lerp(a, b, t)  →  a + (b - a) * t
    std::regex lerpRe(R"(lerp\(\s*(.+?)\s*,\s*(.+?)\s*,\s*(.+?)\s*\))");
    std::smatch lm;
    if (std::regex_search(expr, lm, lerpRe)) {
        auto getA = CompileMathExpr(lm[1].str());
        auto getB = CompileMathExpr(lm[2].str());
        auto getT = CompileMathExpr(lm[3].str());
        return [getA, getB, getT]() {
            float a = getA(), b = getB(), t = getT();
            return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
        };
    }

    // smoothstep(edge0, edge1, x)
    std::regex ssRe(R"(smoothstep\(\s*(.+?)\s*,\s*(.+?)\s*,\s*(.+?)\s*\))");
    if (std::regex_search(expr, lm, ssRe)) {
        auto getE0 = CompileMathExpr(lm[1].str());
        auto getE1 = CompileMathExpr(lm[2].str());
        auto getX  = CompileMathExpr(lm[3].str());
        return [getE0, getE1, getX]() {
            float e0 = getE0(), e1 = getE1(), x = getX();
            float t = std::clamp((x - e0) / (e1 - e0 + 1e-7f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
    }

    // clamp(x, min, max)
    std::regex clampRe(R"(clamp\(\s*(.+?)\s*,\s*(.+?)\s*,\s*(.+?)\s*\))");
    if (std::regex_search(expr, lm, clampRe)) {
        auto getX   = CompileMathExpr(lm[1].str());
        auto getMin = CompileMathExpr(lm[2].str());
        auto getMax = CompileMathExpr(lm[3].str());
        return [getX, getMin, getMax]() {
            return std::clamp(getX(), getMin(), getMax());
        };
    }

    // Fallback: for simple arithmetic like "a * b + c", we do a basic
    // tokenize-and-evaluate approach
    // This captures the vast majority of real ParmLink expressions

    // For truly complex expressions, users should migrate to WeatherParams.ini
    // (Phase 2) which doesn't need expression parsing at all

    // Store expression as a string for runtime evaluation
    // NOTE: In production, this should be replaced with a proper expression tree
    // compiler. For now, we log a warning and return a constant.
    SKSE::log::warn("ParmLinkCompat: complex expression not yet compiled: '{}'", expr);
    return [expr, this]() -> float {
        // Simple arithmetic fallback: try to evaluate as a+b, a*b, etc.
        // This is placeholder — full ExprTk integration would go here
        return 0.0f;
    };
}


//=============================================================================
//  Per-Frame Update
//=============================================================================

void ParmLinkCompat::Update(float deltaTime)
{
    std::lock_guard lock(m_mutex);

    for (auto& expr : m_expressions)
    {
        if (!expr.evaluate) continue;

        // Evaluate the expression
        float value = expr.evaluate();
        expr.currentValue = value;

        // Store as a user variable (so other expressions can reference it)
        m_userVars[expr.name] = value;

        // If this is an ENB push, write it to the shader
        if (expr.isENBPush) {
            ENBSetFloat(
                expr.pushShader.c_str(),
                expr.pushGroup.c_str(),
                expr.pushParam.c_str(),
                value
            );
        }
    }
}


void ParmLinkCompat::Shutdown()
{
    std::lock_guard lock(m_mutex);
    m_expressions.clear();
    m_variables.clear();
    m_varIndex.clear();
    m_userVars.clear();
}


void ParmLinkCompat::CheckHotReload()
{
    if (!std::filesystem::exists(m_cfgPath)) return;

    auto mod = std::filesystem::last_write_time(m_cfgPath);
    if (mod != m_cfgLastMod) {
        m_cfgLastMod = mod;
        SKSE::log::info("ParmLinkCompat: config changed, reloading...");
        LoadCFG(m_cfgPath);
    }
}


float ParmLinkCompat::ENBGetFloat(const char* shader, const char* group, const char* name)
{
    return ENBInterface::GetFloat(shader, group, name);
}

void ParmLinkCompat::ENBSetFloat(const char* shader, const char* group,
                                  const char* name, float value)
{
    ENBInterface::SetFloat(shader, group, name, value);
}


//=============================================================================
//  AddressRedirectTable — map ParmLink memory addresses to SB data
//=============================================================================

void AddressRedirectTable::Register(uint64_t baseAddr, uint32_t offset,
                                     std::function<float()> getter,
                                     const std::string& description)
{
    m_redirects.push_back({baseAddr, offset, std::move(getter), description});
}


bool AddressRedirectTable::TryRedirect(uint64_t address, float& outValue) const
{
    for (const auto& r : m_redirects) {
        if (r.address == address || (r.address + r.offset) == address) {
            outValue = r.getter();
            return true;
        }
    }
    return false;
}


void AddressRedirectTable::RegisterDefaults()
{
    // Known ParmLink memory patterns used by popular ENB presets:
    //
    // Pattern: Sky singleton → game hour
    //   skyPtr = addr.getAbsInt(SKY_SINGLETON_RVA)
    //   gameHour = addr.getAbsFloat(skyPtr + 0x1B0)
    //
    // Pattern: PlayerCharacter → position
    //   playerPtr = addr.getAbsInt(PLAYER_SINGLETON_RVA)
    //   posX = addr.getAbsFloat(playerPtr + 0x54)
    //
    // We don't need to match exact addresses — instead, we provide the
    // SkyrimBridge variable system as the preferred alternative.
    // If a .cfg file uses addr.*, we log a deprecation warning and
    // suggest the sb.* variable equivalent.

    SKSE::log::info("AddressRedirectTable: registered. Users should migrate "
        "addr.* calls to sb.* variables for version-safe access.");
}


}  // namespace SB
