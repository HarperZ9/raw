#pragma once
// The patch ledger (RAW engine runtime): every write the module makes into the game's code or vtables goes
// through here, so RAW_ModuleStop can put the game back as it found it before a live reload
// (docs/LIVE_SESSION.md). Original work, MIT.
#include <cstddef>
#include <string>

namespace raw::rt {

// n bytes at `at`, made writable for the write. The bytes replaced the first time an address is written are
// kept; a second patch of the same address keeps the first original.
bool PatchWrite(void* at, const void* bytes, size_t n);

// one aligned 8-byte entry (a vtable slot) swapped atomically; *old receives the value it held
bool PatchPointer(void* entry, const void* value, void** old = nullptr);

// every patch put back, newest first; the count restored, and a line per failure in *report
size_t PatchRestoreAll(std::string* report);
size_t PatchCount();

// background threads of the module (the sampler, the call graph): a stop waits for none and refuses while any runs
void ThreadBegin();
void ThreadEnd();
int ThreadsRunning();

}  // namespace raw::rt
