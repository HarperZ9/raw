// Papyrus opcode quota (RAW engine runtime). Original work, MIT. See rt_pquota.h.
//
// The VM's opcode loop (fn:BSScript::Internal::CodeTasklet::OpcodeLoop) runs a stack for 100 opcodes, then saves its
// place and queues it for the next VM update. Engine control 8 measured the cost: about 200 opcodes per stack per frame,
// while the VM's CPU stayed near 0.06 ms a frame. The count is one compare, cmp r15d, 100 followed by a jb back to the
// loop head (site:Papyrus.OpcodeQuota). Its immediate is 8 bits, so a larger quota needs a stub: the 10 bytes of the
// compare and the branch become a jmp to code near the image that compares against RAW's own quota variable, then
// branches to the same loop head or back to the instruction after the branch. Flags, registers and the stack are as
// the original left them on both paths.
//
// The site's bytes are checked first (41 83 FF 64 0F 82 rel32); anything else is refused, so a version whose table
// value is wrong is never patched.
#include "rt_pquota.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rt_version.h"

namespace raw::rt {
namespace {

constexpr uint8_t kExpect[6] = {0x41, 0x83, 0xFF, 0x64, 0x0F, 0x82};   // cmp r15d, 0x64; jb rel32
uint8_t* g_stub = nullptr;      // the stub page: code, then the quota variable
volatile uint32_t* g_quota = nullptr;
uint32_t g_site = 0;
std::string g_state = "not installed";

uint8_t* PageNear(const Image& img) {
    const uint64_t lo = uint64_t(img.base) + img.sizeOfImage;
    for (uint64_t a = (lo + 0xFFFF) & ~0xFFFFull; a < lo + 0x7FF00000ull; a += 0x10000)
        if (auto* p = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(a), 0x1000, MEM_RESERVE | MEM_COMMIT,
                                                         PAGE_EXECUTE_READWRITE)))
            return p;
    return nullptr;
}

int32_t Rel(const uint8_t* from_end, const uint8_t* to) { return int32_t(int64_t(to) - int64_t(from_end)); }

std::string Install(const Image& img) {
    g_site = Addr("site:Papyrus.OpcodeQuota");
    if (!g_site) return "no site:Papyrus.OpcodeQuota in this version's table";
    auto* site = const_cast<uint8_t*>(img.base) + g_site;
    if (std::memcmp(site, kExpect, sizeof kExpect) != 0) return "refused: the site's bytes are not cmp r15d, 100; jb";
    int32_t jb;
    std::memcpy(&jb, site + 6, 4);
    const uint8_t* head = site + 10 + jb;     // the loop head the branch goes back to
    const uint8_t* after = site + 10;         // the instruction after the branch
    uint8_t* p = PageNear(img);
    if (!p) return "no page within 2 GB of the image";
    g_quota = reinterpret_cast<volatile uint32_t*>(p + 0x80);
    *g_quota = 100;
    uint8_t* c = p;
    c[0] = 0x44; c[1] = 0x3B; c[2] = 0x3D;    // cmp r15d, [rip+rel32]
    int32_t r = Rel(c + 7, p + 0x80);
    std::memcpy(c + 3, &r, 4);
    c += 7;
    c[0] = 0x0F; c[1] = 0x82;                 // jb head
    r = Rel(c + 6, head);
    std::memcpy(c + 2, &r, 4);
    c += 6;
    c[0] = 0xE9;                              // jmp after
    r = Rel(c + 5, after);
    std::memcpy(c + 1, &r, 4);
    FlushInstructionCache(GetCurrentProcess(), p, 0x80);
    uint8_t patch[10];
    patch[0] = 0xE9;
    r = Rel(site + 5, p);
    std::memcpy(patch + 1, &r, 4);
    std::memset(patch + 5, 0x90, 5);
    DWORD old;
    if (!VirtualProtect(site, 10, PAGE_EXECUTE_READWRITE, &old)) return "VirtualProtect failed";
    // Written at module start (PapyrusQuotaInstall), inside the game's first D3D11CreateDevice, before the VM runs any
    // script, so no thread can be between the compare and the branch while the bytes change.
    std::memcpy(site, patch, 10);
    VirtualProtect(site, 10, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 10);
    g_stub = p;
    return "installed";
}

}  // namespace

std::string PapyrusQuotaInstall(const Image& img, uint32_t quota) {
    if (!g_stub) g_state = Install(img);
    if (g_quota && quota >= 1) *g_quota = quota;
    return g_state;
}

std::string PapyrusQuotaCommand(const std::string& arg) {
    const uint32_t want = uint32_t(std::strtoul(arg.c_str(), nullptr, 10));
    if (g_quota && want >= 1) *g_quota = want;   // a 32-bit store; the loop reads it once per opcode
    char b[256];
    std::snprintf(b, sizeof b, "{\"cmd\":\"papyrus\",\"op\":\"quota\",\"state\":\"%s\",\"site\":\"0x%X\",\"quota\":%u}",
                  g_state.c_str(), g_site, g_quota ? *g_quota : 0u);
    return b;
}

}  // namespace raw::rt
