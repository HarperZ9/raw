"""Writes docs/D3D11_TO_D3D12.md from the rows below and prints the counts.

Each row: (method, status, test, note). Status is T (translated), P (partial)
or M (missing). `test` names the suite check that exercises the method, or ""
when no test in the suite calls it.

Usage: python tools/dx12/coverage_table.py
"""
from __future__ import annotations

import pathlib

ALL = "all scenes"
STAGES = [("VS", "all scenes", "all scenes"), ("PS", "all scenes", "textured_instanced"),
          ("CS", "compute", "compute"), ("GS", "", ""), ("HS", "", ""), ("DS", "", "")]

DEVICE = [
    ("CreateBuffer", "P", ALL, "Constant buffers and dynamic vertex/index buffers are renamed upload chunks. Gaps: stream-output binding; a dynamic buffer that also has a view restarts its contents on Map NO_OVERWRITE."),
    ("CreateTexture1D", "T", "", ""),
    ("CreateTexture2D", "P", ALL, "Arrays, cubes, mips, MSAA, typeless and depth formats. Gaps: staging copies of packed depth-stencil formats carry the depth plane only; shared and GDI flags are ignored."),
    ("CreateTexture3D", "T", "volumes_cubes_arrays", ""),
    ("CreateShaderResourceView", "T", ALL, "Every dimension, buffers included. The stencil-plane view formats select plane 1; no test reads stencil through a view yet."),
    ("CreateUnorderedAccessView", "P", "compute, volumes_cubes_arrays", "Gap: hidden counters (append, consume, counter)."),
    ("CreateRenderTargetView", "P", ALL, "Gap: buffer render targets."),
    ("CreateDepthStencilView", "T", "depth_prepass_lit, gbuffer_frame", "Read-only depth and stencil flags map to DEPTH_READ per plane."),
    ("CreateInputLayout", "T", ALL, "D3D12 checks the layout against the shader when the pipeline state is built."),
    ("CreateVertexShader", "T", ALL, "DXBC runs as it is. Bindings come from the bytecode (RDEF, or the program declarations when RDEF is stripped)."),
    ("CreatePixelShader", "T", ALL, ""),
    ("CreateComputeShader", "T", "compute", ""),
    ("CreateGeometryShader", "T", "", ""),
    ("CreateHullShader", "T", "", "Patch topologies map to the patch pipeline type."),
    ("CreateDomainShader", "T", "", ""),
    ("CreateGeometryShaderWithStreamOutput", "M", "", "Stream output is not translated."),
    ("CreateClassLinkage", "M", "", "D3D12 has no dynamic shader linkage."),
    ("CreateBlendState", "T", "alpha_blend, gbuffer_frame", ""),
    ("CreateDepthStencilState", "T", "depth_prepass_lit, gbuffer_frame", ""),
    ("CreateRasterizerState", "T", "shadow_map, rtt_post", ""),
    ("CreateSamplerState", "T", "textured_instanced, shadow_map", ""),
    ("CreateQuery", "P", "occlusion_query", "Occlusion, occlusion predicate, timestamp, disjoint, event, pipeline statistics. Gap: stream-output statistics."),
    ("CreatePredicate", "P", "", "The object is created; SetPredication is missing."),
    ("CreateCounter", "M", "", "Answers DXGI_ERROR_UNSUPPORTED."),
    ("CreateDeferredContext", "T", "many_draws_deferred", "Each deferred context records its own D3D12 command list."),
    ("OpenSharedResource", "M", "", ""),
    ("CheckFormatSupport", "T", "", ""),
    ("CheckMultisampleQualityLevels", "T", "msaa_resolve", ""),
    ("CheckCounterInfo", "M", "", "Reports no counters."),
    ("CheckCounter", "M", "", ""),
    ("CheckFeatureSupport", "P", "", "Threading, D3D10_X hardware options and doubles; other features answer E_INVALIDARG."),
    ("GetPrivateData", "T", "", ""),
    ("SetPrivateData", "T", "", ""),
    ("SetPrivateDataInterface", "T", "", ""),
    ("GetFeatureLevel", "T", "", "Always 11_0."),
    ("GetCreationFlags", "P", "", "Always 0."),
    ("GetDeviceRemovedReason", "T", "", ""),
    ("GetImmediateContext", "T", ALL, "Called by raw::tx::CreateDevice."),
    ("SetExceptionMode", "P", "", "Accepted and ignored."),
    ("GetExceptionMode", "P", "", "Always 0."),
    ("QueryInterface (ID3D11Device1 and later, IDXGIDevice)", "M", "", "Only ID3D11Device answers. Swap chains come from raw::tx::CreateSwapChain."),
]

