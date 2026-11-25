// ce_rebuild_arch.cpp: Clean-Room Creation Engine Subsystem Replacement Registry
// & Fail-Closed SEH-Safe Live Vtable/RTTI Contract Verifier.
// Original work, MIT.

#include "ce_rebuild_arch.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#include <excpt.h>
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#endif
#endif

namespace raw::ce_rebuild {
namespace {

// MSVC x64 RTTICompleteObjectLocator layout (signature == 1 -> RVA-based x64 ABI).
struct MsvcCompleteObjectLocator64 {
    uint32_t signature;        // Must be 1 for x64
    uint32_t offset;           // Offset of this vtable in complete class (0 = primary)
    uint32_t cdOffset;         // Constructor displacement offset
    uint32_t pTypeDescriptor;  // RVA to RTTITypeDescriptor
    uint32_t pClassDescriptor; // RVA to RTTIClassHierarchyDescriptor
    uint32_t pSelf;            // RVA to this COL itself (self-consistency check)
};

// Isolated POD-only SEH memory readers (no C++ objects with destructors -> MSVC C2712 clean).
bool SehReadU64(uintptr_t addr, uint64_t* outVal) noexcept {
    if (!addr || !outVal) return false;
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
        *outVal = *reinterpret_cast<const volatile uint64_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    *outVal = *reinterpret_cast<const volatile uint64_t*>(addr);
    return true;
#endif
}

bool SehReadCol64(uintptr_t addr, MsvcCompleteObjectLocator64* outCol) noexcept {
    if (!addr || !outCol) return false;
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
        const auto* src = reinterpret_cast<const uint32_t*>(addr);
        outCol->signature        = src[0];
        outCol->offset           = src[1];
        outCol->cdOffset         = src[2];
        outCol->pTypeDescriptor  = src[3];
        outCol->pClassDescriptor = src[4];
        outCol->pSelf            = src[5];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(outCol, reinterpret_cast<const void*>(addr), sizeof(MsvcCompleteObjectLocator64));
    return true;
#endif
}

bool SehReadBytes2(uintptr_t addr, char outBuf[2]) noexcept {
    if (!addr || !outBuf) return false;
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
        const auto* p = reinterpret_cast<const volatile char*>(addr);
        outBuf[0] = p[0];
        outBuf[1] = p[1];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    const auto* p = reinterpret_cast<const volatile char*>(addr);
    outBuf[0] = p[0];
    outBuf[1] = p[1];
    return true;
#endif
}

bool ParseHexU32(std::string_view sv, uint32_t* out) noexcept {
    if (!out) return false;
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
        sv.remove_prefix(2);
    }
    if (sv.empty()) return false;
    uint32_t val = 0;
    const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), val, 16);
    if (res.ec != std::errc{} || res.ptr != sv.data() + sv.size()) return false;
    *out = val;
    return true;
}

bool ParseDecU32(std::string_view sv, uint32_t* out) noexcept {
    if (!out) return false;
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    if (sv.empty()) return false;
    uint32_t val = 0;
    const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
    if (res.ec != std::errc{} || res.ptr != sv.data() + sv.size()) return false;
    *out = val;
    return true;
}

std::string JsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(ch); break;
        }
    }
    return out;
}

