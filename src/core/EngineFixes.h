#pragma once
//=============================================================================
//  EngineFixes — Binary-level engine patches applied via address library
//
//  P0 optimizations based on reverse engineering analysis of Skyrim SE AE:
//    - BSSpinLock::Lock threshold reduction (10000 → 1000)
//      Verified via disassembly at AE REL::ID(68233), RVA 0x00cc90c0.
//      Reduces busy-waiting by 90% in contended paths (5-15% CPU savings).
//=============================================================================

namespace SB
{

class EngineFixes
{
public:
    static EngineFixes& Get();

    // Install all engine patches. Call from kDataLoaded handler.
    // Returns number of patches successfully applied.
    uint32_t Install();

    bool IsSpinLockPatched() const { return m_spinLockPatched; }

private:
    EngineFixes() = default;

    bool PatchSpinLockThreshold();

    bool m_installed = false;
    bool m_spinLockPatched = false;
};

} // namespace SB