CONTEXT = []
for p, t_set, t_res in STAGES:
    CONTEXT += [
        (f"{p}SetShader", "T", t_set, "Class instances are ignored."),
        (f"{p}SetConstantBuffers", "T", t_set if p in ("VS", "CS") else ("depth_prepass_lit" if p == "PS" else ""), "Root constant buffer views; a renamed buffer is rebound by address at the next draw."),
        (f"{p}SetShaderResources", "P" if p == "CS" else "T", t_res if p != "VS" else "",
         "Gap: a resource bound as a compute UAV is not unbound here." if p == "CS" else "A resource bound as a render target or writable depth is bound as null, as D3D11 does."),
        (f"{p}SetSamplers", "T", t_res if p == "PS" else "", "Sampler tables are deduplicated in the 2,048-entry sampler heap."),
    ]
GETTERS = {"VSGetShader", "PSGetShader", "CSGetShader", "GSGetShader", "HSGetShader", "DSGetShader",
           "VSGetConstantBuffers", "PSGetConstantBuffers", "CSGetConstantBuffers", "PSGetSamplers",
           "PSGetShaderResources", "IAGetVertexBuffers", "IAGetIndexBuffer", "IAGetPrimitiveTopology",
           "OMGetRenderTargets", "OMGetBlendState", "OMGetDepthStencilState", "RSGetState",
           "RSGetViewports", "RSGetScissorRects", "GetType"}
for p, _, _ in STAGES:
    CONTEXT += [(f"{p}Get{w}", "T", "", "") for w in ("Shader", "ConstantBuffers", "ShaderResources", "Samplers")]