RebuildManifest BuildBaselineManifest() {
    RebuildManifest m{};
    m.targetBinaryVersion       = "SkyrimSE.exe 1.7.104.0";
    m.targetTimestamp           = kTargetTimestamp17104;
    m.targetSizeOfImage         = kTargetSizeOfImage17104;
    m.totalExtractedRttiClasses = kExtractedPrimaryClasses;
    m.totalSecondaryVtables     = kExtractedSecondaryVtables;
    m.allContractsVerified      = false;

    m.subsystems = {
        {
            SubsystemId::WorldStreaming,
            "WorldStreaming",
            ReplacementStage::Shadowed,
            "TES, TESObjectCELL, TESWorldSpace, GridCellArray, BGSStoryTeller",
            "RawWorldShowcase, CeFormatReader, rt_grids, rt_memory_guards",
            69u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::TerrainLOD,
            "TerrainLOD",
            ReplacementStage::Intercepted,
            "TESObjectLAND, TESLandTexture, BGSTerrainManager (0x3203348), BGSQueuedTerrainUpdate",
            "rt_terrain_lod, RawTerrainGen, RawBiomeWorld, RawWorldShowcase",
            15u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::WaterSim,
            "WaterSim",
            ReplacementStage::Intercepted,
            "TESWaterSystem (0x21A05E8), TESWaterObject, BSWaterShaderProperty",
            "RtWater, BridgeWater, RawFluidSimHost (RawFluidWaterSim.hlsl)",
            46u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::FloraGeometry,
            "FloraGeometry",
            ReplacementStage::Shadowed,
            "BGSGrassManager (0x21A05F8), TESObjectTREE, TESGrass, BSTreeNode, BSLeafAnimNode",
            "RawFloraEngine, RawFloraWind.hlsli, RawWorldShowcase, RawMeshHost",
            19u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::FluidCombustion,
            "FluidCombustion",
            ReplacementStage::Shadowed,
            "BSParticleShaderEmitter, NiParticleSystem, FlameProjectile, Hazard",
            "RawFluidSimHost (RawFluidFireSim.hlsl), RawSpells3D.hlsli",
            61u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::RenderPipeline,
            "RenderPipeline",
            ReplacementStage::Intercepted,
            "ShadowSceneNode, BSCullingProcess, BSGeometry, BSTriShape, BSLightingShaderProperty, NiCamera, BSLight, BSShadowLight",
            "raw::tx (D3D11->DX12), D3D12Bridge, RawMeshHost, RtShadow/RtSky/RtCache, HiZDownsample",
            259u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::MemoryAllocator,
            "MemoryAllocator",
            ReplacementStage::Observed,
            "MemoryManager (0x369A810), ScrapHeap, MemoryHeap, IMemoryHeap, LoadedLandData 37-slot pool",
            "rt_memory_guards, RawFrameArena (IAllocatorBackend)",
            201u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::JobScheduler,
            "JobScheduler",
            ReplacementStage::Observed,
            "TaskQueueInterface (0x3242650), BSTaskManager, BSTaskManagerThread, JobListManager",
            "RawAsyncCompute, RawAssetStream",
            80u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::PhysicsWorld,
            "PhysicsWorld",
            ReplacementStage::Observed,
            "bhkWorld, bhkWorldM, bhkCollisionObject, bhkRigidBody, hkpWorld",
            "rt_softbody, rt_skeleton_ik, CeNavMesh",
            865u, 0u, 0u, 0u, true
        },
        {
            SubsystemId::AssetArchive,
            "AssetArchive",
            ReplacementStage::Intercepted,
            "IOManager, BSResource::LooseFileLocation, BSResource::ArchiveStream, Archive",
            "CeFormatReader, CeNifReader, RawOverrideRegistry, RawAssetBridge, RawAssetScaffold",
            199u, 0u, 0u, 0u, true
        },
    };
    return m;
}

std::mutex& ManifestMutex() {
    static std::mutex mtx;
    return mtx;
}

RebuildManifest& MutableManifest() {
    static RebuildManifest manifest = BuildBaselineManifest();
    return manifest;
}

}  // namespace

bool ParseSubsystemId(std::string_view name, SubsystemId* outId) noexcept {
    if (!outId) return false;
    if (name == "WorldStreaming")  { *outId = SubsystemId::WorldStreaming;  return true; }
    if (name == "TerrainLOD")      { *outId = SubsystemId::TerrainLOD;      return true; }
    if (name == "WaterSim")        { *outId = SubsystemId::WaterSim;        return true; }
    if (name == "FloraGeometry")   { *outId = SubsystemId::FloraGeometry;   return true; }
    if (name == "FluidCombustion") { *outId = SubsystemId::FluidCombustion; return true; }
    if (name == "RenderPipeline")  { *outId = SubsystemId::RenderPipeline;  return true; }
    if (name == "MemoryAllocator") { *outId = SubsystemId::MemoryAllocator; return true; }
    if (name == "JobScheduler")    { *outId = SubsystemId::JobScheduler;    return true; }
    if (name == "PhysicsWorld")    { *outId = SubsystemId::PhysicsWorld;    return true; }
    if (name == "AssetArchive")    { *outId = SubsystemId::AssetArchive;    return true; }
    return false;
}

