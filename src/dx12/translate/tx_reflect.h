// What a DXBC shader binds, read from the container without d3dcompiler.
// Two independent readers: the RDEF chunk (reflection data) and the SHEX/SHDR
// chunk (the declarations in the shader program itself). Shipped games often
// strip RDEF, so the program reader is the fallback; the tests check that the
// two agree and that both agree with D3DReflect.
#pragma once
#include "tx_objects.h"

namespace raw::tx {

enum class ReflectSource { Auto, Rdef, Program };

// False when the container or the requested chunk is missing or malformed.
bool ReflectDxbc(const void* code, size_t size, ReflectSource source, ShaderData& out);

}  // namespace raw::tx
