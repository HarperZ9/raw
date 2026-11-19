// SKSE task and trampoline interfaces. Original work, MIT.
//
// Tasks: AddTask and AddUITask queue a delegate (vtable: Run, Dispose); the host runs and disposes each one at the
// next present, on the thread that presents. UI tasks run there too; the host has no Scaleform thread hook.
// Trampolines: the branch pool is executable memory within 2 GB of the game's image, so a 5-byte relative jump
// from game code reaches it; the local pool is the same near the requesting plugin's own image.
#include <deque>

#include "skse_host.h"

namespace raw::skse {
namespace {

std::mutex g_taskLock;
std::deque<Delegate*> g_tasks;
uint64_t g_tasksRun = 0;

int SafeTask(Delegate* d) {
    __try {
        d->vtbl->Run(d);
        d->vtbl->Dispose(d);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void AddTaskImpl(void* t) {
    Count(H().loading, "Task.AddTask");
    if (!t) return;
    std::lock_guard g(g_taskLock);
    g_tasks.push_back(static_cast<Delegate*>(t));
}

void AddUITaskImpl(void* t) {
    Count(H().loading, "Task.AddUITask");
    if (!t) return;
    std::lock_guard g(g_taskLock);
    g_tasks.push_back(static_cast<Delegate*>(t));
}

TaskInterface g_task{2, AddTaskImpl, AddUITaskImpl};

// Executable memory within reach (+-2 GB less a margin) of `near`, found by walking free regions.
void* AllocNear(uintptr_t nearAddr, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const size_t len = (size + gran - 1) / gran * gran;
    const uintptr_t span = 0x7FF00000ull;
    const uintptr_t lo = nearAddr > span ? nearAddr - span : gran;
    const uintptr_t hi = nearAddr + span;
    for (int dir = 0; dir < 2; ++dir) {
        uintptr_t a = (nearAddr / gran) * gran;
        while (dir == 0 ? a > lo : a < hi) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi)) break;
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (mbi.State == MEM_FREE) {
                const uintptr_t start = (base + gran - 1) / gran * gran;
                if (start + len <= base + mbi.RegionSize && start >= lo && start + len <= hi)
                    if (void* p = VirtualAlloc(reinterpret_cast<void*>(start), len, MEM_RESERVE | MEM_COMMIT,
                                               PAGE_EXECUTE_READWRITE))
                        return p;
            }
            if (dir == 0) {
                if (base <= gran) break;
                a = base - 1;
            } else {
                a = base + mbi.RegionSize;
            }
        }
    }
    return nullptr;
}

void* BranchImpl(PluginHandle h, size_t size) {
    Count(h, "Trampoline.AllocateFromBranchPool");
    void* p = AllocNear(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), size);
    Log("branch pool: %zu bytes for handle %u at %p", size, h, p);
    return p;
}

void* LocalImpl(PluginHandle h, size_t size) {
    Count(h, "Trampoline.AllocateFromLocalPool");
    Plugin* pl = ByHandle(h);
    const uintptr_t nearAddr = reinterpret_cast<uintptr_t>(pl && pl->module ? pl->module : GetModuleHandleW(nullptr));
    void* p = AllocNear(nearAddr, size);
    Log("local pool: %zu bytes for handle %u at %p", size, h, p);
    return p;
}

TrampolineInterface g_tramp{1, BranchImpl, LocalImpl};

}  // namespace

TaskInterface* Tasks() { return &g_task; }
TrampolineInterface* Trampolines() { return &g_tramp; }

void RunTasks() {
    std::deque<Delegate*> run;
    {
        std::lock_guard g(g_taskLock);
        run.swap(g_tasks);
    }
    for (Delegate* d : run) {
        if (SafeTask(d) < 0) Log("fault in a queued task %p", static_cast<void*>(d));
        if (++g_tasksRun == 1) Event("first queued task ran");
    }
}

}  // namespace raw::skse