VtableVerifyResult VerifyClassVtable(uintptr_t moduleBase, const ClassContract& c) noexcept {
    VtableVerifyResult res{};
    res.className = c.className;
    res.subsystem = c.subsystem;

    if (moduleBase == 0u || c.vtableRva < 8u || c.colRva == 0u || c.expectedMethods.empty()) {
        res.detail = "invalid_contract_or_null_module_base";
        return res;
    }

    res.expectedColVa = static_cast<uint64_t>(moduleBase) + static_cast<uint64_t>(c.colRva);

    // Step 1: Read MSVC vtable meta-pointer at [vtable - 8] under SEH.
    uint64_t actualColVa = 0ull;
    if (!SehReadU64(moduleBase + static_cast<uintptr_t>(c.vtableRva - 8u), &actualColVa)) {
        res.memoryReadable = false;
        res.detail = "seh_fault_reading_vtable_col_meta";
        return res;
    }
    res.memoryReadable = true;
    res.actualColVa = actualColVa;
    res.colMatched = (actualColVa == res.expectedColVa);
    if (!res.colMatched) {
        res.detail = "col_pointer_mismatch";
        return res;
    }

    // Step 2: Validate MSVC x64 RTTICompleteObjectLocator self-consistency under SEH.
    MsvcCompleteObjectLocator64 col{};
    if (!SehReadCol64(moduleBase + static_cast<uintptr_t>(c.colRva), &col)) {
        res.memoryReadable = false;
        res.detail = "seh_fault_reading_col_struct";
        return res;
    }
    char tdPrefix[2] = {0, 0};
    const bool tdReadable = (col.pTypeDescriptor != 0u) &&
        SehReadBytes2(moduleBase + static_cast<uintptr_t>(col.pTypeDescriptor) + 0x10u, tdPrefix);
    res.rttiSelfConsistent = (col.signature == 1u) && (col.pSelf == c.colRva) &&
                             tdReadable && (tdPrefix[0] == '.') && (tdPrefix[1] == '?');
    if (!res.rttiSelfConsistent) {
        res.detail = "rtti_col_or_typedesc_inconsistent";
        return res;
    }

    // Step 3: Validate every expected virtual method slot against live vtable entries.
    const uint32_t slotCount = static_cast<uint32_t>(c.expectedMethods.size());
    for (uint32_t i = 0u; i < slotCount; ++i) {
        uint64_t actualSlotVa = 0ull;
        const uintptr_t slotAddr = moduleBase + static_cast<uintptr_t>(c.vtableRva) +
                                   static_cast<uintptr_t>(i) * sizeof(uint64_t);
        if (!SehReadU64(slotAddr, &actualSlotVa)) {
            res.memoryReadable = false;
            if (res.firstMismatchSlot == UINT32_MAX) res.firstMismatchSlot = i;
            res.detail = "seh_fault_reading_vtable_slot";
            return res;
        }
        ++res.slotsChecked;
        const uint64_t expectedSlotVa = static_cast<uint64_t>(moduleBase) +
                                        static_cast<uint64_t>(c.expectedMethods[i]);
        if (actualSlotVa == expectedSlotVa) {
            ++res.slotsMatched;
        } else if (res.firstMismatchSlot == UINT32_MAX) {
            res.firstMismatchSlot = i;
        }
    }

    res.passed = res.memoryReadable && res.colMatched && res.rttiSelfConsistent &&
                 (res.slotsChecked == slotCount) && (res.slotsMatched == slotCount);
    res.detail = res.passed ? "ok" : "vtable_slot_mismatch";
    return res;
}