CONTEXT += [
    ("IASetInputLayout", "T", ALL, ""),
    ("IASetVertexBuffers", "T", ALL, ""),
    ("IASetIndexBuffer", "T", "textured_instanced, dynamic_vb", ""),
    ("IASetPrimitiveTopology", "T", ALL, "Strip cut values follow the index format."),
    ("OMSetRenderTargets", "T", ALL, "Up to eight targets; null slots use a null descriptor."),
    ("OMSetRenderTargetsAndUnorderedAccessViews", "P", "", "Gap: UAV initial counts."),
    ("OMSetBlendState", "T", "alpha_blend", "Blend factor and sample mask included."),
    ("OMSetDepthStencilState", "T", "depth_prepass_lit, gbuffer_frame", "Stencil reference included."),
    ("SOSetTargets", "M", "", ""),
    ("RSSetState", "T", "shadow_map", ""),
    ("RSSetViewports", "T", ALL, ""),
    ("RSSetScissorRects", "T", "rtt_post", "With the scissor test off, a rectangle covering any target is set."),
    ("CSSetUnorderedAccessViews", "P", "compute, volumes_cubes_arrays", "Gap: initial counts."),
    ("SetPredication", "M", "", ""),
    ("GetPredication", "M", "", "Always none."),
    ("IAGetInputLayout", "T", "", ""), ("IAGetVertexBuffers", "T", "", ""), ("IAGetIndexBuffer", "T", "", ""),
    ("IAGetPrimitiveTopology", "T", "", ""), ("OMGetRenderTargets", "T", "", ""),
    ("OMGetRenderTargetsAndUnorderedAccessViews", "T", "", ""), ("OMGetBlendState", "T", "", ""),
    ("OMGetDepthStencilState", "T", "", ""), ("SOGetTargets", "M", "", "Always none."),
    ("RSGetState", "T", "", ""), ("RSGetViewports", "T", "", ""), ("RSGetScissorRects", "T", "", ""),
    ("CSGetUnorderedAccessViews", "T", "", ""),
    ("Draw", "T", "triangle, dynamic_vb", ""),
    ("DrawIndexed", "T", "dynamic_vb", ""),
    ("DrawInstanced", "T", "", ""),
    ("DrawIndexedInstanced", "T", "textured_instanced", ""),
    ("DrawAuto", "M", "", "Stream output."),
    ("DrawInstancedIndirect", "T", "volumes_cubes_arrays", "ExecuteIndirect with a draw signature."),
    ("DrawIndexedInstancedIndirect", "T", "", ""),
    ("Dispatch", "T", "compute, volumes_cubes_arrays", "UAV barriers between dispatches."),
    ("DispatchIndirect", "T", "", ""),
    ("Map", "P", "many_draws, dynamic_vb, all scenes (readback)", "Discard and no-overwrite on renamed buffers, staging read and write, dynamic textures. Gaps: see CreateBuffer and CreateTexture2D."),
    ("Unmap", "P", "many_draws, dynamic_vb", "Same gaps as Map."),
    ("UpdateSubresource", "T", "many_draws, mip_generation", "Buffers, and textures with a box."),
    ("CopySubresourceRegion", "P", "", "Gap: from a GPU buffer into a constant or dynamic buffer."),
    ("CopyResource", "P", "all scenes (readback), gbuffer_frame", "Depth-stencil copies carry both planes. Gap: as CopySubresourceRegion."),
    ("CopyStructureCount", "M", "", ""),
    ("ClearRenderTargetView", "T", ALL, ""),
    ("ClearDepthStencilView", "T", "depth_prepass_lit", ""),
    ("ClearUnorderedAccessViewUint", "T", "", ""),
    ("ClearUnorderedAccessViewFloat", "T", "", ""),
    ("GenerateMips", "P", "mip_generation", "2D textures, arrays and cubes, drawn level by level. Gap: 1D and 3D textures."),
    ("SetResourceMinLOD", "M", "", "Ignored."),
    ("GetResourceMinLOD", "M", "", "Always 0."),
    ("ResolveSubresource", "T", "msaa_resolve", ""),
    ("Begin", "P", "occlusion_query", "Gap: a query that is open across a flush."),
    ("End", "P", "occlusion_query, many_draws", "Same gap as Begin."),
    ("GetData", "P", "occlusion_query, many_draws", "Results are read from a readback buffer once the submission has finished. Gap: stream-output statistics."),
    ("ExecuteCommandList", "P", "many_draws_deferred", "The recorded D3D12 list is submitted as it is. Gaps: a list that maps buffers can be executed once; a buffer the list binds without writing is read at its record-time contents."),
    ("FinishCommandList", "T", "many_draws_deferred", ""),
    ("ClearState", "T", ALL, ""),
    ("Flush", "T", ALL, ""),
    ("GetType", "T", "", ""),
    ("GetContextFlags", "T", "", ""),
]

SWAPCHAIN = [
    ("Present", "T", "swap_chain_present_resize", "The application's stable back buffer is copied or resolved into the flip-model buffer."),
    ("GetBuffer", "P", "swap_chain_present_resize", "Buffer 0 only, as D3D11 flip model allows."),
    ("GetDesc", "T", "", ""),
    ("ResizeBuffers", "T", "swap_chain_present_resize", ""),
    ("ResizeTarget", "T", "", "Forwarded."),
    ("SetFullscreenState", "P", "", "Accepted; the chain stays borderless."),
    ("GetFullscreenState", "P", "", "Always windowed."),
    ("GetContainingOutput", "T", "", "Forwarded."),
    ("GetFrameStatistics", "T", "", "Forwarded."),
    ("GetLastPresentCount", "T", "", "Forwarded."),
]

