// RAWSKSEProbe: RAW's SKSE test plugin with no library under it. Original work, MIT.
//
// It speaks the plugin interface directly (src/skse_host/skse_abi.h), reads no Address Library and touches no game
// structure, so it runs on any build the host supports. One PASS, FAIL or INFO line per check goes to
// <game>/raw_skse_out/RAWSKSEProbe.log:
//   load        SKSEPlugin_Load runs; every interface the host offers is returned, with its version
//   messaging   each SKSE message arrives; a message sent to this plugin by name comes back
//   task        a task queued at PostPostLoad runs and is disposed
//   trampoline  the branch pool is within reach of the game's code, and a write to it holds
//   papyrus     the registration callback receives a VM whose vtable lies in the game's image
//   cosave      SaveGame records a counter; loading that save reads the same value back
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "skse_abi.h"

using namespace raw::skse;

namespace {

const SKSEInterface* g_skse = nullptr;
MessagingInterface* g_msg = nullptr;
SerializationInterface* g_ser = nullptr;
PluginHandle g_handle = kInvalidHandle;
uint32_t g_written = 0;
constexpr uint32_t kUid = 'RAWP', kRecord = 'CNTR', kSelf = 0x52415702;

void Line(const char* verdict, const char* fmt, ...) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *slash = 0;
    strcat_s(path, "\\raw_skse_out\\RAWSKSEProbe.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "%02u:%02u:%02u.%03u %s ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, verdict);
    va_list a;
    va_start(a, fmt);
    std::vfprintf(f, fmt, a);
    va_end(a);
    std::fputc('\n', f);
    std::fclose(f);
}

// A task: an object whose vtable holds Run then Dispose, as a TaskDelegate.
struct Task {
    const Delegate::VTable* vtbl;
    ULONGLONG queued;
    bool ran;
};
void TaskRun(Delegate* d) {
    auto* t = reinterpret_cast<Task*>(d);
    t->ran = true;
    Line("PASS", "task: ran %llu ms after it was queued", GetTickCount64() - t->queued);
}
void TaskDispose(Delegate* d) {
    auto* t = reinterpret_cast<Task*>(d);
    Line(t->ran ? "PASS" : "FAIL", "task: disposed%s", t->ran ? " after running" : " without running");
    delete t;
}
const Delegate::VTable kTaskVtbl{TaskRun, TaskDispose};

bool InImage(uintptr_t p) {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    return p >= base && p < base + nt->OptionalHeader.SizeOfImage;
}

bool OnVm(void* vm) {
    uintptr_t vtbl = 0;
    __try { vtbl = vm ? *reinterpret_cast<uintptr_t*>(vm) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { vtbl = 0; }
    Line(vm && InImage(vtbl) ? "PASS" : "FAIL", "papyrus: callback got VM %p, vtable %p %s", vm, (void*)vtbl,
         InImage(vtbl) ? "in the game image" : "outside the game image");
    return true;
}

void OnSave(SerializationInterface* s) {
    g_written = 0x50000000u | (GetTickCount() & 0xFFFFFF);
    Line(s->WriteRecord(kRecord, 1, &g_written, 4) ? "PASS" : "FAIL", "cosave: wrote counter %08X", g_written);
}

void OnRevert(SerializationInterface*) { Line("INFO", "cosave: revert"); }

void OnLoad(SerializationInterface* s) {
    uint32_t type = 0, version = 0, len = 0, value = 0;
    bool any = false;
    while (s->GetNextRecordInfo(&type, &version, &len)) {
        any = true;
        if (type == kRecord && version == 1 && len == 4 && s->ReadRecordData(&value, 4) == 4)
            Line(g_written == 0 || value == g_written ? "PASS" : "FAIL", "cosave: read counter %08X, written this session %08X",
                 value, g_written);
    }
    if (!any) Line("INFO", "cosave: no records for this save");
}

void OnMessage(Message* m) {
    static const char* names[] = {"PostLoad", "PostPostLoad", "PreLoadGame", "PostLoadGame", "SaveGame", "DeleteGame",
                                  "InputLoaded", "NewGame", "DataLoaded"};
    Line("PASS", "messaging: %s from %s (len %u)", m->type < kMsgTotal ? names[m->type] : "?", m->sender ? m->sender : "?",
         m->dataLen);
    if (m->type == kMsgPostLoad) g_msg->Dispatch(g_handle, kSelf, nullptr, 0, "RAWSKSEProbe");
    if (m->type == kMsgPostPostLoad) {
        if (auto* t = static_cast<TaskInterface*>(g_skse->QueryInterface(kIdTask)))
            t->AddTask(new Task{&kTaskVtbl, GetTickCount64(), false});
    }
}

void OnSelf(Message* m) {
    if (m->type == kSelf) Line("PASS", "messaging: plugin-to-plugin message from %s", m->sender ? m->sender : "?");
}

}  // namespace

