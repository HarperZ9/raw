//=============================================================================
//  DXBCPatcher.cpp — DXBC bytecode patching + D3D11 hook implementation
//
//  Three vtable hooks:
//    CreatePixelShader (Device vtable[15]) — capture bytecode
//    PSSetShader       (Context vtable[9]) — track active PS
//    DrawIndexed       (Context vtable[12]) — bind UAV + swap PS
//
//  DXBC patching adds a store_uav_typed instruction to BSLightingShader
//  pixel shaders so they write the current material type (from CB15) to
//  an R8_UINT UAV at u4. The material type is set per-draw by the
//  DrawIndexed hook, reading from MaterialTracker.
//=============================================================================

#include "DXBCPatcher.h"

#include <d3d11.h>
#include <SKSE/SKSE.h>
#include <cstring>

namespace SB
{
    // ══════════════════════════════════════════════════════════════════════
    //  DXBC Format Constants
    // ══════════════════════════════════════════════════════════════════════

    namespace dxbc
    {
        constexpr uint32_t kMagic = 0x43425844;  // 'DXBC'

        // Chunk FourCCs
        constexpr uint32_t kSHEX = 0x58454853;  // 'SHEX'
        constexpr uint32_t kSHDR = 0x52444853;  // 'SHDR'
        constexpr uint32_t kISGN = 0x4E475349;  // 'ISGN'

        // SM5.0 Opcodes (bits [10:0] of instruction token)
        constexpr uint32_t OP_MOV      = 0x36;
        constexpr uint32_t OP_FTOU     = 0x38;
        constexpr uint32_t OP_RET      = 0x3E;
        constexpr uint32_t OP_DCL_TEMPS = 0x68;
        constexpr uint32_t OP_DCL_CONSTANT_BUFFER = 0x59;
        constexpr uint32_t OP_DCL_UAV_TYPED = 0x9C;
        constexpr uint32_t OP_STORE_UAV_TYPED = 0xA4;

        // Operand types (bits [19:12] of operand token)
        constexpr uint32_t OT_TEMP     = 0;
        constexpr uint32_t OT_INPUT    = 1;
        constexpr uint32_t OT_CB       = 8;
        constexpr uint32_t OT_UAV      = 30;

        // SV_Position system value in ISGN
        constexpr uint32_t SV_POSITION = 1;

        // Resource dimension for Texture2D
        constexpr uint32_t RES_DIM_TEXTURE2D = 3;

        // Return type component: UINT = 4
        constexpr uint32_t RET_TYPE_UINT = 4;

        // ── Helper: encode an operand token ─────────────────────────────
        inline uint32_t MakeOperand4Mask(uint32_t type, uint32_t mask, uint32_t indexDim)
        {
            return 2                 // 4-component
                | (0 << 2)           // mask mode
                | (mask << 4)        // component mask
                | (type << 12)       // operand type
                | (indexDim << 20)   // index dimension
                | (0 << 22);         // index 0 = immediate32
        }

        inline uint32_t MakeOperand4Swizzle(uint32_t type, uint32_t swizzle, uint32_t indexDim)
        {
            return 2                 // 4-component
                | (1 << 2)           // swizzle mode
                | (swizzle << 4)     // swizzle bits
                | (type << 12)       // operand type
                | (indexDim << 20)   // index dimension
                | (0 << 22);         // index 0 = immediate32
        }

        inline uint32_t MakeOperand4Swizzle2D(uint32_t type, uint32_t swizzle)
        {
            return 2                 // 4-component
                | (1 << 2)           // swizzle mode
                | (swizzle << 4)     // swizzle bits
                | (type << 12)       // operand type
                | (2 << 20)          // 2D index
                | (0 << 22)          // index 0 = immediate32
                | (0 << 25);         // index 1 = immediate32
        }

        // Swizzle encoding: each component is 2 bits (x=0, y=1, z=2, w=3)
        constexpr uint32_t SWIZZLE_XYXX = (0) | (1 << 2) | (0 << 4) | (0 << 6);
        constexpr uint32_t SWIZZLE_XXXX = (0) | (0 << 2) | (0 << 4) | (0 << 6);