bool LoadRttiContractsTsv(std::string_view tsvContent,
                          std::vector<ClassContract>* outContracts,
                          std::string* outErr) {
    if (!outContracts) {
        if (outErr) *outErr = "outContracts is null";
        return false;
    }
    outContracts->clear();

    size_t pos = 0;
    bool headerSeen = false;
    uint32_t lineNo = 0;
    while (pos < tsvContent.size()) {
        size_t nl = tsvContent.find('\n', pos);
        if (nl == std::string_view::npos) nl = tsvContent.size();
        std::string_view line = tsvContent.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = (nl < tsvContent.size()) ? (nl + 1) : tsvContent.size();
        ++lineNo;

        if (line.empty() || line.front() == '#') continue;
        if (!headerSeen) {
            if (line.starts_with("class_name\t")) {
                headerSeen = true;
                continue;
            }
        }

        // Split 7 tab-separated columns:
        // class_name, vtable_rva, col_rva, method_count, first_methods_hex, inheritance_chain, subsystem
        std::string_view cols[7]{};
        size_t cStart = 0;
        int cIdx = 0;
        for (; cIdx < 6; ++cIdx) {
            const size_t tab = line.find('\t', cStart);
            if (tab == std::string_view::npos) break;
            cols[cIdx] = line.substr(cStart, tab - cStart);
            cStart = tab + 1;
        }
        if (cIdx < 6) continue;
        cols[6] = line.substr(cStart);

        SubsystemId subId = SubsystemId::WorldStreaming;
        if (!ParseSubsystemId(cols[6], &subId)) {
            // Skip subsystems outside the 10 tracked replacement subsystems (e.g. CoreEngine, Audio, ScriptingUI).
            continue;
        }

        ClassContract c{};
        c.className = std::string(cols[0]);
        c.subsystem = subId;
        if (!ParseHexU32(cols[1], &c.vtableRva) ||
            !ParseHexU32(cols[2], &c.colRva) ||
            !ParseDecU32(cols[3], &c.expectedMethodCount)) {
            if (outErr) *outErr = "malformed numeric field at line " + std::to_string(lineNo);
            return false;
        }

        // Parse comma-separated method RVAs
        std::string_view mList = cols[4];
        size_t mPos = 0;
        while (mPos < mList.size()) {
            size_t comma = mList.find(',', mPos);
            if (comma == std::string_view::npos) comma = mList.size();
            uint32_t mRva = 0u;
            if (ParseHexU32(mList.substr(mPos, comma - mPos), &mRva)) {
                c.expectedMethods.push_back(mRva);
            }
            mPos = (comma < mList.size()) ? (comma + 1) : mList.size();
        }
        c.expectedInheritance = std::string(cols[5]);

        if (!c.expectedMethods.empty()) {
            outContracts->push_back(std::move(c));
        }
    }
    return !outContracts->empty();
}

bool LoadRttiContractsFile(const std::string& filePath,
                           std::vector<ClassContract>* outContracts,
                           std::string* outErr) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs) {
        if (outErr) *outErr = "failed to open RTTI TSV file: " + filePath;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return LoadRttiContractsTsv(oss.str(), outContracts, outErr);
}

