//=============================================================================
//  FPSLimiter.cpp — High-precision frame rate limiter
//
//  Uses busy-wait with SwitchToThread() for sub-millisecond precision.
//  Sleep() alone has ~15ms granularity which is too coarse.
//=============================================================================

#include "FPSLimiter.h"

FPSLimiter g_FPSLimiter;

void FPSLimiter::Initialize(bool enabled, float targetFPS)
{
    m_enabled = enabled;
    m_targetFPS = targetFPS;
    m_targetFrameTime = (targetFPS > 0.0f) ? 1.0 / targetFPS : 0.0;
    QueryPerformanceFrequency(&m_frequency);
    m_firstFrame = true;
}

void FPSLimiter::Wait()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (m_firstFrame)
    {
        m_lastFrame = now;
        m_firstFrame = false;
        return;
    }

    // Measure actual frame time
    double elapsed = static_cast<double>(now.QuadPart - m_lastFrame.QuadPart) /
                     static_cast<double>(m_frequency.QuadPart);

    m_frameTimeMs = static_cast<float>(elapsed * 1000.0);
    if (elapsed > 0.0)
        m_currentFPS = m_currentFPS * 0.9f + static_cast<float>(1.0 / elapsed) * 0.1f;

    if (!m_enabled || m_targetFrameTime <= 0.0)
    {
        m_lastFrame = now;
        return;
    }

    // Wait until target frame time
    double remaining = m_targetFrameTime - elapsed;

    if (remaining > 0.002) // >2ms: sleep most of it
    {
        DWORD sleepMs = static_cast<DWORD>((remaining - 0.002) * 1000.0);
        if (sleepMs > 0)
            Sleep(sleepMs);
    }

    // Busy-wait the remaining time for precision
    for (;;)
    {
        QueryPerformanceCounter(&now);
        elapsed = static_cast<double>(now.QuadPart - m_lastFrame.QuadPart) /
                  static_cast<double>(m_frequency.QuadPart);
        if (elapsed >= m_targetFrameTime)
            break;
        SwitchToThread(); // yield to other threads
    }

    m_lastFrame = now;
}