        // Instruction length encoding in opcode token
        inline uint32_t MakeOpcode(uint32_t op, uint32_t lengthDWORDs)
        {
            return op | (lengthDWORDs << 24);
        }

        // Get instruction length from opcode token
        inline uint32_t InsnLength(uint32_t token)
        {
            uint32_t len = (token >> 24) & 0x7F;
            return len ? len : 1;  // 0 length means extended, treat as 1 for safety
        }

        // Get opcode from token
        inline uint32_t InsnOpcode(uint32_t token)
        {
            return token & 0x7FF;
        }

    } // namespace dxbc

    // ══════════════════════════════════════════════════════════════════════
    //  DXBC Bytecode Patching
    // ══════════════════════════════════════════════════════════════════════

    // Find the SV_Position register number from the ISGN chunk
    static int FindSVPositionRegister(const uint8_t* isgnData, uint32_t isgnSize)
    {
        if (isgnSize < 8) return -1;

        uint32_t elementCount = *reinterpret_cast<const uint32_t*>(isgnData);
        // uint32_t pad = *(isgnData + 4);  // always 8

        const uint8_t* elements = isgnData + 8;
        constexpr uint32_t kElementSize = 24;  // bytes per ISGN element

        for (uint32_t i = 0; i < elementCount; ++i) {
            if (8 + (i + 1) * kElementSize > isgnSize) break;

            const uint8_t* elem = elements + i * kElementSize;
            // uint32_t nameOffset   = *(uint32_t*)(elem + 0);
            // uint32_t semanticIdx  = *(uint32_t*)(elem + 4);
            uint32_t systemValue  = *reinterpret_cast<const uint32_t*>(elem + 8);
            // uint32_t componentType = *(uint32_t*)(elem + 12);
            uint32_t reg          = *reinterpret_cast<const uint32_t*>(elem + 16);

            if (systemValue == dxbc::SV_POSITION) {
                return static_cast<int>(reg);
            }
        }

        return -1;  // Not found
    }

    PatchResult PatchShaderForMaterialWrite(const void* a_bytecode, size_t a_size)
    {
        PatchResult result{};
        const auto* bytes = static_cast<const uint8_t*>(a_bytecode);

        // ── Validate DXBC header ────────────────────────────────────────
        if (a_size < 32) {
            result.error = "DXBC too small";
            return result;
        }

        uint32_t magic = *reinterpret_cast<const uint32_t*>(bytes);
        if (magic != dxbc::kMagic) {
            result.error = "bad DXBC magic";
            return result;
        }

        uint32_t totalSize  = *reinterpret_cast<const uint32_t*>(bytes + 24);
        uint32_t chunkCount = *reinterpret_cast<const uint32_t*>(bytes + 28);

        if (totalSize > a_size || chunkCount > 16) {
            result.error = "DXBC header mismatch";
            return result;
        }

        const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(bytes + 32);

        // ── Find SHEX/SHDR and ISGN chunks ──────────────────────────────
        uint32_t shexOffset = 0, shexSize = 0;
        uint32_t isgnOffset = 0, isgnSize = 0;
        uint32_t shexChunkIdx = UINT32_MAX;

        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t off = chunkOffsets[i];
            if (off + 8 > a_size) continue;

            uint32_t fourCC = *reinterpret_cast<const uint32_t*>(bytes + off);
            uint32_t size   = *reinterpret_cast<const uint32_t*>(bytes + off + 4);

            if (fourCC == dxbc::kSHEX || fourCC == dxbc::kSHDR) {
                shexOffset = off + 8;  // data starts after FourCC + size
                shexSize   = size;
                shexChunkIdx = i;
            } else if (fourCC == dxbc::kISGN) {
                isgnOffset = off + 8;
                isgnSize   = size;
            }
        }

        if (!shexOffset || !shexSize) {
            result.error = "no SHEX/SHDR chunk";
            return result;
        }

        // ── Find SV_Position register ───────────────────────────────────
        int svPosReg = -1;
        if (isgnOffset && isgnSize) {
            svPosReg = FindSVPositionRegister(bytes + isgnOffset, isgnSize);
        }
        if (svPosReg < 0) {
            result.error = "SV_Position not found in ISGN";
            return result;
        }