CHILDREN = [
    ("ID3D11DeviceChild: GetDevice, GetPrivateData, SetPrivateData, SetPrivateDataInterface", "T", "", "Every object."),
    ("ID3D11Resource: GetType, SetEvictionPriority, GetEvictionPriority", "T", ALL, "Eviction priority is ignored."),
    ("GetDesc on buffers, textures, views, state objects and queries", "T", "all scenes (readback)", ""),
    ("ID3D11View::GetResource", "T", "", ""),
    ("ID3D11Asynchronous::GetDataSize", "T", "", ""),
    ("ID3D11CommandList::GetContextFlags", "T", "", ""),
    ("ID3D11ClassInstance, ID3D11ClassLinkage, ID3D11Counter", "M", "", "No objects of these types exist."),
]

NAMES = {"T": "translated", "P": "partial", "M": "missing"}

HEAD = """# D3D11 to D3D12: what is translated

The translation layer in `src/dx12/translate/` implements `ID3D11Device` and
`ID3D11DeviceContext` on a Direct3D 12 device. A D3D11 application calls them like the system
runtime; every call becomes D3D12 work. This page lists every method of those interfaces with
its state and the suite check that exercises it.

Run the proof: build `d3d11_translate_suite` (`cmake -S . -B build -G Ninja
-DCMAKE_BUILD_TYPE=Release -DRAW_BUILD_PLUGIN=OFF` from a Visual Studio developer prompt), then
`build/tests/d3d11_translate_suite.exe --out out`. Add `--bench` for the draw-count timings.

- **translated**: the D3D11 behaviour is implemented.
- **partial**: implemented with the gap named in the note.
- **missing**: answers an error or is ignored; the device counts these calls
  (`raw::tx::GetStats().unsupportedCalls`), and the suite requires the count to stay 0.
- An empty test column means no check in the suite calls the method yet.

{summary}

## Proof

`d3d11_translate_suite` needs no game and no GPU: both back ends run on WARP, so the comparison
is deterministic. Results of 2026-11-18 (`evidence/d3d11-translate-suite-warp.json`): 38 of 38
checks pass.

- **Fourteen scenes**, each rendered on D3D11 and on the layer with the D3D12 debug layer on.
  Every image pair is identical (maximum channel difference 0), query results are equal, the
  debug layer reports no error, no unsupported call is made, and no device reference is left.
  The scenes: a triangle; textured instanced meshes; a depth prepass plus a lit pass; alpha
  blending; render-to-texture post-processing; shadow mapping; a compute pass; many draws with
  constant-buffer updates (immediate, and four deferred contexts on four threads); dynamic
  vertex buffers with map-discard; MSAA resolve; mip generation; occlusion queries; a
  G-buffer frame (four targets, stencil, read-only depth with a depth view, a depth copy,
  R11G11B10 and R10G10B10A2 targets); cube-map faces, a depth array, 3D volumes written by
  compute, a rendered mip pyramid and an indirect draw.
- **Controls that must fail**, each a defect planted in the layer:
  source and destination blend factors swapped (image differs by 127), a dropped render-target
  to shader-resource barrier (the debug layer reports 2 errors), a constant buffer that is not
  rebound after a rename (image differs by 203). All three are caught.
- **Bytecode readers**: for three shaders the RDEF reader, the program-declaration reader and
  the reader on reflection-stripped bytecode all agree with `D3DReflect`.
- **State getters**: 24 set-then-get checks give the same answers on D3D11 and on the layer.
- **Swap chain**: six presents and a resize on the D3D12 flip-model chain.
- **Call-stream replay**: each scene is recorded on D3D11 and replayed on a fresh D3D11 device
  (identical image) and on the layer (identical image). See `docs/D3D11_CALL_STREAM.md`.

## Timings: many draws

`d3d11_translate_suite --bench --frames 40 --threads 4`, WARP, Intel Core i7-13700KF (24 logical
processors), 2026-11-18, `evidence/d3d11-translate-bench-warp.json`. Each draw is preceded by a
constant-buffer update. Median over 36 frames after 4 warm-up frames. Wall time from the first
call until the last recording call returned with nothing submitted (calls), and until the GPU
finished (frame). Other jobs held the CPU at about 60 percent during the run.

| | 10,000 draws: calls | frame | 50,000 draws: calls | frame |
|---|---|---|---|---|
| D3D11, immediate context | 59.1 ms | 61.3 ms | 301.0 ms | 301.1 ms |
| D3D11, 4 deferred contexts | 0.48 ms | 58.7 ms | 0.88 ms | 271.1 ms |
| Translated D3D12, immediate context | 1.38 ms | 64.6 ms | 6.50 ms | 295.1 ms |
| Translated D3D12, 4 deferred contexts | 0.95 ms | 63.7 ms | 3.05 ms | 286.1 ms |

What these numbers say, and what they cannot say:

- WARP rasterises on the CPU, and it dominates every row: the frame takes about 6 microseconds
  a draw on all four paths. On WARP the translated frame is 5 to 9 percent slower than D3D11 at
  10,000 draws, and between 2 percent faster and 6 percent slower at 50,000.
- The D3D11 immediate context on WARP rasterises inside the draw calls, so its call time is the
  whole frame and is not a submission cost. The D3D12 paths record first and rasterise at
  `ExecuteCommandLists`.
- Recording through the layer costs 0.13 to 0.14 microseconds a draw on one thread and 0.06 to
  0.10 on four. The D3D11 runtime records its deferred lists faster (0.02 to 0.05), because it
  only stores the calls and replays them on the immediate context later; the layer's deferred
  lists are finished D3D12 lists.
- Four threads make recording 1.5 to 2.1 times faster than one, thread start included.
- A hardware GPU was not measured: the RTX 4090 was at 100 percent with other work. The CPU
  cost of a real driver, which is where D3D12 is expected to win, is therefore not in this table.

## How it works

| D3D11 | D3D12 |
|---|---|
| Constant buffers; dynamic vertex and index buffers | Chunks of upload-heap memory in size classes. `Map(WRITE_DISCARD)` and `UpdateSubresource` take a fresh chunk, so nothing waits for the GPU. The old chunk returns to the pool when the submission that used it has finished. |
| Other buffers and textures | Committed resources in the default heap, created in the common state. |
| Staging resources | Buffers in a CPU-visible custom heap; textures use the copyable-footprint layout. |
| No resource states | Each command list tracks the states it needs per subresource. At submission the device compares each list's first needs with the global state and runs the missing transitions in a small list just before it. Lists can therefore be recorded on any thread. |
| Shader bytecode (DXBC, Shader Model 4 and 5) | Passed to D3D12 unchanged. |
| Implicit binding slots | A root signature per shader combination, generated from what the bytecode declares: root constant buffer views, and one descriptor table each for resources, samplers and UAVs per stage. |
| Blend, depth-stencil and rasterizer state objects, input layout, topology, target formats | One pipeline state object per distinct combination, cached by a hash of the whole key. |
| Deferred contexts | Each records a real D3D12 command list on its own thread. `FinishCommandList` closes it; `ExecuteCommandList` submits it without recording anything again. |
| `GenerateMips` | A full-screen pass per level with a linear sampler. |
| Queries | Query heaps resolved into a readback buffer; `GetData` answers when the fence passes the submission. |
| Swap chain | A flip-model chain on the D3D12 queue. |

Written from the public Direct3D 11 and Direct3D 12 documentation and the DXBC container
layout. No source of another translation layer was read or copied.

## Known limits that are not one method

- Stream output, class linkage, counters, predication and shared resources are missing as a group.
- Feature level is fixed at 11_0, and only the base D3D11.0 interfaces answer `QueryInterface`.
- Pipeline state objects are compiled at first use on the calling thread; there is no disk cache yet.
- More than 26 constant buffers in one shader combination do not fit the root signature.
- A query that is open when the list is flushed loses the part before the flush.

## Next: the first real game frame

The layer has run test programs only. To replay a Skyrim frame through it:

1. Wire the recorder into the proxy as `docs/D3D11_CALL_STREAM.md` describes: wrap the real
   device with `raw::rec::Install` before `WrappedDevice` takes it, call `OnPresent` from
   `WrappedSwapChain::Present`, and arm a capture from a `[CallStream]` ini key.
2. Capture one gameplay frame, then replay it on a fresh D3D11 device first. That replay must
   reproduce the frame before the file says anything about the layer.
3. Replay the same file on the layer with the D3D12 debug layer on, and compare every buffer of
   `docs/FRAME_BUFFERS.md` with its D3D11 bytes, not only the back buffer.
4. Expect to meet what the suite does not cover yet: block-compressed textures, shaders with
   stripped reflection data at scale, stencil read through a view, depth-stencil contents
   restored into a replay, dynamic textures, 4K targets, and several thousand pipeline states
   compiled at first use.
5. Measure on an idle hardware GPU, with GPU-based validation on for one run.
6. Running the live game on the layer needs more than replay: the proxy must hand the game the
   translated device and swap chain from `D3D11CreateDeviceAndSwapChain`, the device must answer
   `IDXGIDevice`, and RAW's own D3D11 effects must move to the D3D12 device or stay behind the
   stage 1 bridge.
"""


