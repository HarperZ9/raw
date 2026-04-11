#pragma once
//=============================================================================
//  DXBCPatcher.h — DXBC bytecode patching for material ID UAV writes
//
//  Hooks CreatePixelShader to capture shader bytecode, PSSetShader to track
//  the active PS, and DrawIndexed to coordinate UAV binding. Patches
//  BSLightingShader pixel shaders to add a store_uav_typed instruction
//  that writes the current material type to a R8_UINT UAV at u4.
//
//  Phase 2 of ENB-only material-aware rendering pipeline.
//=============================================================================

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11Buffer;

namespace SB
{
    class DXBCPatcher
    {
    public:
        static DXBCPatcher& Get();

        // Install hooks on Device::CreatePixelShader, Context::PSSetShader,
        // and Context::DrawIndexed. Call once during kDataLoaded.
        bool Install(ID3D11Device* a_device, ID3D11DeviceContext* a_ctx);

        bool IsInstalled() const { return m_installed; }

        // Statistics
        uint32_t GetCapturedCount() const { return m_capturedCount; }
        uint32_t GetPatchedCount()  const { return m_patchedCount; }
        uint32_t GetDrawHookCount() const { return m_drawHookCount; }
        uint32_t GetPatchFailCount() const { return m_patchFailCount; }

        // Called by CreatePixelShader hook: store bytecode for later patching
        void OnShaderCreated(ID3D11PixelShader* a_shader,
                             const void* a_bytecode, size_t a_size);

        // Called by PSSetShader hook: track current PS
        void OnShaderSet(ID3D11PixelShader* a_shader);

        // Called by DrawIndexed hook: if in lighting pass, swap PS + bind UAV
        void OnBeforeDraw(ID3D11DeviceContext* a_ctx);
        void OnAfterDraw(ID3D11DeviceContext* a_ctx);

        void Shutdown();

        // Public accessor for RenderInspector: get captured bytecode for a PS
        std::vector<uint8_t> GetBytecode(ID3D11PixelShader* a_shader) const {
            auto it = m_bytecodeStore.find(a_shader);
            if (it != m_bytecodeStore.end()) return it->second;
            return {};
        }

    private:
        DXBCPatcher() = default;

        // Create the material type constant buffer (16 bytes at b15)
        bool CreateMaterialCB(ID3D11Device* a_device);

        // Update the material type CB with current material from MaterialTracker
        void UpdateMaterialCB(ID3D11DeviceContext* a_ctx, uint8_t a_materialType);

        // Try to patch a shader's bytecode to add UAV write.
        // Returns the patched PS, or nullptr on failure.
        ID3D11PixelShader* PatchShader(ID3D11Device* a_device,
                                       ID3D11PixelShader* a_original);

        // Bytecode storage: original PS → captured bytecode
        std::unordered_map<ID3D11PixelShader*, std::vector<uint8_t>> m_bytecodeStore;

        // Patch cache: original PS → patched PS (nullptr = patch failed)
        std::unordered_map<ID3D11PixelShader*, ID3D11PixelShader*> m_patchCache;

        ID3D11Buffer*      m_materialCB = nullptr;
        ID3D11PixelShader* m_currentPS  = nullptr;  // currently bound PS
        ID3D11Device*      m_device     = nullptr;

        bool     m_installed       = false;
        bool     m_swappedThisDraw = false;
        uint32_t m_capturedCount   = 0;
        uint32_t m_patchedCount    = 0;
        uint32_t m_patchFailCount  = 0;
        uint32_t m_drawHookCount   = 0;
    };

    // ── DXBC Bytecode Patching ──────────────────────────────────────────
    // Modifies SM5.0 pixel shader bytecode to add:
    //   dcl_uav_typed_texture2d (uint,uint,uint,uint) u4
    //   dcl_constantbuffer CB15[1], immediateIndexed
    //   dcl_temps +1 (for ftou conversion)
    //   ftou rN.xy, vP.xyxx      (convert SV_Position float→uint)
    //   store_uav_typed u4.xyzw, rN.xyxx, CB15[0].xxxx

    struct PatchResult
    {
        std::vector<uint8_t> bytecode;  // Patched DXBC blob
        bool success = false;
        const char* error = nullptr;
    };

    PatchResult PatchShaderForMaterialWrite(
        const void*  a_bytecode,
        size_t       a_size);

} // namespace SB