std::vector<ClassContract> GetCanonical17104Contracts() {
    // 100% binary-verified RVAs extracted directly from SkyrimSE.exe 1.7.104.0 (TS=0x6A8C7046)
    // via tools/ce_decompiler.py -> data/versions/skyrimse-1.7.104-rtti.tsv.
    return {
        // WorldStreaming
        {"TES", SubsystemId::WorldStreaming, 0x17F3EA0u, 0x1B69448u, 1u,
            {0x1B4660u}, "TES -> ICellAttachDetachEventSource"},
        {"TESObjectCELL", SubsystemId::WorldStreaming, 0x18240C8u, 0x1B79280u, 59u,
            {0x2D39C0u, 0x1E35C0u, 0x1E36C0u, 0x1E6930u, 0x1E35B0u, 0x2B8200u},
            "TESObjectCELL -> TESForm -> BaseFormComponent -> TESFullName"},
        {"TESWorldSpace", SubsystemId::WorldStreaming, 0x182C120u, 0x1B7BD60u, 59u,
            {0x314930u, 0x1E35C0u, 0x1E36C0u, 0x1E6930u, 0x30B4E0u, 0x30B630u},
            "TESWorldSpace -> TESForm -> BaseFormComponent -> TESFullName -> TESModel"},
        {"GridCellArray", SubsystemId::WorldStreaming, 0x18045E0u, 0x1B6DB68u, 9u,
            {0x227B60u, 0x226C60u, 0x227280u, 0x2272B0u, 0x226D30u, 0x227330u},
            "GridCellArray -> GridArray"},
        // TerrainLOD
        {"TESObjectLAND", SubsystemId::TerrainLOD, 0x1820C00u, 0x1B78AA0u, 59u,
            {0x2B62C0u, 0x1E35C0u, 0x1E36C0u, 0x1E6930u, 0x1E35B0u, 0x1E36B0u},
            "TESObjectLAND -> TESForm -> BaseFormComponent -> TESChildCell"},
        {"TESLandTexture", SubsystemId::TerrainLOD, 0x18211B0u, 0x1B78D10u, 59u,
            {0x2B73E0u, 0x1E35C0u, 0x1E36C0u, 0x1E6930u, 0x2B68D0u, 0x2B68F0u},
            "TESLandTexture -> TESForm -> BaseFormComponent"},
        {"BGSQueuedTerrainUpdate", SubsystemId::TerrainLOD, 0x18E3018u, 0x1BA3880u, 6u,
            {0x51C480u, 0x15A97C8u, 0x15A97C8u, 0x15A97C8u, 0x15A97C8u, 0x15A97C8u},
            "BGSQueuedTerrainUpdate"},
        // WaterSim
        {"TESWaterObject", SubsystemId::WaterSim, 0x18E5E40u, 0x1BA4568u, 2u,
            {0x52F0B0u, 0xEEBF20u}, "TESWaterObject -> NiRefObject"},
        // FloraGeometry
        {"BSTreeNode", SubsystemId::FloraGeometry, 0x1B2FBB8u, 0x1C76490u, 64u,
            {0x14E9D20u, 0xEEBF20u, 0x14E9E70u, 0x1B4D10u, 0x1B4D50u, 0x14E7290u},
            "BSTreeNode -> BSLeafAnimNode -> BSFadeNode -> NiNode -> NiAVObject"},
        {"BSLeafAnimNode", SubsystemId::FloraGeometry, 0x1B2F5D0u, 0x1C763E8u, 64u,
            {0x14E8AF0u, 0xEEBF20u, 0x14E8B30u, 0x1B4D10u, 0x1B4D50u, 0x14E7290u},
            "BSLeafAnimNode -> BSFadeNode -> NiNode -> NiAVObject"},
        {"TESObjectTREE", SubsystemId::FloraGeometry, 0x1819390u, 0x1B765B8u, 83u,
            {0x288530u, 0x1E35C0u, 0x1E36C0u, 0x1E6930u, 0x2882A0u, 0x1E36B0u},
            "TESObjectTREE -> TESBoundObject -> TESObject -> TESForm"},
        {"TESGrass", SubsystemId::FloraGeometry, 0x1811E20u, 0x1B735D0u, 109u,
            {0x270890u, 0x1E35C0u, 0x1E36C0u, 0x1E6930u, 0x2704E0u, 0x1E36B0u},
            "TESGrass -> TESBoundObject -> TESObject -> TESForm"},
        // FluidCombustion
        {"BSParticleShaderEmitter", SubsystemId::FluidCombustion, 0x181DED0u, 0x1B77D68u, 4u,
            {0x2A0BA0u, 0xEEBF20u, 0x1535630u, 0x15A97C8u},
            "BSParticleShaderEmitter -> NiRefObject"},
        {"NiParticleSystem", SubsystemId::FluidCombustion, 0x1A5EC88u, 0x1C48AA0u, 58u,
            {0xF9A140u, 0xEEBF20u, 0xF9A260u, 0x20BEC0u, 0x1B4D50u, 0x1B4CA0u},
            "NiParticleSystem -> NiParticles -> BSGeometry -> NiAVObject"},
        // RenderPipeline
        {"ShadowSceneNode", SubsystemId::RenderPipeline, 0x1B3DBA0u, 0x1C7F1C0u, 62u,
            {0x1514280u, 0xEEBF20u, 0x1514400u, 0x1B4D10u, 0x1B4D50u, 0x1B4CA0u},
            "ShadowSceneNode -> NiNode -> NiAVObject -> NiObjectNET -> NiObject -> NiRefObject"},
        {"BSCullingProcess", SubsystemId::RenderPipeline, 0x1A6EF90u, 0x1C4DD98u, 29u,
            {0xFEEF60u, 0x410490u, 0x4104D0u, 0x410420u, 0x410450u, 0x410430u},
            "BSCullingProcess -> NiCullingProcess"},
        {"BSTriShape", SubsystemId::RenderPipeline, 0x1A48890u, 0x1C430D0u, 56u,
            {0xEF25A0u, 0xEEBF20u, 0xEF2620u, 0x20BEC0u, 0x1B4D50u, 0x1B4CA0u},
            "BSTriShape -> BSGeometry -> NiAVObject -> NiObjectNET -> NiObject -> NiRefObject"},
        {"BSLightingShaderProperty", SubsystemId::RenderPipeline, 0x1B3E868u, 0x1C7F260u, 63u,
            {0x151D350u, 0xEEBF20u, 0x151D540u, 0x20BEC0u, 0x1B4D50u, 0x1B4CA0u},
            "BSLightingShaderProperty -> BSShaderProperty -> NiShadeProperty -> NiProperty"},
        {"NiCamera", SubsystemId::RenderPipeline, 0x1A485B8u, 0x1C43038u, 53u,
            {0xEF1A10u, 0xEEBF20u, 0xEF1A50u, 0x20BEC0u, 0x1B4D50u, 0x1B4CA0u},
            "NiCamera -> NiAVObject -> NiObjectNET -> NiObject -> NiRefObject"},
        {"BSShadowLight", SubsystemId::RenderPipeline, 0x1B49B30u, 0x1C815E0u, 17u,
            {0x155D890u, 0xEEBF20u, 0x1576AD0u, 0x155DBD0u, 0x155D960u, 0x155DBB0u},
            "BSShadowLight -> BSLight -> NiRefObject"},
        // MemoryAllocator
        {"ScrapHeap", SubsystemId::MemoryAllocator, 0x1A21930u, 0x1C3DFD0u, 7u,
            {0xCE01D0u, 0xCDFDC0u, 0xCDFDB0u, 0xCDF4C0u, 0xCDF350u, 0xCDF360u},
            "ScrapHeap -> IMemoryStore -> IMemoryStoreBase"},
        {"MemoryHeap", SubsystemId::MemoryAllocator, 0x1A22590u, 0x1C3E318u, 21u,
            {0xCE8A90u, 0xCE8540u, 0xCEC100u, 0xCEBE60u, 0xCED210u, 0xCEBE40u},
            "MemoryHeap -> AbstractHeap -> IMemoryHeap -> IMemoryStore -> IMemoryStoreBase"},
        // JobScheduler
        {"BSTaskManager", SubsystemId::JobScheduler, 0x1A68AE8u, 0x1C4B538u, 24u,
            {0xFC4620u, 0xFC4880u, 0xFC4E70u, 0xFC5F40u, 0xFC5FF0u, 0xFC5BB0u},
            "BSTaskManager -> SynchronizedMap<int64,NiPointer<BSTask>>"},
        // PhysicsWorld
        {"bhkWorld", SubsystemId::PhysicsWorld, 0x1A7CDA8u, 0x1C4EFF8u, 55u,
            {0x1051590u, 0xEEBF20u, 0x10516E0u, 0x20BEC0u, 0x1B4D50u, 0x1B4CA0u},
            "bhkWorld -> bhkSerializable -> bhkRefObject -> NiObject"},
        {"bhkRigidBody", SubsystemId::PhysicsWorld, 0x1828198u, 0x1B7A490u, 61u,
            {0x2F94E0u, 0xEEBF20u, 0x2FA120u, 0x20BEC0u, 0x1B4D50u, 0x1B4CA0u},
            "bhkRigidBody -> bhkEntity -> bhkWorldObject -> bhkSerializable"},
        // AssetArchive
        {"IOManager", SubsystemId::AssetArchive, 0x1A68FD8u, 0x1C4B7E8u, 25u,
            {0xFC73E0u, 0xFC4880u, 0xFC4E70u, 0xFC5F40u, 0xFC5FF0u, 0xFC5BB0u},
            "IOManager -> BSTaskManager"},
        {"BSResource::LooseFileLocation", SubsystemId::AssetArchive, 0x17FF6E0u, 0x1B6CBB8u, 12u,
            {0x20B360u, 0x20BCF0u, 0x20BD10u, 0xED4AB0u, 0xED4BB0u, 0xED4CC0u},
            "BSResource::LooseFileLocation -> BSResource::Location"},
        {"BSResource::ArchiveStream", SubsystemId::AssetArchive, 0x1A43C88u, 0x1C40510u, 13u,
            {0xECA650u, 0xEC6C60u, 0xEC6E50u, 0xEC6FF0u, 0xECB990u, 0xEC6BE0u},
            "BSResource::ArchiveStream -> BSResource::Stream -> BSResource::StreamBase"},
    };
}

