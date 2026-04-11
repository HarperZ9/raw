#include "WeatherSeparationEngine.h"
#include "ENBInterface.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace SB
{

WeatherSeparationEngine& WeatherSeparationEngine::Get()
{
    static WeatherSeparationEngine inst;
    return inst;
}


void WeatherSeparationEngine::Initialize(const std::filesystem::path& configDir)
{
    m_configDir = configDir / "WeatherSep";

    // Create directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(m_configDir, ec);

    // Query AnnotationDatabase for separated parameters
    auto& db = AnnotationDatabase::Get();
    m_separatedParams = db.GetSeparatedParameters();
    m_separatedCount = static_cast<int>(m_separatedParams.size());

    if (m_separatedCount > 0) {
        SKSE::log::info("WeatherSeparationEngine: {} separated parameters found, INI dir: {}",
                        m_separatedCount, m_configDir.string());
    }
}


int WeatherSeparationEngine::GetLoadedWeatherCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<int>(m_weatherINIs.size());
}


// ═══════════════════════════════════════════════════════════════════════════
//  Time-of-Day resolution
// ═══════════════════════════════════════════════════════════════════════════

void WeatherSeparationEngine::ResolveToD4(float gameHour,
                                           ToD4& slotA, ToD4& slotB, float& blend)
{
    // ENB native 4-slot boundaries:
    //   Night:   20:00 - 06:00
    //   Morning: 06:00 - 12:00
    //   Day:     12:00 - 17:00
    //   Sunset:  17:00 - 20:00

    if (gameHour < 6.0f) {
        slotA = ToD4::Night; slotB = ToD4::Morning;
        blend = gameHour / 6.0f;
    } else if (gameHour < 12.0f) {
        slotA = ToD4::Morning; slotB = ToD4::Day;
        blend = (gameHour - 6.0f) / 6.0f;
    } else if (gameHour < 17.0f) {
        slotA = ToD4::Day; slotB = ToD4::Sunset;
        blend = (gameHour - 12.0f) / 5.0f;
    } else if (gameHour < 20.0f) {
        slotA = ToD4::Sunset; slotB = ToD4::Night;
        blend = (gameHour - 17.0f) / 3.0f;
    } else {
        slotA = ToD4::Night; slotB = ToD4::Night;
        blend = 0.0f;
    }

    // Smoothstep for natural transitions
    blend = blend * blend * (3.0f - 2.0f * blend);
}


