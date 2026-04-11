//=============================================================================
//  ENBParamsCompute.cpp — Compute per-frame shader parameters
//
//  Reads TOD-interpolated values from ConfigManager and computes the
//  final ENBParams01 (bloom/lens amounts) and adaptation parameters.
//=============================================================================

#include "ENBParamsCompute.h"
#include "ConfigManager.h"
#include "ENBState.h"
#include "TimeOfDaySystem.h"
#include <algorithm>

ENBParamsCompute g_ENBParams;

void ENBParamsCompute::Update(float frameTime)
{
    const float* todWeights = reinterpret_cast<const float*>(&g_ENB.todFactorDawn);

    // Bloom amount: from [BLOOM] AmountDawn/Sunrise/Day/Sunset/Dusk/Night
    m_params.bloomAmount = g_Config.GetTODFloat("BLOOM", "Amount", todWeights);

    // Lens amount: from [LENS] AmountDawn/Sunrise/Day/Sunset/Dusk/Night
    m_params.lensAmount = g_Config.GetTODFloat("LENS", "Amount", todWeights);

    // Adaptation parameters from enbseries.ini [ADAPTATION]
    // These are NOT time-of-day variant — they're flat values
    // (read once during config load, but we compute adaptTime*elapsed each frame)
    const char* f = g_Config.GetGameDir();
    char enbPath[MAX_PATH];
    snprintf(enbPath, MAX_PATH, "%s\\enbseries.ini", f);

    // Read adaptation section (cached after first load, but cheap to re-read)
    char buf[64];
    GetPrivateProfileStringA("ADAPTATION", "AdaptationMin", "0.1", buf, sizeof(buf), enbPath);
    m_params.adaptMin = static_cast<float>(atof(buf));

    GetPrivateProfileStringA("ADAPTATION", "AdaptationMax", "10.0", buf, sizeof(buf), enbPath);
    m_params.adaptMax = static_cast<float>(atof(buf));

    GetPrivateProfileStringA("ADAPTATION", "AdaptationSensitivity", "0.5", buf, sizeof(buf), enbPath);
    m_params.adaptSens = static_cast<float>(atof(buf));

    GetPrivateProfileStringA("ADAPTATION", "AdaptationTime", "1.0", buf, sizeof(buf), enbPath);
    float adaptTimeBase = static_cast<float>(atof(buf));

    // AdaptationTime * elapsed time (clamped to prevent huge jumps)
    float clampedFrameTime = (std::min)(frameTime, 0.1f); // cap at 100ms
    m_params.adaptTime = 1.0f - expf(-clampedFrameTime * adaptTimeBase);
    m_params.adaptTime = (std::max)(0.0f, (std::min)(1.0f, m_params.adaptTime));

    // Bloom texture is always 1024x1024
    m_params.bloomSizeX = 1024.0f;
    m_params.bloomSizeY = 1024.0f;
}
