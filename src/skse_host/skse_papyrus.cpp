// SKSE papyrus and scaleform interfaces. Original work, MIT.
//
// Papyrus: Register keeps the plugin's callback and calls it with the game's BSScript::IVirtualMachine as soon as
// the VM exists: at once when the plugin registers after the VM was made, otherwise at the first present that
// finds it. The callback binds its native functions through the VM's own interface, so a native bound here
// behaves as one bound under SKSE. SKSE's own Papyrus natives (its script extensions) are not provided.
// Scaleform: no Scaleform hook yet. Register returns false so the plugin can see it; nothing is called.
#include "skse_host.h"

namespace raw::skse {
namespace {

using VmCallback = bool (*)(void* vm);
struct Pending {
    PluginHandle h;
    VmCallback cb;
};
std::vector<Pending> g_pending;
std::mutex g_lock;
size_t g_ran = 0;

int SafeVm(VmCallback cb, void* vm) {
    __try { return cb(vm) ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void Run(const Pending& p, void* vm) {
    const PluginHandle saved = H().loading;
    H().loading = p.h;
    const int r = SafeVm(p.cb, vm);
    H().loading = saved;
    Plugin* pl = ByHandle(p.h);
    Event("papyrus registration for %s: %s", pl ? pl->file.c_str() : "?",
          r == 1 ? "true" : r == 0 ? "returned false" : "fault");
    ++g_ran;
}

bool RegisterImpl(void* cb) {
    const PluginHandle h = H().loading;
    Count(h, "Papyrus.Register");
    if (!cb) return false;
    Pending p{h, reinterpret_cast<VmCallback>(cb)};
    if (void* vm = VirtualMachine()) {
        Run(p, vm);
        return true;
    }
    std::lock_guard g(g_lock);
    g_pending.push_back(p);
    Log("papyrus registration queued until the VM exists (handle %u)", h);
    return true;
}

PapyrusInterface g_pap{1, RegisterImpl};

bool ScaleformRegisterImpl(const char* name, void*) {
    Count(H().loading, "Scaleform.Register");
    Log("scaleform registration '%s' refused: no Scaleform hook in the host", name ? name : "");
    return false;
}

void ScaleformInventoryImpl(void*) { Count(H().loading, "Scaleform.RegisterForInventory"); }

ScaleformInterface g_sf{1, ScaleformRegisterImpl, ScaleformInventoryImpl};

}  // namespace

PapyrusInterface* Papyrus() { return &g_pap; }
ScaleformInterface* Scaleform() { return &g_sf; }

void PapyrusPoll() {
    std::vector<Pending> run;
    {
        std::lock_guard g(g_lock);
        if (g_pending.empty()) return;
        void* vm = VirtualMachine();
        if (!vm) return;
        run.swap(g_pending);
    }
    for (auto& p : run) Run(p, VirtualMachine());
}

}  // namespace raw::skse