void WeatherSeparationEngine::ResolveToD6(float gameHour,
                                           ToD6& slotA, ToD6& slotB, float& blend)
{
    // SB 6-slot boundaries:
    //   Night:   21:00 - 04:00
    //   Dawn:    04:00 - 06:00
    //   Sunrise: 06:00 - 08:00
    //   Day:     08:00 - 16:00
    //   Sunset:  16:00 - 19:00
    //   Dusk:    19:00 - 21:00

    if (gameHour < 4.0f) {
        slotA = ToD6::Night; slotB = ToD6::Dawn;
        blend = gameHour / 4.0f;
    } else if (gameHour < 6.0f) {
        slotA = ToD6::Dawn; slotB = ToD6::Sunrise;
        blend = (gameHour - 4.0f) / 2.0f;
    } else if (gameHour < 8.0f) {
        slotA = ToD6::Sunrise; slotB = ToD6::Day;
        blend = (gameHour - 6.0f) / 2.0f;
    } else if (gameHour < 16.0f) {
        slotA = ToD6::Day; slotB = ToD6::Sunset;
        blend = (gameHour - 8.0f) / 8.0f;
    } else if (gameHour < 19.0f) {
        slotA = ToD6::Sunset; slotB = ToD6::Dusk;
        blend = (gameHour - 16.0f) / 3.0f;
    } else if (gameHour < 21.0f) {
        slotA = ToD6::Dusk; slotB = ToD6::Night;
        blend = (gameHour - 19.0f) / 2.0f;
    } else {
        slotA = ToD6::Night; slotB = ToD6::Night;
        blend = 0.0f;
    }

    blend = blend * blend * (3.0f - 2.0f * blend);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Value computation
// ═══════════════════════════════════════════════════════════════════════════

float WeatherSeparationEngine::ComputeValue(const SeparatedValue& sv, float gameHour) const
{
    if (m_slotMode == ToDSlotMode::SixSlot) {
        ToD6 a, b;
        float blend;
        ResolveToD6(gameHour, a, b, blend);
        float va = sv.values6[static_cast<int>(a)];
        float vb = sv.values6[static_cast<int>(b)];
        return va + (vb - va) * blend;
    } else {
        ToD4 a, b;
        float blend;
        ResolveToD4(gameHour, a, b, blend);
        float va = sv.values4[static_cast<int>(a)];
        float vb = sv.values4[static_cast<int>(b)];
        return va + (vb - va) * blend;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  INI file management
// ═══════════════════════════════════════════════════════════════════════════

WeatherINI& WeatherSeparationEngine::GetOrLoadWeatherINI(uint32_t weatherFormID)
{
    auto it = m_weatherINIs.find(weatherFormID);
    if (it != m_weatherINIs.end())
        return it->second;

    auto& ini = m_weatherINIs[weatherFormID];
    ini.weatherFormID = weatherFormID;
    LoadWeatherINI(weatherFormID, ini);
    return ini;
}


void WeatherSeparationEngine::LoadWeatherINI(uint32_t weatherFormID, WeatherINI& out)
{
    // INI path: WeatherSep/<FormID:08X>.ini
    char filename[32];
    snprintf(filename, sizeof(filename), "%08X.ini", weatherFormID);
    auto path = m_configDir / filename;

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        // Trim
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // Section header: [ShaderFile/ParamKey]
        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos)
                currentSection = line.substr(1, end - 1);
            continue;
        }

        // Key=Value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim key/value
        auto kend = key.find_last_not_of(" \t");
        if (kend != std::string::npos) key = key.substr(0, kend + 1);
        auto vstart = val.find_first_not_of(" \t");
        if (vstart != std::string::npos) val = val.substr(vstart);

        if (currentSection.empty()) continue;

        auto& sv = out.params[currentSection];
        sv.paramKey = currentSection;

        // Parse slot values: Morning=0.5, Day=1.0, etc.
        // 4-slot
        if (key == "Morning")  sv.values4[0] = std::stof(val);
        else if (key == "Day")      sv.values4[1] = std::stof(val);
        else if (key == "Sunset")   sv.values4[2] = std::stof(val);
        else if (key == "Night")    sv.values4[3] = std::stof(val);
        // 6-slot
        else if (key == "Dawn")     sv.values6[0] = std::stof(val);
        else if (key == "Sunrise")  sv.values6[1] = std::stof(val);
        else if (key == "Day6")     sv.values6[2] = std::stof(val);
        else if (key == "Sunset6")  sv.values6[3] = std::stof(val);
        else if (key == "Dusk")     sv.values6[4] = std::stof(val);
        else if (key == "Night6")   sv.values6[5] = std::stof(val);
        // Mode
        else if (key == "SlotMode") sv.slotMode = (val == "6") ? ToDSlotMode::SixSlot : ToDSlotMode::FourSlot;
        // Shader file
        else if (key == "Shader")   sv.shaderFile = val;
    }
}


void WeatherSeparationEngine::WriteWeatherINI(uint32_t weatherFormID, const WeatherINI& ini)
{
    char filename[32];
    snprintf(filename, sizeof(filename), "%08X.ini", weatherFormID);
    auto path = m_configDir / filename;

    std::ofstream file(path);
    if (!file.is_open()) {
        SKSE::log::error("WeatherSeparationEngine: failed to write {}", path.string());
        return;
    }

    file << "; SkyrimBridge Weather Separation — FormID 0x"
         << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
         << weatherFormID << std::dec << "\n";
    file << "; Auto-generated. Edit to override per-weather parameter values.\n\n";

    for (const auto& [paramKey, sv] : ini.params) {
        file << "[" << paramKey << "]\n";
        file << "Shader=" << sv.shaderFile << "\n";
        file << "SlotMode=" << (sv.slotMode == ToDSlotMode::SixSlot ? "6" : "4") << "\n";

        // 4-slot
        file << "Morning=" << sv.values4[0] << "\n";
        file << "Day=" << sv.values4[1] << "\n";
        file << "Sunset=" << sv.values4[2] << "\n";
        file << "Night=" << sv.values4[3] << "\n";

        // 6-slot
        file << "Dawn=" << sv.values6[0] << "\n";
        file << "Sunrise=" << sv.values6[1] << "\n";
        file << "Day6=" << sv.values6[2] << "\n";
        file << "Sunset6=" << sv.values6[3] << "\n";
        file << "Dusk=" << sv.values6[4] << "\n";
        file << "Night6=" << sv.values6[5] << "\n";

        file << "\n";
    }
}


