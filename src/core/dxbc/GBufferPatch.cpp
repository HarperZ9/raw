// GBufferPatch: see the header. Original work, MIT.
#include "GBufferPatchImpl.h"

#include <map>

namespace raw::dxbc
{
namespace
{
using Code = std::vector<Instruction>;
using T = std::vector<uint32_t>;

// The temporaries a patch adds, counted from the shader's own.
struct Regs
{
    uint32_t shadow[4];   // stand-ins for outputs 0 to 3
    uint32_t albedo;      // xyz base colour, w specular mask
    uint32_t spec;        // the gloss exponent, in the component the shader's own multiply used
    uint32_t water;       // xyz world-space normal
    uint32_t scratch;
    explicit Regs(uint32_t base)
        : shadow{base, base + 1, base + 2, base + 3}, albedo(base + 4), spec(base + 5), water(base + 6), scratch(base + 7) {}
    static constexpr uint32_t kCount = 8;
};

T Tmp(uint32_t reg, uint32_t mask) { return Dst(type::Temp, reg, mask); }
T Out(uint32_t reg, uint32_t mask) { return Dst(type::Output, reg, mask); }
T Rd(uint32_t reg, uint32_t swz = kXYZW) { return Src(type::Temp, reg, swz); }
T All(float v) { return Imm4(v, v, v, v); }
constexpr uint32_t X = 1, Y = 2, Z = 4, W = 8, XYZ = 7, XYZW = 15;
constexpr uint32_t kW = Swz(3, 3, 3, 3), kX = Swz(0, 0, 0, 0), kY = Swz(1, 1, 1, 1), kZ = Swz(2, 2, 2, 2);

uint32_t ComponentOf(uint32_t mask) { return mask & 1 ? 0 : mask & 2 ? 1 : mask & 4 ? 2 : 3; }

// Writes to the shader's own outputs go to the stand-in temporaries.
void ShadowOutputs(Code& code, size_t body, const Regs& r)
{
    for (size_t i = body; i < code.size(); ++i) {
        Instruction& in = code[i];
        for (const Operand& o : in.ops) {
            if (o.type != type::Output || o.dim != 1 || o.index[0] >= 4) continue;
            in.tok[o.at] &= ~(0xFFu << 12);                 // operand type 0: temporary
            in.tok[o.at + o.length - 1] = r.shadow[o.index[0]];
        }
        in.Decode();
    }
}

void MaterialOutputs(const Analysis& a, const Regs& r, const PatchOptions& opt, const T& alpha, Code* e)
{
    const bool lit = a.info.kind == ShaderKind::Lighting || a.info.kind == ShaderKind::DistantLod;
    const float cls = float(a.info.materialClass | opt.classFlags) / 255.0f;
    if (lit && a.info.vertexColour) {
        e->push_back(Make(op::Mul, {Tmp(r.scratch, XYZ), Rd(r.albedo), a.vcolOperand}));
        e->push_back(Make(op::Mov, {Out(kAlbedoTarget, XYZ), Rd(r.scratch)}));
    } else {
        e->push_back(Make(op::Mov, {Out(kAlbedoTarget, XYZ), lit ? Rd(r.albedo) : All(0)}));
    }
    e->push_back(Make(op::Mov, {Out(kAlbedoTarget, W), alpha}));
    e->push_back(Make(op::Mov, {Out(kSpecularTarget, X), lit ? Rd(r.albedo, kW) : All(0)}));
    if (a.info.gloss) {
        // log2(max(shininess, 1)) / 13, clamped to [0, 1].
        const uint32_t c = ComponentOf(a.glossMask);
        e->push_back(Make(op::Max, {Tmp(r.scratch, X), Rd(r.spec, Swz(c, c, c, c)), All(1)}));
        e->push_back(Make(op::Log, {Tmp(r.scratch, X), Rd(r.scratch, kX)}));
        e->push_back(Make(op::Mul, {Tmp(r.scratch, X), Rd(r.scratch, kX), All(1.0f / 13.0f)}, true));
        e->push_back(Make(op::Mov, {Out(kSpecularTarget, Y), Rd(r.scratch, kX)}));
    } else {
        e->push_back(Make(op::Mov, {Out(kSpecularTarget, Y), All(0)}));
    }
    e->push_back(Make(op::Mov, {Out(kSpecularTarget, Z), All(cls)}));
    e->push_back(Make(op::Mov, {Out(kSpecularTarget, W), alpha}));
    if (a.info.softLight) {
        e->push_back(Make(op::Mov, {Out(kEmissiveTarget, XYZ), Rd(r.water)}));   // the soft-lighting colour (taps)
    } else if (a.info.emissive && a.info.albedo) {
        e->push_back(Make(op::Mul, {Tmp(r.scratch, XYZ), a.emitA, a.emitB}));
        e->push_back(Make(op::Mul, {Tmp(r.scratch, XYZ), Rd(r.scratch), Rd(r.albedo)}));
        e->push_back(Make(op::Mov, {Out(kEmissiveTarget, XYZ), Rd(r.scratch)}));
    } else {
        e->push_back(Make(op::Mov, {Out(kEmissiveTarget, XYZ), All(0)}));
    }
    e->push_back(Make(op::Mov, {Out(kEmissiveTarget, W), alpha}));
}

void LayerOutput(const Analysis& a, const Regs& r, const T& alpha, Code* e)
{
    const uint32_t L = kLayerTarget, s = r.scratch;
    e->push_back(Make(op::Mov, {Out(L, X), a.posReg >= 0 ? Src(type::Input, uint32_t(a.posReg), kZ) : All(0)}));
    if (a.info.kind == ShaderKind::Water) {
        // World normal to view space with the view rotation in buffer 12, then the game's encoding:
        // n.xy / sqrt(8 - 8 n.z) + 0.5.
        for (uint32_t k = 0; k < 3; ++k)
            e->push_back(Make(op::Dp3, {Tmp(s, 1u << k), SrcCb(12, k, Swz(0, 1, 2, 0)), Rd(r.water, Swz(0, 1, 2, 0))}));
        e->push_back(Make(op::Mad, {Tmp(s, W), Rd(s, kZ), All(-8), All(8)}));
        e->push_back(Make(op::Sqrt, {Tmp(s, W), Rd(s, kW)}));
        e->push_back(Make(op::Max, {Tmp(s, W), Rd(s, kW), All(0.001f)}));
        e->push_back(Make(op::Div, {Tmp(s, X | Y), Rd(s, Swz(0, 1, 0, 0)), Rd(s, kW)}));
        e->push_back(Make(op::Add, {Tmp(s, X | Y), Rd(s, Swz(0, 1, 0, 0)), All(0.5f)}));
        e->push_back(Make(op::Mov, {Out(L, Y | Z), Rd(s, Swz(0, 0, 1, 1))}));
    } else if (a.info.normal) {
        e->push_back(Make(op::Mov, {Out(L, Y | Z), Rd(r.shadow[2], Swz(0, 0, 1, 1))}));   // output 2 holds the encoded normal
    } else {
        // No normal: g holds the draw's alpha and b is -1, a value no encoded normal takes.
        e->push_back(Make(op::Mov, {Out(L, Y), alpha}));
        e->push_back(Make(op::Mov, {Out(L, Z), All(-1)}));
    }
    if (a.outMask[0] & W) {
        e->push_back(Make(op::Ge, {Tmp(s, X), alpha, All(kLayerAlpha)}));
        e->push_back(Make(op::And, {Tmp(s, X), Rd(s, kX), ImmBits(0x3F800000u)}));
        e->push_back(Make(op::Mov, {Out(L, W), Rd(s, kX)}));
    } else {
        e->push_back(Make(op::Mov, {Out(L, W), All(1)}));
    }
}

Code Epilogue(const Analysis& a, const Regs& r, const PatchOptions& opt)
{
    Code e;
    for (uint32_t k = 0; k < 4; ++k)
        if (a.outMask[k]) e.push_back(Make(op::Mov, {Out(k, a.outMask[k]), Rd(r.shadow[k])}));
    const T alpha = (a.outMask[0] & W) ? Rd(r.shadow[0], kW) : All(1);
    MaterialOutputs(a, r, opt, alpha, &e);
    LayerOutput(a, r, alpha, &e);
    return e;
}

// The inserts, keyed by the index of the original instruction they go before.
std::multimap<size_t, Instruction> Taps(const Analysis& a, const Regs& r)
{
    std::multimap<size_t, Instruction> t;
    t.emplace(a.body, Make(op::Mov, {Tmp(r.albedo, XYZW), Imm4(0, 0, 0, 1)}));
    t.emplace(a.body, Make(op::Mov, {Tmp(r.spec, XYZW), All(0)}));
    t.emplace(a.body, Make(op::Mov, {Tmp(r.water, XYZW), Imm4(0, 0, 1, 0)}));
    if (a.albedoBefore != kNone)
        t.emplace(a.albedoBefore, Make(op::Mov, {Tmp(r.albedo, XYZ), Rd(a.albedoReg, a.albedoSwz)}));
    if (a.tintBefore != kNone)   // after the albedo copy when both land on one instruction (a multimap keeps insertion order)
        t.emplace(a.tintBefore, Make(op::Mul, {Tmp(r.albedo, XYZ), Rd(r.albedo), Rd(a.tintReg, Swz(0, 1, 2, 2))}));
    if (a.info.specularMask)
        t.emplace(a.specAfter + 1, Make(op::Mov, {Tmp(r.albedo, W), Rd(a.specReg, Swz(a.specComp, a.specComp, a.specComp, a.specComp))}));
    if (a.info.gloss)
        t.emplace(a.glossBefore, Make(op::Mov, {Tmp(r.spec, a.glossMask), a.glossOperand}));
    if (a.info.softLight)   // a lit shader has no water normal, so its register holds the soft-lighting colour
        t.emplace(a.softAfter + 1, Make(op::Mov, {Tmp(r.water, XYZ), Rd(a.softReg, a.softSwz)}));
    if (a.waterBefore != kNone)
        t.emplace(a.waterBefore, Make(op::Mov, {Tmp(r.water, XYZ), a.waterOperand}));
    return t;
}

void Declare(const Analysis& a, const Code& code, Code* out)
{
    for (uint32_t k = kAlbedoTarget; k <= kLayerTarget; ++k) {
        Instruction d;
        d.tok = {op::DclOutput | 3u << 24, Out(k, XYZW)[0], k};
        d.Decode();
        out->push_back(d);
    }
    if (a.info.kind == ShaderKind::Water && a.cb12At == kNone) {
        // The view rotation is in buffer 12, which the game keeps bound; this shader did not declare it.
        Instruction d = code[a.anyCbAt];
        d.tok[2] = 12; d.tok[3] = 3;
        d.Decode();
        out->push_back(d);
    }
    if (a.posReg >= 0 && a.posDeclAt == kNone) {
        Instruction d;   // dcl_input_ps_siv linear noperspective v.z, position
        d.tok = {op::DclInputPsSiv | 4u << 11 | 4u << 24, Dst(type::Input, uint32_t(a.posReg), Z)[0], uint32_t(a.posReg), 1u};
        d.Decode();
        out->push_back(d);
    }
}
} // namespace

PatchInfo Inspect(const void* bytecode, size_t size)
{
    Container c;
    Program p;
    Analysis a;
    if (!c.Parse(bytecode, size) || !c.Program() || !p.Parse(c.Program()->data)) { a.info.refused = "not a shader container"; return a.info; }
    Analyse(c, p, &a);
    return a.info;
}

bool PatchForGBuffer(const void* bytecode, size_t size, const PatchOptions& options, std::vector<uint8_t>* out, PatchInfo* info)
{
    Container c;
    Program p;
    Analysis a;
    auto done = [&](bool ok) { if (info) *info = a.info; return ok; };
    if (!c.Parse(bytecode, size) || !c.Program() || !p.Parse(c.Program()->data)) { a.info.refused = "not a shader container"; return done(false); }
    if (!Analyse(c, p, &a)) return done(false);

    std::vector<SignatureElement> outs, ins;
    if (!c.ReadSignature(FourCC('O', 'S', 'G', 'N'), &outs) || outs.empty()) { a.info.refused = "no plain output signature"; return done(false); }
    SignatureElement proto;
    for (const SignatureElement& e : outs) if (e.reg == 0 && e.semantic.size() >= 9) proto = e;
    if (proto.semantic.empty()) { a.info.refused = "output 0 is not in the signature"; return done(false); }
    for (uint32_t k = kAlbedoTarget; k <= kLayerTarget; ++k) {
        SignatureElement e = proto;
        e.index = k; e.reg = k; e.mask = 0xF; e.rwMask = 0;
        outs.push_back(e);
    }
    c.WriteSignature(FourCC('O', 'S', 'G', 'N'), outs);
    if (a.posReg >= 0 && c.ReadSignature(FourCC('I', 'S', 'G', 'N'), &ins)) {
        for (SignatureElement& e : ins) if (e.systemValue == 1) e.rwMask |= 4;
        c.WriteSignature(FourCC('I', 'S', 'G', 'N'), ins);
    }

    Code& code = p.Code();
    const Regs r(a.temps);
    code[a.tempsAt].tok[1] = a.temps + Regs::kCount;
    if (a.posDeclAt != kNone) code[a.posDeclAt].tok[1] |= Z << 4;      // the shader now reads SV_Position.z
    if (a.info.kind == ShaderKind::Water && a.cb12At != kNone && code[a.cb12At].tok[3] < 3) code[a.cb12At].tok[3] = 3;
    ShadowOutputs(code, a.body, r);
    const auto taps = Taps(a, r);
    const Code epilogue = Epilogue(a, r, options);
    Code next;
    next.reserve(code.size() + 64);
    for (size_t i = 0; i < code.size(); ++i) {
        if (i == a.body) Declare(a, code, &next);
        const auto range = taps.equal_range(i);
        for (auto it = range.first; it != range.second; ++it) next.push_back(it->second);
        if (code[i].opcode == op::Ret) next.insert(next.end(), epilogue.begin(), epilogue.end());
        next.push_back(code[i]);
    }
    code = std::move(next);
    c.Program()->data = p.Build();
    *out = c.Build();
    return done(true);
}

} // namespace raw::dxbc
