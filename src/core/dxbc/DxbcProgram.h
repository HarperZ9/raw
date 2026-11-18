#pragma once
// DxbcProgram: the token stream of a compiled shader as a list of
// instructions, with the operands of each one decoded far enough to find and
// rewrite registers, and helpers that encode new instructions.
//
// Token layout (the public tokenized program format of D3D10 and D3D11):
// an instruction starts with an opcode token (opcode in bits 0..10, length in
// tokens in bits 24..30, bit 31 for an extended token). An operand token holds
// the component count (bits 0..1), the selection mode (2..3), the components
// (4..11), the operand type (12..19) and the index dimension (20..21), then
// one representation field per index (22..30).
//
// Original work, MIT.
#include <cstdint>
#include <vector>

namespace raw::dxbc
{

namespace op
{
constexpr uint32_t Add = 0, And = 1, Break = 2, Breakc = 3, Call = 4, Callc = 5, Case = 6, Default = 10, Div = 14,
                   Dp3 = 16, Else = 18, Endif = 21, Endloop = 22, Endswitch = 23, Exp = 25, Ge = 29, If = 31,
                   Label = 44, Log = 47, Loop = 48, Mad = 50, Max = 52, CustomData = 53, Mov = 54, Mul = 56,
                   Ret = 62, Retc = 63, Sample = 69, SampleL = 72, SampleD = 73, SampleB = 74, Sqrt = 75,
                   Switch = 76, DclResource = 88, DclConstantBuffer = 89, DclInputPs = 98, DclInputPsSiv = 100,
                   DclOutput = 101, DclTemps = 104, DclGlobalFlags = 106;
}
namespace type
{
constexpr uint32_t Temp = 0, Input = 1, Output = 2, Immediate32 = 4, Immediate64 = 5, Sampler = 6, Resource = 7,
                   ConstantBuffer = 8;
}

struct Operand
{
    uint32_t type = 0;
    uint32_t components = 0;   // 0 none, 1 one, 2 four
    uint32_t mode = 0;         // 0 mask, 1 swizzle, 2 select one
    uint32_t sel = 0;          // bits 4..11 of the operand token
    uint32_t dim = 0;          // index dimension
    uint32_t index[3] = {};    // the immediate part of each index
    bool     relative[3] = {}; // the index also has a register part
    size_t   at = 0;           // token position inside the instruction
    size_t   length = 0;       // tokens, extensions and indices included

    uint32_t Mask() const { return mode == 0 ? (sel & 0xF) : 0; }
    uint32_t Swizzle(uint32_t i) const { return mode == 1 ? (sel >> (2 * i)) & 3 : (mode == 2 ? (sel & 3) : i); }
};

struct Instruction
{
    std::vector<uint32_t> tok;
    uint32_t opcode = 0;
    bool decl = false;               // a declaration or custom data: operands are not decoded
    bool bad = false;                // operands could not be decoded; a patch refuses the shader
    std::vector<Operand> ops;

    void Decode();                   // fills opcode, decl and ops from tok
    std::vector<uint32_t> OperandTokens(size_t i) const;
};

class Program
{
public:
    bool Parse(const std::vector<uint8_t>& chunk);
    std::vector<uint8_t> Build() const;

    bool IsPixelShader() const { return (m_version >> 16) == 0; }
    std::vector<Instruction>& Code() { return m_code; }
    const std::vector<Instruction>& Code() const { return m_code; }
    size_t FirstBody() const;        // index of the first instruction that is not a declaration

private:
    uint32_t m_version = 0;
    std::vector<Instruction> m_code;
};

// Operand encoders. A destination takes a write mask (bit 0 is x). A source
// takes four component picks, packed two bits each with x first.
constexpr uint32_t Swz(uint32_t x, uint32_t y, uint32_t z, uint32_t w) { return x | y << 2 | z << 4 | w << 6; }
constexpr uint32_t kXYZW = Swz(0, 1, 2, 3);

std::vector<uint32_t> Dst(uint32_t type, uint32_t reg, uint32_t mask);
std::vector<uint32_t> Src(uint32_t type, uint32_t reg, uint32_t swizzle);
std::vector<uint32_t> SrcCb(uint32_t slot, uint32_t element, uint32_t swizzle);
std::vector<uint32_t> Imm4(float x, float y, float z, float w);
std::vector<uint32_t> ImmBits(uint32_t bits);

// One instruction from an opcode and its operands, in order.
Instruction Make(uint32_t opcode, std::initializer_list<std::vector<uint32_t>> operands, bool saturate = false);

} // namespace raw::dxbc
