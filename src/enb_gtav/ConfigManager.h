#pragma once
//=============================================================================
//  ConfigManager.h — ENB Configuration System
//
//  Loads and manages enblocal.ini, enbseries.ini, per-shader .fx.ini files,
//  and weather override INIs. Uses Win32 PrivateProfile API for I/O.
//
//  Also serves as the parameter store for ENBGetParameter/ENBSetParameter.
//=============================================================================

#include "ENBState.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

// ---------------------------------------------------------------------------
//  Stored parameter value (in the parameter store)
// ---------------------------------------------------------------------------
struct StoredParam
{
    ENBParameter    param;
    std::string     section;
    std::string     key;
    bool            readOnly = false;
    bool            hidden   = false;
};

// ---------------------------------------------------------------------------
//  Time-of-day parameter — a value that varies across 6 time periods
// ---------------------------------------------------------------------------
struct TODParameter
{
    float dawn    = 1.0f;
    float sunrise = 1.0f;
    float day     = 1.0f;
    float sunset  = 1.0f;
    float dusk    = 1.0f;
    float night   = 1.0f;

    float Interpolate(const float weights[6]) const
    {
        return dawn    * weights[0]
             + sunrise * weights[1]
             + day     * weights[2]
             + sunset  * weights[3]
             + dusk    * weights[4]
             + night   * weights[5];
    }
};

// ---------------------------------------------------------------------------
//  TOD color parameter (float3 that varies by time of day)
// ---------------------------------------------------------------------------
struct TODColorParameter
{
    float dawn[3]    = {1,1,1};
    float sunrise[3] = {1,1,1};
    float day[3]     = {1,1,1};
    float sunset[3]  = {1,1,1};
    float dusk[3]    = {1,1,1};
    float night[3]   = {1,1,1};

    void Interpolate(const float weights[6], float out[3]) const
    {
        for (int c = 0; c < 3; c++)
        {
            out[c] = dawn[c]    * weights[0]
                   + sunrise[c] * weights[1]
                   + day[c]     * weights[2]
                   + sunset[c]  * weights[3]
                   + dusk[c]    * weights[4]
                   + night[c]   * weights[5];
        }
    }
};

// ---------------------------------------------------------------------------
//  enblocal.ini configuration
// ---------------------------------------------------------------------------
struct ENBLocalConfig
{
    // [PROXY]
    bool  enableProxyLibrary = false;
    bool  initProxyFunctions = true;
    char  proxyLibrary[MAX_PATH] = {};

    // [ENGINE]
    bool  forceVSync = false;
    int   vsyncSkipNumFrames = 0;

    // [LIMITER]
    bool  enableFPSLimit = false;
    float fpsLimit = 60.0f;

    // [INPUT]
    int   keyCombination = 16;    // Shift
    int   keyUseEffect   = 123;   // F12
    int   keyFPSLimit    = 36;    // Home
    int   keyShowFPS     = 106;   // Num *
    int   keyScreenshot  = 44;    // PrtScn
    int   keyEditor      = 13;    // Enter

    // [FIX]
    bool  ignoreDamageLimits       = false;
    bool  disableFakeCharacterLight = true;
    bool  disableGameDepthOfFieldMSAABug = true;
    bool  disableGameBlurMSAABug   = true;
    bool  fixDecalsBias            = true;

    // [ANTIALIASING]
    bool  highQualityMSAA        = false;
    bool  highQualityVehicleMSAA = false;

    // [ADAPTIVEQUALITY]
    bool  adaptiveQualityEnable = false;
    int   adaptiveQuality       = 0;
    float adaptiveDesiredFPS    = 20.0f;

    // [GUI]
    bool  highResolutionScaling = true;
    bool  showShadersWindow     = true;
    bool  showWeatherWindow     = false;
};

