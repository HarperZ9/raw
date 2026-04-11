#pragma once
//=============================================================================
//  ParameterBindingEngine.h — Real-time parameter-to-parameter binding
//
//  Evaluates UIBinding annotations from the ShaderPreProcessor:
//    UIBinding = "SourceParam"        — the parameter to watch
//    UIBindingCondition = "==1"       — when to apply the property
//    UIBindingProperty = "hidden"     — what to do (hidden/visible/readonly/readwrite)
//    UIBindingFile = "shader.fx"      — which shader's param (optional)
//
//  Updates ParameterMeta runtime state each frame for GUI and ENB panel use.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "ShaderPreProcessor.h"
#include <string>
#include <vector>

namespace SB
{

// ── Condition operators ──────────────────────────────────────────────

enum class BindingOp : uint8_t
{
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, Always
};

// ── Parsed binding rule ──────────────────────────────────────────────

struct BindingRule
{
    std::string targetKey;      // Parameter being controlled
    std::string sourceKey;      // Parameter being watched (UIBinding value)
    std::string sourceShader;   // Optional: source shader file (UIBindingFile)
    BindingOp   op = BindingOp::Always;
    float       compareValue = 0.f;
    std::string property;       // hidden, visible, readonly, readwrite

    // Resolved state
    bool conditionMet = false;
};

// ── Parameter Binding Engine ─────────────────────────────────────────

class ParameterBindingEngine
{
public:
    static ParameterBindingEngine& Get();

    // Build rules from AnnotationDatabase (call after shaders have compiled)
    void RebuildRules();

    // Evaluate all rules against current parameter values
    // Reads source param values via ENBGetParameter
    void Evaluate();

    // Query binding state for a parameter
    bool IsHidden(const std::string& paramKey) const;
    bool IsReadOnly(const std::string& paramKey) const;

    int GetRuleCount() const { return static_cast<int>(m_rules.size()); }
    int GetActiveCount() const { return m_activeCount; }
    const std::vector<BindingRule>& GetRules() const { return m_rules; }

private:
    static BindingOp ParseOp(const std::string& condition, float& outValue);

    std::vector<BindingRule> m_rules;
    int m_activeCount = 0;

    // Runtime state: paramKey → visibility/readonly flags
    struct ParamState { bool hidden = false; bool readOnly = false; };
    std::unordered_map<std::string, ParamState> m_state;

    int m_lastParamCount = 0;  // Track AnnotationDatabase changes
};

} // namespace SB
