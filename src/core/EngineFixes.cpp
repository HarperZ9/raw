#include "EngineFixes.h"

namespace SB
{

EngineFixes& EngineFixes::Get()
{
    static EngineFixes instance;
    return instance;
}

uint32_t EngineFixes::Install()
{
    if (m_installed)
        return 0;
    m_installed = true;

    uint32_t count = 0;

    if (PatchSpinLockThreshold())
        ++count;

    SKSE::log::info("EngineFixes: {} patches applied", count);
    return count;
}

bool EngineFixes::PatchSpinLockThreshold()
{
    // BSSpinLock::Lock — AE RELOCATION_ID 68233
    // The function contains: cmp r8d, 0x2710    (compare spin count vs 10000)
    // The 4-byte immediate 0x00002710 is at offset +0x3C from function start.
    // Reducing to 1000 (0x3E8) cuts busy-waiting by 90% while preserving
    // the spin→yield→wait semantics.
    REL::Relocation<std::uintptr_t> lockFunc{ REL::ID(68233) };
    auto addr = lockFunc.address();

    if (addr == 0) {
        SKSE::log::warn("EngineFixes: BSSpinLock::Lock address resolution failed");
        return false;
    }

    // Validate: the 4 bytes at +0x3C should be 0x00002710 (10000 LE)
    uint32_t currentValue = *reinterpret_cast<const uint32_t*>(addr + 0x3C);
    if (currentValue != 10000) {
        SKSE::log::warn("EngineFixes: BSSpinLock threshold at {:#x}+0x3C = {} (expected 10000), skipping",
            addr, currentValue);
        return false;
    }

    REL::safe_write<uint32_t>(addr + 0x3C, 1000);
    m_spinLockPatched = true;

    SKSE::log::info("EngineFixes: BSSpinLock threshold reduced 10000 -> 1000 at {:#x}+0x3C", addr);
    return true;
}

} // namespace SB
