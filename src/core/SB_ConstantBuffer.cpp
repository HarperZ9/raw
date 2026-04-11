#include "SB_ConstantBuffer.h"
#include "RenderInspector.h"

#include <d3d11.h>
#include <cstring>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// D3D11 cbuffer alignment: size must be multiple of 16 bytes
static_assert(sizeof(SB::AllData) % 16 == 0,
    "AllData must be 16-byte aligned for D3D11 constant buffer");

namespace SB
{
    // ── PSSetConstantBuffers vtable hook ──────────────────────────────────
    // Prevents ENB, the game engine, or anything else from overwriting our
    // slot b7 after we bind it.  ID3D11DeviceContext vtable index 16.
    //
    // If a call would touch slot 7, we split it into up to two calls that
    // skip our slot.  This is the same VirtualProtect pattern used by
    // D3D11Hook.cpp for the Present hook.

    static constexpr int kVtableIndex_PSSetConstantBuffers = 16;

    using PSSetConstantBuffersFn = void(__stdcall*)(
        ID3D11DeviceContext*,   // this
        UINT StartSlot,
        UINT NumBuffers,
        ID3D11Buffer* const* ppConstantBuffers);

    static PSSetConstantBuffersFn s_originalPSSetCB = nullptr;

    static void __stdcall HookedPSSetConstantBuffers(
        ID3D11DeviceContext* a_ctx,
        UINT a_startSlot,
        UINT a_numBuffers,
        ID3D11Buffer* const* a_ppBuffers)
    {
        // Domain R: Track CB bindings for RenderInspector
        RenderInspector::Get().OnPSSetConstantBuffers(a_ctx, a_startSlot, a_numBuffers, a_ppBuffers);

        UINT endSlot = a_startSlot + a_numBuffers;
        UINT guardSlot = static_cast<UINT>(ConstantBuffer::kSlot);

        // Fast path: call doesn't touch our slot at all
        if (endSlot <= guardSlot || a_startSlot > guardSlot) {
            s_originalPSSetCB(a_ctx, a_startSlot, a_numBuffers, a_ppBuffers);
            return;
        }

        // Slow path: split around slot b7
        // Part 1: slots before b7
        if (a_startSlot < guardSlot) {
            UINT count = guardSlot - a_startSlot;
            s_originalPSSetCB(a_ctx, a_startSlot, count, a_ppBuffers);
        }

        // Part 2: slots after b7
        if (endSlot > guardSlot + 1) {
            UINT afterStart = guardSlot + 1;
            UINT afterCount = endSlot - afterStart;
            UINT bufferOffset = afterStart - a_startSlot;
            s_originalPSSetCB(a_ctx, afterStart, afterCount, a_ppBuffers + bufferOffset);
        }

        // Slot b7 itself is silently dropped — our buffer stays bound.
    }

    // ── ConstantBuffer implementation ────────────────────────────────────

    bool ConstantBuffer::Initialize(ID3D11Device* a_device)
    {
        if (m_active)
            return true;

        if (!a_device) {
            SKSE::log::error("SB_ConstantBuffer: null device");
            return false;
        }

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth      = sizeof(AllData);
        desc.Usage           = D3D11_USAGE_DYNAMIC;
        desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = a_device->CreateBuffer(&desc, nullptr, &m_buffer);
        if (FAILED(hr) || !m_buffer) {
            SKSE::log::error("SB_ConstantBuffer: CreateBuffer failed (hr=0x{:X}, size={})",
                static_cast<unsigned>(hr), sizeof(AllData));
            return false;
        }

        m_active = true;
        SKSE::log::info("SB_ConstantBuffer: initialized ({} bytes, slot b{})",
            sizeof(AllData), kSlot);
        return true;
    }

    void ConstantBuffer::UpdateAndBind(ID3D11DeviceContext* a_ctx, const AllData& a_data)
    {
        if (!m_active || !m_buffer || !a_ctx)
            return;

        static std::size_t s_updateCount = 0;

        // Map with WRITE_DISCARD for zero-stall GPU upload
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = a_ctx->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            if (s_updateCount < 10) {
                SKSE::log::error("SB_ConstantBuffer: Map failed (hr=0x{:X})",
                    static_cast<unsigned>(hr));
            }
            return;
        }

        std::memcpy(mapped.pData, &a_data, sizeof(AllData));
        a_ctx->Unmap(m_buffer, 0);

        // Bind to both pixel and vertex shader stages at slot b7.
        // Use the ORIGINAL function to bypass our own hook (we ARE the one setting b7).
        if (s_originalPSSetCB) {
            s_originalPSSetCB(a_ctx, static_cast<UINT>(kSlot), 1, &m_buffer);
        } else {
            a_ctx->PSSetConstantBuffers(static_cast<UINT>(kSlot), 1, &m_buffer);
        }
        a_ctx->VSSetConstantBuffers(static_cast<UINT>(kSlot), 1, &m_buffer);

        ++s_updateCount;

        // Periodic logging
        if (s_updateCount == 1 || s_updateCount == 100 || s_updateCount == 1000) {
            SKSE::log::info("SB_ConstantBuffer: update #{} — {} bytes → b{} (hooked={})",
                s_updateCount, sizeof(AllData), kSlot, m_hooked ? "yes" : "no");
        }
    }

    bool ConstantBuffer::HookPSSetConstantBuffers(ID3D11DeviceContext* a_ctx)
    {
        if (m_hooked)
            return true;

        if (!a_ctx) {
            SKSE::log::error("SB_ConstantBuffer: null context for vtable hook");
            return false;
        }

        // Get vtable pointer from the context object
        auto** vtable = *reinterpret_cast<void***>(a_ctx);

        // Save original function
        s_originalPSSetCB = reinterpret_cast<PSSetConstantBuffersFn>(
            vtable[kVtableIndex_PSSetConstantBuffers]);

        // Overwrite vtable entry with our hook
        DWORD oldProtect;
        if (VirtualProtect(&vtable[kVtableIndex_PSSetConstantBuffers],
                sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            vtable[kVtableIndex_PSSetConstantBuffers] =
                reinterpret_cast<void*>(&HookedPSSetConstantBuffers);
            VirtualProtect(&vtable[kVtableIndex_PSSetConstantBuffers],
                sizeof(void*), oldProtect, &oldProtect);

            m_hooked = true;
            SKSE::log::info("SB_ConstantBuffer: PSSetConstantBuffers hooked (vtable[{}], guarding b{})",
                kVtableIndex_PSSetConstantBuffers, kSlot);
            return true;
        }

        SKSE::log::error("SB_ConstantBuffer: VirtualProtect failed for PSSetConstantBuffers hook");
        return false;
    }

    void ConstantBuffer::Shutdown()
    {
        // Note: we don't restore the vtable hook on shutdown because the context
        // may already be destroyed. The hook is safe to leave — it just calls
        // the original function for all slots except b7.

        if (m_buffer) {
            m_buffer->Release();
            m_buffer = nullptr;
        }

        m_active = false;
        m_hooked = false;
    }

}  // namespace SB
