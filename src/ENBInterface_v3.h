#pragma once
//=============================================================================
//  ENBInterface_v3.h — Convenience wrappers for ENB SDK access
//
//  SkyrimBridge v3.0 - Additional helper functions for Phase 2-4 components
//  that build on the base ENBInterface.
//=============================================================================

#include "ENBInterface.h"
#include "BridgeData.h"
#include <cstring>
#include <string>

namespace ENBInterface
{
    //=========================================================================
    //  Convenience functions for v3 components
    //=========================================================================

    // Set a single float parameter to a specific shader
    inline void SetFloat(const char* shader, const char* group,
                         const char* name, float value)
    {
        if (!SetParameter) return;
        SetParameter(shader, group, name, &value, sizeof(float));
    }

    // Get a single float parameter from a specific shader
    inline float GetFloat(const char* shader, const char* group,
                          const char* name)
    {
        float value = 0.0f;
        if (GetParameter) {
            GetParameter(shader, group, name, &value, sizeof(float));
        }
        return value;
    }

    // Set a Float4 parameter to a specific shader
    inline void SetFloat4Shader(const char* shader, const char* group,
                                const char* name, const SB::Float4& value)
    {
        if (!SetParameter) return;
        SetParameter(shader, group, name,
                     const_cast<void*>(static_cast<const void*>(&value)), 16);
    }

    // Set a Float4 parameter to all target shaders (uses empty group)
    inline void SetFloat4(const char* name, const SB::Float4& value)
    {
        if (!SetParameter) return;
        void* ptr = const_cast<void*>(static_cast<const void*>(&value));
        for (const auto* shader : SB::kTargetShaders) {
            SetParameter(shader, "", name, ptr, 16);
        }
    }

    // Set a float to all target shaders
    inline void SetFloatAll(const char* name, float value)
    {
        if (!SetParameter) return;
        for (const auto* shader : SB::kTargetShaders) {
            SetParameter(shader, "", name, &value, sizeof(float));
        }
    }
}
