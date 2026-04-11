#include "SystemHealth.h"
#include <cstring>
#include <cstdio>

namespace SB
{

uint32_t SystemHealth::Register(const char* name, SystemCategory cat)
{
    if (m_count >= kMaxSystems)
        return kMaxSystems - 1;  // saturate

    uint32_t id = m_count++;
    auto& sys = m_systems[id];
    sys.name = name;
    sys.category = cat;
    sys.status = HealthStatus::Unknown;
    sys.initialized = false;
    sys.enabled = true;
    sys.hasError = false;
    sys.errorCount = 0;
    sys.warningCount = 0;
    sys.framesSinceUpdate = 0;
    sys.message[0] = '\0';
    return id;
}

void SystemHealth::SetInitialized(uint32_t id, bool ok)
{
    if (id >= m_count) return;
    auto& sys = m_systems[id];
    sys.initialized = ok;
    if (!ok) {
        sys.hasError = true;
        snprintf(sys.message, sizeof(sys.message), "Initialization failed");
    }
    sys.framesSinceUpdate = 0;
}

void SystemHealth::SetEnabled(uint32_t id, bool enabled)
{
    if (id >= m_count) return;
    m_systems[id].enabled = enabled;
    m_systems[id].framesSinceUpdate = 0;
}

void SystemHealth::ReportError(uint32_t id, const char* msg)
{
    if (id >= m_count) return;
    auto& sys = m_systems[id];
    sys.errorCount++;
    sys.hasError = true;
    if (msg) {
        snprintf(sys.message, sizeof(sys.message), "%s", msg);
    }
}

void SystemHealth::ReportWarning(uint32_t id, const char* msg)
{
    if (id >= m_count) return;
    auto& sys = m_systems[id];
    sys.warningCount++;
    if (msg && sys.errorCount == 0) {
        snprintf(sys.message, sizeof(sys.message), "%s", msg);
    }
}

void SystemHealth::Heartbeat(uint32_t id)
{
    if (id >= m_count) return;
    m_systems[id].framesSinceUpdate = 0;
}

void SystemHealth::ClearErrors(uint32_t id)
{
    if (id >= m_count) return;
    auto& sys = m_systems[id];
    sys.errorCount = 0;
    sys.warningCount = 0;
    sys.hasError = false;
    sys.message[0] = '\0';
}

const SystemEntry& SystemHealth::GetSystem(uint32_t id) const
{
    static SystemEntry dummy{};
    if (id >= m_count) return dummy;
    return m_systems[id];
}

uint32_t SystemHealth::GetGreenCount() const
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m_count; ++i)
        if (m_systems[i].status == HealthStatus::Green) ++n;
    return n;
}

uint32_t SystemHealth::GetYellowCount() const
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m_count; ++i)
        if (m_systems[i].status == HealthStatus::Yellow) ++n;
    return n;
}

uint32_t SystemHealth::GetRedCount() const
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m_count; ++i)
        if (m_systems[i].status == HealthStatus::Red) ++n;
    return n;
}

void SystemHealth::EvaluateAll()
{
    for (uint32_t i = 0; i < m_count; ++i) {
        m_systems[i].framesSinceUpdate++;
        EvaluateSystem(m_systems[i]);
    }
}

void SystemHealth::EvaluateSystem(SystemEntry& sys)
{
    // Not initialized → Red
    if (!sys.initialized) {
        sys.status = HealthStatus::Red;
        return;
    }

    // Disabled (intentionally) → Yellow
    if (!sys.enabled) {
        sys.status = HealthStatus::Yellow;
        if (sys.message[0] == '\0')
            snprintf(sys.message, sizeof(sys.message), "Disabled");
        return;
    }

    // Persistent errors → Red
    if (sys.errorCount >= kErrorThreshold) {
        sys.status = HealthStatus::Red;
        return;
    }

    // Recent error → Yellow
    if (sys.hasError || sys.errorCount > 0) {
        sys.status = HealthStatus::Yellow;
        return;
    }

    // Stale (no heartbeat) → Yellow
    if (sys.framesSinceUpdate > kStaleThreshold) {
        sys.status = HealthStatus::Yellow;
        if (sys.message[0] == '\0')
            snprintf(sys.message, sizeof(sys.message), "Stale (%u frames)", sys.framesSinceUpdate);
        return;
    }

    // Warnings only → Yellow
    if (sys.warningCount > 0) {
        sys.status = HealthStatus::Yellow;
        return;
    }

    // All good
    sys.status = HealthStatus::Green;
}

} // namespace SB
