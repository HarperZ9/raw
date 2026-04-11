//=============================================================================
//  ConfigManager.cpp — ENB Configuration System Implementation
//=============================================================================

#include "ConfigManager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

ConfigManager g_Config;

// ═══════════════════════════════════════════════════════════════════════════
//  INI Helpers (Win32 PrivateProfile wrappers)
// ═══════════════════════════════════════════════════════════════════════════

int ConfigManager::ReadInt(const char* file, const char* section, const char* key, int def)
{
    return GetPrivateProfileIntA(section, key, def, file);
}

float ConfigManager::ReadFloat(const char* file, const char* section, const char* key, float def)
{
    char buf[64];
    char defStr[64];
    snprintf(defStr, sizeof(defStr), "%.6g", def);
    GetPrivateProfileStringA(section, key, defStr, buf, sizeof(buf), file);
    return static_cast<float>(atof(buf));
}

bool ConfigManager::ReadBool(const char* file, const char* section, const char* key, bool def)
{
    char buf[32];
    GetPrivateProfileStringA(section, key, def ? "true" : "false", buf, sizeof(buf), file);
    return (_stricmp(buf, "true") == 0 || _stricmp(buf, "1") == 0 || _stricmp(buf, "yes") == 0);
}

std::string ConfigManager::ReadString(const char* file, const char* section, const char* key, const char* def)
{
    char buf[512];
    GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), file);
    return buf;
}

void ConfigManager::ReadColor3(const char* file, const char* section, const char* key, float out[3])
{
    char buf[128];
    GetPrivateProfileStringA(section, key, "1, 1, 1", buf, sizeof(buf), file);
    // Parse "R, G, B" format
    out[0] = out[1] = out[2] = 1.0f;
    sscanf(buf, "%f, %f, %f", &out[0], &out[1], &out[2]);
}

void ConfigManager::WriteFloat(const char* file, const char* section, const char* key, float val)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6g", val);
    WritePrivateProfileStringA(section, key, buf, file);
}

void ConfigManager::WriteBool(const char* file, const char* section, const char* key, bool val)
{
    WritePrivateProfileStringA(section, key, val ? "true" : "false", file);
}

void ConfigManager::WriteString(const char* file, const char* section, const char* key, const char* val)
{
    WritePrivateProfileStringA(section, key, val, file);
}

TODParameter ConfigManager::ReadTODFloat(const char* file, const char* section, const char* baseName, float def)
{
    TODParameter p;
    char key[256];
    snprintf(key, sizeof(key), "%sDawn",    baseName); p.dawn    = ReadFloat(file, section, key, def);
    snprintf(key, sizeof(key), "%sSunrise", baseName); p.sunrise = ReadFloat(file, section, key, def);
    snprintf(key, sizeof(key), "%sDay",     baseName); p.day     = ReadFloat(file, section, key, def);
    snprintf(key, sizeof(key), "%sSunset",  baseName); p.sunset  = ReadFloat(file, section, key, def);
    snprintf(key, sizeof(key), "%sDusk",    baseName); p.dusk    = ReadFloat(file, section, key, def);
    snprintf(key, sizeof(key), "%sNight",   baseName); p.night   = ReadFloat(file, section, key, def);
    return p;
}

