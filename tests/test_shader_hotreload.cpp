// test_shader_hotreload.cpp: Wave 58 Verification Suite for Fail-Closed Live Shader Hot-Reload
// Watchdog (`rt_shader_hotreload`) and `reload_shaders` Console Dispatcher command.
// Exposes BOTH `int run_shader_hotreload_tests()` and `int raw::rt::RunShaderHotReloadTests(checkFn)`.
// Original work, MIT.

#include "rt_shader_hotreload.h"
#include "rt_console_diag.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace raw::rt {

namespace {

bool WriteTextFile(const std::filesystem::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

}  // namespace

int RunShaderHotReloadTests(void (*checkFn)(bool, const char*) = nullptr) {
    int localFail = 0;
    auto Check = [&](bool cond, const char* msg) {
        if (checkFn) {
            checkFn(cond, msg);
        } else {
            std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
        }
        if (!cond) ++localFail;
    };

    // -------------------------------------------------------------------------
    // Check 1: Atomic Pre-Validator & Planted-Failure Controls
    // -------------------------------------------------------------------------
    {
        const std::string validHlsl =
            "#include \"LightingCommon.hlsli\"\n"
            "// Brace inside line comment: { { {\n"
            "/* Block comment with } } } */\n"
            "float4 PSMain(float2 uv : TEXCOORD0) : SV_Target {\n"
            "    return float4(uv, 0.0f, 1.0f);\n"
            "}\n";
        const auto rValid = ValidateShaderSourceBytes(
            reinterpret_cast<const uint8_t*>(validHlsl.data()), validHlsl.size(), "ValidTest.hlsl");

        // Planted failure 1a: empty source
        const auto rEmpty = ValidateShaderSourceBytes(nullptr, 0u, "Empty.hlsl");

        // Planted failure 1b: exceeds max size
        const uint8_t dummyByte = 'a';
        const auto rBig = ValidateShaderSourceBytes(&dummyByte, kMaxShaderSourceBytes + 1u, "Huge.hlsl");

        // Planted failure 1c: embedded NUL byte
        const uint8_t nulBuf[] = {'f', 'l', 'o', 'a', 't', '\0', ' ', 'x', ';'};
        const auto rNul = ValidateShaderSourceBytes(nulBuf, sizeof(nulBuf), "BinaryNul.hlsl");

        // Planted failure 1d: unbalanced braces (missing close + extra close)
        const std::string badBrace1 = "float4 main() { return float4(1,1,1,1);\n";
        const std::string badBrace2 = "float4 main() } { return 0; }\n";
        const auto rBrace1 = ValidateShaderSourceBytes(
            reinterpret_cast<const uint8_t*>(badBrace1.data()), badBrace1.size(), "BadBrace1.hlsl");
        const auto rBrace2 = ValidateShaderSourceBytes(
            reinterpret_cast<const uint8_t*>(badBrace2.data()), badBrace2.size(), "BadBrace2.hlsl");

        // Planted failure 1e: malformed #include & path traversal
        const std::string badInc1 = "#include \"unclosed.hlsli\nfloat x;\n";
        const std::string badInc2 = "#include \"../escape.hlsli\"\nfloat x;\n";
        const auto rInc1 = ValidateShaderSourceBytes(
            reinterpret_cast<const uint8_t*>(badInc1.data()), badInc1.size(), "BadInc1.hlsl");
        const auto rInc2 = ValidateShaderSourceBytes(
            reinterpret_cast<const uint8_t*>(badInc2.data()), badInc2.size(), "BadInc2.hlsl");

        // Planted failure 1f: unterminated block comment
        const std::string badCom = "/* unterminated comment\nfloat x;\n";
        const auto rCom = ValidateShaderSourceBytes(
            reinterpret_cast<const uint8_t*>(badCom.data()), badCom.size(), "BadCom.hlsl");

        // Deterministic FNV-1a verification
        const uint64_t h1 = ComputeShaderFnv1a64(reinterpret_cast<const uint8_t*>(validHlsl.data()), validHlsl.size());
        const uint64_t h2 = ComputeShaderFnv1a64(reinterpret_cast<const uint8_t*>(validHlsl.data()), validHlsl.size());

        std::printf("[ShaderHotReload] Check1 PreValidator: valid=%u inc=%u fnv1a=0x%016llX | rejects=(%s,%s,%s,%s,%s,%s)\n",
                    rValid.valid ? 1u : 0u, rValid.includeCount, static_cast<unsigned long long>(rValid.fnv1a64),
                    ShaderRejectReasonName(rEmpty.reason), ShaderRejectReasonName(rBig.reason),
                    ShaderRejectReasonName(rNul.reason), ShaderRejectReasonName(rBrace1.reason),
                    ShaderRejectReasonName(rInc2.reason), ShaderRejectReasonName(rCom.reason));

        Check(rValid.valid && rValid.reason == ShaderRejectReason::Ok &&
              rValid.includeCount == 1u && rValid.fnv1a64 == h1 && h1 == h2 && h1 != 0ull &&
              !rEmpty.valid && rEmpty.reason == ShaderRejectReason::EmptyFile &&
              !rBig.valid && rBig.reason == ShaderRejectReason::ExceedsMaxSize &&
              !rNul.valid && rNul.reason == ShaderRejectReason::ContainsNulByte &&
              !rBrace1.valid && rBrace1.reason == ShaderRejectReason::UnbalancedBraces &&
              !rBrace2.valid && rBrace2.reason == ShaderRejectReason::UnbalancedBraces &&
              !rInc1.valid && rInc1.reason == ShaderRejectReason::MalformedInclude &&
              !rInc2.valid && rInc2.reason == ShaderRejectReason::MalformedInclude &&
              !rCom.valid && rCom.reason == ShaderRejectReason::UnterminatedCommentOrString,
              "ShaderHotReload pre-validator: valid HLSL passes & all 6 planted-failure classes fail closed");
    }

    // -------------------------------------------------------------------------
    // Check 2: Live Directory Watcher, FNV-1a Dedup, Fail-Closed Rejection & Monotonic Generation
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        const std::filesystem::path tmpDir =
            std::filesystem::temp_directory_path(ec) / "raw_shader_hotreload_w58";
        std::filesystem::remove_all(tmpDir, ec);
        std::filesystem::create_directories(tmpDir, ec);

        const std::filesystem::path fCommon = tmpDir / "Common.hlsli";
        const std::filesystem::path fWater  = tmpDir / "WaterPass.hlsl";
        const std::filesystem::path fIgnore = tmpDir / "readme.txt";

        WriteTextFile(fCommon, "float Sqr(float x) { return x * x; }\n");
        WriteTextFile(fWater,  "#include \"Common.hlsli\"\nfloat4 main() : SV_Target { return float4(1,0,0,1); }\n");
        WriteTextFile(fIgnore, "not a shader file\n");

        const std::string initMsg = ShaderHotReloadInit(tmpDir.string());
        RawShaderHotReloadState s0{};
        const int rcNull = RAW_GetShaderHotReloadState(nullptr);
        const int rc0    = RAW_GetShaderHotReloadState(&s0);

        // No-change poll: generation must remain 0
        const bool chgNone = ShaderHotReloadPollScan(false);

        // Modify WaterPass.hlsl with a valid edit -> generation increments 0 -> 1 & dirty list contains WaterPass.hlsl
        const std::string waterV2 =
            "#include \"Common.hlsli\"\nfloat4 main() : SV_Target { return float4(0,1,0,1); }\n// v2 edit\n";
        WriteTextFile(fWater, waterV2);
        const bool chgValid1 = ShaderHotReloadPollScan(false);
        RawShaderHotReloadState s1{};
        RAW_GetShaderHotReloadState(&s1);
        const auto dirty1 = ShaderHotReloadGetDirtyList();

        // Rewrite exact same content (same FNV-1a hash) -> must NOT spuriously bump generation!
        WriteTextFile(fWater, waterV2);
        const bool chgSameHash = ShaderHotReloadPollScan(false);
        RawShaderHotReloadState s1b{};
        RAW_GetShaderHotReloadState(&s1b);

        // Inject broken edit (unbalanced braces) -> MUST fail closed: rejectedEdits increments, gen stays 1!
        WriteTextFile(fWater, "#include \"Common.hlsli\"\nfloat4 main() : SV_Target { return float4(0,1,0,1);\n");
        const bool chgBroken = ShaderHotReloadPollScan(false);
        RawShaderHotReloadState sReject{};
        RAW_GetShaderHotReloadState(&sReject);

        // Fix WaterPass.hlsl with valid v3 edit -> generation monotonically advances 1 -> 2!
        WriteTextFile(fWater, "#include \"Common.hlsli\"\nfloat4 main() : SV_Target { return float4(0,0,1,1); }\n// v3 fixed\n");
        const bool chgFixed = ShaderHotReloadPollScan(false);
        RawShaderHotReloadState s2{};
        RAW_GetShaderHotReloadState(&s2);

        // Check 3: On-demand ConsoleDiag `reload_shaders` trigger + `help` check
        std::string respReload, respHelp;
        const bool okReload = DispatchDiagnosticCommand("reload_shaders", respReload);
        const bool okHelp   = DispatchDiagnosticCommand("help", respHelp);
        RawShaderHotReloadState s3{};
        RAW_GetShaderHotReloadState(&s3);

        // Verify 60-frame OnPresent cadence doesn't crash and cleanly shuts down
        for (int i = 0; i < 60; ++i) {
            ShaderHotReloadOnPresent();
        }
        ShaderHotReloadShutdown();
        std::filesystem::remove_all(tmpDir, ec);
        const bool cleanedUp = !std::filesystem::exists(tmpDir, ec);

        std::printf("[ShaderHotReload] Check2+3 Watcher+Console: init='%s' gen=%u->%u->%u->%u rej=%u(%s) cmd='%s' clean=%u\n",
                    initMsg.c_str(), s0.reloadGeneration, s1.reloadGeneration,
                    s2.reloadGeneration, s3.reloadGeneration,
                    sReject.rejectedEdits,
                    ShaderRejectReasonName(static_cast<ShaderRejectReason>(sReject.lastRejectReason)),
                    respReload.c_str(), cleanedUp ? 1u : 0u);

        Check(rcNull == 0 && rc0 == 1 &&
              s0.watcherActive == 1u && s0.dirExists == 1u &&
              s0.trackedFileCount == 2u && s0.reloadGeneration == 0u &&
              !chgNone &&
              chgValid1 && s1.reloadGeneration == 1u &&
              dirty1.size() == 1u && dirty1[0] == "WaterPass.hlsl" &&
              std::strcmp(s1.lastDirtyShader, "WaterPass.hlsl") == 0 &&
              !chgSameHash && s1b.reloadGeneration == 1u &&
              !chgBroken && sReject.reloadGeneration == 1u &&
              sReject.rejectedEdits == 1u &&
              sReject.lastRejectReason == static_cast<uint8_t>(ShaderRejectReason::UnbalancedBraces) &&
              chgFixed && s2.reloadGeneration == 2u &&
              okReload && okHelp &&
              respHelp.find("reload_shaders") != std::string::npos &&
              respReload.find("[ShaderHotReload]") != std::string::npos &&
              s3.reloadGeneration == 3u &&
              cleanedUp,
              "ShaderHotReload live watcher: FNV-1a dedup, fail-closed bad edit rejection, monotonic gen & reload_shaders command");
    }

    return localFail;
}

}  // namespace raw::rt

int run_shader_hotreload_tests() {
    return raw::rt::RunShaderHotReloadTests(nullptr);
}

#ifdef RAW_SHADER_HOTRELOAD_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 58: Fail-Closed Live Shader Hot-Reload Watchdog Suite ===\n");
    const int fail = run_shader_hotreload_tests();
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
