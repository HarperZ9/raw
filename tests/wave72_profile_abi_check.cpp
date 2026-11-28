// Standalone C++23 ABI & Planted-Mismatch Test for Wave 72 Multi-Game Creation Engine Profiles
// (Skyrim SE / Fallout 4 / Fallout 76 guard / Starfield CE2 Native DX12 + Agility SDK 1.722.1).
// Original work, MIT.

#include <cstdio>
#include <cstdlib>

#include "../src/common/raw_engine_profile.h"
#include "../src/d3d12_bridge/NativeDx12Host.h"

namespace {

int g_fail = 0;

void Expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    } else {
        std::printf("PASS: %s\n", msg);
    }
}

}  // namespace

int main() {
    using namespace raw::versions;
    using namespace raw::bridge;

    // 1. Known-answer multi-game profile resolution
    const auto sky = ResolveEngineProfileByExe("SkyrimSE.exe", 1, 7);
    Expect(sky.title == EngineTitle::SkyrimSE && sky.gen == EngineGen::CE1_DX11, "SkyrimSE resolves to CE1_DX11");
    Expect(!sky.usesNativeDx12 && sky.offVmImpl == 0x210u && sky.renderContract.hdrDxgiFormat == 10u, "SkyrimSE ABI + RGBA16F contract");

    const auto fo4 = ResolveEngineProfileByExe("Fallout4.exe", 1, 10);
    Expect(fo4.title == EngineTitle::Fallout4 && fo4.gen == EngineGen::CE1_5_FO4_DX11, "Fallout4 resolves to CE1_5_FO4_DX11");
    Expect(!fo4.usesNativeDx12 && fo4.hasPrevisPrecombines && fo4.renderContract.supportsPbrRoughnessMetal && fo4.renderContract.hdrDxgiFormat == 26u, "Fallout4 PBR + PreVis + R11G11B10F contract");

    const auto sf = ResolveEngineProfileByExe("Starfield.exe", 1, 14);
    Expect(sf.title == EngineTitle::Starfield && sf.gen == EngineGen::CE2_Starfield_DX12, "Starfield resolves to CE2_Starfield_DX12");
    Expect(sf.usesNativeDx12 && sf.renderContract.usesNativeDx12Swapchain && sf.renderContract.depthConvention == DepthConvention::ReversedZ_1to0 && sf.renderContract.mvSign == MotionVectorSign::MinusUv_PrevEqCurMinusMv, "Starfield native DX12 Reversed-Z + minus-MV contract");

    const auto fo76 = ResolveEngineProfileByExe("Fallout76.exe");
    Expect(fo76.title == EngineTitle::Fallout76 && fo76.isMultiplayerOnline, "Fallout76 multiplayer safety flag set");

    const auto unk = ResolveEngineProfileByExe("notepad.exe");
    Expect(unk.title == EngineTitle::Unknown && unk.gen == EngineGen::Unknown, "Unknown exe resolves fail-closed");

    // 2. Valid Starfield CE2 Native DX12 + Agility SDK 1.722.1 + SM 6.10 contract
    NativeDx12HostDesc desc{};
    desc.engineProfile = sf;
    desc.agilitySdkVersion = 722;
    desc.hwProfile.shaderModel = 0x6A;  // SM 6.10
    NativeDx12FrameInputs frame{};
    frame.width = 2560;
    frame.height = 1440;
    frame.colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    frame.normalFormat = DXGI_FORMAT_R10G10B10A2_UNORM;

    const auto okRes = ValidateNativeDx12HostContract(desc, frame, true);
    Expect(okRes.ok && okRes.caps.supportsSm69LongVectors && okRes.caps.supportsSm610Preview && okRes.caps.supportsAgility722Preview && okRes.caps.usesReversedZ && okRes.caps.usesMinusMotionVectorSign, "Starfield CE2 Native DX12 + Agility 1.722.1 + SM 6.10 passes");

    // 3. Planted-mismatch controls (5/5 fail-closed!)
    NativeDx12HostDesc badSky = desc;
    badSky.engineProfile = sky;
    const auto r1 = ValidateNativeDx12HostContract(badSky, frame, true);
    Expect(!r1.ok && r1.error == NativeDx12ContractError::WrongEngineGenForNativeDx12, "Planted mismatch: SkyrimSE rejected by NativeDx12Host");

    NativeDx12HostDesc badFo4 = desc;
    badFo4.engineProfile = fo4;
    const auto r2 = ValidateNativeDx12HostContract(badFo4, frame, true);
    Expect(!r2.ok && r2.error == NativeDx12ContractError::WrongEngineGenForNativeDx12, "Planted mismatch: Fallout4 rejected by NativeDx12Host");

    NativeDx12HostDesc badFo76 = desc;
    badFo76.engineProfile = fo76;
    const auto r3 = ValidateNativeDx12HostContract(badFo76, frame, true);
    Expect(!r3.ok && r3.error == NativeDx12ContractError::MultiplayerGuardBlocked, "Planted mismatch: Fallout76 blocked by multiplayer guard");

    NativeDx12HostDesc badSm = desc;
    badSm.hwProfile.shaderModel = 0x65;
    const auto r4 = ValidateNativeDx12HostContract(badSm, frame, true);
    Expect(!r4.ok && r4.error == NativeDx12ContractError::ShaderModelTooLow, "Planted mismatch: SM 6.5 rejected below SM 6.6 floor");

    NativeDx12FrameInputs badFmt = frame;
    badFmt.normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    const auto r5 = ValidateNativeDx12HostContract(desc, badFmt, true);
    Expect(!r5.ok && r5.error == NativeDx12ContractError::NormalFormatMismatch, "Planted mismatch: wrong normal GBuffer format rejected");

    std::printf("wave72_profile_abi_check: failures=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
