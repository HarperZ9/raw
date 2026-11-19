// SKSE serialization: the co-save, a file of plugin records written beside each save. Original work, MIT.
//
// The file is RAW's own format, <save name>.rawcosave in the game's save folder:
//   "RAWC" u32 format(1) u32 chunks, then per chunk: u32 uid, u32 records, then per record:
//   u32 type, u32 version, u32 length, <length bytes>
// SKSE's .skse co-save is not read or written: a plugin's data saved under SKSE does not carry over, and the
// reverse. Load order changes are not remapped yet: ResolveFormId and ResolveHandle return their input.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "skse_host.h"

namespace raw::skse {
namespace {

struct Record {
    uint32_t type, version;
    std::vector<uint8_t> data;
};
struct Callbacks {
    uint32_t uid = 0;
    SerializationCallback revert = nullptr, save = nullptr, load = nullptr, formDelete = nullptr;
};
std::map<PluginHandle, Callbacks> g_cb;
std::vector<Record>* g_writing = nullptr;   // the current plugin's records during a save
const std::vector<Record>* g_reading = nullptr;
size_t g_next = 0, g_pos = 0;               // next record to open; read position in the open one
long g_open = -1;

void SetUid(PluginHandle h, uint32_t uid) { Count(h, "Serialization.SetUniqueID"); g_cb[h].uid = uid; }
void SetRevert(PluginHandle h, void* cb) { Count(h, "Serialization.SetRevertCallback"); g_cb[h].revert = SerializationCallback(cb); }
void SetSave(PluginHandle h, void* cb) { Count(h, "Serialization.SetSaveCallback"); g_cb[h].save = SerializationCallback(cb); }
void SetLoad(PluginHandle h, void* cb) { Count(h, "Serialization.SetLoadCallback"); g_cb[h].load = SerializationCallback(cb); }
void SetFormDelete(PluginHandle h, void* cb) {
    Count(h, "Serialization.SetFormDeleteCallback");
    g_cb[h].formDelete = SerializationCallback(cb);
}

bool OpenRecordImpl(uint32_t type, uint32_t version) {
    Count(H().loading, "Serialization.OpenRecord");
    if (!g_writing) return false;
    g_writing->push_back({type, version, {}});
    return true;
}

bool WriteRecordDataImpl(const void* buf, uint32_t len) {
    Count(H().loading, "Serialization.WriteRecordData");
    if (!g_writing || g_writing->empty() || (!buf && len)) return false;
    auto& d = g_writing->back().data;
    d.insert(d.end(), static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
    return true;
}

bool WriteRecordImpl(uint32_t type, uint32_t version, const void* buf, uint32_t len) {
    Count(H().loading, "Serialization.WriteRecord");
    return OpenRecordImpl(type, version) && WriteRecordDataImpl(buf, len);
}

bool NextImpl(uint32_t* type, uint32_t* version, uint32_t* len) {
    Count(H().loading, "Serialization.GetNextRecordInfo");
    if (!g_reading || g_next >= g_reading->size()) { g_open = -1; return false; }
    const Record& r = (*g_reading)[g_next];
    g_open = long(g_next++);
    g_pos = 0;
    if (type) *type = r.type;
    if (version) *version = r.version;
    if (len) *len = uint32_t(r.data.size());
    return true;
}

uint32_t ReadImpl(void* buf, uint32_t len) {
    Count(H().loading, "Serialization.ReadRecordData");
    if (!g_reading || g_open < 0 || !buf) return 0;
    const auto& d = (*g_reading)[size_t(g_open)].data;
    const uint32_t n = uint32_t(std::min<size_t>(len, d.size() - g_pos));
    memcpy(buf, d.data() + g_pos, n);
    g_pos += n;
    return n;
}

bool ResolveHandleImpl(uint64_t h, uint64_t* out) {
    Count(H().loading, "Serialization.ResolveHandle");
    if (out) *out = h;
    return true;
}

bool ResolveFormIdImpl(uint32_t id, uint32_t* out) {
    Count(H().loading, "Serialization.ResolveFormId");
    if (out) *out = id;
    return true;
}

SerializationInterface g_ser{4, SetUid, SetRevert, SetSave, SetLoad, SetFormDelete, WriteRecordImpl, OpenRecordImpl,
                             WriteRecordDataImpl, NextImpl, ReadImpl, ResolveHandleImpl, ResolveFormIdImpl};

int SafeCb(SerializationCallback cb) {
    __try { cb(&g_ser); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Runs one plugin's callback with that plugin as the current handle.
void RunAs(PluginHandle h, SerializationCallback cb, const char* what) {
    const PluginHandle saved = H().loading;
    H().loading = h;
    if (SafeCb(cb) < 0) Log("fault in handle %u's %s callback", h, what);
    H().loading = saved;
}

std::string CosavePath(const char* saveName) {
    std::string n = saveName ? saveName : "";
    if (n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".ess") == 0) n.resize(n.size() - 4);
    if (n.find(':') != std::string::npos) return n + ".rawcosave";   // already a full path
    return SaveDirectory() + n + ".rawcosave";
}

void Put(std::vector<uint8_t>& o, uint32_t v) {
    const auto* b = reinterpret_cast<const uint8_t*>(&v);
    o.insert(o.end(), b, b + 4);
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::vector<uint8_t> in;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t b[65536];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) in.insert(in.end(), b, b + n);
        std::fclose(f);
    }
    return in;
}

// uid -> records; empty on a missing or malformed file (malformed tails are dropped, the rest kept)
std::map<uint32_t, std::vector<Record>> Parse(const std::vector<uint8_t>& in) {
    std::map<uint32_t, std::vector<Record>> out;
    size_t p = 4;
    auto get = [&](uint32_t& v) {
        if (p + 4 > in.size()) return false;
        memcpy(&v, &in[p], 4);
        p += 4;
        return true;
    };
    uint32_t fmt = 0, chunks = 0;
    if (in.size() < 12 || memcmp(in.data(), "RAWC", 4) != 0 || !get(fmt) || fmt != 1 || !get(chunks)) return out;
    for (uint32_t c = 0; c < chunks; ++c) {
        uint32_t uid = 0, n = 0;
        if (!get(uid) || !get(n)) return out;
        auto& recs = out[uid];
        for (uint32_t i = 0; i < n; ++i) {
            Record r{};
            uint32_t len = 0;
            if (!get(r.type) || !get(r.version) || !get(len) || p + len > in.size()) return out;
            r.data.assign(in.begin() + p, in.begin() + p + len);
            p += len;
            recs.push_back(std::move(r));
        }
    }
    return out;
}

}  // namespace

SerializationInterface* Serialization() { return &g_ser; }

void CosaveSave(const char* saveName) {
    std::vector<uint8_t> out = {'R', 'A', 'W', 'C'};
    Put(out, 1);
    std::vector<std::pair<uint32_t, std::vector<Record>>> chunks;
    for (auto& [h, c] : g_cb) {
        if (!c.save) continue;
        std::vector<Record> recs;
        g_writing = &recs;
        RunAs(h, c.save, "save");
        g_writing = nullptr;
        chunks.emplace_back(c.uid, std::move(recs));
    }
    Put(out, uint32_t(chunks.size()));
    size_t records = 0;
    for (auto& [uid, recs] : chunks) {
        Put(out, uid);
        Put(out, uint32_t(recs.size()));
        for (auto& r : recs) {
            Put(out, r.type);
            Put(out, r.version);
            Put(out, uint32_t(r.data.size()));
            out.insert(out.end(), r.data.begin(), r.data.end());
            ++records;
        }
    }
    const std::string path = CosavePath(saveName);
    FILE* f = std::fopen(path.c_str(), "wb");
    const bool ok = f && std::fwrite(out.data(), 1, out.size(), f) == out.size();
    if (f) std::fclose(f);
    Event("co-save written: %s, %zu chunk(s), %zu record(s), %zu bytes%s", path.c_str(), chunks.size(), records,
          out.size(), ok ? "" : " (write failed)");
}

void CosaveRevert() {
    for (auto& [h, c] : g_cb)
        if (c.revert) RunAs(h, c.revert, "revert");
}

void CosaveLoad(const char* saveName) {
    const std::string path = CosavePath(saveName);
    const auto in = ReadFile(path);
    const auto byUid = Parse(in);
    Event("co-save read: %s, %zu bytes, %zu chunk(s)", path.c_str(), in.size(), byUid.size());
    static const std::vector<Record> none;
    for (auto& [h, c] : g_cb) {
        if (!c.load) continue;
        auto it = byUid.find(c.uid);
        g_reading = it == byUid.end() ? &none : &it->second;
        g_next = 0;
        g_open = -1;
        g_pos = 0;
        RunAs(h, c.load, "load");
        g_reading = nullptr;
    }
}

void CosaveDelete(const char* saveName) {
    const std::string path = CosavePath(saveName);
    if (DeleteFileA(path.c_str())) Event("co-save deleted: %s", path.c_str());
}

}  // namespace raw::skse
