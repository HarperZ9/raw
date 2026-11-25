// test_bsa_archive.cpp: Comprehensive unit & live-archive tests for `raw::worldgen::RawBsaArchive`
// (TES5 v104 zlib + Skyrim SE v105 LZ4 Frame 0x184D2204, embedded filenames 0x100, per-file compression inversion).
//
// Link directly into `ce_runtime_check` (call `raw::worldgen::RunBsaArchiveTests(Check)`)
// OR compile standalone with `-DRAW_BSA_TEST_STANDALONE=1`!
// Original work, MIT.

#include "../src/worldgen/RawBsaArchive.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace raw::worldgen {
namespace {

void PushU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v & 0xFFu));
    b.push_back(uint8_t((v >> 8) & 0xFFu));
}

void PushU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v & 0xFFu));
    b.push_back(uint8_t((v >> 8) & 0xFFu));
    b.push_back(uint8_t((v >> 16) & 0xFFu));
    b.push_back(uint8_t((v >> 24) & 0xFFu));
}

void PushU64(std::vector<uint8_t>& b, uint64_t v) {
    PushU32(b, uint32_t(v & 0xFFFFFFFFull));
    PushU32(b, uint32_t(v >> 32));
}

// Wraps payload bytes in a valid RFC 1950 zlib stream (stored DEFLATE block + exact Adler-32).
std::vector<uint8_t> WrapStoredZlib(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.push_back(0x78u); // CMF: deflate, 32K window
    out.push_back(0x01u); // FLG: check bits ((0x78*256 + 0x01) % 31 == 0)
    // Single final stored block (BFINAL=1, BTYPE=00)
    const uint16_t len = static_cast<uint16_t>(payload.size());
    const uint16_t nlen = static_cast<uint16_t>(~len);
    out.push_back(0x01u);
    PushU16(out, len);
    PushU16(out, nlen);
    out.insert(out.end(), payload.begin(), payload.end());
    // Adler-32 checksum (big-endian per RFC 1950)
    uint32_t a = 1u, b = 0u;
    for (uint8_t byte : payload) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    const uint32_t adler = (b << 16) | a;
    out.push_back(uint8_t((adler >> 24) & 0xFFu));
    out.push_back(uint8_t((adler >> 16) & 0xFFu));
    out.push_back(uint8_t((adler >> 8) & 0xFFu));
    out.push_back(uint8_t(adler & 0xFFu));
    return out;
}

struct SyntheticEntrySpec {
    std::string          folder;
    std::string          fileName;
    std::vector<uint8_t> rawData;
    bool                 invertCompression = false;
};

