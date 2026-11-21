// GBufferAnalyse: reflection of one pixel shader. Reads the declarations,
// names the shader kind and finds where the values a patch exports are held.
// Every rule here was read from disassemblies of shaders the game compiled
// (docs/FRAME_BUFFERS.md, "Shader patches"). Original work, MIT.
#include "GBufferPatchImpl.h"

#include <algorithm>
#include <cstring>

namespace raw::dxbc
{
namespace
{
bool IsSample(uint32_t o) { return o == op::Sample || o == op::SampleL || o == op::SampleD || o == op::SampleB; }

void ReadDeclarations(const Program& p, Analysis* a)
{
    const auto& code = p.Code();
    a->body = p.FirstBody();
    for (size_t i = 0; i < a->body; ++i) {
        const auto& t = code[i].tok;
        switch (code[i].opcode) {
            case op::DclConstantBuffer:
                if (t.size() >= 4 && t[2] < 16) { a->cb[t[2]] = t[3]; a->anyCbAt = i; if (t[2] == 12) a->cb12At = i; }
                break;
            case op::DclResource:       if (t.size() >= 3 && t[2] < 32) a->texDim[t[2]] = (t[0] >> 11) & 0x1F; break;
            case op::DclTemps:          a->temps = t[1]; a->tempsAt = i; break;
            case op::DclOutput:         if (t.size() >= 3 && t[2] < 8) a->outMask[t[2]] = uint8_t((t[1] >> 4) & 0xF); break;
            case op::DclInputPsSiv:     if (t.size() >= 4 && t[3] == 1) { a->posReg = int(t[2]); a->posDeclAt = i; } break;
            default: break;
        }
    }
}

// Nesting depth of each instruction: 0 outside every if, loop and switch.
std::vector<int> Depths(const std::vector<Instruction>& code)
{
    std::vector<int> d(code.size(), 0);
    int depth = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        const uint32_t o = code[i].opcode;
        if (o == op::Endif || o == op::Endloop || o == op::Endswitch) --depth;
        d[i] = depth;
        if (o == op::If || o == op::Loop || o == op::Switch) ++depth;
    }
    return d;
}

bool ReadsBuffer(const Instruction& in, uint32_t slot)
{
    for (const Operand& o : in.ops)
        if (o.type == type::ConstantBuffer && o.index[0] == slot) return true;
    return false;
}
bool SamplesSlot(const Instruction& in, uint32_t slot)
{
    return IsSample(in.opcode) && in.ops.size() >= 3 && in.ops[2].type == type::Resource && in.ops[2].index[0] == slot;
}
// The destination component that receives texture channel ch, or -1.
int ChannelAt(const Instruction& sample, uint32_t ch)
{
    const uint32_t mask = sample.ops[0].Mask();
    for (uint32_t c = 0; c < 4; ++c)
        if ((mask >> c & 1) && sample.ops[2].Swizzle(c) == ch) return int(c);
    return -1;
}
size_t FirstSample(const std::vector<Instruction>& code, const std::vector<int>& depth, size_t from, uint32_t slot)
{
    for (size_t i = from; i < code.size(); ++i)
        if (SamplesSlot(code[i], slot) && depth[i] == 0 && code[i].ops[0].type == type::Temp) return i;
    return kNone;
}

// The base colour. The shader samples slot 0 into a register and keeps the
// colour there (blending more layers into it, tinting it) until lighting
// starts: the first read of the light constants (buffer 2) or of the shadow
// mask (slot 14). The tap goes before that instruction. If the register is
// reused for something else first, the tap goes right after the sample.
void FindAlbedo(const std::vector<Instruction>& code, const std::vector<int>& depth, bool lit, Analysis* a)
{
    const size_t s = FirstSample(code, depth, a->body, 0);
    if (s == kNone) return;
    const int r = ChannelAt(code[s], 0), g = ChannelAt(code[s], 1), b = ChannelAt(code[s], 2);
    if (r < 0 || g < 0 || b < 0) return;
    const uint32_t reg = code[s].ops[0].index[0];
    const uint32_t comps = 1u << r | 1u << g | 1u << b;
    size_t tap = s + 1, opener = kNone;
    bool inPlace = false;
    uint32_t outReg = reg, sw[3] = {uint32_t(r), uint32_t(g), uint32_t(b)};
    // A tint from the material (cb1) multiplied into another register before lighting. Only a fallback: the
    // skin shaders (ps 6a2383091a33f594 and 18d10e9ba7f68469 in the Riverwood capture) blend that product back
    // into the base register (b * b + 2 b t (1 - b)), so the base register at lighting already holds the tinted
    // colour, and the product alone reads about half as bright. When the base register is overwritten before
    // lighting, the tinted copy is the albedo, read right after the multiply.
    size_t tintAt = kNone;
    uint32_t tintReg = 0, tintSw[3] = {};
    for (size_t i = s + 1; lit && i < code.size(); ++i) {
        const bool lighting = ReadsBuffer(code[i], 2) || SamplesSlot(code[i], 14);
        if (tintAt == kNone && depth[i] == 0 && !lighting && code[i].opcode == op::Mul && code[i].ops.size() == 3 &&
            code[i].ops[0].type == type::Temp && code[i].ops[0].index[0] != reg && ReadsBuffer(code[i], 1)) {
            const uint32_t mask = code[i].ops[0].Mask();
            for (int k = 1; k <= 2 && tintAt == kNone; ++k) {
                const auto& o = code[i].ops[k];
                if (o.type != type::Temp || o.index[0] != reg) continue;
                uint32_t to[3] = {4, 4, 4};   // where each albedo channel lands (ps 18d10e9ba7f68469 writes r1.xyz to r2.yzw)
                for (uint32_t c = 0; c < 3; ++c)
                    for (uint32_t d = 0; d < 4; ++d)
                        if ((mask >> d & 1) && o.Swizzle(d) == sw[c] && to[c] == 4) to[c] = d;
                if (to[0] < 4 && to[1] < 4 && to[2] < 4) {
                    tintAt = i; tintReg = code[i].ops[0].index[0];
                    tintSw[0] = to[0]; tintSw[1] = to[1]; tintSw[2] = to[2];
                }
            }
        }
        if (depth[i] == 0) {
            if (lighting) { tap = i; inPlace = true; break; }
            const uint32_t o = code[i].opcode;
            if (o == op::If || o == op::Loop || o == op::Switch) opener = i;
        } else if (lighting && opener != kNone) { tap = opener; inPlace = true; break; }
        const Instruction& in = code[i];
        if (!in.ops.empty() && in.ops[0].type == type::Temp && in.ops[0].index[0] == reg && (in.ops[0].Mask() & comps)) {
            bool readsSelf = false;
            for (size_t k = 1; k < in.ops.size(); ++k)
                readsSelf |= in.ops[k].type == type::Temp && in.ops[k].index[0] == reg;
            if (!readsSelf) {   // the register now holds something else
                if (tintAt != kNone) {
                    tap = tintAt + 1; outReg = tintReg;
                    sw[0] = tintSw[0]; sw[1] = tintSw[1]; sw[2] = tintSw[2];
                }
                break;
            }
        }
    }
    a->albedoBefore = tap;
    a->albedoReg = outReg;
    a->albedoSwz = Swz(sw[0], sw[1], sw[2], sw[2]);
    a->info.albedo = true;
    a->info.albedoInPlace = inPlace;
}

// The hair's tint (ps a27b51950814d269 and bcbe8960ef3af1ff in the Riverwood capture): the shader builds
// t = lerp(1, tint, vertex colour) as add rT.xyz, cb1[k].xyz, -1 then mad rT.xyz, vN.c, rT.xyz, 1, after lighting has
// begun, and multiplies the lit base colour by it at the end. The base register the tap reads at lighting holds
// the grey hair texture alone, which read blonde. The tap multiplies the albedo by rT once rT is built.
float ImmAt(const Instruction& in, const Operand& o, uint32_t c)
{
    const uint32_t n = o.components == 2 ? 4u : 1u;
    float v = 0.0f;
    std::memcpy(&v, &in.tok[o.at + 1 + (c < n ? c : 0)], 4);
    return v;
}
bool ImmXyz(const Instruction& in, const Operand& o, float v)
{
    if (o.type != type::Immediate32 || o.length < 2) return false;
    for (uint32_t c = 0; c < 3; ++c) if (ImmAt(in, o, c) != v) return false;
    return true;
}
void FindHairTint(const std::vector<Instruction>& code, const std::vector<int>& depth, Analysis* a)
{
    if (a->albedoBefore == kNone) return;
    for (size_t i = a->body; i + 1 < code.size(); ++i) {
        const Instruction& add = code[i];
        const Instruction& mad = code[i + 1];
        if (add.opcode != op::Add || add.ops.size() != 3 || depth[i] != 0 || add.ops[0].type != type::Temp || (add.ops[0].Mask() & 7) != 7) continue;
        if (add.ops[1].type != type::ConstantBuffer || add.ops[1].index[0] != 1 || add.ops[1].relative[1] || !ImmXyz(add, add.ops[2], -1.0f)) continue;
        const uint32_t t = add.ops[0].index[0];
        if (mad.opcode != op::Mad || mad.ops.size() != 4 || depth[i + 1] != 0 || mad.ops[0].type != type::Temp || mad.ops[0].index[0] != t ||
            (mad.ops[0].Mask() & 7) != 7 || mad.ops[1].type != type::Input || mad.ops[2].type != type::Temp || mad.ops[2].index[0] != t ||
            !ImmXyz(mad, mad.ops[3], 1.0f)) continue;
        // Read where the albedo is already copied, with rT unchanged since it was built.
        size_t at = std::max(i + 2, a->albedoBefore);
        bool kept = true;
        for (size_t k = i + 2; k < at && kept; ++k)
            kept = code[k].ops.empty() || code[k].ops[0].type != type::Temp || code[k].ops[0].index[0] != t || !(code[k].ops[0].Mask() & 7);
        if (!kept || depth[at] != 0) return;
        a->tintBefore = at; a->tintReg = t;
        a->info.hairTint = true;
        return;
    }
}

// The soft-lighting map of a soft-lit shader (foliage): the colour sampled from slot 12, which the
// game's lit shader multiplies its wrap term by. RAW writes it to the emissive target (soft-lit
// shaders carry no glow), so the deferred lighting has the real per-pixel back light.
void FindSoftLight(const std::vector<Instruction>& code, const std::vector<int>& depth, Analysis* a)
{
    const size_t n = FirstSample(code, depth, a->body, 12);
    if (n == kNone) return;
    const int r = ChannelAt(code[n], 0), g = ChannelAt(code[n], 1), b = ChannelAt(code[n], 2);
    if (r < 0 || g < 0 || b < 0) return;
    a->softAfter = n; a->softReg = code[n].ops[0].index[0];
    a->softSwz = Swz(uint32_t(r), uint32_t(g), uint32_t(b), uint32_t(b));
    a->info.softLight = true;
}

// Environment-mapped shaders: the cube map's colour times the environment mask and scale. The game's shader
// samples the cube along the reflected view and multiplies the colour by the mask (the env mask map or the normal
// map's alpha, times buffer 1 and buffer 2 scales) in the next write to that register; the product then joins the
// lit sum times the received light (ps 955d1a31636cbbe4, the player's armour in the Bannered Mare, 2026-11-19).
// The tap copies that product; RAW multiplies it by its own received light (Lighting.hlsl, class 2).
void FindEnvMap(const std::vector<Instruction>& code, const std::vector<int>& depth, Analysis* a)
{
    uint32_t slot = 32;
    for (uint32_t s = 0; s < 32; ++s) if (a->texDim[s] == 6) { slot = s; break; }
    if (slot == 32) return;
    const size_t n = FirstSample(code, depth, a->body, slot);
    if (n == kNone) return;
    const int r = ChannelAt(code[n], 0), g = ChannelAt(code[n], 1), b = ChannelAt(code[n], 2);
    if (r < 0 || g < 0 || b < 0) return;
    const uint32_t reg = code[n].ops[0].index[0];
    for (size_t i = n + 1; i < code.size(); ++i) {
        const Instruction& in = code[i];
        if (in.ops.empty() || in.ops[0].type != type::Temp || in.ops[0].index[0] != reg) continue;
        // Where the multiply puts each channel: the destination component whose source swizzle reads the sample's
        // component for it (the product may move them: mul r0.xyz, r0.xxxx, r0.yzwy after a sample into r0.yzw).
        const int from[3] = {r, g, b};
        int at[3] = {-1, -1, -1};
        const uint32_t mask = in.ops[0].Mask();
        for (int ch = 0; ch < 3; ++ch)
            for (size_t k = 1; k < in.ops.size() && at[ch] < 0; ++k) {
                const Operand& o = in.ops[k];
                if (o.type != type::Temp || o.index[0] != reg) continue;
                for (uint32_t c = 0; c < 4 && at[ch] < 0; ++c)
                    if ((mask >> c & 1) && o.Swizzle(c) == uint32_t(from[ch])) at[ch] = int(c);
            }
        if ((in.opcode == op::Mul || in.opcode == op::Mad) && depth[i] == 0 && at[0] >= 0 && at[1] >= 0 && at[2] >= 0) {
            a->envAfter = i; a->envReg = reg; a->envSwz = Swz(uint32_t(at[0]), uint32_t(at[1]), uint32_t(at[2]), uint32_t(at[2]));
            a->info.envMap = true;
        }
        return;   // the register's next write: the mask's multiply, or something the tap does not follow
    }
}

// The character light: the lit shaders of actors and many objects add a white fill light the frame's own lights do
// not carry, two fixed directions dotted with the normal (scaled by buffer 1 element 14 x and y) times a factor read
// from slot 11 (times 14 z, clamped to 14 w), into the light sum before the base colour multiplies it (ps
// 9a4ea5fb8b14fb98, the player's armour in the Bannered Mare: mad r2.xyz, r2.xxxx, r2.yyyy, r5.xyzx). The tap copies
// the product; RAW adds it to its own light (Lighting.hlsl).
void FindCharLight(const std::vector<Instruction>& code, const std::vector<int>& depth, Analysis* a)
{
    const size_t n = FirstSample(code, depth, a->body, 11);
    if (n == kNone) return;
    const uint32_t m = code[n].ops[0].Mask();
    if (m == 0 || (m & (m - 1))) return;   // one component
    const uint32_t reg = code[n].ops[0].index[0], comp = m & 1 ? 0 : m & 2 ? 1 : m & 4 ? 2 : 3;
    for (size_t i = n + 1; i < code.size() && i < n + 12; ++i) {
        const Instruction& in = code[i];
        if (in.opcode != op::Mad || in.ops.size() != 4 || depth[i] != 0 || in.ops[0].Mask() != 7) continue;
        bool reads = false;
        for (size_t k = 1; k <= 2; ++k) {
            const Operand& o = in.ops[k];
            if (o.type != type::Temp || o.index[0] != reg) continue;
            bool all = true;
            for (uint32_t c = 0; c < 4; ++c) all &= o.Swizzle(c) == comp;
            reads |= all;
        }
        if (!reads) continue;
        a->charBefore = i; a->charA = in.OperandTokens(1); a->charB = in.OperandTokens(2);
        a->info.charLight = true;
        return;
    }
}

void FindSpecular(const std::vector<Instruction>& code, const std::vector<int>& depth, Analysis* a)
{
    // The specular mask: alpha of the normal map in slot 1.
    const size_t n = FirstSample(code, depth, a->body, 1);
    const int alpha = n == kNone ? -1 : ChannelAt(code[n], 3);
    if (alpha >= 0) {
        a->specAfter = n; a->specReg = code[n].ops[0].index[0]; a->specComp = uint32_t(alpha);
        a->info.specularMask = true;
    }
    // Gloss: the shader raises a dot product to the shininess with log, mul, exp.
    for (size_t i = a->body; i + 2 < code.size(); ++i) {
        if (code[i].opcode != op::Log || code[i + 1].opcode != op::Mul || code[i + 2].opcode != op::Exp) continue;
        const Instruction& mul = code[i + 1];
        if (mul.ops.size() != 3 || code[i].ops.empty() || code[i].ops[0].type != type::Temp) continue;
        const uint32_t logReg = code[i].ops[0].index[0];
        const bool first = mul.ops[1].type == type::Temp && mul.ops[1].index[0] == logReg;
        const bool second = mul.ops[2].type == type::Temp && mul.ops[2].index[0] == logReg;
        if (first == second) continue;
        const uint32_t mask = mul.ops[0].Mask();
        if (mask == 0 || (mask & (mask - 1))) continue;  // one component
        a->glossBefore = i + 1; a->glossMask = mask;
        a->glossOperand = mul.OperandTokens(first ? 2 : 1);
        a->info.gloss = true;
        break;
    }
    // Emissive: colour * scale from one element of buffer 1 joins the lit sum (as a mad into the
    // sum, or as a product added to it) before the base colour multiplies it.
    for (size_t i = a->body; i < code.size(); ++i) {
        const Instruction& in = code[i];
        const bool mad = in.opcode == op::Mad && in.ops.size() == 4, mul = in.opcode == op::Mul && in.ops.size() == 3;
        if ((!mad && !mul) || depth[i] != 0 || in.ops[0].Mask() != 7) continue;
        const Operand &x = in.ops[1], &y = in.ops[2];
        if (x.type != type::ConstantBuffer || y.type != type::ConstantBuffer || x.index[0] != 1 || y.index[0] != 1 ||
            x.index[1] != y.index[1] || x.relative[1] || y.relative[1]) continue;
        a->emitA = in.OperandTokens(1); a->emitB = in.OperandTokens(2);
        a->info.emissive = true;
        break;
    }
}

// The world-space normal of a water surface. Two forms, both read from the game's water shaders:
// a saturated dot product of the normal with one fixed direction; or the wave normal built as
// amplitude * sample + (0, 0, 1) with the amplitude in buffer 1, then normalised with rsq and mul.
void FindWaterNormal(const std::vector<Instruction>& code, Analysis* a)
{
    static const float kDir[3] = {-0.099f, -0.099f, 0.99f}, kUp[3] = {0.0f, 0.0f, 1.0f};
    for (size_t i = a->body; i < code.size(); ++i) {
        const Instruction& in = code[i];
        if (in.opcode != op::Dp3 || !(in.tok[0] & (1u << 13)) || in.ops.size() != 3) continue;
        for (size_t k = 1; k < 3; ++k) {
            const Operand& o = in.ops[k];
            if (o.type != type::Immediate32 || o.components != 2) continue;
            if (memcmp(&in.tok[o.at + 1], kDir, sizeof kDir) != 0) continue;
            a->waterBefore = i;
            a->waterOperand = in.OperandTokens(3 - k);
            return;
        }
    }
    for (size_t i = a->body; i < code.size(); ++i) {
        const Instruction& in = code[i];
        if (in.opcode != op::Mad || in.ops.size() != 4 || in.ops[0].type != type::Temp || in.ops[0].Mask() != 7) continue;
        const Operand &amp = in.ops[1], &up = in.ops[3];
        if (amp.type != type::ConstantBuffer || amp.index[0] != 1 || up.type != type::Immediate32 || up.components != 2) continue;
        if (memcmp(&in.tok[up.at + 1], kUp, sizeof kUp) != 0) continue;
        const uint32_t reg = in.ops[0].index[0];
        for (size_t j = i + 2; j < code.size(); ++j) {
            const Instruction& mul = code[j];
            if (mul.opcode != op::Mul || code[j - 1].opcode != op::Rsq || mul.ops.size() != 3 || mul.ops[0].type != type::Temp) continue;
            const bool reads = (mul.ops[1].type == type::Temp && mul.ops[1].index[0] == reg) ||
                               (mul.ops[2].type == type::Temp && mul.ops[2].index[0] == reg);
            uint32_t c[4] = {}, n = 0;
            for (uint32_t k = 0; k < 4; ++k) if (mul.ops[0].Mask() >> k & 1) c[n++] = k;
            if (!reads || n != 3) continue;
            a->waterBefore = j + 1;
            a->waterOperand = Src(type::Temp, mul.ops[0].index[0], Swz(c[0], c[1], c[2], c[2]));
            return;
        }
    }
}

// The vertex colour, a four-component interpolated input. Two signatures, both read from the
// game's lit shaders: the output alpha is the alpha times its w (mul o0.w, x, vK.w; 31 of the 50
// lit shaders of the Riverwood frame), or the lit sum times the base colour is multiplied by it
// right after (mul rA.xyz, rB.xyz, vK.xyz). Inputs declared with fewer components (view and light
// vectors) and inputs read by dp4 (the projected positions behind the motion vectors) are not it.
void FindVertexColour(const std::vector<Instruction>& code, const std::vector<int>& depth, Analysis* a)
{
    if (a->albedoBefore == kNone) return;
    uint32_t declMask[32] = {};
    bool dp4[32] = {};
    for (size_t i = 0; i < code.size(); ++i) {
        const Instruction& in = code[i];
        if (in.opcode == op::DclInputPs && in.tok.size() >= 3 && in.tok[2] < 32) declMask[in.tok[2]] = (in.tok[1] >> 4) & 0xF;
        if (in.opcode == op::Dp4)
            for (const Operand& o : in.ops) if (o.type == type::Input && o.index[0] < 32) dp4[o.index[0]] = true;
    }
    auto colour = [&](const Operand& o) {
        return o.type == type::Input && !o.relative[0] && o.index[0] < 32 && int(o.index[0]) != a->posReg &&
               declMask[o.index[0]] == 0xF && !dp4[o.index[0]];
    };
    int reg = -1;
    for (size_t i = a->body; i < code.size() && reg < 0; ++i) {
        const Instruction& in = code[i];
        if (in.opcode != op::Mul || in.ops.size() != 3 || in.ops[0].type != type::Output || in.ops[0].index[0] != 0 || in.ops[0].Mask() != 8) continue;
        for (size_t k = 1; k < 3; ++k)
            if (colour(in.ops[k]) && in.ops[k].Swizzle(0) == 3) reg = int(in.ops[k].index[0]);
    }
    for (size_t i = a->albedoBefore; i < code.size() && reg < 0; ++i) {
        const Instruction& in = code[i];
        if (in.opcode != op::Mul || in.ops.size() != 3 || depth[i] != 0 || in.ops[0].type != type::Temp || in.ops[0].Mask() != 7) continue;
        for (size_t k = 1; k < 3; ++k)
            if (colour(in.ops[k]) && in.ops[k].Swizzle(0) == 0 && in.ops[k].Swizzle(1) == 1 && in.ops[k].Swizzle(2) == 2) reg = int(in.ops[k].index[0]);
    }
    if (reg < 0) return;
    a->vcolOperand = Src(type::Input, uint32_t(reg), Swz(0, 1, 2, 2));
    a->info.vertexColour = true;
}

uint8_t LightingClass(const Analysis& a)
{
    bool sixLayers = a.cb[1] >= 13;
    for (int s = 0; s < 6; ++s) sixLayers &= a.texDim[s] == 3;
    if (sixLayers) return material::Landscape;
    if (a.texDim[3] == 6) return material::Eye;   // Eye shader binds corneal cubemap at t3 (MaterialPipeline.cpp)
    for (int s = 0; s < 32; ++s) if (a.texDim[s] == 6) return material::EnvironmentMapped;
    if (a.texDim[7] == 3 && !a.texDim[3] && !a.texDim[4] && !a.texDim[12]) return material::Hair;   // t7 specular shift -> Hair
    // Skin: the face's and the body's lit shaders read two more 2D maps at t3 and t4 (in a Riverwood capture,
    // ps c49dc9fe5ca9dd4c draws the heads, ps 563259c5efe0cd0c the bodies). Before the soft-lit test: the body
    // shader's back light reads t12 too. Parallax reads t3 alone.
    if (a.texDim[3] == 3 && a.texDim[4] == 3) return material::Skin;
    // The hands' and bodies' shader without the tint maps (ps 6a2383091a33f594 in the same capture) samples t2
    // into one channel and t12 exactly as the body shader does, with t11, and has no t3: skin too.
    if (a.texDim[2] == 3 && a.texDim[11] == 3 && a.texDim[12] == 3 && !a.texDim[3]) return material::Skin;
    if (a.texDim[6]) return material::GlowMapped;
    if (a.texDim[12]) return material::SoftLit;
    if (a.texDim[3] || a.texDim[8]) return material::Parallax;
    return material::Lit;
}
} // namespace