// ---------------------------------------------------------------------------
//  enbseries.ini global effect toggles
// ---------------------------------------------------------------------------
struct ENBEffectToggles
{
    bool useEffect                    = true;
    bool useOriginalPostProcessing    = true;
    bool useOriginalBloom             = true;
    bool useOriginalAberration        = true;
    bool enablePostPassShader         = false;
    bool enableAdaptation             = false;
    bool enableBloom                  = false;
    bool enableLens                   = false;
    bool enableDepthOfField           = false;
    bool enableAmbientOcclusion       = true;
    bool enableDetailedShadows        = true;
    bool enableNormalMappingShadow    = true;
    bool enableSkylighting            = false;
    bool enableSubSurfaceScattering   = true;
    bool enableSprites                = false;
    bool enableRainReflections        = false;
    bool enableShore                  = true;
    bool enableWater                  = true;
    bool useProceduralCorrection      = true;
    float brightness                  = 1.0f;
    float gammaCurve                  = 1.0f;
};

// ---------------------------------------------------------------------------
//  ConfigManager
// ---------------------------------------------------------------------------
class ConfigManager
{
public:
    void Initialize(const char* gameDir);
    void LoadAll();
    void SaveENBSeries();

    // Parameter store for ENBGetParameter/ENBSetParameter
    BOOL GetParameter(const char* filename, const char* category,
                      const char* keyname, ENBParameter* outparam);
    BOOL SetParameter(const char* filename, const char* category,
                      const char* keyname, const ENBParameter* inparam);

    // Register a shader variable (called by EffectCompiler when loading .fx)
    void RegisterShaderVariable(const char* shaderFile, const char* varName,
                                ENBParameterType type, void* dataPtr);

    // Access
    const ENBLocalConfig&   GetLocalConfig()  const { return m_local; }
    const ENBEffectToggles& GetEffectToggles() const { return m_effects; }
    const char*             GetGameDir()      const { return m_gameDir; }

    // Per-shader technique selection
    int  GetShaderTechnique(const char* shaderFile) const;

    // TOD-interpolated values
    float GetTODFloat(const char* section, const char* baseName,
                      const float weights[6]) const;
    void  GetTODColor(const char* section, const char* baseName,
                      const float weights[6], float out[3]) const;

private:
    // INI helpers
    int         ReadInt(const char* file, const char* section, const char* key, int def);
    float       ReadFloat(const char* file, const char* section, const char* key, float def);
    bool        ReadBool(const char* file, const char* section, const char* key, bool def);
    std::string ReadString(const char* file, const char* section, const char* key, const char* def);
    void        ReadColor3(const char* file, const char* section, const char* key, float out[3]);
    void        WriteFloat(const char* file, const char* section, const char* key, float val);
    void        WriteBool(const char* file, const char* section, const char* key, bool val);
    void        WriteString(const char* file, const char* section, const char* key, const char* val);

    TODParameter  ReadTODFloat(const char* file, const char* section, const char* baseName, float def);
    TODColorParameter ReadTODColor(const char* file, const char* section, const char* baseName);

    void LoadENBLocal();
    void LoadENBSeries();
    void LoadShaderConfigs();

    char m_gameDir[MAX_PATH] = {};
    char m_enbLocalPath[MAX_PATH] = {};
    char m_enbSeriesPath[MAX_PATH] = {};

    ENBLocalConfig   m_local;
    ENBEffectToggles m_effects;

    // Per-section TOD parameters from enbseries.ini
    // Key: "SECTION/BaseName" -> TODParameter
    std::unordered_map<std::string, TODParameter>      m_todFloats;
    std::unordered_map<std::string, TODColorParameter>  m_todColors;

    // Per-shader technique index: "ENBBLOOM.FX" -> 0
    std::unordered_map<std::string, int> m_shaderTechniques;

    // Parameter store: "filename|SECTION|keyname" -> StoredParam
    // filename="" for shader variables (accessed with filename=NULL)
    std::unordered_map<std::string, StoredParam> m_paramStore;

    std::mutex m_mutex;
};

extern ConfigManager g_Config;