void WeatherSeparationEngine::SaveWeatherINI(uint32_t weatherFormID)
{
    std::lock_guard lock(m_mutex);
    auto it = m_weatherINIs.find(weatherFormID);
    if (it != m_weatherINIs.end() && it->second.dirty) {
        WriteWeatherINI(weatherFormID, it->second);
        it->second.dirty = false;
    }
}


void WeatherSeparationEngine::SaveAllDirty()
{
    for (auto& [formID, ini] : m_weatherINIs) {
        if (ini.dirty) {
            WriteWeatherINI(formID, ini);
            ini.dirty = false;
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame update
// ═══════════════════════════════════════════════════════════════════════════

void WeatherSeparationEngine::Update(float gameHour, float transitionPct,
                                      uint32_t currentWeatherID, uint32_t prevWeatherID,
                                      bool isExterior)
{
    if (!ENBInterface::SetParameter) return;

    // Refresh separated params list periodically (new shaders may have compiled)
    auto& db = AnnotationDatabase::Get();
    int dbSepCount = db.GetSeparatedCount();
    if (dbSepCount != m_separatedCount) {
        m_separatedParams = db.GetSeparatedParameters();
        m_separatedCount = static_cast<int>(m_separatedParams.size());
    }

    if (m_separatedCount == 0) return;

    std::lock_guard lock(m_mutex);

    // Auto-save dirty INIs when weather changes
    if (currentWeatherID != m_lastCurrentWeatherID && m_lastCurrentWeatherID != 0) {
        SaveAllDirty();
        SKSE::log::debug("WeatherSeparationEngine: weather changed 0x{:08X} -> 0x{:08X}, saved dirty INIs",
                         m_lastCurrentWeatherID, currentWeatherID);
    }
    m_lastCurrentWeatherID = currentWeatherID;

    // Periodic auto-save every ~60 seconds (3600 frames at 60fps)
    if (++m_saveCountdown >= 3600) {
        m_saveCountdown = 0;
        SaveAllDirty();
    }

    // Load weather INIs for current and previous weather
    auto& currentINI = GetOrLoadWeatherINI(currentWeatherID);
    auto& prevINI = GetOrLoadWeatherINI(prevWeatherID);

    ENBInterface::ENBParameter param;
    param.Size = sizeof(float);
    param.Type = ENBInterface::ENBParameterType::ENBParam_FLOAT;

    for (const auto* meta : m_separatedParams) {
        // Skip ExteriorWeather params when inside
        if (meta->separation == ParameterMeta::Separation::ExteriorWeather && !isExterior)
            continue;

        std::string key = meta->GetUniqueKey();

        // Initialize default values in INI if not present
        auto ensureParam = [&](WeatherINI& ini) -> SeparatedValue& {
            auto it = ini.params.find(key);
            if (it == ini.params.end()) {
                auto& sv = ini.params[key];
                sv.paramKey = key;
                sv.shaderFile = meta->shaderFile;
                sv.slotMode = m_slotMode;
                // Initialize all slots with the default value
                float def = meta->defaultFloat[0];
                for (int i = 0; i < kToD4Count; ++i) sv.values4[i] = def;
                for (int i = 0; i < kToD6Count; ++i) sv.values6[i] = def;
                ini.dirty = true;
                return sv;
            }
            return it->second;
        };

        auto& currentSV = ensureParam(currentINI);
        auto& prevSV = ensureParam(prevINI);

        // Compute ToD-interpolated values for current and previous weather
        float currentVal = ComputeValue(currentSV, gameHour);
        float prevVal = ComputeValue(prevSV, gameHour);

        // Interpolate between previous and current weather
        float finalVal = prevVal + (currentVal - prevVal) * transitionPct;

        m_currentValues[key] = finalVal;

        // Push to ENB via the parameter's UIName
        std::memcpy(param.Data, &finalVal, sizeof(float));

        const char* keyName = meta->uiName.empty()
            ? meta->varName.c_str()
            : meta->uiName.c_str();

        std::string shaderUpper = meta->shaderFile;
        for (auto& c : shaderUpper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

        ENBInterface::SetParameter(nullptr, shaderUpper.c_str(), keyName, &param);
    }
}


void WeatherSeparationEngine::SetValue(const std::string& paramKey, float value)
{
    std::lock_guard lock(m_mutex);
    m_currentValues[paramKey] = value;
}


float WeatherSeparationEngine::GetValue(const std::string& paramKey) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_currentValues.find(paramKey);
    return (it != m_currentValues.end()) ? it->second : 0.0f;
}

} // namespace SB
