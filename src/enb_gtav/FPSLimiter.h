#pragma once
//=============================================================================
//  FPSLimiter.h — Frame Rate Limiter
//
//  High-precision frame limiter using QueryPerformanceCounter.
//  Configured from enblocal.ini [LIMITER] section.
//=============================================================================

#include <Windows.h>

class FPSLimiter
{
public:
    void Initialize(bool enabled, float targetFPS);
    void SetEnabled(bool enabled)    { m_enabled = enabled; }
    void SetTargetFPS(float fps)     { m_targetFPS = fps; m_targetFrameTime = 1.0 / fps; }

    // Call at the end of each frame. Busy-waits if needed to hit target.
    void Wait();

    float GetCurrentFPS()   const { return m_currentFPS; }
    float GetFrameTimeMs()  const { return m_frameTimeMs; }

private:
    bool   m_enabled         = false;
    float  m_targetFPS       = 60.0f;
    double m_targetFrameTime = 1.0 / 60.0;

    LARGE_INTEGER m_frequency = {};
    LARGE_INTEGER m_lastFrame = {};
    bool          m_firstFrame = true;

    float  m_currentFPS  = 60.0f;
    float  m_frameTimeMs = 16.67f;
};

extern FPSLimiter g_FPSLimiter;
