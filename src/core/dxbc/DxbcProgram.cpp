// DxbcProgram: see the header. Original work, MIT.
#include "DxbcProgram.h"

#include <cstring>

namespace raw::dxbc
{
namespace
{
bool IsDeclaration(uint32_t opcode)
{
    return (opcode >= 88 && opcode <= 106) || (opcode >= 143 && opcode <= 162) || opcode == op::CustomData;
}

// Decodes the operand at tok[at]. Returns its length in tokens, 0 when it runs past the end.
size_t DecodeOperand(const std::vector<uint32_t>& tok, size_t at, Operand* out)
{
    if (at >= tok.size()) return 0;
    const uint32_t t = tok[at];
    Operand o;
    o.at = at;
    o.components = t & 3;
    o.mode = (t >> 2) & 3;
    o.sel = (t >> 4) & 0xFF;
    o.type = (t >> 12) & 0xFF;
    o.dim = (t >> 20) & 3;
    size_t p = at + 1;
    uint32_t ext = t;
    while (ext & 0x80000000u) {                 // extended operand tokens
        if (p >= tok.size()) return 0;
        ext = tok[p++];
    }
    if (o.type == type::Immediate32 || o.type == type::Immediate64) {
        const size_t words = (o.components == 2 ? 4 : 1) * (o.type == type::Immediate64 ? 2 : 1);
        p += words;
    } else {
        for (uint32_t i = 0; i < o.dim; ++i) {
            const uint32_t repr = (t >> (22 + 3 * i)) & 7;   // 0 imm32, 1 imm64, 2 relative, 3 imm32 + relative, 4 imm64 + relative
            if (repr == 0 || repr == 3) { if (p >= tok.size()) return 0; o.index[i] = tok[p]; p += 1; }
            if (repr == 1 || repr == 4) { if (p >= tok.size()) return 0; o.index[i] = tok[p]; p += 2; }
            if (repr >= 2) {
                Operand inner;
                const size_t n = DecodeOperand(tok, p, &inner);
                if (!n) return 0;
                p += n;
                o.relative[i] = true;
            }
        }
    }
    if (p > tok.size()) return 0;
    o.length = p - at;
    *out = o;
    return o.length;
}

uint32_t OperandHead(uint32_t type, uint32_t mode, uint32_t sel, uint32_t dim)
{
    return 2u | mode << 2 | sel << 4 | type << 12 | dim << 20;
}
} // namespace

void Instruction::Decode()
{
    ops.clear();
    opcode = tok[0] & 0x7FF;
    decl = IsDeclaration(opcode);
    if (decl) return;
    size_t p = 1;
    uint32_t ext = tok[0];
    while ((ext & 0x80000000u) && p < tok.size()) ext = tok[p++];
    while (p < tok.size()) {
        Operand o;
        const size_t n = DecodeOperand(tok, p, &o);
        if (!n) { ops.clear(); bad = true; return; }
        ops.push_back(o);
        p += n;
    }
}

std::vector<uint32_t> Instruction::OperandTokens(size_t i) const
{
    return std::vector<uint32_t>(tok.begin() + ops[i].at, tok.begin() + ops[i].at + ops[i].length);
}

bool Program::Parse(const std::vector<uint8_t>& chunk)
{
    m_code.clear();
    if (chunk.size() < 8 || chunk.size() % 4) return false;
    std::vector<uint32_t> t(chunk.size() / 4);
    memcpy(t.data(), chunk.data(), chunk.size());
    m_version = t[0];
    if (t[1] != t.size()) return false;
    size_t p = 2;
    while (p < t.size()) {
        const uint32_t opcode = t[p] & 0x7FF;
        size_t len = (t[p] >> 24) & 0x7F;
        if (opcode == op::CustomData) { if (p + 1 >= t.size()) return false; len = t[p + 1]; }
        if (len == 0 || p + len > t.size()) return false;
        Instruction in;
        in.tok.assign(t.begin() + p, t.begin() + p + len);
        in.Decode();
        m_code.push_back(std::move(in));
        p += len;
    }
    return true;
}

std::vector<uint8_t> Program::Build() const
{
    std::vector<uint32_t> t = {m_version, 0};
    for (const Instruction& in : m_code) t.insert(t.end(), in.tok.begin(), in.tok.end());
    t[1] = uint32_t(t.size());
    std::vector<uint8_t> out(t.size() * 4);
    memcpy(out.data(), t.data(), out.size());
    return out;
}

size_t Program::FirstBody() const
{
    for (size_t i = 0; i < m_code.size(); ++i)
        if (!m_code[i].decl) return i;
    return m_code.size();
}

std::vector<uint32_t> Dst(uint32_t type, uint32_t reg, uint32_t mask)
{
    return {OperandHead(type, 0, mask & 0xF, 1), reg};
}
std::vector<uint32_t> Src(uint32_t type, uint32_t reg, uint32_t swizzle)
{
    return {OperandHead(type, 1, swizzle & 0xFF, 1), reg};
}
std::vector<uint32_t> SrcCb(uint32_t slot, uint32_t element, uint32_t swizzle)
{
    return {OperandHead(type::ConstantBuffer, 1, swizzle & 0xFF, 2), slot, element};
}
std::vector<uint32_t> Imm4(float x, float y, float z, float w)
{
    std::vector<uint32_t> t(5);
    t[0] = OperandHead(type::Immediate32, 0, 0, 0);
    const float v[4] = {x, y, z, w};
    memcpy(&t[1], v, 16);
    return t;
}
std::vector<uint32_t> ImmBits(uint32_t bits)
{
    return {OperandHead(type::Immediate32, 0, 0, 0), bits, bits, bits, bits};
}

Instruction Make(uint32_t opcode, std::initializer_list<std::vector<uint32_t>> operands, bool saturate)
{
    Instruction in;
    in.tok.push_back(0);
    for (const auto& o : operands) in.tok.insert(in.tok.end(), o.begin(), o.end());
    in.tok[0] = opcode | (saturate ? 1u << 13 : 0u) | uint32_t(in.tok.size()) << 24;
    in.Decode();
    return in;
}

} // namespace raw::dxbc