bool Analyse(const Container& c, const Program& p, Analysis* a)
{
    const auto& code = p.Code();
    auto refuse = [&](const char* why) { a->info.refused = why; a->info.kind = ShaderKind::Unknown; return false; };
    if (!p.IsPixelShader()) return refuse("not a pixel shader");
    ReadDeclarations(p, a);
    if (a->tempsAt == kNone) return refuse("no temporaries declared");
    if (!a->outMask[0]) return refuse("no colour output 0");
    for (int k = 4; k < 8; ++k) if (a->outMask[k]) return refuse("already writes outputs 4 to 7");
    for (size_t i = a->body; i < code.size(); ++i) {
        const uint32_t o = code[i].opcode;
        if (code[i].bad) return refuse("an instruction could not be decoded");
        if (o == op::Retc || o == op::Call || o == op::Callc || o == op::Label) return refuse("conditional return or call");
    }
    std::vector<SignatureElement> in;
    if (c.ReadSignature(FourCC('I', 'S', 'G', 'N'), &in))
        for (const SignatureElement& e : in)
            if (e.systemValue == 1) a->posReg = int(e.reg);
    a->info.depth = a->posReg >= 0;

    const std::vector<int> depth = Depths(code);
    const bool threeOut = a->outMask[1] && (a->outMask[2] & 3) == 3;
    FindWaterNormal(code, a);
    if (a->cb[2] >= 30 && a->cb[1] >= 9 && threeOut && a->texDim[0] == 3) {
        a->info.kind = ShaderKind::Lighting;
        a->info.materialClass = LightingClass(*a);
        FindAlbedo(code, depth, true, a);
        FindVertexColour(code, depth, a);
        FindHairTint(code, depth, a);
        // A landscape samples colour layers from slots 0 to 5; slot 1 is not a normal map there.
        if (a->info.materialClass != material::Landscape) FindSpecular(code, depth, a);
        else { const size_t keep = a->specAfter; FindSpecular(code, depth, a); a->specAfter = keep; a->info.specularMask = false; }
        if (a->info.materialClass == material::SoftLit) FindSoftLight(code, depth, a);
        if (a->info.materialClass == material::EnvironmentMapped) FindEnvMap(code, depth, a);
        if (a->info.materialClass != material::Landscape) FindCharLight(code, depth, a);
        a->info.normal = true;
    } else if (a->waterBefore != kNone && a->anyCbAt != kNone) {
        a->info.kind = ShaderKind::Water;
        a->info.materialClass = material::Water;
        a->info.normal = true;
    } else if (a->cb[1] == 3 && a->cb[2] == 9) {
        a->info.kind = ShaderKind::Effect;
        a->info.materialClass = material::Effect;
    } else if (a->cb[12] && !a->cb[1] && threeOut && a->texDim[0] == 3) {
        a->info.kind = ShaderKind::DistantLod;
        a->info.materialClass = material::DistantLod;
        FindAlbedo(code, depth, false, a);
        a->info.normal = true;
    } else {
        a->info.kind = ShaderKind::Generic;
    }
    if (a->info.kind != ShaderKind::Water) a->waterBefore = kNone;
    return true;
}

} // namespace raw::dxbc
