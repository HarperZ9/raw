#include "ParameterBindingEngine.h"
#include "ENBInterface.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <cctype>

namespace SB
{

ParameterBindingEngine& ParameterBindingEngine::Get()
{
    static ParameterBindingEngine inst;
    return inst;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Parse condition string like "==1", "!=0", "<5", ">=2.5"
// ═══════════════════════════════════════════════════════════════════════════

BindingOp ParameterBindingEngine::ParseOp(const std::string& condition, float& outValue)
{
    if (condition.empty()) {
        outValue = 0.f;
        return BindingOp::Always;
    }

    size_t pos = 0;
    BindingOp op = BindingOp::Equal;

    if (condition.size() >= 2 && condition[0] == '=' && condition[1] == '=') {
        op = BindingOp::Equal; pos = 2;
    } else if (condition.size() >= 2 && condition[0] == '!' && condition[1] == '=') {
        op = BindingOp::NotEqual; pos = 2;
    } else if (condition.size() >= 2 && condition[0] == '<' && condition[1] == '=') {
        op = BindingOp::LessEqual; pos = 2;
    } else if (condition.size() >= 2 && condition[0] == '>' && condition[1] == '=') {
        op = BindingOp::GreaterEqual; pos = 2;
    } else if (condition[0] == '<') {
        op = BindingOp::Less; pos = 1;
    } else if (condition[0] == '>') {
        op = BindingOp::Greater; pos = 1;
    } else if (condition[0] == '=') {
        op = BindingOp::Equal; pos = 1;
    }

    // Skip whitespace
    while (pos < condition.size() && condition[pos] == ' ') ++pos;

    // Parse number
    try {
        outValue = std::stof(condition.substr(pos));
    } catch (...) {
        outValue = 0.f;
    }

    return op;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Build rules from AnnotationDatabase
// ═══════════════════════════════════════════════════════════════════════════

void ParameterBindingEngine::RebuildRules()
{
    auto& db = AnnotationDatabase::Get();
    int currentCount = db.GetParameterCount();

    // Only rebuild if database has changed
    if (currentCount == m_lastParamCount && !m_rules.empty())
        return;

    m_lastParamCount = currentCount;
    m_rules.clear();
    m_state.clear();

    auto boundParams = db.GetBoundParameters();

    for (const auto* meta : boundParams) {
        if (meta->uiBinding.empty()) continue;

        BindingRule rule;
        rule.targetKey = meta->GetUniqueKey();
        rule.sourceKey = meta->uiBinding;
        rule.sourceShader = meta->uiBindingFile;
        rule.property = meta->uiBindingProperty;

        // Parse condition
        rule.op = ParseOp(meta->uiBindingCondition, rule.compareValue);

        // Default property to "hidden" if not specified
        if (rule.property.empty())
            rule.property = "hidden";

        m_rules.push_back(std::move(rule));
    }

    if (!m_rules.empty()) {
        static bool s_logged = false;
        if (!s_logged) {
            SKSE::log::info("ParameterBindingEngine: {} rules built", m_rules.size());
            s_logged = true;
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Evaluate all binding rules
// ═══════════════════════════════════════════════════════════════════════════

void ParameterBindingEngine::Evaluate()
{
    if (m_rules.empty()) {
        // Check for new rules every 60 frames
        static int s_rebuildTimer = 0;
        if (++s_rebuildTimer < 60) return;
        s_rebuildTimer = 0;
        RebuildRules();
        if (m_rules.empty()) return;
    }

    // Throttle evaluation — binding state rarely changes (every 10 frames)
    static int s_evalTimer = 0;
    if (++s_evalTimer < 10) return;
    s_evalTimer = 0;

    // Reset all states
    for (auto& [key, state] : m_state) {
        state.hidden = false;
        state.readOnly = false;
    }

    m_activeCount = 0;

    for (auto& rule : m_rules) {
        // Read source parameter value via ENBGetParameter
        float sourceValue = 0.f;

        if (ENBInterface::GetParameter) {
            // Try reading from the source shader
            const char* shader = rule.sourceShader.empty()
                ? nullptr
                : rule.sourceShader.c_str();

            // Uppercase shader name — ENB's internal lookup is case-sensitive
            std::string shaderUpper = shader ? shader : "";
            for (auto& c : shaderUpper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            ENBInterface::ENBParameter outParam;
            if (ENBInterface::GetParameter(nullptr,
                shaderUpper.c_str(),
                rule.sourceKey.c_str(),
                &outParam) && outParam.Size >= 4) {
                std::memcpy(&sourceValue, outParam.Data, sizeof(float));
            }
        }

        // Evaluate condition
        bool met = false;
        switch (rule.op) {
        case BindingOp::Equal:        met = (sourceValue == rule.compareValue); break;
        case BindingOp::NotEqual:     met = (sourceValue != rule.compareValue); break;
        case BindingOp::Less:         met = (sourceValue <  rule.compareValue); break;
        case BindingOp::LessEqual:    met = (sourceValue <= rule.compareValue); break;
        case BindingOp::Greater:      met = (sourceValue >  rule.compareValue); break;
        case BindingOp::GreaterEqual: met = (sourceValue >= rule.compareValue); break;
        case BindingOp::Always:       met = true; break;
        }

        rule.conditionMet = met;
        if (!met) continue;

        ++m_activeCount;

        // Apply property
        auto& state = m_state[rule.targetKey];
        if (rule.property == "hidden")          state.hidden = true;
        else if (rule.property == "visible")    state.hidden = false;
        else if (rule.property == "readonly")   state.readOnly = true;
        else if (rule.property == "readwrite")  state.readOnly = false;
    }
}


bool ParameterBindingEngine::IsHidden(const std::string& paramKey) const
{
    auto it = m_state.find(paramKey);
    return (it != m_state.end()) ? it->second.hidden : false;
}


bool ParameterBindingEngine::IsReadOnly(const std::string& paramKey) const
{
    auto it = m_state.find(paramKey);
    return (it != m_state.end()) ? it->second.readOnly : false;
}

} // namespace SB
