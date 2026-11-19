#pragma once
// RAW's address tables, one file per game runtime (data/versions/skyrimse-<version>.tsv). Original work, MIT.
// Header-only, so raw_engine and raw_skse share one reader. docs/VERSIONS.md describes the format.
//
//   raw::versions::Table t;
//   std::string why;
//   if (raw::versions::FindTable(dir, raw::versions::ReadRunningExe(), &t, &why)) { uint32_t rva = t.Get("TES::Singleton"); }
//
// A table belongs to one exe: its file version, PE time stamp and image size must all match. A runtime no table names
// gets no table, and the caller hooks nothing. A value of 0 means unknown: the feature that needs it stays off.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "version.lib")

namespace raw::versions {

struct Exe {
    uint32_t major = 0, minor = 0, build = 0, sub = 0;   // VS_FIXEDFILEINFO
    uint32_t timestamp = 0, sizeOfImage = 0;             // the loaded PE header
    std::string Version() const {
        char b[48];
        std::snprintf(b, sizeof b, "%u.%u.%u.%u", major, minor, build, sub);
        return b;
    }
    // the packing CommonLib and SKSE use: major<<24 | minor<<16 | build<<4
    uint32_t Packed() const { return major << 24 | minor << 16 | build << 4; }
};

struct Entry { std::string kind, confidence; uint32_t value = 0; };

struct Table {
    std::string version, file;
    uint32_t timestamp = 0, sizeOfImage = 0;
    std::map<std::string, Entry> entries;
    uint32_t Get(const std::string& name) const {
        const auto it = entries.find(name);
        return it == entries.end() ? 0 : it->second.value;
    }
};

inline uint32_t ParseHex(const std::string& s) {
    return s.rfind("0x", 0) == 0 ? uint32_t(std::stoul(s, nullptr, 16)) : 0;
}

// The exe this process runs (the main module).
inline Exe ReadRunningExe() {
    Exe r;
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    r.timestamp = nt->FileHeader.TimeDateStamp;
    r.sizeOfImage = nt->OptionalHeader.SizeOfImage;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    DWORD h = 0;
    const DWORD n = GetFileVersionInfoSizeW(path, &h);
    if (!n) return r;
    std::vector<uint8_t> buf(n);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (GetFileVersionInfoW(path, 0, n, buf.data()) && VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) && fi) {
        r.major = HIWORD(fi->dwFileVersionMS); r.minor = LOWORD(fi->dwFileVersionMS);
        r.build = HIWORD(fi->dwFileVersionLS); r.sub = LOWORD(fi->dwFileVersionLS);
    }
    return r;
}

// Header lines version, timestamp, size_of_image; then rows name, kind, value, confidence, source. # starts a comment.
inline bool ReadTable(const std::string& path, Table* t) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string a, b, c, d;
        std::getline(ss, a, '\t');
        std::getline(ss, b, '\t');
        std::getline(ss, c, '\t');
        std::getline(ss, d, '\t');
        if (a == "version") t->version = b;
        else if (a == "timestamp") t->timestamp = ParseHex(b);
        else if (a == "size_of_image") t->sizeOfImage = ParseHex(b);
        else if (a != "name" && !b.empty())
            if (const uint32_t v = ParseHex(c)) t->entries[a] = {b, d, v};
    }
    t->file = path;
    return !t->version.empty();
}

// The table in dir (any *.tsv) that names exe. why: what was found, or why nothing matched.
inline bool FindTable(const std::string& dir, const Exe& exe, Table* out, std::string* why) {
    char id[160];
    std::snprintf(id, sizeof id, "SkyrimSE.exe %s (time stamp 0x%08X, image 0x%X)", exe.Version().c_str(), exe.timestamp,
                  exe.sizeOfImage);
    std::string seen;
    WIN32_FIND_DATAA fd;
    const std::string d = dir.empty() || dir.back() == '\\' || dir.back() == '/' ? dir : dir + "\\";
    HANDLE f = FindFirstFileA((d + "*.tsv").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            Table t;
            if (!ReadTable(d + fd.cFileName, &t)) continue;
            seen += (seen.empty() ? "" : ", ") + t.version;
            if (t.version != exe.Version()) continue;
            if (t.timestamp != exe.timestamp || t.sizeOfImage != exe.sizeOfImage) {
                char b[200];
                std::snprintf(b, sizeof b, " (%s: time stamp 0x%08X, image 0x%X, not this exe)", fd.cFileName, t.timestamp,
                              t.sizeOfImage);
                seen += b;
                continue;
            }
            FindClose(f);
            if (why) *why = std::string(id) + ": table " + fd.cFileName + ", " + std::to_string(t.entries.size()) + " entries";
            *out = std::move(t);
            return true;
        } while (FindNextFileA(f, &fd));
        FindClose(f);
    }
    if (why) *why = std::string(id) + ": no address table matches (tables: " + (seen.empty() ? "none" : seen) + "); nothing is hooked";
    return false;
}

}  // namespace raw::versions
