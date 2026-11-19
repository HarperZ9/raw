#pragma once
// Engine modules behind the proxy: loading from [Modules] Load, and reloading one while the game runs
// (docs/LIVE_SESSION.md). Original work, MIT.
//
// A reload is asked for with raw_module_go.txt beside the game:
//   reload=<logical name, e.g. raw_engine.dll>
//   file=<the new build's file name in the game folder, e.g. raw_engine.r3.dll>
// At the next poll (every 15 presents, on the render thread, before the present callbacks run) the proxy
//   1. asks the running copy to stop (its RAW_ModuleStop export restores every patch it made; a module without
//      the export, or one that refuses, stays loaded and the reload is refused),
//   2. drops every callback whose code lies inside the old copy,
//   3. loads the new file and calls its RAW_ModuleStart with ProxyInterface::moduleGeneration raised,
//   4. frees the old copy later, after 120 presents and at least 2 s, so a game thread still returning
//      through one of its hooks has left it.
// The answer is raw_module_done.txt: "ok <ms> ..." or "refused: <why>".
#include <windows.h>

#include <cstdint>

namespace SB::Proxy { struct ProxyInterface; }

namespace SB::Proxy::modules
{
void LoadAll(ProxyInterface* pi);       // [Modules] Load=a.dll;b.dll, after lazy init
void OnPresent();                       // the go-file poll and the deferred frees; render thread, before callbacks
HMODULE Find(const char* logicalName);  // the copy now serving a logical name (raw_engine.dll), or null
uint32_t Generation();                  // raised by every reload; a caller caching a module's export re-resolves on change
}

// proxy_main.cpp: removes every registered callback whose address lies in [lo, hi); the count removed
size_t PG_DropCallbacksIn(const void* lo, const void* hi);