const RebuildManifest& GetRebuildManifest() {
    std::lock_guard<std::mutex> lock(ManifestMutex());
    return MutableManifest();
}

RebuildManifest VerifyRebuildContracts(uintptr_t moduleBase, std::span<const ClassContract> contracts) {
    RebuildManifest next = BuildBaselineManifest();
    next.lastVerificationResults.reserve(contracts.size());

    for (const ClassContract& c : contracts) {
        VtableVerifyResult vr = VerifyClassVtable(moduleBase, c);
        ++next.contractsChecked;
        if (vr.passed) {
            ++next.contractsPassed;
        } else {
            ++next.contractsFailed;
        }

        const size_t idx = static_cast<size_t>(c.subsystem);
        if (idx < next.subsystems.size()) {
            auto& entry = next.subsystems[idx];
            ++entry.contractsChecked;
            if (vr.passed) {
                ++entry.contractsPassed;
            } else {
                ++entry.contractsFailed;
            }
        }
        next.lastVerificationResults.push_back(std::move(vr));
    }

    for (auto& entry : next.subsystems) {
        // Fail-closed: lock subsystem writes unless >=1 contract was checked AND 0 failed.
        entry.failClosedLocked = (entry.contractsChecked == 0u) || (entry.contractsFailed > 0u);
    }
    next.allContractsVerified = (next.contractsChecked > 0u) && (next.contractsFailed == 0u);

    {
        std::lock_guard<std::mutex> lock(ManifestMutex());
        MutableManifest() = next;
    }
    return next;
}