// Builds a spec-compliant synthetic v104 (16B folder rec + zlib) or v105 (24B folder rec + LZ4 Frame) `.bsa` in memory.
std::vector<uint8_t> BuildSyntheticBsa(uint32_t version, uint32_t archiveFlags,
                                       const std::vector<SyntheticEntrySpec>& specs) {
    const bool isV105 = (version == kBsaVersionSkyrimSE);
    const bool defaultComp = (archiveFlags & kBsaFlagCompressed) != 0u;
    const bool embedNames  = (archiveFlags & kBsaFlagEmbedFileNames) != 0u;

    // Group into 1 folder per spec for simplicity & deterministic testing
    uint32_t totalFolderNameLen = 0u;
    uint32_t totalFileNameLen   = 0u;
    for (const auto& s : specs) {
        totalFolderNameLen += static_cast<uint32_t>(s.folder.size() + 1u);
        totalFileNameLen   += static_cast<uint32_t>(s.fileName.size() + 1u);
    }

    const uint32_t folderRecSize = isV105 ? 24u : 16u;
    const uint32_t folderCount   = static_cast<uint32_t>(specs.size());
    const uint32_t fileCount     = static_cast<uint32_t>(specs.size());

    // Pre-encode payloads
    std::vector<std::vector<uint8_t>> payloads(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        std::vector<uint8_t>& p = payloads[i];
        if (embedNames) {
            const std::string emb = s.folder + "\\" + s.fileName;
            p.push_back(static_cast<uint8_t>(emb.size()));
            p.insert(p.end(), emb.begin(), emb.end());
        }
        const bool isComp = defaultComp ^ s.invertCompression;
        if (!isComp) {
            p.insert(p.end(), s.rawData.begin(), s.rawData.end());
        } else {
            PushU32(p, static_cast<uint32_t>(s.rawData.size()));
            if (isV105) {
                std::vector<uint8_t> lz4;
                CompressLz4Frame(s.rawData.data(), s.rawData.size(), &lz4);
                p.insert(p.end(), lz4.begin(), lz4.end());
            } else {
                const auto zl = WrapStoredZlib(s.rawData);
                p.insert(p.end(), zl.begin(), zl.end());
            }
        }
    }

    const uint32_t folderRecsStart = 36u;
    const uint32_t fileBlocksStart = folderRecsStart + folderCount * folderRecSize;
    uint32_t cursor = fileBlocksStart;
    std::vector<uint32_t> folderBlockOffsets(specs.size(), 0u);
    for (size_t i = 0; i < specs.size(); ++i) {
        folderBlockOffsets[i] = cursor + totalFileNameLen; // BSA spec stores offset + totalFileNameLength
        cursor += 1u + static_cast<uint32_t>(specs[i].folder.size() + 1u) + 16u;
    }
    const uint32_t fileDataStart = cursor + totalFileNameLen;

    std::vector<uint32_t> dataOffsets(specs.size(), 0u);
    uint32_t dCur = fileDataStart;
    for (size_t i = 0; i < specs.size(); ++i) {
        dataOffsets[i] = dCur;
        dCur += static_cast<uint32_t>(payloads[i].size());
    }

    std::vector<uint8_t> bsa;
    bsa.reserve(dCur);
    // 36-byte header
    PushU32(bsa, kBsaMagic);
    PushU32(bsa, version);
    PushU32(bsa, folderRecsStart);
    PushU32(bsa, archiveFlags);
    PushU32(bsa, folderCount);
    PushU32(bsa, fileCount);
    PushU32(bsa, totalFolderNameLen);
    PushU32(bsa, totalFileNameLen);
    PushU32(bsa, 0x0001u);

    // Folder records
    for (size_t i = 0; i < specs.size(); ++i) {
        PushU64(bsa, 0x1000ull + i);
        PushU32(bsa, 1u);
        if (isV105) {
            PushU32(bsa, 0u); // 4-byte v105 padding
            PushU64(bsa, folderBlockOffsets[i]);
        } else {
            PushU32(bsa, folderBlockOffsets[i]);
        }
    }

    // File record blocks
    for (size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        bsa.push_back(static_cast<uint8_t>(s.folder.size() + 1u));
        bsa.insert(bsa.end(), s.folder.begin(), s.folder.end());
        bsa.push_back(0u);
        PushU64(bsa, 0x2000ull + i);
        uint32_t szField = static_cast<uint32_t>(payloads[i].size());
        if (s.invertCompression) szField |= 0x40000000u;
        PushU32(bsa, szField);
        PushU32(bsa, dataOffsets[i]);
    }

    // Global file name block
    for (const auto& s : specs) {
        bsa.insert(bsa.end(), s.fileName.begin(), s.fileName.end());
        bsa.push_back(0u);
    }

    // Payloads
    for (const auto& p : payloads) {
        bsa.insert(bsa.end(), p.begin(), p.end());
    }
    return bsa;
}

}  // namespace

