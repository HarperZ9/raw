// SKSE messaging: listeners, plugin-to-plugin dispatch, host messages and event dispatchers. Original work, MIT.
#include "skse_host.h"

namespace raw::skse {
namespace {

struct Listener {
    PluginHandle listener;
    std::string sender;       // empty: every sender
    MessageCallback cb;
};
std::vector<Listener> g_listeners;

// Event sources for GetEventDispatcher. A zeroed BSTEventSource (0x58 bytes: three empty arrays, an unlocked
// spin lock, not notifying) is a valid empty source; plugins add and remove sinks with the game's own allocator.
// The host never sends these events yet (docs/SKSE_HOSTING.md), so sinks are accepted and stay quiet.
alignas(16) uint8_t g_sources[kDispTotal][0x58] = {};

int SafeCall(MessageCallback cb, Message* m) {
    __try { cb(m); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

const char* MsgName(uint32_t t) {
    static const char* n[] = {"PostLoad", "PostPostLoad", "PreLoadGame", "PostLoadGame", "SaveGame", "DeleteGame",
                              "InputLoaded", "NewGame", "DataLoaded"};
    return t < kMsgTotal ? n[t] : "custom";
}

size_t Deliver(const std::string& senderName, uint32_t type, void* data, uint32_t len, const char* receiver) {
    std::vector<Listener> ls;
    {
        std::lock_guard g(H().lock);
        ls = g_listeners;
    }
    // The sender string stays valid for the session: "SKSE" or the name held by the sending plugin's record.
    const char* senderPtr = "SKSE";
    for (auto& p : H().plugins)
        if (senderName != "SKSE" && p.name == senderName) senderPtr = p.name.c_str();
    size_t n = 0;
    for (auto& l : ls) {
        Message m{senderPtr, type, len, data};
        if (!l.sender.empty() && _stricmp(l.sender.c_str(), senderName.c_str()) != 0) continue;
        Plugin* target = ByHandle(l.listener);
        if (receiver && (!target || _stricmp(target->name.c_str(), receiver) != 0)) continue;
        const PluginHandle saved = H().loading;
        H().loading = l.listener;
        if (SafeCall(l.cb, &m) < 0) Log("fault in %s's listener for %s message %u", target ? target->file.c_str() : "?",
                                         senderName.c_str(), type);
        H().loading = saved;
        ++n;
    }
    return n;
}

bool RegisterListenerImpl(PluginHandle h, const char* sender, void* cb) {
    Count(h, "Messaging.RegisterListener");
    if (!cb || !ByHandle(h)) return false;
    std::lock_guard g(H().lock);
    g_listeners.push_back({h, sender ? sender : "", reinterpret_cast<MessageCallback>(cb)});
    Log("listener: %s for sender %s", ByHandle(h)->file.c_str(), sender ? sender : "(all)");
    return true;
}

bool DispatchImpl(PluginHandle h, uint32_t type, void* data, uint32_t len, const char* receiver) {
    Count(h, "Messaging.Dispatch");
    Plugin* p = ByHandle(h);
    if (!p) return false;
    const size_t n = Deliver(p->name, type, data, len, receiver);
    Log("dispatch from %s type %u to %s: %zu listener(s)", p->name.c_str(), type, receiver ? receiver : "(all)", n);
    return true;
}

void* GetEventDispatcherImpl(uint32_t id) {
    Count(H().loading, "Messaging.GetEventDispatcher");
    return id < kDispTotal ? g_sources[id] : nullptr;
}

MessagingInterface g_msg{2, RegisterListenerImpl, DispatchImpl, GetEventDispatcherImpl};

}  // namespace

MessagingInterface* Messaging() { return &g_msg; }

void SendFromHost(uint32_t type, void* data, uint32_t len) {
    const size_t n = Deliver("SKSE", type, data, len, nullptr);
    Event("message %s sent to %zu listener(s)", MsgName(type), n);
    WriteStatus();
}

}  // namespace raw::skse
