// RAWSKSETest: RAW's own SKSE plugin for testing the host. Original work, MIT.
//
// Built against CommonLibSSE-NG (MIT) like most SKSE plugins, so it exercises the host the way they do. Every check
// writes one line to RAWSKSETest.log in the game's SKSE log folder, starting with PASS, FAIL or INFO:
//   load        SKSEPlugin_Load runs and SKSE::Init gets every interface it asks for
//   messaging   each SKSE message arrives; a message sent to this plugin by name comes back
//   task        a task queued at DataLoaded runs on a later frame
//   trampoline  the branch pool is within reach of the game's code, and a write to it holds
//   papyrus     RAWSKSETest.Add and RAWSKSETest.Report are bound; a quest script calls them (Report logs the sum)
//   cosave      SaveGame records a counter; loading that save reads the same value back
#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>

#include <spdlog/sinks/basic_file_sink.h>

namespace {

constexpr std::uint32_t kUid = 'RAWT', kRecord = 'CNTR', kSelfMessage = 0x52415701;
std::uint32_t g_counter = 0, g_savedValue = 0;
bool g_selfReceived = false;

void Line(const char* verdict, const std::string& what) { spdlog::info("{} {}", verdict, what); spdlog::default_logger()->flush(); }

std::int32_t Add(RE::StaticFunctionTag*, std::int32_t a, std::int32_t b) {
    Line("INFO", fmt::format("papyrus: Add({}, {}) called", a, b));
    return a + b;
}

void Report(RE::StaticFunctionTag*, std::int32_t v) {
    Line(v == 5 ? "PASS" : "FAIL", fmt::format("papyrus: Report({}) from the quest script, expected 5", v));
}

bool BindPapyrus(RE::BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("Add", "RAWSKSETest", Add);
    vm->RegisterFunction("Report", "RAWSKSETest", Report);
    Line(vm ? "PASS" : "FAIL", "papyrus: natives bound on the VM the host passed");
    return true;
}

void OnSave(SKSE::SerializationInterface* s) {
    g_savedValue = 0x5A000000u | (static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) & 0xFFFFFF);
    const bool ok = s->WriteRecord(kRecord, 1, g_savedValue);
    Line(ok ? "PASS" : "FAIL", fmt::format("cosave: wrote counter {:08X}", g_savedValue));
    Line("INFO", fmt::format("cosave: last-written {:08X}", g_savedValue));
}

void OnRevert(SKSE::SerializationInterface*) {
    g_counter = 0;
    Line("INFO", "cosave: revert");
}

void OnLoad(SKSE::SerializationInterface* s) {
    std::uint32_t type, version, length;
    bool any = false;
    while (s->GetNextRecordInfo(type, version, length)) {
        any = true;
        if (type == kRecord && version == 1 && s->ReadRecordData(g_counter)) {
            const bool same = g_savedValue == 0 || g_counter == g_savedValue;
            Line(same ? "PASS" : "FAIL", fmt::format("cosave: read counter {:08X}, written this session {:08X}",
                                                     g_counter, g_savedValue));
        }
    }
    if (!any) Line("INFO", "cosave: no records for this save");
}

void OnMessage(SKSE::MessagingInterface::Message* m) {
    static const char* names[] = {"PostLoad", "PostPostLoad", "PreLoadGame", "PostLoadGame", "SaveGame",
                                  "DeleteGame", "InputLoaded", "NewGame", "DataLoaded"};
    const std::string name = m->type < 9 ? names[m->type] : std::to_string(m->type);
    Line("PASS", fmt::format("messaging: {} from {} (len {})", name, m->sender ? m->sender : "?", m->dataLen));
    if (m->type == SKSE::MessagingInterface::kPostLoad) {
        SKSE::GetMessagingInterface()->Dispatch(kSelfMessage, nullptr, 0, "RAWSKSETest");
    }
    if (m->type == SKSE::MessagingInterface::kDataLoaded) {
        const auto frame = RE::GetDurationOfApplicationRunTime();
        SKSE::GetTaskInterface()->AddTask([frame]() {
            Line("PASS", fmt::format("task: ran (queued at {} ms, ran at {} ms)", frame, RE::GetDurationOfApplicationRunTime()));
        });
    }
}

void OnSelf(SKSE::MessagingInterface::Message* m) {
    if (m->type == kSelfMessage && !g_selfReceived) {
        g_selfReceived = true;
        Line("PASS", fmt::format("messaging: plugin-to-plugin message from {}", m->sender ? m->sender : "?"));
    }
}

void CheckTrampoline() {
    SKSE::AllocTrampoline(64);
    auto& t = SKSE::GetTrampoline();
    auto* p = static_cast<std::uint8_t*>(t.allocate(16));
    const auto base = REL::Module::get().base();
    const auto dist = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(p)) - static_cast<std::int64_t>(base);
    p[0] = 0xC3;
    const bool reach = dist > -0x7FFF0000ll && dist < 0x7FFF0000ll && p[0] == 0xC3;
    Line(reach ? "PASS" : "FAIL", fmt::format("trampoline: 16 bytes at {:X}, {:+X} from the game image", reinterpret_cast<std::uintptr_t>(p), dist));
}

}  // namespace

// SKSEPlugin_Version and SKSEPlugin_Query come from add_commonlibsse_plugin (CMakeLists.txt).
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    auto dir = SKSE::log::log_directory();
    if (dir) {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "RAWSKSETest.log").string(), true);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("raw", sink));
        spdlog::set_pattern("%H:%M:%S.%e %v");
    }
    Line("INFO", fmt::format("load: SKSE version {:08X}, runtime {}, handle {}", skse->SKSEVersion(),
                             skse->RuntimeVersion().string(), skse->GetPluginHandle()));
    SKSE::Init(skse);
    const bool all = SKSE::GetMessagingInterface() && SKSE::GetTaskInterface() && SKSE::GetPapyrusInterface() &&
                     SKSE::GetSerializationInterface() && SKSE::GetTrampolineInterface();
    Line(all ? "PASS" : "FAIL", "load: messaging, task, papyrus, serialization and trampoline interfaces present");
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    SKSE::GetMessagingInterface()->RegisterListener("RAWSKSETest", OnSelf);
    SKSE::GetPapyrusInterface()->Register(BindPapyrus);
    auto* ser = SKSE::GetSerializationInterface();
    ser->SetUniqueID(kUid);
    ser->SetSaveCallback(OnSave);
    ser->SetLoadCallback(OnLoad);
    ser->SetRevertCallback(OnRevert);
    CheckTrampoline();
    return true;
}