        // ── Parse SHEX tokens ───────────────────────────────────────────
        const uint32_t* shexTokens = reinterpret_cast<const uint32_t*>(bytes + shexOffset);
        uint32_t shexDWORDs = shexSize / 4;

        if (shexDWORDs < 2) {
            result.error = "SHEX too small";
            return result;
        }

        // uint32_t versionToken = shexTokens[0];
        // uint32_t lengthToken  = shexTokens[1]; // total DWORDs in SHEX data

        // Scan for: dcl_temps, end of declarations, and last ret
        uint32_t dclTempsOffset = UINT32_MAX;  // DWORD offset within SHEX
        uint32_t dclTempsValue  = 0;
        uint32_t firstInsnOffset = UINT32_MAX;
        uint32_t lastRetOffset   = UINT32_MAX;

        uint32_t pos = 2;  // skip version + length tokens
        while (pos < shexDWORDs) {
            uint32_t token = shexTokens[pos];
            uint32_t opcode = dxbc::InsnOpcode(token);
            uint32_t len    = dxbc::InsnLength(token);

            if (len == 0 || pos + len > shexDWORDs)
                break;

            // Track dcl_temps
            if (opcode == dxbc::OP_DCL_TEMPS && pos + 1 < shexDWORDs) {
                dclTempsOffset = pos;
                dclTempsValue  = shexTokens[pos + 1];
            }

            // Track first non-declaration instruction
            // Declarations have opcodes >= 0x42 (roughly)
            if (firstInsnOffset == UINT32_MAX && opcode < 0x42) {
                firstInsnOffset = pos;
            }

            // Track last ret
            if (opcode == dxbc::OP_RET) {
                lastRetOffset = pos;
            }

            pos += len;
        }

        if (firstInsnOffset == UINT32_MAX) {
            result.error = "no instructions found";
            return result;
        }
        if (lastRetOffset == UINT32_MAX) {
            result.error = "no ret instruction found";
            return result;
        }

        // ── Determine new temp register ─────────────────────────────────
        uint32_t newTempReg = dclTempsValue;  // We'll use register index = old count
        uint32_t newTempCount = dclTempsValue + 1;

        // ── Build injected tokens ───────────────────────────────────────

        // 1. dcl_uav_typed_texture2d (uint,uint,uint,uint) u4
        //    4 DWORDs: opcode, operand, register, return_type
        uint32_t dclUAV[4] = {
            dxbc::MakeOpcode(dxbc::OP_DCL_UAV_TYPED, 4)
                | (dxbc::RES_DIM_TEXTURE2D << 11),           // opcode + dimension
            dxbc::MakeOperand4Mask(dxbc::OT_UAV, 0xF, 1),   // u?.xyzw, 1D index
            4,                                                // u4
            (dxbc::RET_TYPE_UINT)                             // return type packed
                | (dxbc::RET_TYPE_UINT << 5)
                | (dxbc::RET_TYPE_UINT << 10)
                | (dxbc::RET_TYPE_UINT << 15)
        };

        // 2. dcl_constantbuffer CB15[1], immediateIndexed
        //    4 DWORDs: opcode, operand, CB_slot, element_count
        uint32_t dclCB[4] = {
            dxbc::MakeOpcode(dxbc::OP_DCL_CONSTANT_BUFFER, 4),
            dxbc::MakeOperand4Mask(dxbc::OT_CB, 0xF, 2),    // CB?.xyzw, 2D index
            15,                                               // CB15
            1                                                 // 1 element (16 bytes)
        };

        // 3. ftou rN.xy, v{svPosReg}.xyxx
        //    5 DWORDs: opcode, dst_operand, dst_reg, src_operand, src_reg
        uint32_t ftouInsn[5] = {
            dxbc::MakeOpcode(dxbc::OP_FTOU, 5),
            dxbc::MakeOperand4Mask(dxbc::OT_TEMP, 0x3, 1),  // rN.xy (mask = 0x3 = xy)
            newTempReg,
            dxbc::MakeOperand4Swizzle(dxbc::OT_INPUT, dxbc::SWIZZLE_XYXX, 1),
            static_cast<uint32_t>(svPosReg)
        };

