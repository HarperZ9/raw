#pragma once
// The SKSE64 plugin interface as a binary contract: the structures a plugin receives and the exports it provides.
// Original work, MIT. Layouts follow the MIT-licensed declarations in CommonLibSSE-NG (include/SKSE/Impl/Stubs.h,
// include/SKSE/Interfaces.h; Ryan McKenzie and contributors), which every CommonLib plugin is compiled against.
// No SKSE source is used: SKSE's own files carry no open licence (see docs/SKSE_HOSTING.md).
#include <cstddef>
#include <cstdint>

namespace raw::skse {

using PluginHandle = uint32_t;
constexpr PluginHandle kInvalidHandle = 0xFFFFFFFFu;

// QueryInterface ids
enum : uint32_t { kIdInvalid = 0, kIdScaleform, kIdPapyrus, kIdSerialization, kIdTask, kIdMessaging, kIdObject,
                  kIdTrampoline, kIdTotal };

// Messages the host sends as sender "SKSE"
enum : uint32_t { kMsgPostLoad, kMsgPostPostLoad, kMsgPreLoadGame, kMsgPostLoadGame, kMsgSaveGame, kMsgDeleteGame,
                  kMsgInputLoaded, kMsgNewGame, kMsgDataLoaded, kMsgTotal };

// GetEventDispatcher ids
enum : uint32_t { kDispModEvent, kDispCamera, kDispCrosshair, kDispAction, kDispNiNodeUpdate, kDispTotal };

struct PluginInfo {
    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};

// SKSEPlugin_Version, exported as data by plugins built for 1.6.x
struct PluginVersionData {
    uint32_t dataVersion;        // 1
    uint32_t pluginVersion;
    char pluginName[256];
    char author[256];
    char supportEmail[252];
    uint8_t flags1;              // bit 0: uses no game structures
    uint8_t pad1;
    uint16_t pad2;
    uint8_t flags2;              // bit 0: Address Library, bit 1: signature scanning, bit 2: structures after 1.6.629
    uint8_t pad3;
    uint16_t pad4;
    uint32_t compatibleVersions[16];  // packed runtime versions, zero-terminated
    uint32_t xseMinimum;
};
static_assert(offsetof(PluginVersionData, pluginName) == 0x008);
static_assert(offsetof(PluginVersionData, supportEmail) == 0x208);
static_assert(offsetof(PluginVersionData, flags1) == 0x304);
static_assert(offsetof(PluginVersionData, flags2) == 0x308);
static_assert(offsetof(PluginVersionData, compatibleVersions) == 0x30C);
static_assert(offsetof(PluginVersionData, xseMinimum) == 0x34C);
static_assert(sizeof(PluginVersionData) == 0x350);

struct SKSEInterface {
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t id);
    PluginHandle (*GetPluginHandle)();
    uint32_t (*GetReleaseIndex)();
    const void* (*GetPluginInfo)(const char* name);
};

struct Message {
    const char* sender;
    uint32_t type;
    uint32_t dataLen;
    void* data;
};
using MessageCallback = void (*)(Message*);

struct MessagingInterface {
    uint32_t interfaceVersion;   // 2
    bool (*RegisterListener)(PluginHandle listener, const char* sender, void* callback);
    bool (*Dispatch)(PluginHandle sender, uint32_t type, void* data, uint32_t len, const char* receiver);
    void* (*GetEventDispatcher)(uint32_t id);
};

struct PapyrusInterface {
    uint32_t interfaceVersion;   // 1
    bool (*Register)(void* callback);   // bool (*)(BSScript::IVirtualMachine*)
};

struct ScaleformInterface {
    uint32_t interfaceVersion;   // 1
    bool (*Register)(const char* name, void* callback);
    void (*RegisterForInventory)(void* callback);
};

struct SerializationInterface {
    uint32_t version;            // 4
    void (*SetUniqueID)(PluginHandle, uint32_t uid);
    void (*SetRevertCallback)(PluginHandle, void* cb);
    void (*SetSaveCallback)(PluginHandle, void* cb);
    void (*SetLoadCallback)(PluginHandle, void* cb);
    void (*SetFormDeleteCallback)(PluginHandle, void* cb);
    bool (*WriteRecord)(uint32_t type, uint32_t version, const void* buf, uint32_t len);
    bool (*OpenRecord)(uint32_t type, uint32_t version);
    bool (*WriteRecordData)(const void* buf, uint32_t len);
    bool (*GetNextRecordInfo)(uint32_t* type, uint32_t* version, uint32_t* len);
    uint32_t (*ReadRecordData)(void* buf, uint32_t len);
    bool (*ResolveHandle)(uint64_t handle, uint64_t* out);
    bool (*ResolveFormId)(uint32_t formId, uint32_t* out);
};
using SerializationCallback = void (*)(SerializationInterface*);

struct TaskInterface {
    uint32_t interfaceVersion;   // 2
    void (*AddTask)(void* task);     // TaskDelegate*
    void (*AddUITask)(void* task);   // UIDelegate_v1*
};

// TaskDelegate and UIDelegate_v1: an object whose vtable holds Run then Dispose.
struct Delegate {
    struct VTable { void (*Run)(Delegate*); void (*Dispose)(Delegate*); };
    const VTable* vtbl;
};

struct TrampolineInterface {
    uint32_t interfaceVersion;   // 1
    void* (*AllocateFromBranchPool)(PluginHandle, size_t);
    void* (*AllocateFromLocalPool)(PluginHandle, size_t);
};

// Plugin exports
using QueryFn = bool (*)(const SKSEInterface*, PluginInfo*);
using LoadFn = bool (*)(const SKSEInterface*);

// Packed versions: major << 24 | minor << 16 | build << 4 | sub
constexpr uint32_t PackVersion(uint32_t major, uint32_t minor, uint32_t build, uint32_t sub = 0) {
    return (major << 24) | (minor << 16) | ((build & 0xFFF) << 4) | (sub & 0xF);
}

}  // namespace raw::skse
