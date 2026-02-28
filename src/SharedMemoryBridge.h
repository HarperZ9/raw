#pragma once
//=============================================================================
//  SharedMemoryBridge.h — External app data access via named shared memory
//
//  Phase 3: Writes SkyrimBridge AllData to a memory-mapped file every frame.
//  External applications read game state without touching the game process.
//
//  Usage from external apps (C/C++):
//    HANDLE hMap = OpenFileMapping(FILE_MAP_READ, FALSE, L"SkyrimBridge_GameState");
//    void*  ptr  = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SB_SharedData));
//    auto*  data = reinterpret_cast<const SB::SB_SharedData*>(ptr);
//    // data->header.version, data->allData.celestial, etc.
//
//  Consumers: OBS overlays, Corsair iCUE LED sync, stream deck plugins,
//  companion mobile apps, external analysis tools, replay systems.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "BridgeData.h"
#include "WeatherParameterComputer.h"

#include <Windows.h>
#include <string>
#include <atomic>

namespace SB
{
    //=========================================================================
    //  Shared memory layout — must match external client expectations
    //=========================================================================

    #pragma pack(push, 1)

    struct SharedMemoryHeader
    {
        uint32_t magic;           // 'SB01' = 0x53423031
        uint32_t version;         // Protocol version (increment on layout change)
        uint32_t structSize;      // sizeof(SB_SharedData) for validation
        uint32_t frameCount;      // Frame counter (monotonic)
        float    deltaTime;       // Seconds since last frame
        float    gameHour;        // Current game hour [0,24)
        uint32_t weatherFormID;   // Current weather TESWeather FormID
        uint8_t  weatherCategory; // WeatherCategory enum value
        uint8_t  isInterior;      // 1 if player is indoors
        uint8_t  isInMenu;        // 1 if any menu is open
        uint8_t  isLoading;       // 1 if loading screen active
        float    transitionPct;   // Weather transition progress [0,1]
        uint32_t padding[7];      // Alignment to 64 bytes (36 + 28 = 64)
    };
    static_assert(sizeof(SharedMemoryHeader) == 64, "Header must be 64 bytes");

    struct SB_SharedData
    {
        SharedMemoryHeader header;
        AllData            allData;

        // Weather parameter computer output (Phase 2 integration)
        struct {
            float bloomIntensity;
            float bloomRadius;
            float adaptSpeed;
            float exposureBias;
            float saturation;
            float contrast;
            float colorTemp;
            float sharpen;
            float grain;
            float aoIntensity;
            float ssrIntensity;
            float godRayIntensity;
            float dofStrength;
            float lensDirt;
            float rainOnLens;
            float frostOnLens;
            float _pad[16];  // Room for expansion
        } weatherParams;
    };

    #pragma pack(pop)

    static constexpr uint32_t kSharedMemMagic   = 0x53423031;  // 'SB01'
    static constexpr uint32_t kSharedMemVersion  = 1;
    static constexpr const wchar_t* kSharedMemName = L"SkyrimBridge_GameState";
    static constexpr const wchar_t* kEventName     = L"SkyrimBridge_DataReady";


    //=========================================================================
    //  SharedMemoryBridge — manages the shared memory region
    //=========================================================================

    class SharedMemoryBridge
    {
    public:
        static SharedMemoryBridge& Get()
        {
            static SharedMemoryBridge inst;
            return inst;
        }

        // ─── Lifecycle ──────────────────────────────────────────────────

        bool Initialize();
        void Shutdown();
        bool IsActive() const { return m_active.load(std::memory_order_relaxed); }

        // ─── Per-Frame Update ───────────────────────────────────────────

        // Write current frame data to shared memory
        void WriteFrame(const AllData& data, float deltaTime, uint32_t frameCount);

        // ─── Statistics ─────────────────────────────────────────────────

        uint32_t GetFramesWritten()  const { return m_framesWritten; }
        uint32_t GetClientsConnected() const;  // Check if any process has the mapping open

    private:
        SharedMemoryBridge() = default;
        ~SharedMemoryBridge() { Shutdown(); }

        HANDLE          m_hMapFile  = nullptr;
        HANDLE          m_hEvent    = nullptr;   // Signaled each frame for waiting clients
        SB_SharedData*  m_pData     = nullptr;
        std::atomic<bool> m_active{false};
        uint32_t        m_framesWritten = 0;
    };

}  // namespace SB
