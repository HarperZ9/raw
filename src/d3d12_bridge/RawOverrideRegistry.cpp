// RawOverrideRegistry.cpp: Creator Hot-Swap Asset Override Registry (Zero .nif / .dds / .esp Workflow).
// Original work, MIT.
#include "RawOverrideRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace raw::bridge {
namespace {
namespace fs = std::filesystem;

std::string LowerNorm(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

uint64_t FileWriteStamp(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0ull : static_cast<uint64_t>(t.time_since_epoch().count());
}

bool TryParseHexFormId(const std::string& stem, uint32_t* outId) {
    if (stem.size() < 3 || stem.size() > 10) return false;
    size_t start = 0;
    if (stem[0] == '0' && (stem[1] == 'x' || stem[1] == 'X')) start = 2;
    else if (stem.size() != 8) return false;
    for (size_t i = start; i < stem.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(stem[i]))) return false;
    }
    *outId = static_cast<uint32_t>(std::strtoul(stem.c_str() + start, nullptr, 16));
    return *outId != 0;
}

}  // namespace

bool RawOverrideRegistry::LoadEntry(RawOverrideEntry& e, RawMeshHost* meshHost) {
    RawMeshAsset mesh;
    if (!LoadModernMeshAsset(e.sourcePath, &mesh)) return false;
    e.mesh = std::move(mesh);
    e.embeddedAlbedo = {};
    if (!e.mesh.materials.empty() && !e.mesh.materials[0].embeddedBaseColorImage.empty()) {
        const auto& img = e.mesh.materials[0].embeddedBaseColorImage;
        LoadModernTextureFromMemory(img.data(), img.size(), e.key + ":albedo", &e.embeddedAlbedo);
    }
    e.hasGpuMesh = false;
    if (meshHost && meshHost->Ready()) {
        e.hasGpuMesh = meshHost->UploadAsset(e.mesh, &e.gpuMesh);
    }
    e.lastWriteTicks = FileWriteStamp(e.sourcePath);
    return true;
}

uint32_t RawOverrideRegistry::ScanDirectory(const std::string& overridesDir, RawMeshHost* meshHost, std::string* outLog) {
    m_entries.clear();
    m_byFormId.clear();
    m_byKey.clear();

    std::error_code ec;
    const fs::path root(overridesDir);
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return 0;

    // 1. Optional overrides.ini mapping explicit FormID / EDID / virtual mesh path -> relative .glb/.obj file.
    const fs::path iniPath = root / "overrides.ini";
    if (fs::exists(iniPath, ec)) {
        std::ifstream in(iniPath);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = LowerNorm(line.substr(0, eq));
            std::string rel = line.substr(eq + 1);
            if (k.empty() || rel.empty()) continue;
            fs::path target = root / rel;
            if (!fs::exists(target, ec)) continue;
            RawOverrideEntry e{};
            e.key = k;
            TryParseHexFormId(k, &e.formId);
            e.sourcePath = target.string();
            if (LoadEntry(e, meshHost)) {
                const size_t idx = m_entries.size();
                m_byKey[e.key] = idx;
                if (e.formId) m_byFormId[e.formId] = idx;
                m_entries.push_back(std::move(e));
            }
        }
    }

    // 2. Auto-bind dropped .glb / .obj files by filename stem (e.g. "0x00012345.glb" or "WHIronSword.glb").
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = LowerNorm(entry.path().extension().string());
        if (ext != ".glb" && ext != ".obj") continue;
        const std::string key = LowerNorm(entry.path().stem().string());
        if (m_byKey.count(key)) continue;
        RawOverrideEntry e{};
        e.key = key;
        TryParseHexFormId(key, &e.formId);
        e.sourcePath = entry.path().string();
        if (LoadEntry(e, meshHost)) {
            const size_t idx = m_entries.size();
            m_byKey[e.key] = idx;
            if (e.formId) m_byFormId[e.formId] = idx;
            m_entries.push_back(std::move(e));
        }
    }

    if (outLog) {
        std::ostringstream oss;
        oss << "[RawOverrides] loaded " << m_entries.size() << " creator mesh overrides ("
            << TotalMeshlets() << " RAW-MESH meshlets) from " << overridesDir;
        *outLog = oss.str();
    }
    return static_cast<uint32_t>(m_entries.size());
}

uint32_t RawOverrideRegistry::PollHotReload(RawMeshHost* meshHost) {
    uint32_t reloaded = 0;
    for (auto& e : m_entries) {
        const uint64_t ts = FileWriteStamp(e.sourcePath);
        if (ts != 0 && ts != e.lastWriteTicks) {
            if (LoadEntry(e, meshHost)) ++reloaded;
        }
    }
    return reloaded;
}

const RawOverrideEntry* RawOverrideRegistry::FindByFormId(uint32_t formId) const {
    const auto it = m_byFormId.find(formId);
    return it == m_byFormId.end() ? nullptr : &m_entries[it->second];
}

const RawOverrideEntry* RawOverrideRegistry::FindByKey(const std::string& key) const {
    const auto it = m_byKey.find(LowerNorm(key));
    return it == m_byKey.end() ? nullptr : &m_entries[it->second];
}

uint32_t RawOverrideRegistry::TotalMeshlets() const {
    uint32_t n = 0;
    for (const auto& e : m_entries) n += static_cast<uint32_t>(e.mesh.meshlets.size());
    return n;
}

}  // namespace raw::bridge
