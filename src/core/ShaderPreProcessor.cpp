#include "ShaderPreProcessor.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  ParameterMeta
// ═══════════════════════════════════════════════════════════════════════════

std::string ParameterMeta::GetUniqueKey() const
{
    if (!uniqueName.empty()) return uniqueName;
    if (!uiGroup.empty() && !uiName.empty()) return uiGroup + "." + uiName;
    if (!uiName.empty()) return uiName;
    return varName;
}

// ═══════════════════════════════════════════════════════════════════════════
//  AnnotationDatabase
// ═══════════════════════════════════════════════════════════════════════════

AnnotationDatabase& AnnotationDatabase::Get()
{
    static AnnotationDatabase inst;
    return inst;
}

void AnnotationDatabase::MergeFromShader(const std::string& shaderFile,
                                          const std::vector<ParameterMeta>& params)
{
    std::lock_guard lock(m_mtx);
    ++m_generation;

    // Clear previous entries for this shader
    if (auto it = m_shaderParams.find(shaderFile); it != m_shaderParams.end()) {
        for (auto& key : it->second)
            m_params.erase(key);
        it->second.clear();
    }

    auto& keys = m_shaderParams[shaderFile];
    for (auto& p : params) {
        auto key = p.GetUniqueKey();
        m_params[key] = p;
        keys.push_back(key);
    }
}

void AnnotationDatabase::MergeTechniques(const std::string& shaderFile,
                                          const std::vector<TechniqueMeta>& techs)
{
    std::lock_guard lock(m_mtx);
    m_techniques[shaderFile] = techs;
}

const ParameterMeta* AnnotationDatabase::FindParameter(const std::string& uniqueKey) const
{
    std::lock_guard lock(m_mtx);
    auto it = m_params.find(uniqueKey);
    return (it != m_params.end()) ? &it->second : nullptr;
}

std::vector<const ParameterMeta*> AnnotationDatabase::GetParametersForShader(
    const std::string& shaderFile) const
{
    std::lock_guard lock(m_mtx);
    std::vector<const ParameterMeta*> result;
    auto it = m_shaderParams.find(shaderFile);
    if (it != m_shaderParams.end()) {
        for (auto& key : it->second) {
            auto pit = m_params.find(key);
            if (pit != m_params.end())
                result.push_back(&pit->second);
        }
    }
    return result;
}

std::vector<const ParameterMeta*> AnnotationDatabase::GetSeparatedParameters() const
{
    std::lock_guard lock(m_mtx);
    std::vector<const ParameterMeta*> result;
    for (auto& [_, p] : m_params)
        if (p.separation != ParameterMeta::Separation::None)
            result.push_back(&p);
    return result;
}

std::vector<const ParameterMeta*> AnnotationDatabase::GetExternBoundParameters() const
{
    std::lock_guard lock(m_mtx);
    std::vector<const ParameterMeta*> result;
    for (auto& [_, p] : m_params)
        if (!p.externBinding.empty())
            result.push_back(&p);
    return result;
}

std::vector<const ParameterMeta*> AnnotationDatabase::GetBoundParameters() const
{
    std::lock_guard lock(m_mtx);
    std::vector<const ParameterMeta*> result;
    for (auto& [_, p] : m_params)
        if (!p.uiBinding.empty())
            result.push_back(&p);
    return result;
}

int AnnotationDatabase::GetParameterCount() const
{
    std::lock_guard lock(m_mtx);
    return static_cast<int>(m_params.size());
}

int AnnotationDatabase::GetSeparatedCount() const
{
    std::lock_guard lock(m_mtx);
    int count = 0;
    for (auto& [_, p] : m_params)
        if (p.separation != ParameterMeta::Separation::None)
            count++;
    return count;
}

int AnnotationDatabase::GetShaderCount() const
{
    std::lock_guard lock(m_mtx);
    return static_cast<int>(m_shaderParams.size());
}

std::vector<std::string> AnnotationDatabase::GetAllShaderNames() const
{
    std::lock_guard lock(m_mtx);
    std::vector<std::string> names;
    names.reserve(m_shaderParams.size());
    for (auto& [name, _] : m_shaderParams)
        names.push_back(name);
    return names;
}

int AnnotationDatabase::GetGeneration() const
{
    std::lock_guard lock(m_mtx);
    return m_generation;
}

void AnnotationDatabase::Clear()
{
    std::lock_guard lock(m_mtx);
    m_params.clear();
    m_shaderParams.clear();
    m_techniques.clear();
}

