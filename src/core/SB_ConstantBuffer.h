#pragma once
//=============================================================================
//  SB_ConstantBuffer.h — Direct D3D11 constant buffer for shader data
//
//  Bypasses ENBSetParameter entirely. Maps AllData into a GPU constant buffer
//  bound to register(b7) every frame. Shaders declare:
//      cbuffer SkyrimBridgeData : register(b7) { float4 SB_*; ... }
//
//  A vtable hook on PSSetConstantBuffers guards slot b7 from being
//  overwritten by ENB or the game engine.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "BridgeData.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;

namespace SB
{
    class ConstantBuffer
    {
    public:
        static ConstantBuffer& Get()
        {
            static ConstantBuffer inst;
            return inst;
        }

        // Constant buffer slot — shaders use register(b7)
        static constexpr int kSlot = 7;

        // Create the GPU buffer (DYNAMIC, CONSTANT_BUFFER, sizeof(AllData) bytes)
        bool Initialize(ID3D11Device* a_device);

        // Map AllData → GPU, bind to PS slot b7 (and VS slot b7)
        void UpdateAndBind(ID3D11DeviceContext* a_ctx, const AllData& a_data);

        // Hook PSSetConstantBuffers vtable to prevent anything from overwriting b7
        bool HookPSSetConstantBuffers(ID3D11DeviceContext* a_ctx);

        // Release GPU resources and restore hooks
        void Shutdown();

        bool IsActive() const { return m_active; }

    private:
        ConstantBuffer() = default;
        ~ConstantBuffer() { Shutdown(); }

        ConstantBuffer(const ConstantBuffer&) = delete;
        ConstantBuffer& operator=(const ConstantBuffer&) = delete;

        ID3D11Buffer* m_buffer = nullptr;
        bool          m_active = false;
        bool          m_hooked = false;
    };

}  // namespace SB
