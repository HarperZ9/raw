#pragma once
//=============================================================================
//  ENBParamsCompute.h — ENB Shader Parameter Computation
//
//  Computes the ENBParams01 float4 (bloom amount, lens amount) and other
//  per-frame values from the TOD-interpolated configuration parameters.
//  These are passed to shaders via the common constant buffer.
//=============================================================================

#include <cstring>

struct ENBShaderParams
{
    // ENBParams01: x = bloom amount, y = lens amount
    float bloomAmount  = 0.1f;
    float lensAmount   = 1.0f;

    // Adaptation parameters: x=min, y=max, z=sensitivity, w=time*elapsed
    float adaptMin     = 0.1f;
    float adaptMax     = 10.0f;
    float adaptSens    = 0.5f;
    float adaptTime    = 1.0f;

    // Bloom size (for enbbloom.fx BloomSize uniform)
    float bloomSizeX   = 1024.0f;
    float bloomSizeY   = 1024.0f;
};

class ENBParamsCompute
{
public:
    // Compute all per-frame shader parameters from current ENB state
    void Update(float frameTime);

    const ENBShaderParams& GetParams() const { return m_params; }

private:
    ENBShaderParams m_params;
};

extern ENBParamsCompute g_ENBParams;
