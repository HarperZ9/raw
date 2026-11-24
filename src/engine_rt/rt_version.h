#pragma once
// The game version RAW runs against, and its address table (RAW engine runtime). Original work, MIT.
//
// Each supported runtime has one data file, data/versions/skyrimse-<version>.tsv, deployed beside the game as
// raw_versions\skyrimse-<version>.tsv. The file names the exe it belongs to (file version, PE time stamp and image
// size) and holds every address RAW uses: settings, globals, vtables, functions and structure offsets. Supporting a new
// runtime is a new file. A runtime no file matches gets no hooks at all: the module logs why and stops.

#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

struct RunningExe {
    uint32_t major = 0, minor = 0, build = 0, sub = 0;   // VS_FIXEDFILEINFO of the exe
    uint32_t timestamp = 0, sizeOfImage = 0;             // the loaded PE header
    std::string Version() const;                         // "1.7.104.0"
    uint32_t Packed() const { return major << 24 | minor << 16 | build << 4; }
};
RunningExe ReadRunningExe(const Image& img);

// Finds the table for the running exe under <gameDir>raw_versions. False (with the reason in *why) when no table
// matches the version, the time stamp and the image size together.
bool LoadVersionTable(const Image& img, const std::string& gameDir, std::string* why);
const std::string& TableVersion();          // "" until a table is loaded
uint32_t RuntimeVersion();                  // packed major<<24 | minor<<16 | build<<4, 0 when unknown

// An entry's value for the running version; 0 when the table has no value (the feature then stays off).
uint32_t Addr(const char* name);
// A structure offset; 0 when unknown (offsets of 0 are never stored).
uint32_t Off(const char* name);
// The table's settings and globals, the inputs the call graph and the settings code take by name.
std::vector<RefTarget> VersionRefs();
// One line per entry the code asked for and found missing, for the log.
std::string MissingEntries();
// Deferred first-frame .data pointer-to-RTTI-vtable scan: resolves core singletons on pure vanilla
// installs with neither a static .tsv nor Address Library installed. Returns number of newly resolved entries.
uint32_t ResolveDeferredSingletons(const Image& img);

}  // namespace raw::rt
