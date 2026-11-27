#pragma once
// CeTopologyRepair.h: Wave 85 Clean-Room NIF Geometry Topology Repair & Plain-English
// Corrupt-NIF Diagnostics.
// Implements spatial-hash vertex seam welding (preserving UV/hard-edge splits),
// degenerate & NaN-vertex triangle stripping, Gram-Schmidt / MikkTSpace tangent-frame
// orthonormalization (|N|=1, |T|=1, |dot(N,T)| <= 1e-5, handedness w = +/-1), and
// deterministic plain-English diagnostics naming exact corrupt mesh/vertex/index spans.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.

#include <cstdint>
#include <string>
#include <vector>

#include "../d3d12_bridge/RawAssetBridge.h"

namespace raw::ce
{

struct RepairedVertex
{
    float pos[3]     = {0.0f, 0.0f, 0.0f};
    float normal[3]  = {0.0f, 0.0f, 1.0f};
    float uv[2]      = {0.0f, 0.0f};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f}; // xyz unit tangent, w handedness (+1 or -1)
};
static_assert(sizeof(RepairedVertex) == 48u, "RepairedVertex must be 48 bytes");

struct TopologyDiagnostic
{
    std::string meshName;
    uint32_t    firstVertexIdx = 0u;
    uint32_t    firstIndexIdx  = 0u;
    std::string code;
    std::string message;
};

struct TopologyRepairConfig
{
    float posWeldEps                 = 1.0e-4f;
    float uvWeldEps                  = 1.0e-4f;
    float normalDotMin               = 0.996f;
    float minTriangleArea            = 1.0e-7f;
    bool  enableSeamWelding          = true;
    bool  enableDegenerateStrip      = true;
    bool  enableTangentOrthonormalize = true;
    bool  failClosedOnCorruptInput   = false;
};

struct TopologyRepairResult
{
    bool                            ok                          = true;
    std::vector<RepairedVertex>     vertices;
    std::vector<uint32_t>           indices;
    uint32_t                        inputVertexCount            = 0u;
    uint32_t                        weldedVertexCount           = 0u;
    uint32_t                        strippedNanTriangles        = 0u;
    uint32_t                        strippedOutOfRangeTriangles = 0u;
    uint32_t                        strippedDegenerateTriangles = 0u;
    uint32_t                        boundaryEdgeCount           = 0u;
    uint32_t                        nonManifoldEdgeCount        = 0u;
    float                           maxTangentOrthoError        = 0.0f;
    std::vector<TopologyDiagnostic> diagnostics;
};

// Repairs an indexed NIF / BSTriShape triangle mesh into watertight, seam-welded,
// degenerate-free geometry with orthonormal Gram-Schmidt / MikkTSpace tangent frames.
// Emits plain-English diagnostics for corrupt vertices, out-of-range indices, or
// degenerate faces instead of crashing silently.
TopologyRepairResult RepairNifMeshTopology(
    const std::string&                        meshName,
    const std::vector<raw::bridge::RawMeshVertex>& srcVerts,
    const std::vector<uint32_t>&              srcIndices,
    const TopologyRepairConfig&               cfg = {});

// Verifies that every vertex has finite unit normal (|N|==1), finite unit tangent (|T|==1),
// orthogonality |dot(N,T)| <= 1e-5f, and valid handedness sign w in {-1.0f, +1.0f}.
bool VerifyOrthonormalTangentFrames(
    const std::vector<RepairedVertex>& verts,
    float*                             outMaxOrthoErr = nullptr,
    float*                             outMaxUnitErr  = nullptr) noexcept;

} // namespace raw::ce
