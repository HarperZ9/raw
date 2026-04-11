//=============================================================================
//  MaterialTracker.cpp — BSShader::BeginTechnique hook implementation
//
//  Hooks BSShader::BeginTechnique via a 14-byte prologue detour.
//  The SKSE Trampoline's write_branch<5> is designed for patching existing
//  JMP/CALL instructions, not function prologues. We instead:
//    1. Allocate relay memory from SKSE trampoline (near game module)
//    2. Copy the first 14 bytes of the original function to the relay
//    3. Append an absolute JMP back to original+14 in the relay
//    4. Overwrite the original function start with an absolute JMP to our hook
//    5. Save the relay address as the "original" function pointer
//
//  This is the standard x64 prologue detour technique.
//=============================================================================

#include "MaterialTracker.h"
#include "RenderInspector.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <REL/Relocation.h>

namespace SB
{
    // ── Hook internals (anonymous namespace) ─────────────────────────────

    namespace
    {
        // Original function signature:
        //   bool BSShader::BeginTechnique(uint32_t vDesc, uint32_t pDesc, bool skipPS)
        // At ABI level (x64 fastcall):
        //   RCX = this, RDX = vDesc, R8 = pDesc, R9 = skipPS
        using BeginTechnique_t = bool(__fastcall*)(RE::BSShader*, uint32_t, uint32_t, bool);
        BeginTechnique_t s_originalBeginTechnique = nullptr;

        // BSLightingShader's shaderType value.
        // From engine RE: BSLightingShader sets shaderType = 6 at offset 0x20.
        // Verified via fxpFilename check on first encounter.
        constexpr int32_t kBSLightingShaderType = 6;
        bool s_typeVerified = false;

        // The hooked function
        bool __fastcall HookedBeginTechnique(
            RE::BSShader* a_shader,
            uint32_t      a_vertexDescriptor,
            uint32_t      a_pixelDescriptor,
            bool          a_skipPixelShader)
        {
            ++detail::g_hookCallCount;
            detail::g_inLightingPass = (a_shader && a_shader->shaderType == kBSLightingShaderType);

            if (detail::g_inLightingPass) {
                // One-time verification: confirm via fxpFilename
                if (!s_typeVerified) {
                    if (a_shader->fxpFilename) {
                        const char* name = a_shader->fxpFilename;
                        bool isLighting = false;
                        // Check if filename contains "Lighting" (case-sensitive)
                        for (const char* p = name; *p; ++p) {
                            if (p[0] == 'L' && p[1] == 'i' && p[2] == 'g' && p[3] == 'h' && p[4] == 't') {
                                isLighting = true;
                                break;
                            }
                        }
                        if (isLighting) {
                            s_typeVerified = true;
                            SKSE::log::info("MaterialTracker: confirmed BSLightingShader (type={}, fxp='{}')",
                                a_shader->shaderType, name);
                        } else {
                            SKSE::log::warn("MaterialTracker: shaderType {} but fxp='{}' — not BSLightingShader!",
                                a_shader->shaderType, name);
                        }
                    }
                }

                // Extract technique from pixel descriptor bits [29:24]
                uint32_t technique = (a_pixelDescriptor >> 24) & 0x3F;
                detail::g_currentMaterial = MaterialTracker::ClassifyTechnique(technique);
                detail::g_currentTechnique = technique;
                ++detail::g_lightingCallCount;
            }

            // Domain R: Notify RenderInspector of technique change
            RenderInspector::Get().OnBeginTechnique(a_shader, (a_pixelDescriptor >> 24) & 0x3F);

            return s_originalBeginTechnique(a_shader, a_vertexDescriptor, a_pixelDescriptor, a_skipPixelShader);
        }

    } // anonymous namespace

    // ── MaterialTracker implementation ───────────────────────────────────

    MaterialTracker& MaterialTracker::Get()
    {
        static MaterialTracker instance;
        return instance;
    }