extern "C" __declspec(dllexport) PluginVersionData SKSEPlugin_Version = [] {
    PluginVersionData v{};
    v.dataVersion = 1;
    v.pluginVersion = PackVersion(1, 0, 0);
    strcpy_s(v.pluginName, "RAWSKSEProbe");
    strcpy_s(v.author, "RAW");
    v.independenceEx = 1;   // no game structures
    v.compatibleVersions[0] = PackVersion(1, 6, 1170);
    v.compatibleVersions[1] = PackVersion(1, 7, 104);
    return v;
}();

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* skse) {
    g_skse = skse;
    g_handle = skse->GetPluginHandle();
    Line("INFO", "load: SKSE %08X, runtime %08X, handle %u", skse->skseVersion, skse->runtimeVersion, g_handle);
    g_msg = static_cast<MessagingInterface*>(skse->QueryInterface(kIdMessaging));
    g_ser = static_cast<SerializationInterface*>(skse->QueryInterface(kIdSerialization));
    auto* task = static_cast<TaskInterface*>(skse->QueryInterface(kIdTask));
    auto* tramp = static_cast<TrampolineInterface*>(skse->QueryInterface(kIdTrampoline));
    auto* pap = static_cast<PapyrusInterface*>(skse->QueryInterface(kIdPapyrus));
    Line(g_msg && g_ser && task && tramp && pap ? "PASS" : "FAIL",
         "load: messaging v%u, serialization v%u, task v%u, trampoline v%u, papyrus v%u", g_msg ? g_msg->interfaceVersion : 0,
         g_ser ? g_ser->version : 0, task ? task->interfaceVersion : 0, tramp ? tramp->interfaceVersion : 0,
         pap ? pap->interfaceVersion : 0);
    if (!g_msg || !g_ser || !tramp || !pap) return false;
    g_msg->RegisterListener(g_handle, "SKSE", reinterpret_cast<void*>(OnMessage));
    g_msg->RegisterListener(g_handle, "RAWSKSEProbe", reinterpret_cast<void*>(OnSelf));
    g_ser->SetUniqueID(g_handle, kUid);
    g_ser->SetSaveCallback(g_handle, reinterpret_cast<void*>(OnSave));
    g_ser->SetLoadCallback(g_handle, reinterpret_cast<void*>(OnLoad));
    g_ser->SetRevertCallback(g_handle, reinterpret_cast<void*>(OnRevert));
    pap->Register(reinterpret_cast<void*>(OnVm));
    auto* p = static_cast<uint8_t*>(tramp->AllocateFromBranchPool(g_handle, 64));
    const auto base = reinterpret_cast<intptr_t>(GetModuleHandleW(nullptr));
    const intptr_t dist = p ? reinterpret_cast<intptr_t>(p) - base : 0;
    if (p) p[0] = 0xC3;
    Line(p && dist > -0x7FFF0000ll && dist < 0x7FFF0000ll && p[0] == 0xC3 ? "PASS" : "FAIL",
         "trampoline: 64 bytes at %p, %+lld from the game image", p, (long long)dist);
    return true;
}
