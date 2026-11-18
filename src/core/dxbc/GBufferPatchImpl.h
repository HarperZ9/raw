#pragma once
// What the analysis of one pixel shader hands to the patch. Original work, MIT.
#include "DxbcContainer.h"
#include "DxbcProgram.h"
#include "GBufferPatch.h"

namespace raw::dxbc
{

constexpr size_t kNone = ~size_t(0);

struct Analysis
{
    PatchInfo info;
    uint32_t  cb[16] = {};          // declared size of each constant buffer slot, 0 when absent
    uint32_t  texDim[32] = {};      // resource dimension of each texture slot (3 is 2D, 6 is a cube)
    uint32_t  temps = 0;
    size_t    tempsAt = kNone;      // index of dcl_temps
    size_t    anyCbAt = kNone;      // index of some constant buffer declaration
    size_t    cb12At = kNone;       // index of the declaration of buffer 12
    uint8_t   outMask[8] = {};      // declared write mask of each colour output
    int       posReg = -1;          // the input register that carries SV_Position
    size_t    posDeclAt = kNone;    // index of its declaration, when the shader reads it
    size_t    body = 0;             // index of the first instruction

    // Taps: where an intermediate value is copied, as an index into the original code.
    size_t    albedoBefore = kNone; uint32_t albedoReg = 0, albedoSwz = 0;
    size_t    specAfter = kNone;    uint32_t specReg = 0, specComp = 0;
    size_t    glossBefore = kNone;  uint32_t glossMask = 0; std::vector<uint32_t> glossOperand;
    std::vector<uint32_t> emitA, emitB;
    std::vector<uint32_t> vcolOperand;   // the vertex colour input, as the shader reads it
    size_t    waterBefore = kNone;  std::vector<uint32_t> waterOperand;
};

// Fills the declarations, the kind, the material class and the taps. False
// with info.refused set when the shader cannot be patched.
bool Analyse(const Container& c, const Program& p, Analysis* a);

} // namespace raw::dxbc