        // 4. store_uav_typed u4.xyzw, rN.xyxx, CB15[0].xxxx
        //    8 DWORDs: opcode, dst_op, dst_reg, addr_op, addr_reg, val_op, val_cb, val_elem
        uint32_t storeInsn[8] = {
            dxbc::MakeOpcode(dxbc::OP_STORE_UAV_TYPED, 8),
            dxbc::MakeOperand4Mask(dxbc::OT_UAV, 0xF, 1),   // u4.xyzw
            4,                                                // u4
            dxbc::MakeOperand4Swizzle(dxbc::OT_TEMP, dxbc::SWIZZLE_XYXX, 1),
            newTempReg,                                       // rN.xyxx
            dxbc::MakeOperand4Swizzle2D(dxbc::OT_CB, dxbc::SWIZZLE_XXXX),
            15,                                               // CB15
            0                                                 // [0] (first element)
        };

        // ── Build patched SHEX ──────────────────────────────────────────
        // New SHEX = original tokens with:
        //   - dcl_temps value incremented by 1
        //   - dcl_uav_typed + dcl_constantbuffer inserted before first instruction
        //   - ftou + store_uav_typed inserted before last ret

        uint32_t addedDecls = 4 + 4;     // dclUAV + dclCB = 8 DWORDs
        uint32_t addedInsns = 5 + 8;     // ftou + store = 13 DWORDs
        uint32_t newShexDWORDs = shexDWORDs + addedDecls + addedInsns;

        std::vector<uint32_t> newShex;
        newShex.reserve(newShexDWORDs);

        // Copy up to firstInsnOffset (version + length + all declarations)
        for (uint32_t i = 0; i < firstInsnOffset; ++i) {
            uint32_t val = shexTokens[i];
            // Patch dcl_temps in-place
            if (i == dclTempsOffset + 1 && dclTempsOffset != UINT32_MAX) {
                val = newTempCount;
            }
            newShex.push_back(val);
        }

        // Insert our declarations
        newShex.insert(newShex.end(), dclUAV, dclUAV + 4);
        newShex.insert(newShex.end(), dclCB, dclCB + 4);

        // Copy instructions up to lastRetOffset (adjusted for inserted decls)
        for (uint32_t i = firstInsnOffset; i < lastRetOffset; ++i) {
            newShex.push_back(shexTokens[i]);
        }

        // Insert our instructions before ret
        newShex.insert(newShex.end(), ftouInsn, ftouInsn + 5);
        newShex.insert(newShex.end(), storeInsn, storeInsn + 8);

        // Copy ret and anything after
        for (uint32_t i = lastRetOffset; i < shexDWORDs; ++i) {
            newShex.push_back(shexTokens[i]);
        }

        // Update SHEX length token (index 1) = total DWORDs
        if (newShex.size() >= 2) {
            newShex[1] = static_cast<uint32_t>(newShex.size());
        }

        // ── Build patched DXBC ──────────────────────────────────────────
        uint32_t newShexBytes = static_cast<uint32_t>(newShex.size() * 4);
        int32_t shexDelta = static_cast<int32_t>(newShexBytes) - static_cast<int32_t>(shexSize);
        uint32_t newTotalSize = totalSize + shexDelta;

        result.bytecode.resize(newTotalSize);
        auto* out = result.bytecode.data();

        // Copy header (magic + checksum + version + totalSize + chunkCount)
        std::memcpy(out, bytes, 32);
        // Update total size
        *reinterpret_cast<uint32_t*>(out + 24) = newTotalSize;
        // Zero the checksum (release D3D runtime doesn't validate it)
        std::memset(out + 4, 0, 16);

