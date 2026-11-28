// RawEsmNpcWeatherLoader.cpp: Clean-Room Native `.ESM` World Metadata Parser (WTHR/CLMT/NPC_/REGN/QUST)
// + Physical `RawWeatherSim` Bridge. Original work, MIT. Zero Windows / D3D12 / Bethesda SDK deps.

#include "RawEsmNpcWeatherLoader.h"
#include "RawEsmWorldLoader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace raw::worldgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

inline uint16_t ReadU16LE(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t ReadU32LE(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline float ReadF32LE(const uint8_t* p) noexcept {
    const uint32_t bits = ReadU32LE(p);
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(float));
    return v;
}

inline bool TagEq(const uint8_t* p, const char tag[4]) noexcept {
    return p[0] == static_cast<uint8_t>(tag[0]) &&
           p[1] == static_cast<uint8_t>(tag[1]) &&
           p[2] == static_cast<uint8_t>(tag[2]) &&
           p[3] == static_cast<uint8_t>(tag[3]);
}

std::string ToLowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string ReadZString(const uint8_t* p, size_t n) {
    while (n > 0u && p[n - 1u] == 0u) --n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

bool IsTargetMetaGroupTag(const uint8_t* tag) noexcept {
    return TagEq(tag, "WTHR") || TagEq(tag, "CLMT") || TagEq(tag, "NPC_") ||
           TagEq(tag, "REGN") || TagEq(tag, "QUST");
}

inline Vec3 ReadNam0Rgb(const uint8_t* nam0, size_t nam0Size, size_t slotIndex, size_t todIndex) noexcept {
    // NAM0 layout: slotIndex * 16 + todIndex * 4 bytes (todIndex: 0=Sunrise, 1=Day, 2=Sunset, 3=Night)
    const size_t off = slotIndex * 16u + todIndex * 4u;
    if (off + 3u > nam0Size) {
        return {0.5f, 0.5f, 0.5f};
    }
    return {
        static_cast<float>(nam0[off + 0u]) / 255.0f,
        static_cast<float>(nam0[off + 1u]) / 255.0f,
        static_cast<float>(nam0[off + 2u]) / 255.0f
    };
}

void ParseWthrRecord(uint32_t formId, const uint8_t* payload, size_t payloadLen, EsmWorldMetaIndex& idx) {
    EsmWeatherRecord w{};
    w.formId = formId;

    float cloudSpeedSum   = 0.0f;
    uint32_t cloudSpeedN  = 0u;
    std::vector<float> jnamDayAlphas;

    size_t sp = 0u;
    uint32_t pendingLargeSize = 0u;
    while (sp + 6u <= payloadLen) {
        const uint8_t* sub = payload + sp;
        uint32_t subSz = ReadU16LE(sub + 4u);
        sp += 6u;
        if (TagEq(sub, "XXXX") && subSz == 4u && sp + 4u <= payloadLen) {
            pendingLargeSize = ReadU32LE(payload + sp);
            sp += 4u;
            continue;
        }
        if (subSz == 0u && pendingLargeSize > 0u) {
            subSz = pendingLargeSize;
            pendingLargeSize = 0u;
        }
        if (sp + size_t(subSz) > payloadLen) break;

        const uint8_t* data = payload + sp;
        if (TagEq(sub, "EDID") && subSz > 0u) {
            w.editorId = ReadZString(data, subSz);
        } else if (TagEq(sub, "DATA") && subSz >= 12u) {
            w.hasData           = true;
            w.windSpeedByte     = data[0];
            w.transDelta        = data[3];
            w.sunGlare          = data[4];
            w.sunDamage         = data[5];
            w.precipBeginFadeIn = data[6];
            w.precipEndFadeOut  = data[7];
            w.thunderFrequency  = data[10];
            w.precipFlags       = data[11];
            if (subSz >= 18u) {
                w.windDirectionDeg = (static_cast<float>(data[17]) / 255.0f) * 360.0f;
            }
        } else if ((TagEq(sub, "RNAM") || TagEq(sub, "QNAM")) && subSz > 0u) {
            for (uint32_t i = 0u; i < subSz; ++i) {
                // Encoded around 127 = stationary, deviation = speed
                const float spd = std::abs(static_cast<float>(data[i]) - 127.0f) / 127.0f;
                cloudSpeedSum += spd;
                ++cloudSpeedN;
            }
        } else if (TagEq(sub, "JNAM") && subSz >= 16u) {
            // 32 cloud layers * 4 floats (Sunrise, Day, Sunset, Night) = 512 bytes
            const uint32_t layers = subSz / 16u;
            jnamDayAlphas.resize(layers, 0.0f);
            for (uint32_t l = 0u; l < layers; ++l) {
                const float dayAlpha = ReadF32LE(data + size_t(l) * 16u + 4u);
                jnamDayAlphas[l] = std::isfinite(dayAlpha) ? Clamp01(dayAlpha) : 0.0f;
            }
        } else if (TagEq(sub, "NAM1") && subSz >= 4u) {
            w.disabledCloudLayersMask = ReadU32LE(data);
        } else if (TagEq(sub, "FNAM") && subSz >= 16u) {
            w.hasFnam      = true;
            w.fogDayNear   = ReadF32LE(data + 0u);
            w.fogDayFar    = ReadF32LE(data + 4u);
            w.fogNightNear = ReadF32LE(data + 8u);
            w.fogNightFar  = ReadF32LE(data + 12u);
            if (subSz >= 24u) {
                w.fogDayPow   = ReadF32LE(data + 16u);
                w.fogNightPow = ReadF32LE(data + 20u);
            }
            if (subSz >= 32u) {
                w.fogDayMax   = ReadF32LE(data + 24u);
                w.fogNightMax = ReadF32LE(data + 28u);
            }
        } else if (TagEq(sub, "NAM0") && subSz >= 144u) {
            w.hasNam0 = true;
            w.colors.skyUpperDay   = ReadNam0Rgb(data, subSz, 0u, 1u);
            w.colors.skyUpperNight = ReadNam0Rgb(data, subSz, 0u, 3u);
            w.colors.fogNearDay    = ReadNam0Rgb(data, subSz, 1u, 1u);
            w.colors.fogNearNight  = ReadNam0Rgb(data, subSz, 1u, 3u);
            w.colors.ambientDay    = ReadNam0Rgb(data, subSz, 3u, 1u);
            w.colors.ambientNight  = ReadNam0Rgb(data, subSz, 3u, 3u);
            w.colors.sunlightDay   = ReadNam0Rgb(data, subSz, 4u, 1u);
            w.colors.sunlightNight = ReadNam0Rgb(data, subSz, 4u, 3u);
            w.colors.sunDay        = ReadNam0Rgb(data, subSz, 5u, 1u);
            w.colors.sunNight      = ReadNam0Rgb(data, subSz, 5u, 3u);
            w.colors.skyLowerDay   = ReadNam0Rgb(data, subSz, 7u, 1u);
            w.colors.skyLowerNight = ReadNam0Rgb(data, subSz, 7u, 3u);
            w.colors.horizonDay    = ReadNam0Rgb(data, subSz, 8u, 1u);
            w.colors.horizonNight  = ReadNam0Rgb(data, subSz, 8u, 3u);
            if (subSz >= 208u) {
                w.colors.fogFarDay   = ReadNam0Rgb(data, subSz, 12u, 1u);
                w.colors.fogFarNight = ReadNam0Rgb(data, subSz, 12u, 3u);
            }
        }
        sp += size_t(subSz);
    }

    if (cloudSpeedN > 0u) {
        w.cloudSpeedMean01 = Clamp01(cloudSpeedSum / static_cast<float>(cloudSpeedN));
    }
    if (!jnamDayAlphas.empty()) {
        float sumA = 0.0f;
        for (uint32_t l = 0u; l < jnamDayAlphas.size(); ++l) {
            const bool disabled = (l < 32u) && ((w.disabledCloudLayersMask & (1u << l)) != 0u);
            if (!disabled) {
                sumA += jnamDayAlphas[l];
            }
        }
        // Effective sky cloud fraction across all 32 Skyrim cloud strata (disabled layers contribute 0).
        w.cloudAlphaDayMean01 = Clamp01(sumA / static_cast<float>(jnamDayAlphas.size()));
    }

    const size_t slot = idx.weathers.size();
    idx.weatherByFormId[w.formId] = slot;
    if (!w.editorId.empty()) {
        idx.weatherByEdidLower[ToLowerAscii(w.editorId)] = slot;
    }
    idx.weathers.push_back(std::move(w));
}

void ParseClmtRecord(uint32_t formId, const uint8_t* payload, size_t payloadLen, EsmWorldMetaIndex& idx) {
    EsmClimateRecord c{};
    c.formId = formId;

    size_t sp = 0u;
    uint32_t pendingLargeSize = 0u;
    while (sp + 6u <= payloadLen) {
        const uint8_t* sub = payload + sp;
        uint32_t subSz = ReadU16LE(sub + 4u);
        sp += 6u;
        if (TagEq(sub, "XXXX") && subSz == 4u && sp + 4u <= payloadLen) {
            pendingLargeSize = ReadU32LE(payload + sp);
            sp += 4u;
            continue;
        }
        if (subSz == 0u && pendingLargeSize > 0u) {
            subSz = pendingLargeSize;
            pendingLargeSize = 0u;
        }
        if (sp + size_t(subSz) > payloadLen) break;

        const uint8_t* data = payload + sp;
        if (TagEq(sub, "EDID") && subSz > 0u) {
            c.editorId = ReadZString(data, subSz);
        } else if (TagEq(sub, "TNAM") && subSz >= 6u) {
            c.sunriseBeginMin     = static_cast<uint16_t>(data[0]) * 10u;
            c.sunriseEndMin       = static_cast<uint16_t>(data[1]) * 10u;
            c.sunsetBeginMin      = static_cast<uint16_t>(data[2]) * 10u;
            c.sunsetEndMin        = static_cast<uint16_t>(data[3]) * 10u;
            c.volatility          = data[4];
            c.moonPhaseLengthDays = static_cast<uint8_t>(data[5] & 0x3Fu);
            c.hasSecunda          = (data[5] & 0x40u) != 0u;
            c.hasMasser           = (data[5] & 0x80u) != 0u;
        } else if (TagEq(sub, "WLST") && subSz >= 12u) {
            const uint32_t count = subSz / 12u;
            c.weatherList.reserve(c.weatherList.size() + count);
            for (uint32_t i = 0u; i < count; ++i) {
                const uint8_t* e = data + size_t(i) * 12u;
                EsmWeatherChanceEntry entry{};
                entry.weatherFormId = ReadU32LE(e + 0u);
                entry.chance        = ReadU32LE(e + 4u);
                entry.globalFormId  = ReadU32LE(e + 8u);
                c.weatherList.push_back(entry);
            }
        }
        sp += size_t(subSz);
    }

    const size_t slot = idx.climates.size();
    idx.climateByFormId[c.formId] = slot;
    if (!c.editorId.empty()) {
        idx.climateByEdidLower[ToLowerAscii(c.editorId)] = slot;
    }
    idx.climates.push_back(std::move(c));
}

void ParseNpcRecord(uint32_t formId, const uint8_t* payload, size_t payloadLen, EsmWorldMetaIndex& idx) {
    EsmNpcRecord npc{};
    npc.formId = formId;

    size_t sp = 0u;
    uint32_t pendingLargeSize = 0u;
    while (sp + 6u <= payloadLen) {
        const uint8_t* sub = payload + sp;
        uint32_t subSz = ReadU16LE(sub + 4u);
        sp += 6u;
        if (TagEq(sub, "XXXX") && subSz == 4u && sp + 4u <= payloadLen) {
            pendingLargeSize = ReadU32LE(payload + sp);
            sp += 4u;
            continue;
        }
        if (subSz == 0u && pendingLargeSize > 0u) {
            subSz = pendingLargeSize;
            pendingLargeSize = 0u;
        }
        if (sp + size_t(subSz) > payloadLen) break;

        const uint8_t* data = payload + sp;
        if (TagEq(sub, "EDID") && subSz > 0u) {
            npc.editorId = ReadZString(data, subSz);
        } else if (TagEq(sub, "FULL") && subSz > 0u) {
            if (idx.isLocalized && subSz == 4u) {
                npc.fullNameStringId = ReadU32LE(data);
            } else {
                npc.fullName = ReadZString(data, subSz);
            }
        } else if (TagEq(sub, "ACBS") && subSz >= 16u) {
            npc.acbsFlags    = ReadU32LE(data + 0u);
            npc.level        = ReadU16LE(data + 8u);
            npc.calcMinLevel = ReadU16LE(data + 10u);
            npc.calcMaxLevel = ReadU16LE(data + 12u);
            npc.speedMult    = ReadU16LE(data + 14u);
        } else if (TagEq(sub, "RNAM") && subSz >= 4u) {
            npc.raceFormId = ReadU32LE(data);
        } else if (TagEq(sub, "SNAM") && subSz >= 4u) {
            ++npc.factionCount;
        }
        sp += size_t(subSz);
    }

    const size_t slot = idx.npcs.size();
    idx.npcByFormId[npc.formId] = slot;
    if (!npc.editorId.empty()) {
        idx.npcByEdidLower[ToLowerAscii(npc.editorId)] = slot;
    }
    idx.npcs.push_back(std::move(npc));
}

void ParseRegnRecord(uint32_t formId, const uint8_t* payload, size_t payloadLen, EsmWorldMetaIndex& idx) {
    EsmRegionRecord r{};
    r.formId = formId;

    size_t sp = 0u;
    uint32_t pendingLargeSize = 0u;
    while (sp + 6u <= payloadLen) {
        const uint8_t* sub = payload + sp;
        uint32_t subSz = ReadU16LE(sub + 4u);
        sp += 6u;
        if (TagEq(sub, "XXXX") && subSz == 4u && sp + 4u <= payloadLen) {
            pendingLargeSize = ReadU32LE(payload + sp);
            sp += 4u;
            continue;
        }
        if (subSz == 0u && pendingLargeSize > 0u) {
            subSz = pendingLargeSize;
            pendingLargeSize = 0u;
        }
        if (sp + size_t(subSz) > payloadLen) break;

        const uint8_t* data = payload + sp;
        if (TagEq(sub, "EDID") && subSz > 0u) {
            r.editorId = ReadZString(data, subSz);
        } else if (TagEq(sub, "RDWT") && subSz >= 12u) {
            const uint32_t count = subSz / 12u;
            r.weatherTable.reserve(r.weatherTable.size() + count);
            for (uint32_t i = 0u; i < count; ++i) {
                const uint8_t* e = data + size_t(i) * 12u;
                EsmWeatherChanceEntry entry{};
                entry.weatherFormId = ReadU32LE(e + 0u);
                entry.chance        = ReadU32LE(e + 4u);
                entry.globalFormId  = ReadU32LE(e + 8u);
                r.weatherTable.push_back(entry);
            }
        }
        sp += size_t(subSz);
    }

    const size_t slot = idx.regions.size();
    idx.regionByFormId[r.formId] = slot;
    if (!r.editorId.empty()) {
        idx.regionByEdidLower[ToLowerAscii(r.editorId)] = slot;
    }
    idx.regions.push_back(std::move(r));
}

void ParseQustRecord(uint32_t formId, const uint8_t* payload, size_t payloadLen, EsmWorldMetaIndex& idx) {
    EsmQuestRecord q{};
    q.formId = formId;

    size_t sp = 0u;
    uint32_t pendingLargeSize = 0u;
    while (sp + 6u <= payloadLen) {
        const uint8_t* sub = payload + sp;
        uint32_t subSz = ReadU16LE(sub + 4u);
        sp += 6u;
        if (TagEq(sub, "XXXX") && subSz == 4u && sp + 4u <= payloadLen) {
            pendingLargeSize = ReadU32LE(payload + sp);
            sp += 4u;
            continue;
        }
        if (subSz == 0u && pendingLargeSize > 0u) {
            subSz = pendingLargeSize;
            pendingLargeSize = 0u;
        }
        if (sp + size_t(subSz) > payloadLen) break;

        const uint8_t* data = payload + sp;
        if (TagEq(sub, "EDID") && subSz > 0u) {
            q.editorId = ReadZString(data, subSz);
        } else if (TagEq(sub, "FULL") && subSz > 0u) {
            if (idx.isLocalized && subSz == 4u) {
                q.fullNameStringId = ReadU32LE(data);
            } else {
                q.fullName = ReadZString(data, subSz);
            }
        } else if (TagEq(sub, "DNAM") && subSz >= 4u) {
            q.flags    = ReadU16LE(data + 0u);
            q.priority = data[2];
            if (subSz >= 12u) {
                q.questType = ReadU32LE(data + 8u);
            }
        }
        sp += size_t(subSz);
    }

    const size_t slot = idx.quests.size();
    idx.questByFormId[q.formId] = slot;
    if (!q.editorId.empty()) {
        idx.questByEdidLower[ToLowerAscii(q.editorId)] = slot;
    }
    idx.quests.push_back(std::move(q));
}

void ParseMetaRecordsInGroupBuffer(const uint8_t* buf, size_t len, EsmWorldMetaIndex& idx) {
    size_t pos = 0u;
    std::vector<uint8_t> inflated;

    while (pos + 24u <= len) {
        const uint8_t* rec = buf + pos;
        if (TagEq(rec, "GRUP")) {
            const uint32_t subGrupSz = ReadU32LE(rec + 4u);
            if (subGrupSz < 24u || pos + size_t(subGrupSz) > len) break;
            ParseMetaRecordsInGroupBuffer(rec + 24u, size_t(subGrupSz) - 24u, idx);
            pos += size_t(subGrupSz);
            continue;
        }

        const uint32_t dataSize = ReadU32LE(rec + 4u);
        const uint32_t flags    = ReadU32LE(rec + 8u);
        const uint32_t formId   = ReadU32LE(rec + 12u);
        const size_t recTotal   = 24u + size_t(dataSize);
        if (pos + recTotal > len) break;

        if (IsTargetMetaGroupTag(rec)) {
            const uint8_t* payload = rec + 24u;
            size_t payloadLen      = dataSize;

            if ((flags & 0x00040000u) != 0u) {
                ++idx.compressedSeen;
                if (dataSize < 5u) {
                    pos += recTotal;
                    continue;
                }
                const uint32_t uncompSz = ReadU32LE(payload);
                inflated.clear();
                if (!InflateEsmZlib(payload + 4u, size_t(dataSize) - 4u, uncompSz, &inflated)) {
                    pos += recTotal;
                    continue;
                }
                payload    = inflated.data();
                payloadLen = inflated.size();
            }

            if (TagEq(rec, "WTHR"))      ParseWthrRecord(formId, payload, payloadLen, idx);
            else if (TagEq(rec, "CLMT")) ParseClmtRecord(formId, payload, payloadLen, idx);
            else if (TagEq(rec, "NPC_")) ParseNpcRecord(formId, payload, payloadLen, idx);
            else if (TagEq(rec, "REGN")) ParseRegnRecord(formId, payload, payloadLen, idx);
            else if (TagEq(rec, "QUST")) ParseQustRecord(formId, payload, payloadLen, idx);
        }

        pos += recTotal;
    }
}

Vec3 LerpVec3(Vec3 a, Vec3 b, float t) noexcept {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}

}  // namespace

// ============================================================================
// Lookup Methods
// ============================================================================

const EsmWeatherRecord* EsmWorldMetaIndex::FindWeatherByFormId(uint32_t formId) const {
    auto it = weatherByFormId.find(formId);
    return (it == weatherByFormId.end()) ? nullptr : &weathers[it->second];
}

const EsmWeatherRecord* EsmWorldMetaIndex::FindWeatherByEdid(const std::string& edid) const {
    auto it = weatherByEdidLower.find(ToLowerAscii(edid));
    return (it == weatherByEdidLower.end()) ? nullptr : &weathers[it->second];
}

const EsmClimateRecord* EsmWorldMetaIndex::FindClimateByFormId(uint32_t formId) const {
    auto it = climateByFormId.find(formId);
    return (it == climateByFormId.end()) ? nullptr : &climates[it->second];
}

const EsmClimateRecord* EsmWorldMetaIndex::FindClimateByEdid(const std::string& edid) const {
    auto it = climateByEdidLower.find(ToLowerAscii(edid));
    return (it == climateByEdidLower.end()) ? nullptr : &climates[it->second];
}

const EsmNpcRecord* EsmWorldMetaIndex::FindNpcByFormId(uint32_t formId) const {
    auto it = npcByFormId.find(formId);
    return (it == npcByFormId.end()) ? nullptr : &npcs[it->second];
}

const EsmNpcRecord* EsmWorldMetaIndex::FindNpcByEdid(const std::string& edid) const {
    auto it = npcByEdidLower.find(ToLowerAscii(edid));
    return (it == npcByEdidLower.end()) ? nullptr : &npcs[it->second];
}

const EsmRegionRecord* EsmWorldMetaIndex::FindRegionByFormId(uint32_t formId) const {
    auto it = regionByFormId.find(formId);
    return (it == regionByFormId.end()) ? nullptr : &regions[it->second];
}

const EsmRegionRecord* EsmWorldMetaIndex::FindRegionByEdid(const std::string& edid) const {
    auto it = regionByEdidLower.find(ToLowerAscii(edid));
    return (it == regionByEdidLower.end()) ? nullptr : &regions[it->second];
}

const EsmQuestRecord* EsmWorldMetaIndex::FindQuestByFormId(uint32_t formId) const {
    auto it = questByFormId.find(formId);
    return (it == questByFormId.end()) ? nullptr : &quests[it->second];
}

const EsmQuestRecord* EsmWorldMetaIndex::FindQuestByEdid(const std::string& edid) const {
    auto it = questByEdidLower.find(ToLowerAscii(edid));
    return (it == questByEdidLower.end()) ? nullptr : &quests[it->second];
}

// ============================================================================
// Top-Level Streaming ESM Metadata Loaders
// ============================================================================

bool LoadEsmWorldMetaFromMemory(const uint8_t* bytes, size_t size, const std::string& label,
                                EsmWorldMetaIndex* outIndex, std::string* outErr) {
    if (!bytes || !outIndex || size < 24u) {
        SetErr(outErr, "ESM memory buffer null or smaller than 24-byte TES4 header.");
        return false;
    }
    if (!TagEq(bytes, "TES4")) {
        SetErr(outErr, "Invalid ESM header magic (expected 'TES4').");
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    EsmWorldMetaIndex idx{};
    idx.esmPath     = label;
    idx.isLocalized = (ReadU32LE(bytes + 8u) & 0x00000080u) != 0u;
    idx.weathers.reserve(128u);
    idx.climates.reserve(32u);
    idx.npcs.reserve(8192u);
    idx.regions.reserve(128u);
    idx.quests.reserve(1024u);

    const uint32_t tes4DataSz = ReadU32LE(bytes + 4u);
    size_t pos = 24u + size_t(tes4DataSz);
    if (pos > size) {
        SetErr(outErr, "ESM TES4 header record extends past EOF.");
        return false;
    }

    while (pos + 24u <= size) {
        const uint8_t* hdr = bytes + pos;
        if (!TagEq(hdr, "GRUP")) {
            const uint32_t recSz = ReadU32LE(hdr + 4u);
            if (pos + 24u + size_t(recSz) > size) break;
            pos += 24u + size_t(recSz);
            continue;
        }
        const uint32_t groupSize = ReadU32LE(hdr + 4u);
        const uint32_t groupType = ReadU32LE(hdr + 12u);
        if (groupSize < 24u || pos + size_t(groupSize) > size) break;

        if (groupType == 0u && IsTargetMetaGroupTag(hdr + 8u)) {
            ParseMetaRecordsInGroupBuffer(hdr + 24u, size_t(groupSize) - 24u, idx);
        }
        pos += size_t(groupSize);
    }

    const auto t1 = std::chrono::steady_clock::now();
    idx.loadTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    *outIndex = std::move(idx);
    return true;
}

bool LoadEsmWorldMeta(const std::string& esmPath, EsmWorldMetaIndex* outIndex, std::string* outErr) {
    if (esmPath.empty() || !outIndex) {
        SetErr(outErr, "Empty ESM path or null output index.");
        return false;
    }
    std::ifstream f(esmPath, std::ios::binary | std::ios::ate);
    if (!f) {
        SetErr(outErr, "Failed to open ESM file: " + esmPath);
        return false;
    }
    const std::streamoff endOff = f.tellg();
    if (endOff < 24) {
        SetErr(outErr, "ESM file smaller than 24-byte TES4 header.");
        return false;
    }
    const uint64_t totalSize = static_cast<uint64_t>(endOff);
    f.seekg(0, std::ios::beg);

    uint8_t tes4Hdr[24]{};
    if (!f.read(reinterpret_cast<char*>(tes4Hdr), 24) || !TagEq(tes4Hdr, "TES4")) {
        SetErr(outErr, "Invalid ESM header magic (expected 'TES4').");
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    EsmWorldMetaIndex idx{};
    idx.esmPath     = esmPath;
    idx.isLocalized = (ReadU32LE(tes4Hdr + 8u) & 0x00000080u) != 0u;
    idx.weathers.reserve(128u);
    idx.climates.reserve(32u);
    idx.npcs.reserve(8192u);
    idx.regions.reserve(128u);
    idx.quests.reserve(1024u);

    const uint32_t tes4DataSz = ReadU32LE(tes4Hdr + 4u);
    uint64_t pos = 24ull + static_cast<uint64_t>(tes4DataSz);
    if (pos > totalSize) {
        SetErr(outErr, "ESM TES4 header record extends past EOF.");
        return false;
    }

    std::vector<uint8_t> groupBuf;
    while (pos + 24ull <= totalSize) {
        f.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
        uint8_t hdr[24]{};
        if (!f.read(reinterpret_cast<char*>(hdr), 24)) break;

        if (!TagEq(hdr, "GRUP")) {
            const uint32_t recSz = ReadU32LE(hdr + 4u);
            pos += 24ull + static_cast<uint64_t>(recSz);
            continue;
        }
        const uint32_t groupSize = ReadU32LE(hdr + 4u);
        const uint32_t groupType = ReadU32LE(hdr + 12u);
        if (groupSize < 24u || pos + static_cast<uint64_t>(groupSize) > totalSize) break;

        if (groupType == 0u && IsTargetMetaGroupTag(hdr + 8u)) {
            const size_t payloadBytes = static_cast<size_t>(groupSize) - 24u;
            groupBuf.resize(payloadBytes);
            if (payloadBytes > 0u &&
                !f.read(reinterpret_cast<char*>(groupBuf.data()), static_cast<std::streamsize>(payloadBytes))) {
                SetErr(outErr, "Unexpected EOF reading target ESM metadata GRUP.");
                return false;
            }
            ParseMetaRecordsInGroupBuffer(groupBuf.data(), payloadBytes, idx);
        }
        pos += static_cast<uint64_t>(groupSize);
    }

    const auto t1 = std::chrono::steady_clock::now();
    idx.loadTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    *outIndex = std::move(idx);
    return true;
}

// ============================================================================
// Direct Physical `RawWeatherSim` Bridge
// ============================================================================

EsmPhysicalWeatherParams MapEsmWeatherToPhysicalParams(const EsmWeatherRecord& wthr,
                                                       float timeOfDayHours) {
    EsmPhysicalWeatherParams out{};

    const float windNorm = static_cast<float>(wthr.windSpeedByte) / 255.0f;
    const float thunderNorm = (wthr.thunderFrequency > 0u)
        ? Clamp01(1.0f - static_cast<float>(wthr.thunderFrequency) / 255.0f)
        : 0.0f;
    const float precipFadeNorm = 1.0f - Clamp01(static_cast<float>(wthr.precipBeginFadeIn) / 255.0f);

    // 1. Directional daylight vs night weight for fog & NAM0 tints:
    const float wrappedHour = std::fmod(std::fmod(timeOfDayHours, 24.0f) + 24.0f, 24.0f);
    // 0.0 = deep night, 1.0 = full midday
    float dayWeight = 0.0f;
    if (wrappedHour >= 6.0f && wrappedHour <= 19.0f) {
        if (wrappedHour < 8.0f)       dayWeight = (wrappedHour - 6.0f) * 0.5f;
        else if (wrappedHour > 17.0f) dayWeight = (19.0f - wrappedHour) * 0.5f;
        else                          dayWeight = 1.0f;
    }

    // 2. Fog extinction density from authentic FNAM near/far/max fog distances:
    const float fogNear = Lerp(wthr.fogNightNear, wthr.fogDayNear, dayWeight);
    const float fogFar  = Lerp(wthr.fogNightFar,  wthr.fogDayFar,  dayWeight);
    const float fogMax  = Lerp(wthr.fogNightMax,  wthr.fogDayMax,  dayWeight);

    float fogDensity = 0.08f;
    if (wthr.hasFnam && std::isfinite(fogFar) && fogFar > 1.0f) {
        const float safeFar    = std::max(500.0f, fogFar);
        const float nearFactor = Clamp01(1.0f - std::max(0.0f, fogNear) / 14000.0f);
        // Log-scale distance extinction: 1000u -> 0.90, 8000u -> 0.60, 40000u -> 0.36, 250000u -> 0.10
        const float farFactor  = Clamp01(1.0f - std::log10(safeFar / 500.0f) / 3.2f);
        const float maxScale   = (std::isfinite(fogMax) && fogMax > 0.0f) ? Clamp01(fogMax) : 1.0f;
        fogDensity = Clamp01((0.30f * nearFactor + 0.70f * farFactor) * (0.35f + 0.65f * maxScale));
    }

    const std::string edidLow = ToLowerAscii(wthr.editorId);
    const bool edidFog = (edidLow.find("fog") != std::string::npos);
    if (edidFog) {
        fogDensity = std::max(fogDensity, 0.58f);
    }
    out.fogDensity = Clamp01(fogDensity);

    // 3. Precipitation rates (rain / snow) from authentic DATA precipFlags:
    const bool isRainy    = wthr.IsRainy();
    const bool isSnowy    = wthr.IsSnowy();
    const bool isCloudy   = wthr.IsCloudy();
    const bool isPleasant = wthr.IsPleasant();

    if (isRainy) {
        out.rainRate01 = Clamp(0.62f + 0.25f * precipFadeNorm + 0.13f * windNorm, 0.55f, 1.0f);
        out.precipPhase = PrecipPhase::Rain;
    } else if (isSnowy) {
        out.snowRate01 = Clamp(0.62f + 0.25f * precipFadeNorm + 0.13f * windNorm, 0.55f, 1.0f);
        out.precipPhase = (windNorm > 0.45f) ? PrecipPhase::DryPowderBlizzard : PrecipPhase::WetSnow;
    } else {
        out.precipPhase = PrecipPhase::None;
    }

    // 4. Physical wind speed (m/s) & Beaufort scale [0..12]:
    float windMps = 0.8f + windNorm * 22.0f;
    if (isRainy || isSnowy) {
        windMps = std::max(windMps, 7.5f + windNorm * 14.0f);
    } else if (isCloudy) {
        windMps = std::max(windMps, 3.0f + windNorm * 8.0f);
    }
    out.windSpeedMps     = Clamp(windMps, 0.2f, 35.0f);
    out.beaufortWindScale = Clamp(std::pow(out.windSpeedMps / 0.836f, 2.0f / 3.0f), 0.0f, 12.0f);
    out.windDirectionRad  = wthr.windDirectionDeg * (kPi / 180.0f);

    // 5. Cloud cover [0..1] combining DATA classification flags + JNAM/NAM1 active cloud-layer density:
    const float layerHint  = Clamp01(wthr.cloudAlphaDayMean01 * 2.4f);
    const bool  edidCloudy = (edidLow.find("cloudy") != std::string::npos) ||
                             (edidLow.find("overcast") != std::string::npos);
    if (isRainy || isSnowy) {
        out.cloudCover01 = Clamp(0.82f + 0.16f * std::max(layerHint, windNorm), 0.80f, 0.99f);
    } else if (isCloudy || edidCloudy) {
        out.cloudCover01 = Clamp(0.50f + 0.34f * layerHint, 0.50f, 0.85f);
    } else if (edidFog || out.fogDensity > 0.52f) {
        out.cloudCover01 = Clamp(0.45f + 0.30f * out.fogDensity, 0.45f, 0.78f);
    } else if (isPleasant) {
        out.cloudCover01 = Clamp(0.06f + 0.34f * layerHint, 0.05f, 0.42f);
    } else {
        out.cloudCover01 = Clamp(0.20f + 0.55f * layerHint, 0.15f, 0.75f);
    }

    out.thunderIntensity01 = (isRainy && thunderNorm > 0.05f) ? thunderNorm : 0.0f;

    // 6. Suggested RawWeatherSim regime:
    if (isSnowy) {
        out.suggestedRegime = WeatherRegime::Blizzard;
    } else if (isRainy) {
        out.suggestedRegime = (out.thunderIntensity01 > 0.20f || windNorm > 0.55f)
            ? WeatherRegime::Thunderstorm
            : WeatherRegime::Rainstorm;
    } else if (edidFog || out.fogDensity > 0.52f) {
        out.suggestedRegime = WeatherRegime::MountainFogCloudCap;
    } else if (isCloudy || edidCloudy) {
        out.suggestedRegime = WeatherRegime::Overcast;
    } else {
        out.suggestedRegime = (dayWeight < 0.2f) ? WeatherRegime::ArcticClearAurora
                                                 : WeatherRegime::ClearAlpine;
    }

    // 7. Directional sky / horizon / sun / ambient color tints from NAM0:
    out.skyTintRgb     = LerpVec3(wthr.colors.skyUpperNight, wthr.colors.skyUpperDay, dayWeight);
    out.horizonTintRgb = LerpVec3(wthr.colors.horizonNight,  wthr.colors.horizonDay,  dayWeight);
    out.sunTintRgb     = LerpVec3(wthr.colors.sunlightNight, wthr.colors.sunlightDay, dayWeight);
    out.ambientTintRgb = LerpVec3(wthr.colors.ambientNight,  wthr.colors.ambientDay,  dayWeight);

    return out;
}

EsmPhysicalWeatherParams ApplyEsmWeatherToSim(const EsmWeatherRecord& wthr,
                                              RawWeatherSim& sim,
                                              float timeOfDayHours,
                                              bool immediate) {
    const EsmPhysicalWeatherParams p = MapEsmWeatherToPhysicalParams(wthr, timeOfDayHours);
    sim.SetTimeOfDay(timeOfDayHours);
    sim.SetRegime(p.suggestedRegime, immediate);
    return p;
}

}  // namespace raw::worldgen
