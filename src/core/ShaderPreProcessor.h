#pragma once
//=============================================================================
//  ShaderPreProcessor.h — HLSL annotation parser & source transformer
//
//  Intercepts shader source in the D3DCompile hook to:
//  1. Parse custom annotations (Separation, UIGroup, UIBinding, ExternBinding)
//  2. Transform fxgroup blocks into sequential technique naming
//  3. Transform #pragma uidefine into #define injection
//  4. Strip string variable declarations (UI-only markers)
//  5. Build a persistent AnnotationDatabase for runtime systems
//
//  Replaces ENB Extender's (Kitsuune) annotation parser.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace SB
{

// ── Annotation value (parsed from < ... > blocks) ─────────────────────

struct AnnotationValue
{
    enum Type { kString, kFloat, kInt, kBool };
    Type type = kString;
    std::string stringVal;
    float floatVal = 0.f;
    int intVal = 0;
    bool boolVal = false;
};

// ── Parameter metadata (one per annotated variable) ───────────────────

struct ParameterMeta
{
    // Identity
    std::string varName;        // HLSL variable name
    std::string hlslType;       // "float", "float2", "float3", "float4", "int", "bool", "string"
    std::string shaderFile;     // Source .fx file

    // Standard ENB annotations
    std::string uiName;
    float uiMin = 0.f;
    float uiMax = 1.f;
    std::string uiWidget;       // Spinner, Color, Vector, Dropdown, Quality
    std::string uiList;         // Dropdown list items
    bool uiHidden = false;

    // Extender: Weather separation
    enum class Separation { None, ExteriorWeather, Weather };
    Separation separation = Separation::None;

    // Extender: UI grouping
    std::string uiGroup;        // Dot-separated hierarchy
    std::string uiGroupName;
    bool uiGroupOpen = false;
    bool uiTopLevel = false;
    int uiOrdering = 0;
    bool uiVisible = true;
    bool uiReadOnly = false;
    bool uiIgnorePerfMode = false;

    // Extender: Parameter binding
    std::string uiBinding;
    std::string uiBindingFile;
    std::string uiBindingProperty;  // readonly, readwrite, hidden, visible
    std::string uiBindingCondition; // ==2, !=0, <5, etc.

    // Extender: Unique name override
    std::string uniqueName;

    // Extender: Extern binding (game data → shader)
    std::string externBinding;

    // Extender: Group scope markers (string vars only)
    bool isGroupBegin = false;
    bool isGroupEnd = false;
    bool isSeparator = false;
    bool isWeatherString = false;

    // Default value
    float defaultFloat[4] = {0, 0, 0, 0};
    int defaultInt = 0;
    bool defaultBool = false;

    // All raw annotations preserved
    std::unordered_map<std::string, AnnotationValue> annotations;

    // Derived unique key for database lookup
    std::string GetUniqueKey() const;
};

// ── Technique metadata ────────────────────────────────────────────────

struct TechniqueMeta
{
    std::string name;
    std::string uiName;
    std::string renderTarget;
    bool uiDefault = false;
    std::string uiDropdownName;
    bool uiDropdownVisible = true;
    bool uiDropdownTopLevel = false;
    int uiDropdownOrdering = 0;
    std::string uiDropdownGroup;

    std::unordered_map<std::string, AnnotationValue> annotations;
};

// ── Pre-processing result ─────────────────────────────────────────────

struct PreProcessResult
{
    std::string cleanedSource;              // Source with transforms applied
    std::vector<ParameterMeta> parameters;
    std::vector<TechniqueMeta> techniques;

    // Extra defines from #pragma uidefine
    struct DefinePair { std::string name; std::string value; };
    std::vector<DefinePair> extraDefines;

    // Flags
    bool hadFxGroups       = false;
    bool hadUiDefines      = false;
    bool hadSeparation     = false;
    bool hadExternBindings = false;
};

// ── Annotation Database (persistent across compilations) ──────────────

class AnnotationDatabase
{
public:
    static AnnotationDatabase& Get();

    // Merge parameters discovered during shader compilation
    void MergeFromShader(const std::string& shaderFile,
                         const std::vector<ParameterMeta>& params);
    void MergeTechniques(const std::string& shaderFile,
                         const std::vector<TechniqueMeta>& techs);

    // Query
    const ParameterMeta* FindParameter(const std::string& uniqueKey) const;
    std::vector<const ParameterMeta*> GetParametersForShader(const std::string& shaderFile) const;
    std::vector<const ParameterMeta*> GetSeparatedParameters() const;
    std::vector<const ParameterMeta*> GetExternBoundParameters() const;
    std::vector<const ParameterMeta*> GetBoundParameters() const;

    int GetParameterCount() const;
    int GetSeparatedCount() const;
    int GetShaderCount() const;
    std::vector<std::string> GetAllShaderNames() const;
    int GetGeneration() const;

    void Clear();
    void ClearShader(const std::string& shaderFile);

private:
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, ParameterMeta> m_params;
    std::unordered_map<std::string, std::vector<std::string>> m_shaderParams;
    std::unordered_map<std::string, std::vector<TechniqueMeta>> m_techniques;
    int m_generation = 0;  // Incremented on every MergeFromShader call
};

// ── Shader Pre-Processor ──────────────────────────────────────────────

class ShaderPreProcessor
{
public:
    static ShaderPreProcessor& Get();

    // Main entry point: parse + transform shader source
    PreProcessResult Process(const std::string& source,
                             const std::string& sourceName);

    AnnotationDatabase& GetDatabase() { return AnnotationDatabase::Get(); }

    // Stats
    int GetProcessCount() const { return m_processCount; }
    int GetCacheHits() const { return m_cacheHits; }

private:
    // ── Parsing ─────────────────────────────────────────────────────

    // Parse < type key = value; ... > block. Advances pos past '>'.
    static std::unordered_map<std::string, AnnotationValue>
        ParseAnnotationBlock(const std::string& src, size_t& pos);

    // Scan for all annotated variable declarations
    static void ScanVariables(const std::string& src,
                              const std::string& shaderFile,
                              std::vector<ParameterMeta>& outParams);

    // Scan for technique declarations with annotations
    static void ScanTechniques(const std::string& src,
                               std::vector<TechniqueMeta>& outTechs);

    // ── Source transformations ───────────────────────────────────────

    static std::string TransformFxGroups(const std::string& src,
                                         std::vector<TechniqueMeta>& outTechs);

    static std::string TransformUiDefines(const std::string& src,
                                           const std::string& shaderFile,
                                           std::vector<ParameterMeta>& outParams,
                                           std::vector<PreProcessResult::DefinePair>& outDefines);

    static std::string StripStringVars(const std::string& src,
                                        std::vector<ParameterMeta>& outParams,
                                        const std::string& shaderFile);

    // ── Utility ─────────────────────────────────────────────────────

    static std::string StripComments(const std::string& src);
    static void SkipWhitespace(const std::string& src, size_t& pos);
    static std::string ReadIdentifier(const std::string& src, size_t& pos);
    static std::string ReadQuotedString(const std::string& src, size_t& pos);
    static float ReadFloat(const std::string& src, size_t& pos);
    static int ReadInt(const std::string& src, size_t& pos);
    static bool IsTypeKeyword(const std::string& word);
    static size_t FindMatchingBrace(const std::string& src, size_t openPos);

    // Populate meta fields from raw annotations
    static void ApplyAnnotations(ParameterMeta& meta,
                                 const std::unordered_map<std::string, AnnotationValue>& annots);
    static void ApplyTechAnnotations(TechniqueMeta& meta,
                                     const std::unordered_map<std::string, AnnotationValue>& annots);

    // ── Source cache (avoid re-parsing same source) ─────────────────

    struct CachedResult
    {
        PreProcessResult result;
    };
    std::unordered_map<uint64_t, CachedResult> m_cache;
    std::mutex m_cacheMtx;

    static uint64_t HashSource(const std::string& src);

    int m_processCount = 0;
    int m_cacheHits = 0;
};

} // namespace SB
