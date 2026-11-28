#pragma once
// ce_rebuild_arch.h: Clean-Room Creation Engine Subsystem Replacement Registry,
// Fail-Closed SEH-Safe Live Vtable/RTTI Contract Verifier, and Decoupled C++23 Backend Interfaces.
//
// Decouples Bethesda .esm/.esp/.bsa/.nif world data from the 2011 Creation Engine runtime by:
//   1) Modeling each Creation Engine subsystem along a strict 4-stage clean-room replacement track.
//   2) Verifying live in-process MSVC x64 RTTI (CompleteObjectLocator + TypeDescriptor) and vtable
//      method slots against binary-extracted SkyrimSE.exe 1.7.104 contracts (fail-closed under SEH).
//   3) Providing zero-Bethesda-header clean-room backend interfaces (IWorldStreamBackend,
//      IFloraBackend, IFluidBackend, IAllocatorBackend) backed by RAW native systems.
//
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace raw::ce_rebuild {

// ---------------------------------------------------------------------------
// 1. Creation Engine Replacement Subsystems & Maturity Stages
// ---------------------------------------------------------------------------

enum class SubsystemId : uint32_t {
    WorldStreaming   = 0,
    TerrainLOD       = 1,
    WaterSim         = 2,
    FloraGeometry    = 3,
    FluidCombustion  = 4,
    RenderPipeline   = 5,
    MemoryAllocator  = 6,
    JobScheduler     = 7,
    PhysicsWorld     = 8,
    AssetArchive     = 9,
    Count            = 10
};

enum class ReplacementStage : uint8_t {
    Observed          = 0,  // Binary RTTI/offsets verified; read-only telemetry only
    Shadowed          = 1,  // RAW runs clean-room shadow simulation/geometry alongside legacy engine
    Intercepted       = 2,  // RAW actively intercepts/routes legacy engine calls into RAW native path
    ReplacedCleanRoom = 3   // 100% clean-room RAW native implementation; legacy engine path bypassed
};

[[nodiscard]] constexpr const char* SubsystemIdName(SubsystemId id) noexcept {
    switch (id) {
        case SubsystemId::WorldStreaming:  return "WorldStreaming";
        case SubsystemId::TerrainLOD:      return "TerrainLOD";
        case SubsystemId::WaterSim:        return "WaterSim";
        case SubsystemId::FloraGeometry:   return "FloraGeometry";
        case SubsystemId::FluidCombustion: return "FluidCombustion";
        case SubsystemId::RenderPipeline:  return "RenderPipeline";
        case SubsystemId::MemoryAllocator: return "MemoryAllocator";
        case SubsystemId::JobScheduler:    return "JobScheduler";
        case SubsystemId::PhysicsWorld:    return "PhysicsWorld";
        case SubsystemId::AssetArchive:    return "AssetArchive";
        default:                           return "Unknown";
    }
}

[[nodiscard]] constexpr const char* ReplacementStageName(ReplacementStage stage) noexcept {
    switch (stage) {
        case ReplacementStage::Observed:          return "Observed";
        case ReplacementStage::Shadowed:          return "Shadowed";
        case ReplacementStage::Intercepted:       return "Intercepted";
        case ReplacementStage::ReplacedCleanRoom: return "ReplacedCleanRoom";
        default:                                  return "Unknown";
    }
}

[[nodiscard]] bool ParseSubsystemId(std::string_view name, SubsystemId* outId) noexcept;

// ---------------------------------------------------------------------------
// 2. Fail-Closed SEH-Safe Live Vtable & RTTI Contract Verifier
// ---------------------------------------------------------------------------

// Binary-verified SkyrimSE.exe 1.7.104.0 metadata (matches data/versions/skyrimse-1.7.104-rtti.tsv).
inline constexpr uint32_t kTargetTimestamp17104       = 0x6A8C7046u;
inline constexpr uint32_t kTargetSizeOfImage17104     = 0x03929000u;
inline constexpr uint32_t kExtractedPrimaryClasses    = 7376u;
inline constexpr uint32_t kExtractedSecondaryVtables  = 1247u;

struct ClassContract {
    std::string           className;
    SubsystemId           subsystem           = SubsystemId::WorldStreaming;
    uint32_t              vtableRva           = 0u;
    uint32_t              colRva              = 0u;
    uint32_t              expectedMethodCount = 0u;
    std::vector<uint32_t> expectedMethods;       // First N (up to 12) virtual method RVAs
    std::string           expectedInheritance;   // Expected RTTI base-class chain
};

struct VtableVerifyResult {
    std::string className;
    SubsystemId subsystem          = SubsystemId::WorldStreaming;
    bool        passed             = false;      // Fail-closed: true iff EVERY check passed
    bool        memoryReadable     = false;      // False if SEH caught an access violation
    bool        colMatched         = false;      // *(vtable - 8) == moduleBase + colRva
    bool        rttiSelfConsistent = false;      // COL.sig == 1 && COL.pSelf == colRva && TD starts with ".?"
    uint32_t    slotsChecked       = 0u;
    uint32_t    slotsMatched       = 0u;
    uint32_t    firstMismatchSlot  = UINT32_MAX;
    uint64_t    expectedColVa      = 0ull;
    uint64_t    actualColVa        = 0ull;
    std::string detail;
};

struct SubsystemManifestEntry {
    SubsystemId      id                 = SubsystemId::WorldStreaming;
    std::string      name;
    ReplacementStage stage              = ReplacementStage::Observed;
    std::string      legacyClasses;
    std::string      rawReplacementModules;
    uint32_t         catalogClassCount  = 0u;    // Total 1.7.104 RTTI classes mapped to this subsystem
    uint32_t         contractsChecked   = 0u;
    uint32_t         contractsPassed    = 0u;
    uint32_t         contractsFailed    = 0u;
    bool             failClosedLocked   = true;  // True until all subsystem contracts pass verification
};

struct RebuildManifest {
    std::string                         targetBinaryVersion        = "SkyrimSE.exe 1.7.104.0";
    uint32_t                            targetTimestamp            = kTargetTimestamp17104;
    uint32_t                            targetSizeOfImage          = kTargetSizeOfImage17104;
    uint32_t                            totalExtractedRttiClasses  = kExtractedPrimaryClasses;
    uint32_t                            totalSecondaryVtables      = kExtractedSecondaryVtables;
    uint32_t                            contractsChecked           = 0u;
    uint32_t                            contractsPassed            = 0u;
    uint32_t                            contractsFailed            = 0u;
    bool                                allContractsVerified       = false;
    std::vector<SubsystemManifestEntry> subsystems;
    std::vector<VtableVerifyResult>     lastVerificationResults;
};

// Fail-closed, SEH-safe runtime verifier: validates live in-process vtable meta-pointer (vtable - 8),
// MSVC x64 RTTICompleteObjectLocator self-consistency (signature == 1, pSelf == colRva, TypeDescriptor ".?"),
// and every expected virtual method slot against `moduleBase`. Never throws or crashes on unmapped addresses.
[[nodiscard]] VtableVerifyResult VerifyClassVtable(uintptr_t moduleBase, const ClassContract& c) noexcept;

// Parses `data/versions/skyrimse-1.7.104-rtti.tsv` content into ClassContract records.
// If `filterSubsystems` is non-empty, only records matching those subsystems are retained.
[[nodiscard]] bool LoadRttiContractsTsv(std::string_view tsvContent,
                                        std::vector<ClassContract>* outContracts,
                                        std::string* outErr = nullptr);

// Loads `skyrimse-1.7.104-rtti.tsv` from disk and parses it via LoadRttiContractsTsv().
[[nodiscard]] bool LoadRttiContractsFile(const std::string& filePath,
                                         std::vector<ClassContract>* outContracts,
                                         std::string* outErr = nullptr);

// Returns the built-in canonical 1.7.104.0 core class contracts extracted directly from SkyrimSE.exe.
[[nodiscard]] std::vector<ClassContract> GetCanonical17104Contracts();

// Returns the current global RebuildManifest (thread-safe snapshot / reference for telemetry & unit tests).
[[nodiscard]] const RebuildManifest& GetRebuildManifest();

// Executes fail-closed verification across `contracts` against `moduleBase`, updates the manifest,
// and returns the updated manifest snapshot.
RebuildManifest VerifyRebuildContracts(uintptr_t moduleBase, std::span<const ClassContract> contracts);

// Resets the global manifest to its initial fail-closed baseline (primarily for deterministic unit testing).
void ResetRebuildManifest();

// Serializes a RebuildManifest into compact deterministic JSON for live bridge telemetry.
[[nodiscard]] std::string FormatManifestJson(const RebuildManifest& manifest);

// ---------------------------------------------------------------------------
// 3. Clean-Room C++23 Engine Replacement Interfaces
//    (Zero Bethesda runtime headers — pure data-driven .esm/.esp/.bsa decoupling)
// ---------------------------------------------------------------------------

struct WorldCoord3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct CellKey {
    uint32_t worldspaceFormId = 0u;
    int32_t  cellX            = 0;
    int32_t  cellY            = 0;
    bool     isInterior       = false;
};

struct CellStreamDescriptor {
    CellKey  key{};
    uint32_t cellFormId        = 0u;
    uint32_t lodLevel          = 0u;
    float    defaultLandZ      = -2048.0f;
    float    defaultWaterZ     = -14000.0f;
    bool     hasWater          = false;
    bool     hasHeightmap      = false;
};

struct WorldStreamStats {
    uint32_t residentCells     = 0u;
    uint32_t pendingLoads      = 0u;
    uint32_t evictedTotal      = 0u;
    uint64_t usedVramBytes     = 0ull;
    uint64_t budgetVramBytes   = 0ull;
    WorldCoord3D floatingOrigin{};
};

// Replaces: TES, GridCellArray, TESObjectCELL, TESWorldSpace, BGSTerrainManager, LoadedLandData (37-slot pool).
class IWorldStreamBackend {
public:
    virtual ~IWorldStreamBackend() = default;
    virtual bool UpdateCameraFocus(WorldCoord3D cameraWorldPos, float viewDistanceU) = 0;
    virtual bool RequestCellLoad(const CellStreamDescriptor& desc) = 0;
    virtual bool EvictCell(const CellKey& key) = 0;
    virtual bool SampleHeight(double worldX, double worldY, float* outHeightZ) const = 0;
    [[nodiscard]] virtual WorldStreamStats QueryStats() const = 0;
};

struct FloraSpeciesDesc {
    uint32_t speciesId         = 0u;
    uint32_t biomeMask         = 0xFFFFFFFFu;
    float    minHeightU        = 120.0f;
    float    maxHeightU        = 780.0f;
    float    canopyRadiusU     = 220.0f;
    float    stiffness         = 0.65f;
};

struct FloraWindState {
    float windDirX             = 1.0f;
    float windDirY             = 0.0f;
    float windSpeedU           = 85.0f;
    float gustStrength         = 0.25f;
    float timeSeconds          = 0.0f;
};

// Replaces: BGSGrassManager, TESObjectTREE, TESGrass, BSTreeNode, BSLeafAnimNode, flat LOD billboards.
class IFloraBackend {
public:
    virtual ~IFloraBackend() = default;
    virtual bool RegisterSpecies(const FloraSpeciesDesc& species) = 0;
    virtual uint32_t PopulateCellFlora(const CellKey& key, uint64_t deterministicSeed) = 0;
    virtual void EvictCellFlora(const CellKey& key) = 0;
    virtual void UpdateWindField(const FloraWindState& wind) = 0;
    [[nodiscard]] virtual uint64_t ActiveInstanceCount() const = 0;
};

struct WaterBodyDesc {
    uint32_t waterFormId       = 0u;
    WorldCoord3D center{};
    float    extentX           = 4096.0f;
    float    extentY           = 4096.0f;
    float    surfaceZ          = 0.0f;
    float    flowVelX          = 0.0f;
    float    flowVelY          = 0.0f;
    float    turbidity         = 0.15f;
};

struct CombustionEmitterDesc {
    uint32_t emitterId         = 0u;
    WorldCoord3D position{};
    float    radiusU           = 32.0f;
    float    temperatureK      = 1850.0f;
    float    fuelRate          = 1.0f;
    bool     isSpellVolume     = false;
};

struct FluidSimTelemetry {
    uint32_t activeWaterBodies = 0u;
    uint32_t activeCombustion  = 0u;
    uint32_t simGridVoxels     = 0u;
    float    lastStepMs        = 0.0f;
};

// Replaces: TESWaterSystem, TESWaterObject, BSParticleShaderEmitter, NiParticleSystem, Hazard, FlameProjectile.
class IFluidBackend {
public:
    virtual ~IFluidBackend() = default;
    virtual bool RegisterWaterBody(const WaterBodyDesc& body) = 0;
    virtual bool SpawnCombustionEmitter(const CombustionEmitterDesc& emitter) = 0;
    virtual bool RemoveCombustionEmitter(uint32_t emitterId) = 0;
    virtual void StepSimulation(float deltaSeconds, const FloraWindState& ambientWind) = 0;
    [[nodiscard]] virtual FluidSimTelemetry QueryTelemetry() const = 0;
};

enum class AllocDomain : uint8_t {
    FrameLinear    = 0,  // Triple-buffered lock-free per-frame scratch (replaces ScrapHeap)
    TerrainChunk   = 1,  // Unbounded LRU terrain height/meshlet pool (replaces 37-slot LoadedLandData pool)
    AssetStreaming = 2,  // BSA/BA2/glTF decompression & upload staging
    Simulation     = 3   // Flora, fluid, AI, and physics state slabs
};

struct AllocatorStats {
    uint64_t bytesReserved     = 0ull;
    uint64_t bytesCommitted    = 0ull;
    uint64_t bytesInUse        = 0ull;
    uint64_t activeAllocations = 0ull;
    uint64_t frameResets       = 0ull;
};

// Replaces: MemoryManager (0x369A810), ScrapHeap (0x1A21930), LoadedLandData 37-slot spinlock pool (0x2B4BC3).
class IAllocatorBackend {
public:
    virtual ~IAllocatorBackend() = default;
    [[nodiscard]] virtual void* Allocate(AllocDomain domain, size_t sizeBytes, size_t alignment = 16u) = 0;
    virtual void Deallocate(AllocDomain domain, void* ptr) = 0;
    virtual void ResetFrameArena(uint32_t frameSlotIndex) = 0;
    [[nodiscard]] virtual AllocatorStats QueryStats(AllocDomain domain) const = 0;
};

// Non-owning clean-room backend service locator for wiring RAW native subsystems at startup.
struct CleanRoomBackendRegistry {
    IWorldStreamBackend* worldStream = nullptr;
    IFloraBackend*       flora       = nullptr;
    IFluidBackend*       fluid       = nullptr;
    IAllocatorBackend*   allocator   = nullptr;
};

CleanRoomBackendRegistry& GetBackendRegistry() noexcept;

}  // namespace raw::ce_rebuild
