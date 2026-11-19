#pragma once
// What the SKSE host needs from the game, named as in RAW's version layer (data/versions, src/common/raw_versions.h).
// Original work, MIT.
//
// Values come from the address table that names the running exe (raw_versions/ beside the game). The Address Library
// id is the fallback: when the table lacks an address and an Address Library database for the running build is
// installed, the host takes it from there (ids from CommonLibSSE-NG, include/RE/Offsets.h). A value still unknown
// turns its feature off; nothing is guessed.
#include <cstdint>

namespace raw::skse {

struct Need {
    const char* name;   // version-layer name
    uint64_t id;        // Address Library id, 0 for structure offsets
};

enum : int { kAddrSkyrimVM, kAddrDataHandler, kAddrUI, kAddrInputManager, kAddrSaveImpl, kAddrLoadImpl,
             kAddrSaveLoadManager, kAddrCount };

inline const Need g_addr[kAddrCount] = {
    {"SkyrimVM::Singleton", 400475},
    {"TESDataHandler::Singleton", 400269},
    {"UI::Singleton", 400327},
    {"BSInputDeviceManager::Singleton", 402776},
    {"fn:BGSSaveLoadManager::Save_Impl", 35727},
    {"fn:BGSSaveLoadManager::Load_Impl", 35728},
    {"BGSSaveLoadManager::Singleton", 403340},
};

enum : int { kOffVmImpl, kOffUiMenuMap, kOffCount };

inline const Need g_off[kOffCount] = {
    {"off:SkyrimVM::impl", 0},
    {"off:UI::menuMap", 0},
};

}  // namespace raw::skse