        // Copy chunk offsets (adjusting offsets for chunks after SHEX)
        uint32_t* outOffsets = reinterpret_cast<uint32_t*>(out + 32);
        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t off = chunkOffsets[i];
            if (i > shexChunkIdx) {
                outOffsets[i] = off + shexDelta;
            } else {
                outOffsets[i] = off;
            }
        }

        // Copy chunks, replacing SHEX with our patched version
        uint32_t headerSize = 32 + chunkCount * 4;
        uint32_t writePos = headerSize;

        for (uint32_t i = 0; i < chunkCount; ++i) {
            uint32_t srcOff = chunkOffsets[i];
            if (srcOff + 8 > a_size) continue;

            uint32_t chunkFourCC = *reinterpret_cast<const uint32_t*>(bytes + srcOff);
            uint32_t chunkSize   = *reinterpret_cast<const uint32_t*>(bytes + srcOff + 4);

            // Update the output offset
            outOffsets[i] = writePos;

            if (i == shexChunkIdx) {
                // Write patched SHEX chunk
                *reinterpret_cast<uint32_t*>(out + writePos) = chunkFourCC;
                *reinterpret_cast<uint32_t*>(out + writePos + 4) = newShexBytes;
                std::memcpy(out + writePos + 8, newShex.data(), newShexBytes);
                writePos += 8 + newShexBytes;
            } else {
                // Copy chunk as-is (FourCC + size + data)
                uint32_t fullChunkSize = 8 + chunkSize;
                if (srcOff + fullChunkSize <= a_size) {
                    std::memcpy(out + writePos, bytes + srcOff, fullChunkSize);
                    writePos += fullChunkSize;
                }
            }
        }

        // Adjust final size
        result.bytecode.resize(writePos);
        *reinterpret_cast<uint32_t*>(result.bytecode.data() + 24) = writePos;

        result.success = true;
        return result;
    }

    // ══════════════════════════════════════════════════════════════════════
    //  D3D11 Vtable Hooks
    // ══════════════════════════════════════════════════════════════════════

    namespace
    {
        // Vtable indices
        constexpr uint32_t kVT_CreatePixelShader = 15;  // ID3D11Device
        constexpr uint32_t kVT_PSSetShader       = 9;   // ID3D11DeviceContext
        constexpr uint32_t kVT_DrawIndexed       = 12;  // ID3D11DeviceContext
        constexpr uint32_t kVT_Draw              = 13;  // ID3D11DeviceContext

        // Material CB slot
        constexpr uint32_t kMaterialCBSlot = 15;  // b15

        // ── CreatePixelShader hook ──────────────────────────────────────

        using CreatePixelShaderFn = HRESULT(__stdcall*)(
            ID3D11Device*,
            const void* pShaderBytecode,
            SIZE_T BytecodeLength,
            ID3D11ClassLinkage* pClassLinkage,
            ID3D11PixelShader** ppPixelShader);

        CreatePixelShaderFn s_origCreatePS = nullptr;

        HRESULT __stdcall HookedCreatePixelShader(
            ID3D11Device*       a_device,
            const void*         a_bytecode,
            SIZE_T              a_length,
            ID3D11ClassLinkage* a_linkage,
            ID3D11PixelShader** a_outPS)
        {
            HRESULT hr = s_origCreatePS(a_device, a_bytecode, a_length, a_linkage, a_outPS);
            if (SUCCEEDED(hr) && a_outPS && *a_outPS && a_bytecode && a_length > 0) {
                DXBCPatcher::Get().OnShaderCreated(*a_outPS, a_bytecode, a_length);
            }
            return hr;
        }

        // ── PSSetShader hook ────────────────────────────────────────────

        using PSSetShaderFn = void(__stdcall*)(
            ID3D11DeviceContext*,
            ID3D11PixelShader*,
            ID3D11ClassInstance* const*,
            UINT NumClassInstances);

        PSSetShaderFn s_origPSSetShader = nullptr;

        void __stdcall HookedPSSetShader(
            ID3D11DeviceContext*        a_ctx,
            ID3D11PixelShader*          a_shader,
            ID3D11ClassInstance* const* a_classInstances,
            UINT                        a_numInstances)
        {
            DXBCPatcher::Get().OnShaderSet(a_shader);
            s_origPSSetShader(a_ctx, a_shader, a_classInstances, a_numInstances);
        }

        // ── DrawIndexed hook ────────────────────────────────────────────

        using DrawIndexedFn = void(__stdcall*)(
            ID3D11DeviceContext*,
            UINT IndexCount,
            UINT StartIndexLocation,
            INT  BaseVertexLocation);

        DrawIndexedFn s_origDrawIndexed = nullptr;

        void __stdcall HookedDrawIndexed(
            ID3D11DeviceContext* a_ctx,
            UINT a_indexCount,
            UINT a_startIndex,
            INT  a_baseVertex)
        {
            DXBCPatcher::Get().OnBeforeDraw(a_ctx);
            s_origDrawIndexed(a_ctx, a_indexCount, a_startIndex, a_baseVertex);
            DXBCPatcher::Get().OnAfterDraw(a_ctx);
        }

        // ── Hook installer helper ───────────────────────────────────────
        bool HookVtableEntry(void** a_vtable, uint32_t a_index, void* a_hook, void** a_original)
        {
            *a_original = a_vtable[a_index];
            DWORD oldProtect;
            if (VirtualProtect(&a_vtable[a_index], sizeof(void*),
                    PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                a_vtable[a_index] = a_hook;
                VirtualProtect(&a_vtable[a_index], sizeof(void*),
                    oldProtect, &oldProtect);
                return true;
            }
            return false;
        }

    } // anonymous namespace

    // ══════════════════════════════════════════════════════════════════════
    //  DXBCPatcher Implementation
    // ══════════════════════════════════════════════════════════════════════

    DXBCPatcher& DXBCPatcher::Get()
    {
        static DXBCPatcher instance;
        return instance;
    }

    bool DXBCPatcher::Install(ID3D11Device* a_device, ID3D11DeviceContext* a_ctx)
    {
        if (m_installed)
            return true;

        if (!a_device || !a_ctx) {
            SKSE::log::error("DXBCPatcher: null device or context");
            return false;
        }

        m_device = a_device;

        // Create material type constant buffer
        if (!CreateMaterialCB(a_device)) {
            SKSE::log::error("DXBCPatcher: failed to create material CB");
            return false;
        }

        // Hook CreatePixelShader on the device
        auto** devVtable = *reinterpret_cast<void***>(a_device);
        if (!HookVtableEntry(devVtable, kVT_CreatePixelShader,
                reinterpret_cast<void*>(&HookedCreatePixelShader),
                reinterpret_cast<void**>(&s_origCreatePS)))
        {
            SKSE::log::error("DXBCPatcher: failed to hook CreatePixelShader");
            return false;
        }

        // Hook PSSetShader on the context
        auto** ctxVtable = *reinterpret_cast<void***>(a_ctx);
        if (!HookVtableEntry(ctxVtable, kVT_PSSetShader,
                reinterpret_cast<void*>(&HookedPSSetShader),
                reinterpret_cast<void**>(&s_origPSSetShader)))
        {
            SKSE::log::error("DXBCPatcher: failed to hook PSSetShader");
            return false;
        }

        // Hook DrawIndexed on the context
        if (!HookVtableEntry(ctxVtable, kVT_DrawIndexed,
                reinterpret_cast<void*>(&HookedDrawIndexed),
                reinterpret_cast<void**>(&s_origDrawIndexed)))
        {
            SKSE::log::error("DXBCPatcher: failed to hook DrawIndexed");
            return false;
        }

        m_installed = true;
        SKSE::log::info("DXBCPatcher: installed (CreatePS vtable[{}], PSSetShader vtable[{}], "
            "DrawIndexed vtable[{}])",
            kVT_CreatePixelShader, kVT_PSSetShader, kVT_DrawIndexed);
        return true;
    }

    void DXBCPatcher::OnShaderCreated(ID3D11PixelShader* a_shader,
                                       const void* a_bytecode, size_t a_size)
    {
        // Store bytecode for later patching
        auto& store = m_bytecodeStore[a_shader];
        store.resize(a_size);
        std::memcpy(store.data(), a_bytecode, a_size);
        ++m_capturedCount;
    }

    void DXBCPatcher::OnShaderSet(ID3D11PixelShader* a_shader)
    {
        m_currentPS = a_shader;
    }

    void DXBCPatcher::OnBeforeDraw(ID3D11DeviceContext* a_ctx)
    {
        ++m_drawHookCount;
        m_swappedThisDraw = false;

        // MaterialTracker/GBufferManager removed — OnBeforeDraw is now a no-op.
        // The DXBC patching infrastructure remains for future use but the
        // per-draw material classification that drove it has been removed.
        (void)a_ctx;
    }

    void DXBCPatcher::OnAfterDraw(ID3D11DeviceContext* a_ctx)
    {
        if (!m_swappedThisDraw)
            return;

        // Restore original PS
        if (m_currentPS) {
            s_origPSSetShader(a_ctx, m_currentPS, nullptr, 0);
        }

        m_swappedThisDraw = false;
    }

    ID3D11PixelShader* DXBCPatcher::PatchShader(ID3D11Device* a_device,
                                                  ID3D11PixelShader* a_original)
    {
        // Look up stored bytecode
        auto it = m_bytecodeStore.find(a_original);
        if (it == m_bytecodeStore.end()) {
            SKSE::log::trace("DXBCPatcher: no bytecode for PS {:X}",
                reinterpret_cast<uintptr_t>(a_original));
            ++m_patchFailCount;
            return nullptr;
        }

        const auto& bytecode = it->second;

        // Attempt DXBC patching
        PatchResult patchResult = PatchShaderForMaterialWrite(
            bytecode.data(), bytecode.size());

        if (!patchResult.success) {
            // Log at trace level (many shaders won't be BSLightingShader)
            SKSE::log::trace("DXBCPatcher: patch failed for PS {:X}: {}",
                reinterpret_cast<uintptr_t>(a_original),
                patchResult.error ? patchResult.error : "unknown");
            ++m_patchFailCount;
            return nullptr;
        }

        // Create the patched pixel shader
        ID3D11PixelShader* patchedPS = nullptr;
        HRESULT hr = s_origCreatePS(a_device,
            patchResult.bytecode.data(),
            patchResult.bytecode.size(),
            nullptr, &patchedPS);

        if (FAILED(hr) || !patchedPS) {
            SKSE::log::warn("DXBCPatcher: CreatePixelShader failed for patched PS (hr={:#X})",
                static_cast<uint32_t>(hr));
            ++m_patchFailCount;
            return nullptr;
        }

        ++m_patchedCount;
        if (m_patchedCount <= 5 || (m_patchedCount % 50) == 0) {
            SKSE::log::info("DXBCPatcher: patched shader #{} (original={:X}, patched={:X})",
                m_patchedCount,
                reinterpret_cast<uintptr_t>(a_original),
                reinterpret_cast<uintptr_t>(patchedPS));
        }

        return patchedPS;
    }

    bool DXBCPatcher::CreateMaterialCB(ID3D11Device* a_device)
    {
        // 16-byte CB: { materialType (uint), pad, pad, pad }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth      = 16;
        desc.Usage           = D3D11_USAGE_DYNAMIC;
        desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = a_device->CreateBuffer(&desc, nullptr, &m_materialCB);
        return SUCCEEDED(hr) && m_materialCB;
    }

    void DXBCPatcher::UpdateMaterialCB(ID3D11DeviceContext* a_ctx, uint8_t a_materialType)
    {
        if (!m_materialCB || !a_ctx)
            return;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(a_ctx->Map(m_materialCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            auto* data = static_cast<uint32_t*>(mapped.pData);
            data[0] = a_materialType;  // Material type as uint
            data[1] = 0;
            data[2] = 0;
            data[3] = 0;
            a_ctx->Unmap(m_materialCB, 0);
        }

        // Bind at b15
        a_ctx->PSSetConstantBuffers(kMaterialCBSlot, 1, &m_materialCB);
    }

    void DXBCPatcher::Shutdown()
    {
        // Release patched shaders
        for (auto& [orig, patched] : m_patchCache) {
            if (patched) patched->Release();
        }
        m_patchCache.clear();
        m_bytecodeStore.clear();

        if (m_materialCB) {
            m_materialCB->Release();
            m_materialCB = nullptr;
        }

        m_installed = false;
    }

} // namespace SB