void AnnotationDatabase::ClearShader(const std::string& shaderFile)
{
    std::lock_guard lock(m_mtx);
    if (auto it = m_shaderParams.find(shaderFile); it != m_shaderParams.end()) {
        for (auto& key : it->second)
            m_params.erase(key);
        m_shaderParams.erase(it);
    }
    m_techniques.erase(shaderFile);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ShaderPreProcessor — Singleton
// ═══════════════════════════════════════════════════════════════════════════

ShaderPreProcessor& ShaderPreProcessor::Get()
{
    static ShaderPreProcessor inst;
    return inst;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Utility functions
// ═══════════════════════════════════════════════════════════════════════════

uint64_t ShaderPreProcessor::HashSource(const std::string& src)
{
    // FNV-1a 64-bit
    uint64_t hash = 14695981039346656037ULL;
    for (char c : src) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Replace comments with spaces (preserving newlines for line number accuracy)
std::string ShaderPreProcessor::StripComments(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        // Line comment
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n')
                out += ' ', i++;
        }
        // Block comment
        else if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
            out += ' '; i++;
            out += ' '; i++;
            while (i < src.size()) {
                if (i + 1 < src.size() && src[i] == '*' && src[i + 1] == '/') {
                    out += ' '; i++;
                    out += ' '; i++;
                    break;
                }
                out += (src[i] == '\n') ? '\n' : ' ';
                i++;
            }
        }
        // String literal (don't strip inside strings)
        else if (src[i] == '"') {
            out += src[i++];
            while (i < src.size() && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    out += src[i++];
                }
                out += src[i++];
            }
            if (i < src.size()) out += src[i++]; // closing quote
        }
        else {
            out += src[i++];
        }
    }
    return out;
}

void ShaderPreProcessor::SkipWhitespace(const std::string& src, size_t& pos)
{
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
        pos++;
}

std::string ShaderPreProcessor::ReadIdentifier(const std::string& src, size_t& pos)
{
    SkipWhitespace(src, pos);
    std::string id;
    while (pos < src.size() &&
           (std::isalnum(static_cast<unsigned char>(src[pos])) || src[pos] == '_'))
        id += src[pos++];
    return id;
}

std::string ShaderPreProcessor::ReadQuotedString(const std::string& src, size_t& pos)
{
    SkipWhitespace(src, pos);
    if (pos >= src.size() || src[pos] != '"') return {};
    pos++; // skip opening quote
    std::string val;
    while (pos < src.size() && src[pos] != '"') {
        if (src[pos] == '\\' && pos + 1 < src.size()) {
            pos++; // skip escape
        }
        val += src[pos++];
    }
    if (pos < src.size()) pos++; // skip closing quote
    return val;
}

float ShaderPreProcessor::ReadFloat(const std::string& src, size_t& pos)
{
    SkipWhitespace(src, pos);
    size_t start = pos;
    if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) pos++;
    while (pos < src.size() && (std::isdigit(static_cast<unsigned char>(src[pos])) || src[pos] == '.'))
        pos++;
    // Handle scientific notation (e.g., 1.0e-5, 2.5E+3)
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
        pos++;
        if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) pos++;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
    }
    // Handle 'f' suffix
    if (pos < src.size() && (src[pos] == 'f' || src[pos] == 'F')) pos++;
    if (pos == start) return 0.f;
    try { return std::stof(src.substr(start, pos - start)); }
    catch (...) { return 0.f; }
}

int ShaderPreProcessor::ReadInt(const std::string& src, size_t& pos)
{
    SkipWhitespace(src, pos);
    size_t start = pos;
    if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) pos++;
    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
        pos++;
    if (pos == start) return 0;
    try { return std::stoi(src.substr(start, pos - start)); }
    catch (...) { return 0; }
}

bool ShaderPreProcessor::IsTypeKeyword(const std::string& word)
{
    static const char* types[] = {
        "float", "float1", "float2", "float3", "float4",
        "half", "half1", "half2", "half3", "half4",
        "int", "int1", "int2", "int3", "int4",
        "uint", "uint1", "uint2", "uint3", "uint4",
        "bool", "string",
        "double", "double2", "double3", "double4",
    };
    for (auto* t : types)
        if (word == t) return true;
    return false;
}