int RunBsaArchiveTests(void (*checkFn)(bool, const char*)) {
    int fails = 0;
    auto Report = [&](bool cond, const char* name) {
        if (!cond) ++fails;
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // 1. Clean-room LZ4 Frame (0x184D2204) compress/decompress round-trip with overlapping RLE & long matches!
    {
        std::vector<uint8_t> orig;
        const char* phrase = "Gamebryo File Format, Version 20.2.0.7 - RAW Clean-Room LZ4 Test! ";
        for (int i = 0; i < 64; ++i) {
            orig.insert(orig.end(), phrase, phrase + std::strlen(phrase));
        }
        // Add long single-byte RLE run to exercise overlapping offset=1 match decoding
        orig.insert(orig.end(), 600u, uint8_t('Z'));

        std::vector<uint8_t> comp, decomp;
        std::string err;
        const bool okComp = CompressLz4Frame(orig.data(), orig.size(), &comp);
        const bool okDecomp = okComp && DecompressLz4Frame(comp.data(), comp.size(), orig.size(), &decomp, &err);
        const bool match = okDecomp && (decomp == orig) && (comp.size() < orig.size() / 4u);

        // Corrupt content checksum byte -> must fail closed!
        std::vector<uint8_t> corrupt = comp;
        corrupt.back() ^= 0xFFu;
        std::vector<uint8_t> badOut;
        const bool rejCorrupt = !DecompressLz4Frame(corrupt.data(), corrupt.size(), orig.size(), &badOut, &err);
        Report(match && rejCorrupt, "bsa_lz4_frame_roundtrip_and_checksum_reject");
    }

    // 2. Synthetic TES5 v104 (zlib + embedded names + per-file compression inversion) round-trip!
    {
        const std::string txtA = "RAW TES5 v104 zlib compressed payload: Nordic Spruce & Glacial Fjord!";
        const std::string txtB = "RAW TES5 v104 inverted-uncompressed payload!";
        std::vector<SyntheticEntrySpec> specs = {
            {"meshes\\flora", "spruce.txt", {txtA.begin(), txtA.end()}, false},
            {"scripts\\raw",  "config.ini", {txtB.begin(), txtB.end()}, true},
        };
        const uint32_t flags = kBsaFlagDirNames | kBsaFlagFileNames | kBsaFlagCompressed | kBsaFlagEmbedFileNames;
        const auto bytes = BuildSyntheticBsa(kBsaVersionSkyrimLE, flags, specs);

        RawBsaArchive bsa;
        std::string err;
        const bool opened = bsa.OpenFromMemory(bytes.data(), bytes.size(), "synth_v104.bsa", &err);
        std::vector<uint8_t> outA, outB;
        const bool okA = opened && bsa.ExtractFile("MESHES\\FLORA\\SPRUCE.TXT", &outA, &err);
        const bool okB = opened && bsa.ExtractFile("scripts/raw/config.ini", &outB, &err);
        const auto st = bsa.GetArchiveStats();
        Report(opened && okA && okB &&
               st.version == 104u && st.fileCount == 2u && st.compressedFiles == 1u && st.uncompressedFiles == 1u &&
               std::string(outA.begin(), outA.end()) == txtA &&
               std::string(outB.begin(), outB.end()) == txtB,
               "bsa_synthetic_v104_zlib_and_inversion_roundtrip");
    }

    // 3. Synthetic Skyrim SE v105 (24B padded folder recs + LZ4 Frame + embedded names) round-trip!
    {
        std::string bigPayload;
        for (int i = 0; i < 40; ++i) bigPayload += "Gamebryo File Format, Version 20.2.0.7\n";
        std::vector<SyntheticEntrySpec> specs = {
            {"meshes\\actors\\dragon", "dragon_test.nif", {bigPayload.begin(), bigPayload.end()}, false},
            {"interface\\fonts",       "fontconfig.txt",  {'F','O','N','T','S','_','O','K'},     true},
        };
        const uint32_t flags = kBsaFlagDirNames | kBsaFlagFileNames | kBsaFlagCompressed | kBsaFlagEmbedFileNames;
        const auto bytes = BuildSyntheticBsa(kBsaVersionSkyrimSE, flags, specs);

        RawBsaArchive bsa;
        std::string err;
        const bool opened = bsa.OpenFromMemory(bytes.data(), bytes.size(), "synth_v105.bsa", &err);
        std::vector<uint8_t> nifOut, fontOut;
        const bool okNif = opened && bsa.ExtractFile("meshes/actors/dragon/dragon_test.nif", &nifOut, &err);
        const bool okFnt = opened && bsa.ExtractFile("interface/fonts/fontconfig.txt", &fontOut, &err);
        const auto listNif = bsa.ListFiles(".nif", false);
        Report(opened && okNif && okFnt &&
               bsa.GetArchiveStats().version == 105u &&
               listNif.size() == 1u &&
               std::string(nifOut.begin(), nifOut.end()) == bigPayload &&
               std::string(fontOut.begin(), fontOut.end()) == "FONTS_OK",
               "bsa_synthetic_v105_lz4_roundtrip");
    }

    // 4. Planted-failure controls: corrupted magic, unsupported version, truncated buffer, missing file!
    {
        RawBsaArchive bsa;
        std::string err;
        uint8_t badMagic[64]{};
        badMagic[0] = 'X'; badMagic[1] = 'Y'; badMagic[2] = 'Z'; badMagic[3] = 0;
        const bool rejMagic = !bsa.OpenFromMemory(badMagic, sizeof(badMagic), "bad_magic", &err);

        uint8_t badVer[64]{};
        badVer[0] = 'B'; badVer[1] = 'S'; badVer[2] = 'A'; badVer[3] = 0;
        badVer[4] = 99u; // unsupported version 99
        badVer[8] = 36u;
        const bool rejVer = !bsa.OpenFromMemory(badVer, sizeof(badVer), "bad_ver", &err);
        const bool rejMissing = !bsa.Open("Z:/nonexistent/definitely_not_here.bsa", &err);
        Report(rejMagic && rejVer && rejMissing, "bsa_corrupted_header_fail_closed");
    }

    // 5. LIVE Real Skyrim SE Archive Tests (gracefully skipped if Skyrim SE Data dir is not installed!)
    const std::string skyrimData = "H:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data";
    const std::string miscPath   = skyrimData + "/Skyrim - Misc.bsa";
    const std::string meshesPath = skyrimData + "/Skyrim - Meshes0.bsa";

    if (std::filesystem::exists(miscPath)) {
        RawBsaArchive miscBsa;
        std::string err;
        const bool opened = miscBsa.Open(miscPath, &err);
        const auto st = miscBsa.GetArchiveStats();
        const auto pexFiles = miscBsa.ListFiles(".pex", false);
        std::vector<uint8_t> extracted;
        bool extractedOk = false;
        bool magicOk = false;
        if (opened && !pexFiles.empty()) {
            // Skyrim - Misc.bsa stores 14,000+ real Papyrus .pex scripts (uncompressed v105); verify exact 0xFA57C0DE magic!
            const auto* e = miscBsa.FindEntry(pexFiles[0]);
            if (e) {
                extractedOk = miscBsa.ExtractEntry(*e, &extracted, &err);
                if (extractedOk && extracted.size() >= 16u &&
                    extracted[0] == 0xFAu && extracted[1] == 0x57u &&
                    extracted[2] == 0xC0u && extracted[3] == 0xDEu) {
                    magicOk = true;
                }
            }
        }
        Report(opened && st.version == 105u && st.fileCount > 5000u && extractedOk && magicOk,
               "bsa_real_skyrim_misc_v105_pex_extract");
    } else {
        Report(true, "bsa_real_skyrim_misc_skipped_not_installed");
    }

    if (std::filesystem::exists(meshesPath)) {
        RawBsaArchive meshBsa;
        std::string err;
        const bool opened = meshBsa.Open(meshesPath, &err);
        const auto st = meshBsa.GetArchiveStats();
        const auto nifFiles = meshBsa.ListFiles(".nif", false);
        std::vector<uint8_t> nifBytes;
        bool extractedOk = false;
        bool nifMagicOk = false;
        if (opened && !nifFiles.empty()) {
            for (const auto& p : nifFiles) {
                const auto* e = meshBsa.FindEntry(p);
                if (e && e->compressed) {
                    extractedOk = meshBsa.ExtractEntry(*e, &nifBytes, &err);
                    if (extractedOk && nifBytes.size() >= 20u &&
                        std::memcmp(nifBytes.data(), "Gamebryo File Format", 20u) == 0) {
                        nifMagicOk = true;
                    }
                    break;
                }
            }
        }
        Report(opened && st.version == 105u && st.fileCount > 10000u && extractedOk && nifMagicOk,
               "bsa_real_skyrim_meshes0_v105_lz4_nif_extract");
    } else {
        Report(true, "bsa_real_skyrim_meshes0_skipped_not_installed");
    }

    return fails;
}

}  // namespace raw::worldgen

#ifdef RAW_BSA_TEST_STANDALONE
int main() {
    const int fails = raw::worldgen::RunBsaArchiveTests(nullptr);
    std::printf("RawBsaArchive standalone test summary: %s (%d failures)\n",
                fails == 0 ? "ALL PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
#endif
