#include "rec_format.h"

namespace raw::rec
{
namespace
{
struct OpInfo { u32 op; const char* name; const char* sig; };

const OpInfo kOps[] = {
    {OP_FRAME_BEGIN, "FRAME_BEGIN", ""}, {OP_STATE_BEGIN, "STATE_BEGIN", ""},
    {OP_STATE_END, "STATE_END", ""}, {OP_FRAME_END, "FRAME_END", ""},
    {OP_SUMMARY, "SUMMARY", "qqq"}, {OP_UNSUPPORTED, "UNSUPPORTED", "u"},
    {OP_DEFINE_BUFFER, "DEFINE_BUFFER", "quuuuuu"},
    {OP_DEFINE_TEXTURE, "DEFINE_TEXTURE", "quuuuuuu"},
    {OP_DEFINE_VIEW, "DEFINE_VIEW", "quuq"}, {OP_DEFINE_SHADER, "DEFINE_SHADER", "quuq"},
    {OP_DEFINE_INPUT_LAYOUT, "DEFINE_INPUT_LAYOUT", "quu"},
    {OP_DEFINE_STATE, "DEFINE_STATE", "quu"}, {OP_DEFINE_QUERY, "DEFINE_QUERY", "quuu"},
    {OP_DEFINE_OPAQUE, "DEFINE_OPAQUE", "qu"},
    {OP_SET_SHADER, "SET_SHADER", "uuq"},
    {OP_SET_CONSTANT_BUFFERS, "SET_CONSTANT_BUFFERS", "uuu"},
    {OP_SET_SHADER_RESOURCES, "SET_SHADER_RESOURCES", "uuu"},
    {OP_SET_SAMPLERS, "SET_SAMPLERS", "uuu"}, {OP_CS_SET_UAVS, "CS_SET_UAVS", "uu"},
    {OP_IA_SET_INPUT_LAYOUT, "IA_SET_INPUT_LAYOUT", "q"},
    {OP_IA_SET_VERTEX_BUFFERS, "IA_SET_VERTEX_BUFFERS", "uu"},
    {OP_IA_SET_INDEX_BUFFER, "IA_SET_INDEX_BUFFER", "quu"},
    {OP_IA_SET_TOPOLOGY, "IA_SET_TOPOLOGY", "u"},
    {OP_OM_SET_RENDER_TARGETS, "OM_SET_RENDER_TARGETS", "u"},
    {OP_OM_SET_RTS_AND_UAVS, "OM_SET_RTS_AND_UAVS", "u"},
    {OP_OM_SET_BLEND_STATE, "OM_SET_BLEND_STATE", "qffffu"},
    {OP_OM_SET_DEPTH_STENCIL_STATE, "OM_SET_DEPTH_STENCIL_STATE", "qu"},
    {OP_RS_SET_STATE, "RS_SET_STATE", "q"}, {OP_RS_SET_VIEWPORTS, "RS_SET_VIEWPORTS", "u"},
    {OP_RS_SET_SCISSORS, "RS_SET_SCISSORS", "u"}, {OP_SO_SET_TARGETS, "SO_SET_TARGETS", "u"},
    {OP_SET_PREDICATION, "SET_PREDICATION", "qu"},
    {OP_SET_RESOURCE_MIN_LOD, "SET_RESOURCE_MIN_LOD", "qf"},
    {OP_CLEAR_STATE, "CLEAR_STATE", ""},
    {OP_DRAW, "DRAW", "uu"}, {OP_DRAW_INDEXED, "DRAW_INDEXED", "uuu"},
    {OP_DRAW_INSTANCED, "DRAW_INSTANCED", "uuuu"},
    {OP_DRAW_INDEXED_INSTANCED, "DRAW_INDEXED_INSTANCED", "uuuuu"},
    {OP_DRAW_AUTO, "DRAW_AUTO", ""},
    {OP_DRAW_INDEXED_INSTANCED_INDIRECT, "DRAW_INDEXED_INSTANCED_INDIRECT", "qu"},
    {OP_DRAW_INSTANCED_INDIRECT, "DRAW_INSTANCED_INDIRECT", "qu"},
    {OP_DISPATCH, "DISPATCH", "uuu"}, {OP_DISPATCH_INDIRECT, "DISPATCH_INDIRECT", "qu"},
    {OP_CLEAR_RTV, "CLEAR_RTV", "qffff"}, {OP_CLEAR_DSV, "CLEAR_DSV", "qufu"},
    {OP_CLEAR_UAV_UINT, "CLEAR_UAV_UINT", "quuuu"},
    {OP_CLEAR_UAV_FLOAT, "CLEAR_UAV_FLOAT", "qffff"},
    {OP_MAP, "MAP", "quuu"}, {OP_UNMAP, "UNMAP", "quu"},
    {OP_UPDATE_SUBRESOURCE, "UPDATE_SUBRESOURCE", "quu"},
    {OP_COPY_RESOURCE, "COPY_RESOURCE", "qq"},
    {OP_COPY_SUBRESOURCE_REGION, "COPY_SUBRESOURCE_REGION", "quuuuquu"},
    {OP_COPY_STRUCTURE_COUNT, "COPY_STRUCTURE_COUNT", "quuq"},
    {OP_RESOLVE_SUBRESOURCE, "RESOLVE_SUBRESOURCE", "quuquu"},
    {OP_GENERATE_MIPS, "GENERATE_MIPS", "q"},
    {OP_BEGIN, "BEGIN", "q"}, {OP_END, "END", "q"}, {OP_GET_DATA, "GET_DATA", "quu"},
    {OP_FLUSH, "FLUSH", ""}, {OP_EXECUTE_COMMAND_LIST, "EXECUTE_COMMAND_LIST", "u"},
    {OP_FINISH_COMMAND_LIST, "FINISH_COMMAND_LIST", "u"},
};

const OpInfo* Find(u32 op)
{
    for (const OpInfo& i : kOps)
        if (i.op == op) return &i;
    return nullptr;
}
} // namespace

const char* OpName(u32 op) { const OpInfo* i = Find(op); return i ? i->name : "?"; }
const char* OpSig(u32 op)  { const OpInfo* i = Find(op); return i ? i->sig : ""; }

const char* KindName(u32 kind)
{
    static const char* const names[K_COUNT] = {
        "none", "resource", "buffer", "texture1d", "texture2d", "texture3d", "srv", "rtv",
        "dsv", "uav", "vs", "hs", "ds", "gs", "ps", "cs", "input_layout", "blend_state",
        "depth_stencil_state", "rasterizer_state", "sampler", "query", "predicate", "opaque"};
    return kind < K_COUNT ? names[kind] : "?";
}

const char* ReasonName(u32 why)
{
    static const char* const names[WHY_COUNT] = {
        "none", "multisampled", "unknown_format", "staging_create_failed", "map_failed",
        "no_bytecode_attached", "no_layout_attached", "not_mapped_in_capture"};
    return why < WHY_COUNT ? names[why] : "?";
}
} // namespace raw::rec
