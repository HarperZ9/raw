// test_crash_forensics.cpp: Wave 47 VEH Crash-Dump Forensics & 7,376-Class RTTI Symbolizer Tests.
// Linkable into ce_runtime_check via raw::rt::RunCrashForensicsTests(checkFn),
// or buildable standalone with -DRAW_CRASH_FORENSICS_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_crash_forensics.h"
#include "../src/engine_rt/rt_live_config.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace raw::rt {
namespace {

struct FakeObject {
    uint64_t vtablePtr = 0ull;
    uint64_t payload   = 0xCAFEBABE12345678ull;
};

struct MockMemoryRegion {
    const FakeObject* objects[4]{};
    uint64_t          addrs[4]{};
    size_t            count = 0u;
};

bool MockReadU64(uint64_t address, uint64_t* outValue, void* user) noexcept {
    const auto* mem = static_cast<const MockMemoryRegion*>(user);
    if (!mem || !outValue) return false;
    for (size_t i = 0u; i < mem->count; ++i) {
        if (address == mem->addrs[i] && mem->objects[i]) {
            *outValue = mem->objects[i]->vtablePtr;
            return true;
        }
    }
    return false;
}

std::string FindRttiTsvPath() {
    const char* candidates[] = {
        "E:\\wt\\raw-rt9\\data\\versions\\skyrimse-1.7.104-rtti.tsv",
        "data\\versions\\skyrimse-1.7.104-rtti.tsv",
        "..\\data\\versions\\skyrimse-1.7.104-rtti.tsv",
        "..\\..\\data\\versions\\skyrimse-1.7.104-rtti.tsv",
        "raw_versions\\skyrimse-1.7.104-rtti.tsv",
    };
    std::error_code ec;
    for (const char* p : candidates) {
        if (std::filesystem::exists(p, ec)) return p;
    }
    return {};
}

}  // namespace

int RunCrashForensicsTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Load REAL 7,376-class skyrimse-1.7.104-rtti.tsv catalog and verify O(log N) RTTI lookups
    RttiVtableIndex rtti;
    const std::string tsvPath = FindRttiTsvPath();
    const bool loadedReal = !tsvPath.empty() && rtti.LoadFromFile(tsvPath);

    const uint32_t playerRva = rtti.LookupRvaByClassName("PlayerCharacter");
    const uint32_t niNodeRva = rtti.LookupRvaByClassName("NiNode");
    const uint32_t refrRva   = rtti.LookupRvaByClassName("TESObjectREFR");

    const char* subPlayer = nullptr;
    const char* clsPlayer = rtti.LookupByRva(playerRva, &subPlayer);
    const char* clsNiNode = rtti.LookupByRva(niNodeRva);
    const char* clsRefr   = rtti.LookupByRva(refrRva);

    if (!checkFn) {
        std::printf("[CrashForensics RTTI] loaded=%zu classes | PlayerCharacter=0x%X (%s) NiNode=0x%X TESObjectREFR=0x%X\n",
                    rtti.Count(), playerRva, subPlayer ? subPlayer : "?", niNodeRva, refrRva);
    }

    check(loadedReal && rtti.Count() >= 7000u, "crash_forensics_loads_real_7000plus_rtti_catalog");
    check(playerRva != 0u && niNodeRva != 0u && refrRva != 0u &&
          clsPlayer && std::strcmp(clsPlayer, "PlayerCharacter") == 0 &&
          clsNiNode && std::strcmp(clsNiNode, "NiNode") == 0 &&
          clsRefr   && std::strcmp(clsRefr,   "TESObjectREFR") == 0,
          "crash_forensics_rtti_exact_lookup_player_ninode_refr");

    // 2. Planted-failure control: unknown RVA -> nullptr; fatal vs non-fatal exception filter; SEH guard
    {
        const bool unknownNull =
            rtti.LookupByRva(0u) == nullptr &&
            rtti.LookupByRva(0x1234567u) == nullptr &&
            rtti.LookupByRva(0xDEADBEEFu) == nullptr;
        check(unknownNull, "crash_forensics_unknown_rva_returns_nullptr");

        const bool fatalFilterOk =
            IsFatalCrashExceptionCode(0xC0000005u) && // ACCESS_VIOLATION
            IsFatalCrashExceptionCode(0xC000001Du) && // ILLEGAL_INSTRUCTION
            IsFatalCrashExceptionCode(0xC0000094u) && // INT_DIVIDE_BY_ZERO
            IsFatalCrashExceptionCode(0xC00000FDu) && // STACK_OVERFLOW
            IsFatalCrashExceptionCode(0xC0000096u) && // PRIV_INSTRUCTION
            !IsFatalCrashExceptionCode(0xE06D7363u) && // MSVC C++ exception (MUST IGNORE!)
            !IsFatalCrashExceptionCode(0x40010006u) && // DBG_PRINTEXCEPTION_C (MUST IGNORE!)
            !IsFatalCrashExceptionCode(0x406D1388u) && // SetThreadName (MUST IGNORE!)
            !IsFatalCrashExceptionCode(0x80000003u) && // BREAKPOINT (MUST IGNORE!)
            !IsFatalCrashExceptionCode(0u);
        check(fatalFilterOk, "crash_forensics_fatal_exception_filter_rejects_cpp_and_dbg");

        bool sehOk = !CrashForensicsIsInSehGuard();
        {
            ScopedCrashForensicsSehGuard g1;
            sehOk = sehOk && CrashForensicsIsInSehGuard();
            {
                ScopedCrashForensicsSehGuard g2;
                sehOk = sehOk && CrashForensicsIsInSehGuard();
            }
            sehOk = sehOk && CrashForensicsIsInSehGuard();
        }
        sehOk = sehOk && !CrashForensicsIsInSehGuard();
        check(sehOk, "crash_forensics_nested_seh_guard_balanced");
    }

    // 3. Synthetic crash report with fake PlayerCharacter/NiNode/TESObjectREFR vtables in registers + stack
    {
        constexpr uint64_t kFakeExeBase = 0x00007FF610000000ull;
        constexpr uint32_t kFakeExeSize = 0x03929000u;

        FakeObject fakePlayer{kFakeExeBase + playerRva, 1ull};
        FakeObject fakeNiNode{kFakeExeBase + niNodeRva, 2ull};
        FakeObject fakeRefr  {kFakeExeBase + refrRva,   3ull};
        FakeObject fakeBogus {kFakeExeBase + 0x123456u, 4ull};

        MockMemoryRegion mockMem{};
        mockMem.objects[0] = &fakePlayer; mockMem.addrs[0] = 0x0000022000101000ull;
        mockMem.objects[1] = &fakeNiNode; mockMem.addrs[1] = 0x0000022000202000ull;
        mockMem.objects[2] = &fakeRefr;   mockMem.addrs[2] = 0x0000022000303000ull;
        mockMem.objects[3] = &fakeBogus;  mockMem.addrs[3] = 0x0000022000404000ull;
        mockMem.count = 4u;

        CrashReportInput in{};
        in.exceptionCode          = 0xC0000005u;
        in.exceptionAddress       = kFakeExeBase + 0x006565B0ull;
        in.accessViolationOp      = 0u;
        in.accessViolationAddress = 0x0000000000000060ull;
        in.exeBase                = kFakeExeBase;
        in.exeSize                = kFakeExeSize;
        in.rttiCatalogCount       = static_cast<uint32_t>(rtti.Count());
        in.regs.gpr[1]            = mockMem.addrs[0]; // RCX -> PlayerCharacter
        in.regs.gpr[2]            = mockMem.addrs[3]; // RDX -> bogus unknown vtable (ignored)
        in.regs.gpr[7]            = mockMem.addrs[1]; // RDI -> NiNode
        in.regs.gpr[4]            = 0x000000A00010F000ull; // RSP
        in.regs.rip               = in.exceptionAddress;
        in.regs.eflags            = 0x00010246u;

        const uint8_t sampleInsn[4] = {0x48u, 0x8Bu, 0x41u, 0x60u}; // mov rax, [rcx+0x60]
        std::memcpy(in.faultBytes, sampleInsn, sizeof(sampleInsn));
        in.faultBytesLen = sizeof(sampleInsn);
        std::snprintf(in.decodedInstruction, sizeof(in.decodedInstruction), "mov rax, [rcx+0x60]");

        in.stackFrames[0] = kFakeExeBase + 0x006565B0ull;
        in.stackFrames[1] = kFakeExeBase + 0x00649810ull;
        in.stackFrameCount = 2u;

        const uint64_t stackSlots[6] = {
            0x0ull,
            0x0000000000000042ull,
            mockMem.addrs[0], // duplicate PlayerCharacter (deduped cleanly!)
            mockMem.addrs[2], // [RSP+0x18] -> TESObjectREFR
            mockMem.addrs[3], // bogus
            kFakeExeBase + niNodeRva, // direct vtable pointer on stack
        };

        in.rttiHitCount = ResolveRttiHitsFromMemory(
            rtti, kFakeExeBase, kFakeExeSize, in.regs,
            stackSlots, 6u, &MockReadU64, &mockMem,
            in.rttiHits, 32u);

        in.frameCounter     = 14420ull;
        in.activeDetours    = 12u;
        in.cellFormId       = 0x00009745u;
        in.interior         = 0u;
        in.acousticEnvClass = 1u;
        in.windSpeed        = 8.4f;
        in.synopticSnow     = 0.65f;

        const std::string report = FormatCrashReport(in);
        const bool reportOk =
            in.rttiHitCount >= 3u &&
            report.find("EXCEPTION_ACCESS_VIOLATION") != std::string::npos &&
            report.find("SkyrimSE.exe+0x6565B0") != std::string::npos &&
            report.find("mov rax, [rcx+0x60]") != std::string::npos &&
            report.find("48 8B 41 60") != std::string::npos &&
            report.find("RCX") != std::string::npos &&
            report.find("PlayerCharacter") != std::string::npos &&
            report.find("NiNode") != std::string::npos &&
            report.find("TESObjectREFR") != std::string::npos &&
            report.find("[RSP+0x18]") != std::string::npos &&
            report.find("Frame=14420") != std::string::npos;

        if (!checkFn) {
            std::printf("[CrashReport] len=%zu bytes, rttiHits=%u\n", report.size(), in.rttiHitCount);
        }
        check(reportOk, "crash_forensics_synthetic_report_attributes_rtti_regs_and_stack");
    }

    // 4. [Diagnostics] INI round-trip (default CrashForensics=1) & CrashForensics=0 disable path
    {
        const LiveEngineConfig def = DefaultLiveEngineConfig();
        const std::string iniText  = FormatDefaultLiveConfigIni(def);
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(iniText, parsedDef);

        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[Diagnostics]\nCrashForensics=0\n", parsedOff);

        LiveStatusDumpData dump{};
        dump.config = parsedOff;
        const std::string dumpText = FormatLiveStatusDump(dump);

        const bool iniOk =
            def.crashForensicsEnabled &&
            iniText.find("CrashForensics=1") != std::string::npos &&
            parsedDef.crashForensicsEnabled &&
            !parsedOff.crashForensicsEnabled &&
            dumpText.find("CrashForensics: 0") != std::string::npos;
        check(iniOk, "crash_forensics_ini_roundtrip_and_disable_path");
    }

    if (!checkFn) {
        std::printf("CrashForensicsTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_CRASH_FORENSICS_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunCrashForensicsTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
