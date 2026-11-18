#pragma once
// D3D11 call stream: opcodes, object kinds and constants shared by the
// recorder, the replayer and the dump tool. The byte layout of every record
// is documented in docs/D3D11_CALL_STREAM.md.
#include <cstdint>

namespace raw::rec
{
using u8  = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

inline constexpr char kMagic[8]         = {'R', 'A', 'W', 'D', '3', 'D', '1', '1'};
inline constexpr u32  kVersion          = 1;
inline constexpr u32  kHeaderFixedBytes = 40;
inline constexpr u32  kKeep             = 0xFFFFFFFFu;   // "keep current" count marker

enum Op : u32
{
    OP_FRAME_BEGIN = 1, OP_STATE_BEGIN = 2, OP_STATE_END = 3, OP_FRAME_END = 4,
    OP_SUMMARY = 5, OP_UNSUPPORTED = 6,

    OP_DEFINE_BUFFER = 16, OP_DEFINE_TEXTURE = 17, OP_DEFINE_VIEW = 18,
    OP_DEFINE_SHADER = 19, OP_DEFINE_INPUT_LAYOUT = 20, OP_DEFINE_STATE = 21,
    OP_DEFINE_QUERY = 22, OP_DEFINE_OPAQUE = 23,

    OP_SET_SHADER = 32, OP_SET_CONSTANT_BUFFERS = 33, OP_SET_SHADER_RESOURCES = 34,
    OP_SET_SAMPLERS = 35, OP_CS_SET_UAVS = 36, OP_IA_SET_INPUT_LAYOUT = 37,
    OP_IA_SET_VERTEX_BUFFERS = 38, OP_IA_SET_INDEX_BUFFER = 39, OP_IA_SET_TOPOLOGY = 40,
    OP_OM_SET_RENDER_TARGETS = 41, OP_OM_SET_RTS_AND_UAVS = 42, OP_OM_SET_BLEND_STATE = 43,
    OP_OM_SET_DEPTH_STENCIL_STATE = 44, OP_RS_SET_STATE = 45, OP_RS_SET_VIEWPORTS = 46,
    OP_RS_SET_SCISSORS = 47, OP_SO_SET_TARGETS = 48, OP_SET_PREDICATION = 49,
    OP_SET_RESOURCE_MIN_LOD = 50, OP_CLEAR_STATE = 51,

    OP_DRAW = 64, OP_DRAW_INDEXED = 65, OP_DRAW_INSTANCED = 66,
    OP_DRAW_INDEXED_INSTANCED = 67, OP_DRAW_AUTO = 68,
    OP_DRAW_INDEXED_INSTANCED_INDIRECT = 69, OP_DRAW_INSTANCED_INDIRECT = 70,
    OP_DISPATCH = 71, OP_DISPATCH_INDIRECT = 72,

    OP_CLEAR_RTV = 80, OP_CLEAR_DSV = 81, OP_CLEAR_UAV_UINT = 82, OP_CLEAR_UAV_FLOAT = 83,

    OP_MAP = 96, OP_UNMAP = 97, OP_UPDATE_SUBRESOURCE = 98, OP_COPY_RESOURCE = 99,
    OP_COPY_SUBRESOURCE_REGION = 100, OP_COPY_STRUCTURE_COUNT = 101,
    OP_RESOLVE_SUBRESOURCE = 102, OP_GENERATE_MIPS = 103,

    OP_BEGIN = 112, OP_END = 113, OP_GET_DATA = 114, OP_FLUSH = 115,
    OP_EXECUTE_COMMAND_LIST = 116, OP_FINISH_COMMAND_LIST = 117,

    OP_MAX = 128
};

// Object kinds. K_RESOURCE is only a lookup hint for the recorder ("ask the
// object which resource dimension it is"); it never appears in a file.
enum Kind : u32
{
    K_NONE = 0, K_RESOURCE = 1, K_BUFFER = 2, K_TEX1D = 3, K_TEX2D = 4, K_TEX3D = 5,
    K_SRV = 6, K_RTV = 7, K_DSV = 8, K_UAV = 9,
    K_VS = 10, K_HS = 11, K_DS = 12, K_GS = 13, K_PS = 14, K_CS = 15,
    K_INPUT_LAYOUT = 16, K_BLEND = 17, K_DEPTH_STENCIL = 18, K_RASTERIZER = 19,
    K_SAMPLER = 20, K_QUERY = 21, K_PREDICATE = 22, K_OPAQUE = 23, K_COUNT = 24
};

// Shader stage index used by the OP_SET_* records. K_VS + stage is the kind.
enum Stage : u32 { ST_VS = 0, ST_HS = 1, ST_DS = 2, ST_GS = 3, ST_PS = 4, ST_CS = 5, ST_COUNT = 6 };

// SUBDATA.state values and reason codes for contents that were not recorded.
enum : u32 { SUB_NONE = 0, SUB_DATA = 1, SUB_UNDEFINED = 2 };
enum : u32
{
    WHY_NONE = 0, WHY_MULTISAMPLED = 1, WHY_UNKNOWN_FORMAT = 2, WHY_STAGING_CREATE_FAILED = 3,
    WHY_MAP_FAILED = 4, WHY_NO_BYTECODE = 5, WHY_NO_LAYOUT = 6, WHY_NOT_MAPPED_IN_CAPTURE = 7,
    WHY_COUNT = 8
};

// Marker codes carried by OP_EXECUTE_COMMAND_LIST / OP_FINISH_COMMAND_LIST / OP_UNSUPPORTED.
enum : u32 { UNS_EXECUTE_COMMAND_LIST = 1, UNS_FINISH_COMMAND_LIST = 2 };

const char* OpName(u32 op);     // "?" for an unknown opcode
const char* OpSig(u32 op);      // leading fixed fields: u = u32, q = u64, f = f32
const char* KindName(u32 kind);
const char* ReasonName(u32 why);
} // namespace raw::rec