def table(title: str, rows: list[tuple[str, str, str, str]]) -> str:
    out = [f"## {title}", "", "| Method | State | Test | Note |", "|---|---|---|---|"]
    out += [f"| `{m}` | {NAMES[s]} | {t} | {n} |" for m, s, t, n in rows]
    return "\n".join(out) + "\n"


def main() -> None:
    CONTEXT[:] = [(m, st, t or ("state_getters" if m in GETTERS else ""), n) for m, st, t, n in CONTEXT]
    groups = [("ID3D11Device", DEVICE), ("ID3D11DeviceContext", CONTEXT),
              ("IDXGISwapChain", SWAPCHAIN), ("Child objects", CHILDREN)]
    lines = ["| Interface | Methods | Translated | Partial | Missing | Translated or partial with a test |",
             "|---|---|---|---|---|---|"]
    total = {"n": 0, "T": 0, "P": 0, "M": 0, "tested": 0}
    for name, rows in groups:
        c = {k: sum(1 for r in rows if r[1] == k) for k in "TPM"}
        tested = sum(1 for r in rows if r[1] != "M" and r[2])
        lines.append(f"| {name} | {len(rows)} | {c['T']} | {c['P']} | {c['M']} | {tested} |")
        total["n"] += len(rows)
        total["tested"] += tested
        for k in "TPM":
            total[k] += c[k]
    lines.append(f"| **All** | **{total['n']}** | **{total['T']}** | **{total['P']}** | **{total['M']}** | **{total['tested']}** |")
    body = HEAD.format(summary="\n".join(lines)) + "\n" + "\n".join(table(n, r) for n, r in groups)
    path = pathlib.Path(__file__).resolve().parents[2] / "docs" / "D3D11_TO_D3D12.md"
    path.write_text(body, encoding="utf-8", newline="\n")
    print(f"{path.name}: {total['n']} rows, translated {total['T']}, partial {total['P']}, "
          f"missing {total['M']}, with a test {total['tested']}")
    assert len(DEVICE) == 41 and len(CONTEXT) == 108, (len(DEVICE), len(CONTEXT))


if __name__ == "__main__":
    main()
