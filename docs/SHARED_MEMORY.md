# Shared Memory Interface

Playground writes all game state to a named memory-mapped file every frame. External applications can read this data without touching the game process.

## Memory Layout

| Region | Offset | Size | Description |
|---|---|---|---|
| Header | 0 | 64 bytes | SharedMemoryHeader |
| AllData | 64 | sizeof(AllData) | All 24 domain structs |
| WeatherParams | 64+sizeof(AllData) | 128 bytes | WeatherParameterComputer output |

### SharedMemoryHeader (64 bytes)

```c
struct SharedMemoryHeader {
    uint32_t magic;           // 0x53423031 ('SB01')
    uint32_t version;         // Protocol version (1)
    uint32_t structSize;      // sizeof(SB_SharedData) for validation
    uint32_t frameCount;      // Monotonic frame counter
    float    deltaTime;       // Seconds since last frame
    float    gameHour;        // Current game hour [0,24)
    uint32_t weatherFormID;   // Current TESWeather FormID
    uint8_t  weatherCategory; // WeatherCategory enum value
    uint8_t  isInterior;      // 1 if player is indoors
    uint8_t  isInMenu;        // 1 if any menu is open
    uint8_t  isLoading;       // 1 if loading screen active
    float    transitionPct;   // Weather transition progress [0,1]
    uint32_t padding[7];      // Alignment to 64 bytes
};
```

### Constants

| Name | Value |
|---|---|
| Magic | `0x53423031` (`'SB01'`) |
| Version | `1` |
| Shared Memory Name | `Playground_GameState` |
| Event Name | `Playground_DataReady` |

## Reading from C/C++

```c
#include <Windows.h>
#include "SB_SharedLayout.h"  // or define structs manually

HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Playground_GameState");
if (!hMap) {
    // Playground not running
    return;
}

const SB::SB_SharedData* data = (const SB::SB_SharedData*)
    MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SB::SB_SharedData));

if (data && data->header.magic == 0x53423031) {
    float gameHour = data->header.gameHour;
    float playerHP = data->allData.player.Vitals.x;
    float sunElevation = data->allData.celestial.SunNDC.w;
    // ...
}

UnmapViewOfFile(data);
CloseHandle(hMap);
```

## Reading from C#

```csharp
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

var mmf = MemoryMappedFile.OpenExisting("Playground_GameState");
var accessor = mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);

// Read header
uint magic = accessor.ReadUInt32(0);
if (magic != 0x53423031) return;

uint frameCount = accessor.ReadUInt32(8);
float gameHour = accessor.ReadSingle(16 + 4);
```

## Reading from Python

```python
import mmap
import struct

try:
    shm = mmap.mmap(-1, 4096, "Playground_GameState", access=mmap.ACCESS_READ)
    magic = struct.unpack_from('<I', shm, 0)[0]
    if magic == 0x53423031:
        frame_count = struct.unpack_from('<I', shm, 8)[0]
        game_hour = struct.unpack_from('<f', shm, 20)[0]
        # Player position at AllData offset + PlayerData offset
    shm.close()
except:
    pass  # Playground not running
```

## Event Signaling

After each frame write, Playground signals the named event `Playground_DataReady`. External apps can wait on this event for frame-synchronous reads:

```c
HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, L"Playground_DataReady");
WaitForSingleObject(hEvent, 1000);  // Wait up to 1s for new frame
// Read shared memory...
```

## Use Cases

| Application | What It Reads |
|---|---|
| OBS overlay | Player position, weather, time for stream info |
| Corsair iCUE | Health%, combat state, weather for LED sync |
| Stream Deck | Equipment, quest progress, UI state for buttons |
| Companion app | Full game state for mobile dashboard |
| Replay system | Camera matrices, player state for replay playback |

## ENB External Plugin

The standalone `Playground_ENB.dllplugin` uses this same shared memory interface. It reads `Playground_GameState` and serves parameters to ENB shaders via `ENBGetParameter` (pull model), providing a secondary data path that doesn't require SKSE.