TODColorParameter ConfigManager::ReadTODColor(const char* file, const char* section, const char* baseName)
{
    TODColorParameter p;
    char key[256];
    snprintf(key, sizeof(key), "%sDawn",    baseName); ReadColor3(file, section, key, p.dawn);
    snprintf(key, sizeof(key), "%sSunrise", baseName); ReadColor3(file, section, key, p.sunrise);
    snprintf(key, sizeof(key), "%sDay",     baseName); ReadColor3(file, section, key, p.day);
    snprintf(key, sizeof(key), "%sSunset",  baseName); ReadColor3(file, section, key, p.sunset);
    snprintf(key, sizeof(key), "%sDusk",    baseName); ReadColor3(file, section, key, p.dusk);
    snprintf(key, sizeof(key), "%sNight",   baseName); ReadColor3(file, section, key, p.night);
    return p;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════════════════════

void ConfigManager::Initialize(const char* gameDir)
{
    strncpy_s(m_gameDir, gameDir, MAX_PATH - 1);
    snprintf(m_enbLocalPath,  MAX_PATH, "%s\\enblocal.ini",  gameDir);
    snprintf(m_enbSeriesPath, MAX_PATH, "%s\\enbseries.ini", gameDir);
}

void ConfigManager::LoadAll()
{
    LoadENBLocal();
    LoadENBSeries();
    LoadShaderConfigs();

    // Register core enbseries.ini params in the parameter store
    // so ENBGetParameter("enbseries.ini", "GLOBAL", "UseEffect", ...) works
    {
        StoredParam sp;
        sp.param.Type = ENBParam_BOOL;
        sp.param.Size = 4;
        BOOL val = m_effects.useEffect ? TRUE : FALSE;
        memcpy(sp.param.Data, &val, 4);
        sp.section = "GLOBAL";
        sp.key = "UseEffect";
        m_paramStore["enbseries.ini|GLOBAL|UseEffect"] = sp;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Load enblocal.ini
// ═══════════════════════════════════════════════════════════════════════════

void ConfigManager::LoadENBLocal()
{
    const char* f = m_enbLocalPath;

    // [PROXY]
    m_local.enableProxyLibrary = ReadBool(f, "PROXY", "EnableProxyLibrary", false);
    m_local.initProxyFunctions = ReadBool(f, "PROXY", "InitProxyFunctions", true);
    strncpy_s(m_local.proxyLibrary, ReadString(f, "PROXY", "ProxyLibrary", "").c_str(), MAX_PATH - 1);

    // [ENGINE]
    m_local.forceVSync         = ReadBool(f, "ENGINE", "ForceVSync", false);
    m_local.vsyncSkipNumFrames = ReadInt(f, "ENGINE", "VSyncSkipNumFrames", 0);

    // [LIMITER]
    m_local.enableFPSLimit = ReadBool(f, "LIMITER", "EnableFPSLimit", false);
    m_local.fpsLimit       = ReadFloat(f, "LIMITER", "FPSLimit", 60.0f);

    // [INPUT]
    m_local.keyCombination = ReadInt(f, "INPUT", "KeyCombination", 16);
    m_local.keyUseEffect   = ReadInt(f, "INPUT", "KeyUseEffect", 123);
    m_local.keyFPSLimit    = ReadInt(f, "INPUT", "KeyFPSLimit", 36);
    m_local.keyShowFPS     = ReadInt(f, "INPUT", "KeyShowFPS", 106);
    m_local.keyScreenshot  = ReadInt(f, "INPUT", "KeyScreenshot", 44);
    m_local.keyEditor      = ReadInt(f, "INPUT", "KeyEditor", 13);

    // [FIX]
    m_local.ignoreDamageLimits         = ReadBool(f, "FIX", "IgnoreDamageLimits", false);
    m_local.disableFakeCharacterLight  = ReadBool(f, "FIX", "DisableFakeCharacterLight", true);
    m_local.disableGameDepthOfFieldMSAABug = ReadBool(f, "FIX", "DisableGameDepthOfFieldMSAABug", true);
    m_local.disableGameBlurMSAABug     = ReadBool(f, "FIX", "DisableGameBlurMSAABug", true);
    m_local.fixDecalsBias              = ReadBool(f, "FIX", "FixDecalsBias", true);

    // [ANTIALIASING]
    m_local.highQualityMSAA        = ReadBool(f, "ANTIALIASING", "HighQualityMSAA", false);
    m_local.highQualityVehicleMSAA = ReadBool(f, "ANTIALIASING", "HighQualityVehicleMSAA", false);

    // [ADAPTIVEQUALITY]
    m_local.adaptiveQualityEnable = ReadBool(f, "ADAPTIVEQUALITY", "Enable", false);
    m_local.adaptiveQuality       = ReadInt(f, "ADAPTIVEQUALITY", "Quality", 0);
    m_local.adaptiveDesiredFPS    = ReadFloat(f, "ADAPTIVEQUALITY", "DesiredFPS", 20.0f);

    // [GUI]
    m_local.highResolutionScaling = ReadBool(f, "GUI", "HighResolutionScaling", true);
    m_local.showShadersWindow     = ReadBool(f, "GUI", "ShowShadersWindow", true);
    m_local.showWeatherWindow     = ReadBool(f, "GUI", "ShowWeatherWindow", false);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Load enbseries.ini — all 26 sections
// ═══════════════════════════════════════════════════════════════════════════

void ConfigManager::LoadENBSeries()
{
    const char* f = m_enbSeriesPath;

    // [GLOBAL]
    m_effects.useEffect = ReadBool(f, "GLOBAL", "UseEffect", true);

    // [EFFECT]
    m_effects.useOriginalPostProcessing = ReadBool(f, "EFFECT", "UseOriginalPostProcessing", true);
    m_effects.useOriginalBloom          = ReadBool(f, "EFFECT", "UseOriginalBloom", true);
    m_effects.useOriginalAberration     = ReadBool(f, "EFFECT", "UseOriginalAberrationAndLensDistortion", true);
    m_effects.enablePostPassShader      = ReadBool(f, "EFFECT", "EnablePostPassShader", false);
    m_effects.enableAdaptation          = ReadBool(f, "EFFECT", "EnableAdaptation", false);
    m_effects.enableBloom               = ReadBool(f, "EFFECT", "EnableBloom", false);
    m_effects.enableLens                = ReadBool(f, "EFFECT", "EnableLens", false);
    m_effects.enableDepthOfField        = ReadBool(f, "EFFECT", "EnableDepthOfField", false);
    m_effects.enableAmbientOcclusion    = ReadBool(f, "EFFECT", "EnableAmbientOcclusion", true);
    m_effects.enableDetailedShadows     = ReadBool(f, "EFFECT", "EnableDetailedShadows", true);
    m_effects.enableNormalMappingShadow = ReadBool(f, "EFFECT", "EnableNormalMappingShadow", true);
    m_effects.enableSkylighting         = ReadBool(f, "EFFECT", "EnableSkylighting", false);
    m_effects.enableSubSurfaceScattering= ReadBool(f, "EFFECT", "EnableSubSurfaceScattering", true);
    m_effects.enableSprites             = ReadBool(f, "EFFECT", "EnableSprites", false);
    m_effects.enableRainReflections     = ReadBool(f, "EFFECT", "EnableRainReflections", false);
    m_effects.enableShore               = ReadBool(f, "EFFECT", "EnableShore", true);
    m_effects.enableWater               = ReadBool(f, "EFFECT", "EnableWater", true);

    // [COLORCORRECTION]
    m_effects.useProceduralCorrection = ReadBool(f, "COLORCORRECTION", "UseProceduralCorrection", true);
    m_effects.brightness              = ReadFloat(f, "COLORCORRECTION", "Brightness", 1.0f);
    m_effects.gammaCurve              = ReadFloat(f, "COLORCORRECTION", "GammaCurve", 1.0f);

    // ── TOD float parameters ─────────────────────────────────────────
    // Helper macro to load a TOD float parameter for a section
    #define LOAD_TOD(sec, name, def) \
        m_todFloats[sec "/" name] = ReadTODFloat(f, sec, name, def)

    #define LOAD_TOD_COLOR(sec, name) \
        m_todColors[sec "/" name] = ReadTODColor(f, sec, name)

    // [BLOOM]
    LOAD_TOD("BLOOM", "Amount", 0.1f);

    // [GAMEBLOOM]
    LOAD_TOD("GAMEBLOOM", "Amount", 1.0f);

    // [LENS]
    LOAD_TOD("LENS", "Amount", 1.0f);

    // [GAMELENS]
    LOAD_TOD("GAMELENS", "Amount", 1.0f);

    // [GAMEVOLUMETRICRAYS]
    LOAD_TOD("GAMEVOLUMETRICRAYS", "Amount", 1.0f);

    // [SKY]
    LOAD_TOD("SKY", "SunIntensity", 1.0f);
    LOAD_TOD("SKY", "SunSaturation", 1.0f);
    LOAD_TOD_COLOR("SKY", "SunColorFilter");
    LOAD_TOD("SKY", "MoonIntensity", 1.0f);
    LOAD_TOD_COLOR("SKY", "MoonColorFilter");
    LOAD_TOD("SKY", "StarIntensity", 1.0f);
    LOAD_TOD("SKY", "SkyIntensity", 1.0f);
    LOAD_TOD("SKY", "CloudIntensity", 1.0f);
    LOAD_TOD("SKY", "CloudSaturation", 1.0f);

    // [LIGHTNATURAL]
    LOAD_TOD("LIGHTNATURAL", "DirectLightingIntensity", 1.0f);
    LOAD_TOD("LIGHTNATURAL", "DirectLightingCurve", 1.0f);
    LOAD_TOD("LIGHTNATURAL", "DirectLightingDesaturation", 0.0f);
    LOAD_TOD("LIGHTNATURAL", "DirectLightingColorFilterAmount", 0.0f);
    LOAD_TOD_COLOR("LIGHTNATURAL", "DirectLightingColorFilter");
    LOAD_TOD("LIGHTNATURAL", "AmbientUpIntensity", 1.0f);
    LOAD_TOD_COLOR("LIGHTNATURAL", "AmbientUpColorFilter");
    LOAD_TOD("LIGHTNATURAL", "AmbientDownIntensity", 1.0f);
    LOAD_TOD_COLOR("LIGHTNATURAL", "AmbientDownColorFilter");
    LOAD_TOD("LIGHTNATURAL", "DirectionalAmbientIntensity", 1.0f);
    LOAD_TOD("LIGHTNATURAL", "DirectionalAmbientSaturation", 1.0f);
    LOAD_TOD_COLOR("LIGHTNATURAL", "DirectionalAmbientColorFilter");

    // [LIGHTARTIFICIAL]
    LOAD_TOD("LIGHTARTIFICIAL", "AmbientUpIntensity", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "AmbientDownIntensity", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "AmbientUpIntensityInterior", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "AmbientDownIntensityInterior", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "SpotIntensity", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "VehicleFrontIntensity", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "VehicleBackIntensity", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "EmergencyIntensity", 1.0f);
    LOAD_TOD("LIGHTARTIFICIAL", "EmergencyForcedAmount", 1.0f);

    // [LIGHTSPRITE]
    LOAD_TOD("LIGHTSPRITE", "Intensity", 1.0f);
    LOAD_TOD("LIGHTSPRITE", "WaterReflectionMultiplier", 1.0f);

    // [SKYLIGHTING]
    LOAD_TOD("SKYLIGHTING", "AmbientMinLevel", 0.1f);
    LOAD_TOD("SKYLIGHTING", "AmbientSkyMix", 1.0f);

    // [SSAO_SSIL]
    LOAD_TOD("SSAO_SSIL", "AOIntensity", 1.5f);
    LOAD_TOD("SSAO_SSIL", "AOAmount", 1.0f);
    LOAD_TOD("SSAO_SSIL", "ILAmount", 1.0f);

    // [RAINREFLECTION]
    LOAD_TOD("RAINREFLECTION", "WetnessAmount", 0.1f);

    // [SHORE]
    LOAD_TOD("SHORE", "ReflectionAmount", 1.0f);
    LOAD_TOD("SHORE", "DarkeningAmount", 0.3f);
    LOAD_TOD("SHORE", "StaticWetness", 1.0f);
    LOAD_TOD("SHORE", "DynamicWetness", 1.0f);

    // [WATER]
    LOAD_TOD("WATER", "DisplaceAmount", 1.0f);

    #undef LOAD_TOD
    #undef LOAD_TOD_COLOR
}

// ═══════════════════════════════════════════════════════════════════════════
//  Load per-shader .fx.ini configs
// ═══════════════════════════════════════════════════════════════════════════

void ConfigManager::LoadShaderConfigs()
{
    // Each shader has a config at enbseries/<shadername>.ini
    // containing [SHADERNAME] section with TECHNIQUE=N and variable overrides
    static const char* shaderFiles[] = {
        "enbeffect.fx",
        "enbbloom.fx",
        "enblens.fx",
        "enbadaptation.fx",
        "enbeffectprepass.fx",
        "enbeffectpostpass.fx",
        "enblightsprite.fx",
    };

    for (const char* shader : shaderFiles)
    {
        char iniPath[MAX_PATH];
        snprintf(iniPath, MAX_PATH, "%s\\enbseries\\%s.ini", m_gameDir, shader);

        // Section name is uppercase shader filename
        char section[256];
        strncpy_s(section, shader, sizeof(section) - 1);
        for (char* p = section; *p; p++) *p = static_cast<char>(toupper(*p));

        int technique = ReadInt(iniPath, section, "TECHNIQUE", 0);
        m_shaderTechniques[section] = technique;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Save enbseries.ini
// ═══════════════════════════════════════════════════════════════════════════

void ConfigManager::SaveENBSeries()
{
    const char* f = m_enbSeriesPath;
    WriteBool(f, "GLOBAL", "UseEffect", m_effects.useEffect);
    // TODO: Write all sections back
}

// ═══════════════════════════════════════════════════════════════════════════
//  Parameter store (ENBGetParameter / ENBSetParameter)
// ═══════════════════════════════════════════════════════════════════════════

BOOL ConfigManager::GetParameter(const char* filename, const char* category,
                                  const char* keyname, ENBParameter* outparam)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Build lookup key
    // filename=NULL means shader variable -> use "" as prefix
    std::string prefix = filename ? filename : "";
    std::string lookupKey = prefix + "|" + category + "|" + keyname;

    auto it = m_paramStore.find(lookupKey);
    if (it == m_paramStore.end())
        return FALSE;

    if (it->second.hidden)
        return FALSE;

    *outparam = it->second.param;
    return TRUE;
}

BOOL ConfigManager::SetParameter(const char* filename, const char* category,
                                  const char* keyname, const ENBParameter* inparam)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string prefix = filename ? filename : "";
    std::string lookupKey = prefix + "|" + category + "|" + keyname;

    auto it = m_paramStore.find(lookupKey);
    if (it == m_paramStore.end())
        return FALSE;

    if (it->second.hidden || it->second.readOnly)
        return FALSE;

    it->second.param = *inparam;
    return TRUE;
}

void ConfigManager::RegisterShaderVariable(const char* shaderFile, const char* varName,
                                            ENBParameterType type, void* dataPtr)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Shader variables: filename=NULL -> prefix=""
    // category = uppercase shader filename (e.g., "ENBEFFECT.FX")
    char upperShader[256];
    strncpy_s(upperShader, shaderFile, sizeof(upperShader) - 1);
    for (char* p = upperShader; *p; p++) *p = static_cast<char>(toupper(*p));

    std::string lookupKey = std::string("|") + upperShader + "|" + varName;

    StoredParam sp;
    sp.param.Type = type;
    sp.param.Size = ENBParameterTypeToSize(type);
    if (dataPtr && sp.param.Size > 0 && sp.param.Size <= 16)
        memcpy(sp.param.Data, dataPtr, sp.param.Size);
    sp.section = upperShader;
    sp.key = varName;

    m_paramStore[lookupKey] = sp;
}

int ConfigManager::GetShaderTechnique(const char* shaderFile) const
{
    char upper[256];
    strncpy_s(upper, shaderFile, sizeof(upper) - 1);
    for (char* p = upper; *p; p++) *p = static_cast<char>(toupper(*p));

    auto it = m_shaderTechniques.find(upper);
    return (it != m_shaderTechniques.end()) ? it->second : 0;
}

float ConfigManager::GetTODFloat(const char* section, const char* baseName,
                                  const float weights[6]) const
{
    std::string key = std::string(section) + "/" + baseName;
    auto it = m_todFloats.find(key);
    if (it == m_todFloats.end())
        return 1.0f;
    return it->second.Interpolate(weights);
}

void ConfigManager::GetTODColor(const char* section, const char* baseName,
                                 const float weights[6], float out[3]) const
{
    std::string key = std::string(section) + "/" + baseName;
    auto it = m_todColors.find(key);
    if (it == m_todColors.end())
    {
        out[0] = out[1] = out[2] = 1.0f;
        return;
    }
    it->second.Interpolate(weights, out);
}