void ResetRebuildManifest() {
    std::lock_guard<std::mutex> lock(ManifestMutex());
    MutableManifest() = BuildBaselineManifest();
}

std::string FormatManifestJson(const RebuildManifest& m) {
    std::ostringstream oss;
    oss << "{"
        << "\"target\":\"" << JsonEscape(m.targetBinaryVersion) << "\","
        << "\"timestamp\":\"0x" << std::hex << std::uppercase << m.targetTimestamp << "\","
        << "\"sizeOfImage\":\"0x" << m.targetSizeOfImage << std::dec << "\","
        << "\"totalExtractedRttiClasses\":" << m.totalExtractedRttiClasses << ","
        << "\"totalSecondaryVtables\":" << m.totalSecondaryVtables << ","
        << "\"contractsChecked\":" << m.contractsChecked << ","
        << "\"contractsPassed\":" << m.contractsPassed << ","
        << "\"contractsFailed\":" << m.contractsFailed << ","
        << "\"allContractsVerified\":" << (m.allContractsVerified ? "true" : "false") << ","
        << "\"subsystems\":[";
    for (size_t i = 0; i < m.subsystems.size(); ++i) {
        const auto& s = m.subsystems[i];
        if (i > 0) oss << ",";
        oss << "{"
            << "\"name\":\"" << JsonEscape(s.name) << "\","
            << "\"stage\":\"" << ReplacementStageName(s.stage) << "\","
            << "\"catalogClassCount\":" << s.catalogClassCount << ","
            << "\"checked\":" << s.contractsChecked << ","
            << "\"passed\":" << s.contractsPassed << ","
            << "\"failed\":" << s.contractsFailed << ","
            << "\"failClosedLocked\":" << (s.failClosedLocked ? "true" : "false") << ","
            << "\"rawModules\":\"" << JsonEscape(s.rawReplacementModules) << "\""
            << "}";
    }
    oss << "]}";
    return oss.str();
}

CleanRoomBackendRegistry& GetBackendRegistry() noexcept {
    static CleanRoomBackendRegistry reg{};
    return reg;
}

}  // namespace raw::ce_rebuild