    bool MaterialTracker::Install()
    {
        if (m_installed)
            return true;

        // Resolve BSShader::BeginTechnique address via Address Library
        // SE ID: 101341, AE ID: 108328
        REL::RelocationID beginTechniqueID(101341, 108328);
        auto addr = beginTechniqueID.address();

        if (!addr) {
            SKSE::log::error("MaterialTracker: failed to resolve BeginTechnique address");
            return false;
        }

        SKSE::log::info("MaterialTracker: BeginTechnique resolved at {:#X}", addr);

        // Allocate trampoline memory for the prologue relay.
        // We need: 14 bytes (stolen prologue) + 14 bytes (absolute JMP back) = 28 bytes
        // Plus 14 bytes for the hook relay (absolute JMP to our function) = 42 total
        // AllocTrampoline allocates from SKSE's reserved trampoline (near game module,
        // so the memory is within ±2GB for rel32 jumps if needed).
        SKSE::AllocTrampoline(64);
        auto& trampoline = SKSE::GetTrampoline();

        constexpr size_t kStealSize = 14;  // bytes to overwrite at original function

        // ── Build the "original" relay ──────────────────────────────────
        // Layout: [stolen 14 bytes] [FF 25 00000000 addr+14]
        auto* originalRelay = static_cast<uint8_t*>(trampoline.allocate(kStealSize + 14));

        // Copy the original prologue bytes
        std::memcpy(originalRelay, reinterpret_cast<void*>(addr), kStealSize);

        // Append absolute JMP back to original function + kStealSize
        // FF 25 00000000 = JMP [RIP+0], followed by 8-byte absolute address
        originalRelay[kStealSize + 0]  = 0xFF;
        originalRelay[kStealSize + 1]  = 0x25;
        originalRelay[kStealSize + 2]  = 0x00;
        originalRelay[kStealSize + 3]  = 0x00;
        originalRelay[kStealSize + 4]  = 0x00;
        originalRelay[kStealSize + 5]  = 0x00;
        *reinterpret_cast<uint64_t*>(originalRelay + kStealSize + 6) =
            static_cast<uint64_t>(addr + kStealSize);

        s_originalBeginTechnique = reinterpret_cast<BeginTechnique_t>(originalRelay);

        // ── Patch the original function start ───────────────────────────
        // Write a 14-byte absolute JMP to our hook function.
        // FF 25 00000000 [8-byte address of HookedBeginTechnique]
        uint8_t hookJmp[14];
        hookJmp[0] = 0xFF;
        hookJmp[1] = 0x25;
        hookJmp[2] = 0x00;
        hookJmp[3] = 0x00;
        hookJmp[4] = 0x00;
        hookJmp[5] = 0x00;
        *reinterpret_cast<uint64_t*>(hookJmp + 6) =
            reinterpret_cast<uint64_t>(&HookedBeginTechnique);

        REL::safe_write(addr, hookJmp, sizeof(hookJmp));

        m_installed = true;
        SKSE::log::info("MaterialTracker: BeginTechnique hook installed "
            "(original relay at {:#X}, hook at {:#X})",
            reinterpret_cast<uintptr_t>(originalRelay),
            reinterpret_cast<uintptr_t>(&HookedBeginTechnique));

        return true;
    }

    MaterialType MaterialTracker::ClassifyTechnique(uint32_t a_technique)
    {
        // BSLightingShader technique IDs (from ShaderCache.h / engine RE):
        //   0  = None (default)
        //   1  = Envmap           → MetalGlossy
        //   2  = Glowmap          → Emissive
        //   3  = Parallax         → General
        //   4  = Facegen          → Skin
        //   5  = FacegenRGBTint   → Skin
        //   6  = Hair             → Hair
        //   7  = ParallaxOcc      → General
        //   8  = MTLand           → Terrain
        //   9  = LODLand          → Terrain
        //  10  = (unused)
        //  11  = MultilayerParallax → MetalGlossy
        //  12  = TreeAnim         → Vegetation
        //  13  = (unused)
        //  14  = MultiIndexSparkle → MetalGlossy (crystal/ice)
        //  15  = (unused)
        //  16  = Eye              → Eye
        //  17  = (cloud?)
        //  18  = LODLandNoise     → Terrain
        //  19  = MTLandLODBlend   → Terrain
        switch (a_technique) {
        case 4:
        case 5:
            return MaterialType::Skin;

        case 6:
            return MaterialType::Hair;

        case 16:
            return MaterialType::Eye;

        case 1:
        case 11:
        case 14:
            return MaterialType::MetalGlossy;

        case 8:
        case 9:
        case 18:
        case 19:
            return MaterialType::Terrain;

        case 12:
            return MaterialType::Vegetation;

        case 2:
            return MaterialType::Emissive;

        default:
            return MaterialType::General;
        }
    }

} // namespace SB