size_t ShaderPreProcessor::FindMatchingBrace(const std::string& src, size_t openPos)
{
    int depth = 1;
    size_t pos = openPos + 1;
    while (pos < src.size() && depth > 0) {
        if (src[pos] == '{') depth++;
        else if (src[pos] == '}') depth--;
        if (depth > 0) pos++;
    }
    return (depth == 0) ? pos : std::string::npos;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Annotation block parser: < type key = value; ... >
// ═══════════════════════════════════════════════════════════════════════════

std::unordered_map<std::string, AnnotationValue>
ShaderPreProcessor::ParseAnnotationBlock(const std::string& src, size_t& pos)
{
    std::unordered_map<std::string, AnnotationValue> result;

    // pos should be AT the '<'
    if (pos >= src.size() || src[pos] != '<') return result;
    pos++; // skip '<'

    while (pos < src.size()) {
        SkipWhitespace(src, pos);
        if (pos >= src.size()) break;
        if (src[pos] == '>') { pos++; break; }

        // Read type keyword (string, float, int, bool)
        std::string typeKw = ReadIdentifier(src, pos);
        if (typeKw.empty()) { pos++; continue; } // skip garbage

        // Read key name
        std::string key = ReadIdentifier(src, pos);
        if (key.empty()) continue;

        // Skip '='
        SkipWhitespace(src, pos);
        if (pos < src.size() && src[pos] == '=') pos++;

        // Read value based on type
        AnnotationValue val;
        SkipWhitespace(src, pos);

        if (typeKw == "string") {
            val.type = AnnotationValue::kString;
            val.stringVal = ReadQuotedString(src, pos);
        }
        else if (typeKw == "float" || typeKw == "half" || typeKw == "double") {
            val.type = AnnotationValue::kFloat;
            val.floatVal = ReadFloat(src, pos);
        }
        else if (typeKw == "int" || typeKw == "uint") {
            val.type = AnnotationValue::kInt;
            val.intVal = ReadInt(src, pos);
        }
        else if (typeKw == "bool") {
            val.type = AnnotationValue::kBool;
            std::string boolStr = ReadIdentifier(src, pos);
            val.boolVal = (boolStr == "true" || boolStr == "1");
            val.intVal = val.boolVal ? 1 : 0;
        }
        else {
            // Unknown type — try to skip value until ';' or '>'
            while (pos < src.size() && src[pos] != ';' && src[pos] != '>') pos++;
        }

        result[key] = val;

        // Skip trailing ';'
        SkipWhitespace(src, pos);
        if (pos < src.size() && src[pos] == ';') pos++;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Apply parsed annotations to metadata structs
// ═══════════════════════════════════════════════════════════════════════════

void ShaderPreProcessor::ApplyAnnotations(
    ParameterMeta& meta,
    const std::unordered_map<std::string, AnnotationValue>& annots)
{
    meta.annotations = annots;

    auto getString = [&](const char* key) -> std::string {
        auto it = annots.find(key);
        return (it != annots.end()) ? it->second.stringVal : std::string{};
    };
    auto getFloat = [&](const char* key, float def = 0.f) -> float {
        auto it = annots.find(key);
        return (it != annots.end()) ? it->second.floatVal : def;
    };
    auto getInt = [&](const char* key, int def = 0) -> int {
        auto it = annots.find(key);
        if (it == annots.end()) return def;
        return (it->second.type == AnnotationValue::kInt) ? it->second.intVal
             : (it->second.type == AnnotationValue::kBool) ? (it->second.boolVal ? 1 : 0)
             : def;
    };
    auto getBool = [&](const char* key, bool def = false) -> bool {
        auto it = annots.find(key);
        if (it == annots.end()) return def;
        return (it->second.type == AnnotationValue::kBool) ? it->second.boolVal
             : (it->second.type == AnnotationValue::kInt) ? (it->second.intVal != 0)
             : def;
    };

    // Standard ENB
    meta.uiName   = getString("UIName");
    meta.uiMin    = getFloat("UIMin", 0.f);
    meta.uiMax    = getFloat("UIMax", 1.f);
    meta.uiWidget = getString("UIWidget");
    meta.uiList   = getString("UIList");
    meta.uiHidden = (getInt("UIHidden") != 0);

    // Extender: Separation
    auto sep = getString("Separation");
    if (sep == "ExteriorWeather")
        meta.separation = ParameterMeta::Separation::ExteriorWeather;
    else if (sep == "Weather")
        meta.separation = ParameterMeta::Separation::Weather;
    else
        meta.separation = ParameterMeta::Separation::None;

    // Extender: Grouping
    meta.uiGroup          = getString("UIGroup");
    meta.uiGroupName      = getString("UIGroupName");
    meta.uiGroupOpen      = getBool("UIGroupOpen");
    meta.uiTopLevel       = getBool("UITopLevel");
    meta.uiOrdering       = getInt("UIOrdering");
    meta.uiVisible        = getBool("UIVisible", true);
    meta.uiReadOnly       = getBool("UIReadOnly");
    meta.uiIgnorePerfMode = getBool("UIIgnorePerfMode");
    meta.uniqueName       = getString("UniqueName");

    // Extender: Binding
    meta.uiBinding          = getString("UIBinding");
    meta.uiBindingFile      = getString("UIBindingFile");
    meta.uiBindingProperty  = getString("UIBindingProperty");
    meta.uiBindingCondition = getString("UIBindingCondition");

    // Extender: Extern binding
    meta.externBinding = getString("ExternBinding");

    // Extender: Group scope markers (string variables only)
    meta.isGroupBegin    = getBool("UIGroupBegin");
    meta.isGroupEnd      = getBool("UIGroupEnd");
    meta.isSeparator     = getBool("UISeparator");
    meta.isWeatherString = getBool("UIWeatherString") || getBool("UIWeatherOnlyString");
}

void ShaderPreProcessor::ApplyTechAnnotations(
    TechniqueMeta& meta,
    const std::unordered_map<std::string, AnnotationValue>& annots)
{
    meta.annotations = annots;

    auto getString = [&](const char* key) -> std::string {
        auto it = annots.find(key);
        return (it != annots.end()) ? it->second.stringVal : std::string{};
    };
    auto getBool = [&](const char* key, bool def = false) -> bool {
        auto it = annots.find(key);
        return (it != annots.end()) ? it->second.boolVal : def;
    };
    auto getInt = [&](const char* key, int def = 0) -> int {
        auto it = annots.find(key);
        return (it != annots.end()) ? it->second.intVal : def;
    };

    meta.uiName             = getString("UIName");
    meta.renderTarget       = getString("RenderTarget");
    meta.uiDefault          = getBool("UIDefault");
    meta.uiDropdownName     = getString("UIDropdownName");
    meta.uiDropdownVisible  = getBool("UIDropdownVisible", true);
    meta.uiDropdownTopLevel = getBool("UIDropdownTopLevel");
    meta.uiDropdownOrdering = getInt("UIDropdownOrdering");
    meta.uiDropdownGroup    = getString("UIDropdownGroup");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Variable scanner — finds annotated variable declarations
// ═══════════════════════════════════════════════════════════════════════════

void ShaderPreProcessor::ScanVariables(const std::string& src,
                                        const std::string& shaderFile,
                                        std::vector<ParameterMeta>& outParams)
{
    // Scan for pattern: TYPE IDENTIFIER < annotations > [= default] ;
    // We use the comment-stripped source for analysis.
    size_t pos = 0;
    while (pos < src.size()) {
        SkipWhitespace(src, pos);
        if (pos >= src.size()) break;

        // Try to read a type keyword
        size_t savedPos = pos;
        std::string word = ReadIdentifier(src, pos);

        if (word.empty()) { pos = savedPos + 1; continue; }

        // Skip non-type keywords (struct, cbuffer, technique, etc.)
        if (!IsTypeKeyword(word)) {
            // Skip to next statement
            while (pos < src.size() && src[pos] != ';' && src[pos] != '{' && src[pos] != '}')
                pos++;
            if (pos < src.size()) {
                if (src[pos] == '{') {
                    // Skip entire block
                    pos = FindMatchingBrace(src, pos);
                    if (pos != std::string::npos) pos++;
                } else {
                    pos++;
                }
            }
            continue;
        }

        // We have a type keyword. Read the variable name.
        std::string varName = ReadIdentifier(src, pos);
        if (varName.empty()) continue;

        // Check for annotation block '<'
        SkipWhitespace(src, pos);
        if (pos >= src.size() || src[pos] != '<') {
            // No annotation — skip to ';'
            while (pos < src.size() && src[pos] != ';') pos++;
            if (pos < src.size()) pos++;
            continue;
        }

        // Parse the annotation block
        auto annots = ParseAnnotationBlock(src, pos);
        if (annots.empty()) continue;

        // Build ParameterMeta
        ParameterMeta meta;
        meta.varName    = varName;
        meta.hlslType   = word;
        meta.shaderFile = shaderFile;
        ApplyAnnotations(meta, annots);

        // Parse default value after '>' and '='
        SkipWhitespace(src, pos);
        if (pos < src.size() && src[pos] == '=') {
            pos++; // skip '='
            SkipWhitespace(src, pos);

            if (word == "bool") {
                std::string bv = ReadIdentifier(src, pos);
                meta.defaultBool = (bv == "true" || bv == "1");
            }
            else if (word == "int" || word == "uint") {
                meta.defaultInt = ReadInt(src, pos);
            }
            else if (word == "string") {
                // String default — skip
                ReadQuotedString(src, pos);
            }
            else {
                // Float type — might be scalar or vector/initializer list
                if (pos < src.size() && src[pos] == '{') {
                    pos++; // skip '{'
                    for (int c = 0; c < 4; c++) {
                        SkipWhitespace(src, pos);
                        if (pos < src.size() && src[pos] == '}') break;
                        meta.defaultFloat[c] = ReadFloat(src, pos);
                        SkipWhitespace(src, pos);
                        if (pos < src.size() && src[pos] == ',') pos++;
                    }
                    if (pos < src.size() && src[pos] == '}') pos++;
                }
                else if (pos < src.size() && std::isalpha(static_cast<unsigned char>(src[pos]))) {
                    // float2(...), float3(...), float4(...) constructor
                    ReadIdentifier(src, pos); // skip type name
                    SkipWhitespace(src, pos);
                    if (pos < src.size() && src[pos] == '(') {
                        pos++;
                        for (int c = 0; c < 4; c++) {
                            SkipWhitespace(src, pos);
                            if (pos < src.size() && src[pos] == ')') break;
                            meta.defaultFloat[c] = ReadFloat(src, pos);
                            SkipWhitespace(src, pos);
                            if (pos < src.size() && src[pos] == ',') pos++;
                        }
                        if (pos < src.size() && src[pos] == ')') pos++;
                    }
                }
                else {
                    meta.defaultFloat[0] = ReadFloat(src, pos);
                }
            }
        }

        // Skip to ';'
        while (pos < src.size() && src[pos] != ';') pos++;
        if (pos < src.size()) pos++;

        outParams.push_back(std::move(meta));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Technique scanner
// ═══════════════════════════════════════════════════════════════════════════

void ShaderPreProcessor::ScanTechniques(const std::string& src,
                                         std::vector<TechniqueMeta>& outTechs)
{
    // Search for technique keywords: technique11 (SM5.0), technique10 (SM4.0), technique (SM2/3)
    // ENB SE uses technique11 but some shader authors omit the version suffix
    static const char* kTechKeywords[] = { "technique11", "technique10", "technique" };

    for (const char* techKw : kTechKeywords) {
        size_t kwLen = std::strlen(techKw);
        size_t pos = 0;

        while ((pos = src.find(techKw, pos)) != std::string::npos) {
            // Ensure it's a word boundary (not part of a larger identifier)
            if (pos > 0 && (std::isalnum(static_cast<unsigned char>(src[pos - 1])) || src[pos - 1] == '_')) {
                pos += kwLen;
                continue;
            }

            // For "technique" (no suffix), make sure it's not actually "technique10" or "technique11"
            if (kwLen == 9) { // "technique" is 9 chars
                char next = (pos + kwLen < src.size()) ? src[pos + kwLen] : ' ';
                if (next == '1') { pos += kwLen; continue; } // skip, will be caught by technique10/11
            }

            pos += kwLen;
            std::string name = ReadIdentifier(src, pos);

        TechniqueMeta meta;
        meta.name = name;

        SkipWhitespace(src, pos);
        if (pos < src.size() && src[pos] == '<') {
            auto annots = ParseAnnotationBlock(src, pos);
            ApplyTechAnnotations(meta, annots);
        }

        outTechs.push_back(std::move(meta));

        // Skip the technique body
        SkipWhitespace(src, pos);
        if (pos < src.size() && src[pos] == '{') {
            pos = FindMatchingBrace(src, pos);
            if (pos != std::string::npos) pos++;
        }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  fxgroup transformation
//
//  fxgroup MyEffect <annotations> {
//      technique11 <tech_annotations> { pass { ... } }
//      technique11 { pass { ... } }
//  }
//  →
//  technique11 MyEffect <fxgroup_annots + tech0_annots> { pass { ... } }
//  technique11 MyEffect1 { pass { ... } }
// ═══════════════════════════════════════════════════════════════════════════

std::string ShaderPreProcessor::TransformFxGroups(const std::string& src,
                                                   std::vector<TechniqueMeta>& outTechs)
{
    std::string result;
    result.reserve(src.size());
    size_t pos = 0;
    const char* kw = "fxgroup";
    size_t kwLen = 7;

    while (pos < src.size()) {
        size_t fxPos = src.find(kw, pos);
        if (fxPos == std::string::npos) {
            result.append(src, pos, src.size() - pos);
            break;
        }

        // Check word boundary
        if (fxPos > 0 && (std::isalnum(static_cast<unsigned char>(src[fxPos - 1])) || src[fxPos - 1] == '_')) {
            result.append(src, pos, fxPos + kwLen - pos);
            pos = fxPos + kwLen;
            continue;
        }

        // Copy everything before the fxgroup
        result.append(src, pos, fxPos - pos);
        pos = fxPos + kwLen;

        // Read fxgroup name
        std::string groupName = ReadIdentifier(src, pos);

        // Read fxgroup annotations
        SkipWhitespace(src, pos);
        std::unordered_map<std::string, AnnotationValue> groupAnnots;
        if (pos < src.size() && src[pos] == '<') {
            groupAnnots = ParseAnnotationBlock(src, pos);
        }

        // Find the fxgroup body { ... }
        SkipWhitespace(src, pos);
        if (pos >= src.size() || src[pos] != '{') {
            // Malformed — emit as-is
            result += "/* SB: malformed fxgroup */ ";
            continue;
        }

        size_t bodyEnd = FindMatchingBrace(src, pos);
        if (bodyEnd == std::string::npos) {
            result.append(src, pos, src.size() - pos);
            break;
        }

        std::string body = src.substr(pos + 1, bodyEnd - pos - 1);
        pos = bodyEnd + 1; // skip past '}'

        // Parse technique blocks inside the fxgroup body
        // Accept technique11, technique10, or technique (all map to technique11 output)
        size_t techPos = 0;
        int techIndex = 0;
        while (techPos < body.size()) {
            // Find next technique keyword
            size_t tpos = std::string::npos;
            size_t kwLen = 0;
            for (const char* kw : { "technique11", "technique10", "technique" }) {
                size_t found = body.find(kw, techPos);
                if (found < tpos) {
                    // For "technique", ensure it's not "technique1x"
                    size_t len = std::strlen(kw);
                    if (len == 9 && found + 9 < body.size() && body[found + 9] == '1')
                        continue;
                    tpos = found;
                    kwLen = len;
                }
            }
            if (tpos == std::string::npos) break;

            // Skip past the technique keyword
            techPos = tpos + kwLen;

            // Read technique's own annotations (if any)
            SkipWhitespace(body, techPos);
            std::unordered_map<std::string, AnnotationValue> techAnnots;
            if (techPos < body.size() && body[techPos] == '<') {
                techAnnots = ParseAnnotationBlock(body, techPos);
            }

            // Find technique body
            SkipWhitespace(body, techPos);
            if (techPos >= body.size() || body[techPos] != '{') continue;
            size_t techBodyEnd = FindMatchingBrace(body, techPos);
            if (techBodyEnd == std::string::npos) break;

            std::string techBody = body.substr(techPos, techBodyEnd - techPos + 1);
            techPos = techBodyEnd + 1;

            // Build the technique name
            std::string techName = groupName;
            if (techIndex > 0)
                techName += std::to_string(techIndex);

            // Build annotation string
            if (techIndex == 0) {
                // First technique: merge fxgroup + technique annotations
                auto merged = groupAnnots;
                for (auto& [k, v] : techAnnots) merged[k] = v;

                result += "technique11 " + techName + " <";
                bool first = true;
                for (auto& [k, v] : merged) {
                    if (!first) result += " ";
                    first = false;
                    if (v.type == AnnotationValue::kString)
                        result += "string " + k + " = \"" + v.stringVal + "\";";
                    else if (v.type == AnnotationValue::kFloat)
                        result += "float " + k + " = " + std::to_string(v.floatVal) + ";";
                    else if (v.type == AnnotationValue::kInt)
                        result += "int " + k + " = " + std::to_string(v.intVal) + ";";
                    else if (v.type == AnnotationValue::kBool)
                        result += "bool " + k + " = " + (v.boolVal ? "true" : "false") + ";";
                }
                result += "> ";

                TechniqueMeta tm;
                tm.name = techName;
                ApplyTechAnnotations(tm, merged);
                outTechs.push_back(std::move(tm));
            }
            else {
                // Subsequent techniques: no annotations
                result += "technique11 " + techName + " ";

                TechniqueMeta tm;
                tm.name = techName;
                if (!techAnnots.empty()) ApplyTechAnnotations(tm, techAnnots);
                outTechs.push_back(std::move(tm));
            }

            result += techBody + "\n";
            techIndex++;
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  #pragma uidefine transformation
//
//  #pragma uidefine(int MY_DEFINE < annotations > = 0)
//  →
//  #define MY_DEFINE 0   (or value from .fx.ini)
// ═══════════════════════════════════════════════════════════════════════════

std::string ShaderPreProcessor::TransformUiDefines(
    const std::string& src,
    const std::string& shaderFile,
    std::vector<ParameterMeta>& outParams,
    std::vector<PreProcessResult::DefinePair>& outDefines)
{
    std::string result;
    result.reserve(src.size());
    size_t pos = 0;
    const char* kw = "#pragma uidefine";
    size_t kwLen = 16;

    while (pos < src.size()) {
        size_t pragmaPos = src.find(kw, pos);
        if (pragmaPos == std::string::npos) {
            result.append(src, pos, src.size() - pos);
            break;
        }

        // Copy everything before
        result.append(src, pos, pragmaPos - pos);
        pos = pragmaPos + kwLen;

        // Skip to '('
        SkipWhitespace(src, pos);
        if (pos >= src.size() || src[pos] != '(') {
            result += "/* SB: malformed uidefine */\n";
            continue;
        }
        pos++; // skip '('

        // Read type
        std::string type = ReadIdentifier(src, pos);
        // Read name
        std::string name = ReadIdentifier(src, pos);

        // Read annotations if present
        SkipWhitespace(src, pos);
        std::unordered_map<std::string, AnnotationValue> annots;
        if (pos < src.size() && src[pos] == '<') {
            annots = ParseAnnotationBlock(src, pos);
        }

        // Read default value after '='
        SkipWhitespace(src, pos);
        std::string defaultVal = "0";
        if (pos < src.size() && src[pos] == '=') {
            pos++; // skip '='
            SkipWhitespace(src, pos);
            // Read until ')' or end of line (handling continuation with '\')
            size_t valStart = pos;
            while (pos < src.size() && src[pos] != ')') {
                if (src[pos] == '\\' && pos + 1 < src.size()) pos++; // skip escaped
                pos++;
            }
            defaultVal = src.substr(valStart, pos - valStart);
            // Trim whitespace from default value
            while (!defaultVal.empty() && std::isspace(static_cast<unsigned char>(defaultVal.back())))
                defaultVal.pop_back();
            while (!defaultVal.empty() && std::isspace(static_cast<unsigned char>(defaultVal.front())))
                defaultVal.erase(defaultVal.begin());
        }

        // Skip closing ')'
        if (pos < src.size() && src[pos] == ')') pos++;

        // TODO: load saved value from .fx.ini (for now, use default)
        std::string value = defaultVal;

        // Emit #define
        result += "#define " + name + " " + value + "\n";

        // Record the define
        outDefines.push_back({name, value});

        // Record metadata
        ParameterMeta meta;
        meta.varName    = name;
        meta.hlslType   = type;
        meta.shaderFile = shaderFile;
        ApplyAnnotations(meta, annots);
        if (type == "int" || type == "uint") {
            try { meta.defaultInt = std::stoi(defaultVal); } catch (...) {}
        }
        else if (type == "float") {
            try { meta.defaultFloat[0] = std::stof(defaultVal); } catch (...) {}
        }
        else if (type == "bool") {
            meta.defaultBool = (defaultVal == "true" || defaultVal == "1");
        }
        outParams.push_back(std::move(meta));
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Strip string variable declarations (UI-only markers)
//
//  string myGroup < bool UIGroupBegin = true; > = "My Group";
//  → replaced with whitespace (preserving line count)
// ═══════════════════════════════════════════════════════════════════════════

std::string ShaderPreProcessor::StripStringVars(const std::string& src,
                                                 std::vector<ParameterMeta>& outParams,
                                                 const std::string& shaderFile)
{
    std::string result;
    result.reserve(src.size());
    size_t pos = 0;

    while (pos < src.size()) {
        // Look for "string" keyword
        size_t spos = src.find("string", pos);
        if (spos == std::string::npos) {
            result.append(src, pos, src.size() - pos);
            break;
        }

        // Word boundary check
        if (spos > 0 && (std::isalnum(static_cast<unsigned char>(src[spos - 1])) || src[spos - 1] == '_')) {
            result.append(src, pos, spos + 6 - pos);
            pos = spos + 6;
            continue;
        }
        // Also check after
        size_t afterKw = spos + 6;
        if (afterKw < src.size() && (std::isalnum(static_cast<unsigned char>(src[afterKw])) || src[afterKw] == '_')) {
            result.append(src, pos, afterKw - pos);
            pos = afterKw;
            continue;
        }

        // Check if this is a variable declaration (not inside a function/annotation)
        // Look ahead for identifier then '<'
        size_t probe = afterKw;
        SkipWhitespace(src, probe);
        size_t nameStart = probe;
        std::string varName;
        while (probe < src.size() && (std::isalnum(static_cast<unsigned char>(src[probe])) || src[probe] == '_'))
            varName += src[probe++];

        if (varName.empty()) {
            // "string" as annotation type keyword — not a declaration
            result.append(src, pos, afterKw - pos);
            pos = afterKw;
            continue;
        }

        SkipWhitespace(src, probe);
        if (probe >= src.size() || src[probe] != '<') {
            // No annotation — might be a string decl without annotations, skip it
            // But it could also be "string" in other contexts; leave it alone
            result.append(src, pos, afterKw - pos);
            pos = afterKw;
            continue;
        }

        // This IS a string variable with annotations — parse and strip
        size_t declStart = spos;

        // Parse annotations
        auto annots = ParseAnnotationBlock(src, probe);

        // Skip default value
        SkipWhitespace(src, probe);
        if (probe < src.size() && src[probe] == '=') {
            probe++;
            SkipWhitespace(src, probe);
            if (probe < src.size() && src[probe] == '"') {
                ReadQuotedString(src, probe);
            }
        }

        // Skip semicolon
        SkipWhitespace(src, probe);
        if (probe < src.size() && src[probe] == ';') probe++;

        size_t declEnd = probe;

        // Copy everything before the declaration
        result.append(src, pos, declStart - pos);

        // Replace declaration with spaces (preserve newlines for line numbers)
        for (size_t i = declStart; i < declEnd; i++) {
            result += (src[i] == '\n') ? '\n' : ' ';
        }

        // Record metadata
        ParameterMeta meta;
        meta.varName    = varName;
        meta.hlslType   = "string";
        meta.shaderFile = shaderFile;
        ApplyAnnotations(meta, annots);
        outParams.push_back(std::move(meta));

        pos = declEnd;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Main entry point
// ═══════════════════════════════════════════════════════════════════════════

PreProcessResult ShaderPreProcessor::Process(const std::string& source,
                                              const std::string& sourceName)
{
    m_processCount++;

    // Check source cache
    uint64_t hash = HashSource(source);
    {
        std::lock_guard lock(m_cacheMtx);
        auto it = m_cache.find(hash);
        if (it != m_cache.end()) {
            m_cacheHits++;
            return it->second.result;
        }
    }

    PreProcessResult result;

    // Fast path: no annotations or extended syntax
    bool hasAnnotations = source.find('<') != std::string::npos;
    bool hasFxGroup     = source.find("fxgroup") != std::string::npos;
    bool hasUiDefine    = source.find("uidefine") != std::string::npos;

    if (!hasAnnotations && !hasFxGroup && !hasUiDefine) {
        result.cleanedSource = source;
        // Cache and return
        std::lock_guard lock(m_cacheMtx);
        m_cache[hash] = {result};
        return result;
    }

    // Strip comments for analysis (but keep original for transformation)
    std::string stripped = StripComments(source);
    std::string working = source;

    // 1. Transform fxgroup blocks
    if (hasFxGroup) {
        working = TransformFxGroups(working, result.techniques);
        result.hadFxGroups = true;
    }

    // 2. Transform #pragma uidefine
    if (hasUiDefine) {
        working = TransformUiDefines(working, sourceName,
                                      result.parameters, result.extraDefines);
        result.hadUiDefines = !result.extraDefines.empty();
    }

    // 3. Strip string variable declarations (UI markers)
    working = StripStringVars(working, result.parameters, sourceName);

    // 4. Scan remaining variables for annotations (use comment-stripped version)
    std::string workingStripped = StripComments(working);
    ScanVariables(workingStripped, sourceName, result.parameters);

    // 5. Scan techniques (if not already done by fxgroup transform)
    if (!hasFxGroup) {
        ScanTechniques(workingStripped, result.techniques);
    }

    // Update flags
    for (auto& p : result.parameters) {
        if (p.separation != ParameterMeta::Separation::None)
            result.hadSeparation = true;
        if (!p.externBinding.empty())
            result.hadExternBindings = true;
    }

    result.cleanedSource = std::move(working);

    // Store in annotation database
    if (!result.parameters.empty()) {
        AnnotationDatabase::Get().MergeFromShader(sourceName, result.parameters);
    }
    if (!result.techniques.empty()) {
        AnnotationDatabase::Get().MergeTechniques(sourceName, result.techniques);
    }

    // Log summary
    if (!result.parameters.empty() || result.hadFxGroups || result.hadUiDefines) {
        int sepCount = 0, externCount = 0, bindCount = 0;
        for (auto& p : result.parameters) {
            if (p.separation != ParameterMeta::Separation::None) sepCount++;
            if (!p.externBinding.empty()) externCount++;
            if (!p.uiBinding.empty()) bindCount++;
        }
        SKSE::log::info("ShaderPreProcessor: '{}' — {} params, {} separated, "
            "{} extern, {} bound, {} techniques, {} uidefines",
            sourceName, result.parameters.size(), sepCount,
            externCount, bindCount, result.techniques.size(),
            result.extraDefines.size());
    }

    // Cache result
    {
        std::lock_guard lock(m_cacheMtx);
        m_cache[hash] = {result};
    }

    return result;
}

} // namespace SB
