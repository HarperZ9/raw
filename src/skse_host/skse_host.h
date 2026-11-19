#pragma once
// RAW's SKSE plugin host: loads SKSE plugins from Data/SKSE/Plugins with no SKSE installed. Original work, MIT.
// See docs/SKSE_HOSTING.md for what is implemented, what is stubbed, and the timing of each message.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "skse_abi.h"

namespace raw::skse {

struct Plugin {
    std::string file;          // dll name
    std::string name;          // from SKSEPlugin_Version or SKSEPlugin_Query
    uint32_t version = 0;
    HMODULE module = nullptr;
    PluginHandle handle = kInvalidHandle;
    PluginInfo info{};
    std::string status;        // loaded | refused: <why> | failed: <why>
    std::map<std::string, uint32_t> calls;   // interface function -> count
};

struct Host {
    std::string gameDir, outDir;
    uint32_t runtimeVersion = 0;
    std::vector<Plugin> plugins;
    PluginHandle loading = kInvalidHandle;   // the plugin inside SKSEPlugin_Query/Load
    std::recursive_mutex lock;
    std::vector<std::string> events;         // milestones, for the status file
};

Host& H();
void Log(const char* fmt, ...);
void Event(const char* fmt, ...);
void Count(PluginHandle h, const char* fn);
Plugin* ByHandle(PluginHandle h);
// The interface table plugins receive (skse_host.cpp)
const SKSEInterface* Interface();
void* QueryInterfaceImpl(uint32_t id);

// skse_messaging.cpp
MessagingInterface* Messaging();
void SendFromHost(uint32_t type, void* data, uint32_t len);

// skse_task.cpp
TaskInterface* Tasks();
TrampolineInterface* Trampolines();
void RunTasks();

// skse_serialization.cpp
SerializationInterface* Serialization();
void CosaveSave(const char* saveName);
void CosaveLoad(const char* saveName);
void CosaveRevert();
void CosaveDelete(const char* saveName);

// skse_papyrus.cpp
PapyrusInterface* Papyrus();
ScaleformInterface* Scaleform();
void PapyrusPoll();

// skse_engine.cpp: the game side (addresses, polling, save and load hooks)
bool EngineInit();
void EngineOnPresent();
void* VirtualMachine();          // BSScript::IVirtualMachine*, or null before the VM exists
std::string SaveDirectory();     // folder the game writes saves to, with a trailing backslash

// skse_addrlib.cpp: Address Library database, when installed
bool AddrLibLoad(const std::string& path);
uint64_t AddrLibOffset(uint64_t id);    // 0 when absent or unknown
size_t AddrLibCount();

// skse_loader.cpp
void LoadPlugins();
void WriteStatus();

}  // namespace raw::skse

// For RAW's own code and tools: the game address of an Address Library id, 0 without the database.
extern "C" __declspec(dllexport) uint64_t RAW_SKSE_AddressOf(uint64_t id);
