#pragma once
//=============================================================================
//  ExternBindingProcessor.h — Extender-compatible ExternBinding injection
//
//  Maps ENB Extender's ExternBinding annotation values to AllData fields
//  and injects them via ENBSetParameter each frame. Enables existing
//  Extender-compatible shaders to work without modification.
//
//  Supported bindings:
//    WVPMatColumn[0-3]        — ViewProjection matrix columns
//    InvWVPMatColumn[0-3]     — Inverse ViewProjection matrix columns
//    InvCamRotMatColumn[0-2]  — Inverse camera rotation matrix columns
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "BridgeData.h"
#include <string>
#include <vector>

namespace SB
{

class ExternBindingProcessor
{
public:
    static ExternBindingProcessor& Get();

    // Called each frame after AnnotationDatabase is populated.
    // Resolves all extern-bound parameters and pushes values via ENBSetParameter.
    void Update(const AllData& data);

    int GetBindingCount() const { return m_bindingCount; }
    int GetPushCount() const { return m_pushCount; }

private:
    // Resolve an ExternBinding name to a Float4 value from AllData
    static bool ResolveBinding(const std::string& bindingName,
                               const AllData& data,
                               Float4& outValue);

    // Extract column `col` from a row-major Float4x4
    static Float4 MatrixColumn(const Float4x4& mat, int col);

    int m_bindingCount = 0;
    int m_pushCount = 0;
};

} // namespace SB
