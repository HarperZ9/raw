# D3D11 call stream: record one frame, replay it on any device

The recorder captures one frame of an application's Direct3D 11 calls into a single file. The
file holds the pipeline state at the start of the frame, every call the frame made on the
immediate context, and the contents of every buffer and texture the frame touched. The replayer
reads that file and issues the same frame on any `ID3D11Device`: the system runtime, the WARP
software rasteriser, or a D3D11-to-D3D12 translation layer under test.

Use it to get a real frame out of an application once, then run that frame as often as needed
without the application.

Code: `src/dx12/translate/record/` (namespace `raw::rec`, Windows SDK and the C++ standard
library only). MIT, written from the public Microsoft Direct3D 11 documentation and our own
design. No source from apitrace, RenderDoc, DXVK, D3D11On12, VKD3D, ENB or Community Shaders was
read or copied.

## Build and run

```
cmake -S src/dx12/translate/record -B build-record -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-record -j2
build-record/d3d11_record_selftest            # records and replays on WARP, exit code 0 on success
build-record/d3d11_stream_dump frame.rawd3d11 # text listing of a capture
```

Targets: the static library `raw_d3d11_record` (links `d3d11` and `dxguid`), `d3d11_stream_dump`
and `d3d11_record_selftest`. The folder also works through `add_subdirectory`. Only the self-test
uses `d3dcompiler`.

## Recording

```cpp
#include "rec_api.h"

ID3D11Device* dev = nullptr;
ID3D11DeviceContext* ctx = nullptr;
raw::rec::Install(realDevice, realContext, &dev, &ctx);   // hand dev and ctx to the application
raw::rec::Recorder* rec = raw::rec::GetRecorder(dev);

rec->BeginCapture(L"frame.rawd3d11");   // arms; the capture starts at the next frame boundary
rec->OnPresent();                       // call once per presented frame
```

`Install` returns `RecDevice` and `RecContext`, which forward every method of `ID3D11Device` and
`ID3D11DeviceContext` to the real objects. Buffers, textures, views, shaders, state objects and
queries are not wrapped: the application receives the real objects. Each object gets a 64-bit id,
stored on the object as private data, so a reused pointer never shares an id with a destroyed
object.

`OnPresent` is the frame boundary. A capture armed with `BeginCapture` starts at the next
`OnPresent` and ends at the one after it. `Recorder::CapturesCompleted()` and
`Recorder::LastCaptureOk()` report the outcome. `Recorder::SetBackbufferInfo(width, height,
format)` fills three header fields and is optional.

Cost outside a capture: each context call tests one atomic flag. Each shader creation copies the
bytecode onto the shader object, and each input-layout creation copies the element array, the
semantic names and the signature bytecode onto the layout. Every other description is read back
with `GetDesc` when a capture needs it.

What a capture does:

1. **Initial state.** The recorder reads the pipeline state from the real context with the
   `*Get*` methods and writes it as ordinary Set records between `STATE_BEGIN` and `STATE_END`:
   input layout, vertex and index buffers, topology, the shader, constant buffers, shader
   resources and samplers of all six stages, compute UAVs, render targets with depth-stencil and
   output-merger UAVs, blend state with factor and mask, depth-stencil state with reference,
   rasterizer state, viewports, scissors, stream-output targets and predication. Full slot
   ranges are written.
2. **First-reference definition.** The first time a record references an object, a `DEFINE`
   record for it is written first. It holds the kind, the creation description, and for buffers
   and textures the current contents of every subresource, read through a staging copy and
   `Map(READ)`. A view also defines its resource. An object created during the captured frame is
   defined right after its Create call, so its initial data is in the file.
3. **Calls.** Every `ID3D11DeviceContext` method that changes state, draws or moves data is
   written before it is forwarded. The record list is in the opcode table below.
4. **Summary.** The last record counts records per opcode and objects per kind, gives the total
   content bytes, lists every subresource whose contents could not be recorded, and names the
   resource behind the last render target that was bound.

## Replaying

```cpp
raw::rec::Replayer rp;
if (!rp.Open(L"frame.rawd3d11")) { /* rp.Error() */ }
raw::rec::ReplayOptions opt;            // strict by default
if (!rp.Run(device, context, opt)) { /* rp.Result().message */ }
ID3D11Texture2D* target = rp.FindTexture(rp.LastRenderTarget());   // read it back
```

`Run` creates the objects from the `DEFINE` records and issues the calls in file order. It calls
only base `ID3D11Device` and `ID3D11DeviceContext` methods. It never calls `QueryInterface`, uses
no DXGI, and stores no private data on the replay device. Replayed objects stay alive until
`ReleaseObjects`, the next `Run`, or destruction of the replayer.

| `ReplayOptions` field | Default | Meaning |
|---|---|---|
| `strict` | `true` | Stop at the first failed `HRESULT` or malformed record. `Result()` gives the record index, opcode, `HRESULT` and a message. When `false`, the run continues and counts the failure in `softFailures`. |
| `skipInitialState` | `false` | Do not issue the Set records of the initial-state block. Objects defined inside the block are still created. Used as a control. |
| `clearStateFirst` | `true` | Call `ClearState` on the replay context before the first record. |
| `uploadThroughStaging` | `false` | Restore texture contents with staging copies. Only immutable textures are still created with initial data. |
| `getDataTimeoutMs` | `2000` | Upper bound for one blocking `GetData` poll. |

`ReplayResult` also carries a call counter per opcode (`opCalls`), the number of objects created,
and counters for contents that were not restored.

How contents are restored: buffers are created with their contents as initial data. Textures are
created with initial data when the device accepts it. When it does not, or when
`uploadThroughStaging` is set, the replayer writes each subresource into a staging texture and
copies it across with `CopySubresourceRegion`. That second route also reaches depth-stencil
textures, which `UpdateSubresource` cannot write.

## Limits

- **Deferred contexts are not recorded.** `CreateDeferredContext` returns the real deferred
  context. `ExecuteCommandList` and `FinishCommandList` on the immediate context are written as
  markers, and the replayer counts them in `unsupported`. A frame that draws through command
  lists does not replay correctly.
- **Multisampled texture contents are not recorded.** The texture is defined and created, its
  contents are marked undefined with reason `multisampled`, and the summary lists it. A frame
  that clears or fully redraws its multisampled targets is not affected.
- **Depth restore depends on the replay device.** The recorder reads depth-stencil textures.
  The replayer restores them through initial data or a staging copy. If the replay device
  refuses both, the texture is created without contents and `depthNotRestored` counts it.
- **`GetData` results are not compared.** `GetData` is replayed as a blocking poll so later
  calls see finished work. A query whose `End` was issued in an earlier frame is not polled, and
  `getDataNotIssued` counts it. An application that changes its calls based on a query result
  recorded those calls already, so the replay follows the recorded path.
- **Newer interfaces are not wrapped.** `QueryInterface` on the wrappers answers `IUnknown`,
  `ID3D11DeviceChild` (context), `ID3D11Device` and `ID3D11DeviceContext` with the wrapper and
  forwards every other IID to the real object. Calls made through `ID3D11Device1` or
  `ID3D11DeviceContext1` and later bypass the recorder.
- **Class instances are not recorded.** Shaders are set without class instances on replay.
- **Hidden counters of append, consume and counter buffers are not captured.** Initial counts
  passed to the UAV setters are recorded.
- **A write map that was opened before the capture started** has no data on record. Its `UNMAP`
  is marked undefined with reason `not_mapped_in_capture`.
- **Shared, GDI and keyed-mutex resource flags are dropped on replay.** The file keeps the
  original flags.
- **Formats without a known size** (video and planar formats) are recorded as undefined with
  reason `unknown_format`.
- **Objects created before `Install`** have no attached bytecode or layout. A shader in that
  state is recorded with reason `no_bytecode_attached` and cannot be replayed. Install before
  the application creates anything.
- View and state descriptions are stored as the Windows SDK structures of a 64-bit build.

## File format

All values are little-endian. `u32`, `u64`, `f32` are unsigned 32-bit, unsigned 64-bit and IEEE
single. An object reference is a `u64` id, and 0 is the null object. Fields are packed in the
order given. A `BLOB` is a `u64` byte count, the bytes, and zero padding to a multiple of 8. Every
record payload is zero padded to a multiple of 8.

### Header

| Offset | Type | Field |
|---|---|---|
| 0 | char[8] | magic `RAWD3D11` |
| 8 | u32 | version, currently 1 |
| 12 | u32 | header bytes; the first record starts at this offset |
| 16 | u32 | backbuffer width (0 when not supplied) |
| 20 | u32 | backbuffer height |
| 24 | u32 | backbuffer `DXGI_FORMAT` |
| 28 | u32 | feature level of the capture device |
| 32 | u32 | adapter description length in bytes |
| 36 | u32 | reserved, 0 |
| 40 | bytes | adapter description, UTF-8, zero padded to a multiple of 8 |

### Record

| Type | Field |
|---|---|
| u32 | opcode |
| u32 | payload bytes, a multiple of 8 |
| bytes | payload |

A reader skips an opcode it does not know by its length. A valid file ends with a `SUMMARY`
record. A file that does not is a truncated capture and the reader rejects it.

### SUBDATA: the contents of one subresource

| Type | Field |
|---|---|
| u32 | state: 0 no data, 1 data follows, 2 undefined |
| u32 | reason code when state is 2 |
| u32 | row bytes (for block-compressed formats, bytes per row of blocks) |
| u32 | rows |
| u32 | slices |
| u32 | reserved, 0 |
| BLOB | present only when state is 1: `rowBytes * rows * slices` bytes, rows tightly packed |

Reason codes: 1 `multisampled`, 2 `unknown_format`, 3 `staging_create_failed`, 4 `map_failed`,
5 `no_bytecode_attached`, 6 `no_layout_attached`, 7 `not_mapped_in_capture`.

### Object kinds

| Kind | Value | Kind | Value | Kind | Value |
|---|---|---|---|---|---|
| buffer | 2 | uav | 9 | input layout | 16 |
| texture 1D | 3 | vertex shader | 10 | blend state | 17 |
| texture 2D | 4 | hull shader | 11 | depth-stencil state | 18 |
| texture 3D | 5 | domain shader | 12 | rasterizer state | 19 |
| srv | 6 | geometry shader | 13 | sampler | 20 |
| rtv | 7 | pixel shader | 14 | query | 21 |
| dsv | 8 | compute shader | 15 | predicate | 22 |
| | | | | opaque (counter) | 23 |

Shader stage index in Set records: 0 VS, 1 HS, 2 DS, 3 GS, 4 PS, 5 CS.

### Markers and summary

| Op | Name | Payload |
|---|---|---|
| 1 | FRAME_BEGIN | empty |
| 2 | STATE_BEGIN | empty; the initial-state block follows |
| 3 | STATE_END | empty |
| 4 | FRAME_END | empty |
| 5 | SUMMARY | u64 resource id of the last render target bound; u64 total content bytes; u64 records before the summary; u32 n, then n pairs {u32 opcode, u32 count}; u32 n, then n pairs {u32 kind, u32 defined objects}; u32 n, then n entries {u64 id, u32 subresource, u32 reason} |
| 6 | UNSUPPORTED | u32 code; reserved |

### DEFINE records

| Op | Name | Payload |
|---|---|---|
| 16 | DEFINE_BUFFER | u64 id; u32 ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride; SUBDATA |
| 17 | DEFINE_TEXTURE | u64 id; u32 dimension (1, 2, 3), width, height, depth, mip levels, array size, format, sample count, sample quality, usage, bind flags, CPU access flags, misc flags, subresource count; one SUBDATA per subresource in D3D11 subresource order |
| 18 | DEFINE_VIEW | u64 id; u32 kind; u32 description bytes; u64 resource id; the `D3D11_*_VIEW_DESC` structure |
| 19 | DEFINE_SHADER | u64 id; u32 kind; u32 flags (bit 0 bytecode present, bit 1 stream-output declaration present); u64 FNV-1a 64 hash of the bytecode; BLOB bytecode; BLOB stream-output declaration when bit 1 is set |
| 20 | DEFINE_INPUT_LAYOUT | u64 id; u32 state (1 data, 2 undefined); u32 0; when state is 1: u32 element count; u32 0; per element u32 SemanticIndex, Format, InputSlot, AlignedByteOffset, InputSlotClass, InstanceDataStepRate, NAME; BLOB signature bytecode |
| 21 | DEFINE_STATE | u64 id; u32 kind; u32 description bytes; the `D3D11_BLEND_DESC`, `D3D11_DEPTH_STENCIL_DESC`, `D3D11_RASTERIZER_DESC` or `D3D11_SAMPLER_DESC` structure |
| 22 | DEFINE_QUERY | u64 id; u32 kind (query or predicate); u32 Query; u32 MiscFlags |
| 23 | DEFINE_OPAQUE | u64 id; u32 kind; u32 0. The id is known and no object is created |

`NAME` is u32 length (0xFFFFFFFF for a null name), u32 0, then the bytes padded to a multiple of
8. A stream-output declaration is u32 entry count, u32 stride count, u32 rasterized stream, u32 0;
per entry u32 Stream, u32 SemanticIndex, u32 packed (StartComponent, ComponentCount shifted left
8, OutputSlot shifted left 16), u32 0, NAME; then one u32 per stride, padded to a multiple of 8.

The shader hash is FNV-1a 64 over the bytecode, the same hash RAW's proxy logs at shader
creation, so ids in a capture line up with the proxy's shader logs. The replayer refuses a shader
whose bytecode does not match its hash.

### Pipeline state

| Op | Name | Payload |
|---|---|---|
| 32 | SET_SHADER | u32 stage; u32 class-instance count (instances are not recorded); u64 shader |
| 33 | SET_CONSTANT_BUFFERS | u32 stage; u32 start slot; u32 count; u32 0; u64 buffer per slot |
| 34 | SET_SHADER_RESOURCES | same layout; u64 srv per slot |
| 35 | SET_SAMPLERS | same layout; u64 sampler per slot |
| 36 | CS_SET_UAVS | u32 start slot; u32 count; u64 uav per slot; u32 initial count per slot (0xFFFFFFFF keeps the current count) |
| 37 | IA_SET_INPUT_LAYOUT | u64 layout |
| 38 | IA_SET_VERTEX_BUFFERS | u32 start slot; u32 count; u64 buffer per slot; u32 stride per slot; u32 offset per slot |
| 39 | IA_SET_INDEX_BUFFER | u64 buffer; u32 format; u32 offset |
| 40 | IA_SET_TOPOLOGY | u32 topology |
| 41 | OM_SET_RENDER_TARGETS | u32 count; u32 0; u64 rtv per slot; u64 dsv |
| 42 | OM_SET_RTS_AND_UAVS | u32 NumRTVs (0xFFFFFFFF keeps the targets and no rtv ids follow); u32 0; u64 rtv per slot; u64 dsv; u32 UAV start slot; u32 NumUAVs (0xFFFFFFFF keeps the UAVs and none follow); u64 uav per slot; u32 initial count per slot |
| 43 | OM_SET_BLEND_STATE | u64 state; f32 factor[4]; u32 sample mask |
| 44 | OM_SET_DEPTH_STENCIL_STATE | u64 state; u32 stencil reference |
| 45 | RS_SET_STATE | u64 state |
| 46 | RS_SET_VIEWPORTS | u32 count; u32 0; `D3D11_VIEWPORT` (six f32) per viewport |
| 47 | RS_SET_SCISSORS | u32 count; u32 0; `D3D11_RECT` (four i32) per rectangle |
| 48 | SO_SET_TARGETS | u32 count; u32 0; u64 buffer per slot; u32 offset per slot |
| 49 | SET_PREDICATION | u64 predicate; u32 value |
| 50 | SET_RESOURCE_MIN_LOD | u64 resource; f32 LOD |
| 51 | CLEAR_STATE | empty |

### Draws and clears

| Op | Name | Payload |
|---|---|---|
| 64 | DRAW | u32 vertex count; u32 start vertex |
| 65 | DRAW_INDEXED | u32 index count; u32 start index; i32 base vertex |
| 66 | DRAW_INSTANCED | u32 vertices per instance; u32 instance count; u32 start vertex; u32 start instance |
| 67 | DRAW_INDEXED_INSTANCED | u32 indices per instance; u32 instance count; u32 start index; i32 base vertex; u32 start instance |
| 68 | DRAW_AUTO | empty |
| 69 | DRAW_INDEXED_INSTANCED_INDIRECT | u64 argument buffer; u32 offset |
| 70 | DRAW_INSTANCED_INDIRECT | u64 argument buffer; u32 offset |
| 71 | DISPATCH | u32 x; u32 y; u32 z |
| 72 | DISPATCH_INDIRECT | u64 argument buffer; u32 offset |
| 80 | CLEAR_RTV | u64 rtv; f32 colour[4] |
| 81 | CLEAR_DSV | u64 dsv; u32 clear flags; f32 depth; u32 stencil |
| 82 | CLEAR_UAV_UINT | u64 uav; u32 values[4] |
| 83 | CLEAR_UAV_FLOAT | u64 uav; f32 values[4] |

### Resource data, queries and the rest

| Op | Name | Payload |
|---|---|---|
| 96 | MAP | u64 resource; u32 subresource; u32 map type; u32 map flags. Written only when the real `Map` succeeded |
| 97 | UNMAP | u64 resource; u32 subresource; u32 0; SUBDATA. A write map carries the whole mapped subresource, read at `Unmap`. A read map has state 0 |
| 98 | UPDATE_SUBRESOURCE | u64 resource; u32 subresource; u32 has-box; u32 left, top, front, right, bottom, back; SUBDATA with exactly the bytes the box covers |
| 99 | COPY_RESOURCE | u64 destination; u64 source |
| 100 | COPY_SUBRESOURCE_REGION | u64 destination; u32 destination subresource; u32 x, y, z; u64 source; u32 source subresource; u32 has-box; u32 left, top, front, right, bottom, back |
| 101 | COPY_STRUCTURE_COUNT | u64 destination buffer; u32 offset; u32 0; u64 source uav |
| 102 | RESOLVE_SUBRESOURCE | u64 destination; u32 destination subresource; u32 0; u64 source; u32 source subresource; u32 format |
| 103 | GENERATE_MIPS | u64 srv |
| 112 | BEGIN | u64 query or predicate |
| 113 | END | u64 query or predicate |
| 114 | GET_DATA | u64 query; u32 data size; u32 flags |
| 115 | FLUSH | empty |
| 116 | EXECUTE_COMMAND_LIST | u32 1; u32 restore-state flag. Marker only |
| 117 | FINISH_COMMAND_LIST | u32 2; u32 restore-state flag. Marker only |

## Relation to the frame ledger

RAW's proxy also has a frame ledger (`src/d3d11_proxy/FrameLedger.*`, described in
`docs/FRAME_BUFFERS.md`). The two tools answer different questions about the same frame.

| | Frame ledger | Call stream |
|---|---|---|
| Output | A manifest, a row per draw, and an image of every target after each span | One file with state, calls and contents |
| Purpose | Inventory and analysis of what the game wrote | Running the frame again on another device |
| Replayable | No | Yes |
| Contents kept | Targets after they are written, constant buffers and pixel-shader bytecode per draw | Every touched resource as it was before the frame first used it |

They can be armed for the same frame. The ledger's final image of a target is then the expected
result for a replay of the stream, which gives an independent check of a replay of a real frame.

## Recording a real game frame

Nothing in RAW's proxy calls the recorder yet. The self-test proves the recorder on its own
scene. The steps below wire it into the proxy. Each names the function it touches.

1. **Build.** In the top-level `CMakeLists.txt`, add `add_subdirectory(src/dx12/translate/record)`
   and link `raw_d3d11_record` into the `d3d11_proxy` target.
2. **Install below the proxy's wrappers.** In `src/d3d11_proxy/proxy_main.cpp`, in both
   `SB_D3D11CreateDevice` and `SB_D3D11CreateDeviceAndSwapChain`, after the real create call
   succeeds and before `new SB::Proxy::WrappedContext(realContext)`:
   ```cpp
   ID3D11Device* recDev = nullptr; ID3D11DeviceContext* recCtx = nullptr;
   if (callStreamEnabled && SUCCEEDED(raw::rec::Install(realDevice, realContext, &recDev, &recCtx))) {
       realDevice->Release(); realContext->Release();    // the wrappers hold their own references
       realDevice = recDev; realContext = recCtx;
   }
   ```
   `WrappedContext` and `WrappedDevice` then forward into the recorder, so the stream holds what
   the runtime receives: the game's calls and the passes RAW adds.
3. **Route every other user of the real objects through the recorder.** The same two functions
   store `realDevice` and `realContext` in `s_proxyInterface.device` and
   `s_proxyInterface.context` and pass them to `BridgeHook_OnDeviceCreated` and
   `OcclusionCuller::Get().Initialize`. After step 2 those are the recorder's wrappers, which is
   required: a state change made on the real context behind the recorder is missing from the
   stream. Check each consumer for a `QueryInterface` to a newer interface, since that returns
   the unwrapped object. `WrappedDevice::QueryInterface` already refuses `ID3D11Device1` to
   `ID3D11Device5`. `WrappedContext::QueryInterface` handles `ID3D11DeviceContext1` to `4`
   separately and must be checked.
4. **Frame boundary.** In `src/d3d11_proxy/WrappedSwapChain.cpp`, in
   `WrappedSwapChain::Present` and `WrappedSwapChain::Present1`, call
   `raw::rec::GetRecorder(device)->OnPresent()` (the device from `Install`, or any wrapper that forwards `GetPrivateData` to it) immediately before each `m_real->Present` or
   `m_real1->Present1` call, after `BridgeHook_OnPresent()` and
   `RenderPhaseDetector::Get().OnPresent()`, so RAW's own end-of-frame work lands inside the
   captured frame.
5. **Trigger.** Read a `[CallStream]` section from `d3d11_proxy.ini` with the frame ledger's
   convention, and call `BeginCapture` from the same Present hook when the count is reached:
   ```ini
   [CallStream]
   Enabled=0
   OutDir=<absolute folder>   ; outside the game folder
   AfterGameplayFrames=600    ; frames in a row with more than MinDraws draws
   MinDraws=500
   Captures=1
   Every=600                  ; gameplay frames between captures
   ```
   Write each capture to `<OutDir>\NNN.rawd3d11`. `BeginCapture` returns `false` while a capture
   is armed or running.
6. **Header.** Call `Recorder::SetBackbufferInfo` with the swap-chain size and format where
   `SB_D3D11CreateDeviceAndSwapChain` already has `modifiedDesc`.
7. **Safe mode.** The zero-wrap path in `SB_D3D11CreateDeviceAndSwapChain` returns unwrapped
   objects, so there is no Present hook. Leave the recorder off in safe mode.
8. **First capture.** Run `d3d11_stream_dump` on the file and read the summary: undefined
   contents, command-list markers (`EXECUTE_COMMAND_LIST`) and the last render target. Replay it
   on the system runtime first, read back `FindTexture(LastRenderTarget())`, and compare it with
   the frame ledger's image of the same target from the same frame.

Open questions for that work, none of them measured yet: whether the game uses deferred contexts
or multisampled targets in a gameplay frame, the size of a capture at full resolution (the
contents of every touched texture are in the file), and the time the capture frame takes.

## Verified

Checked by `d3d11_record_selftest` on the WARP software rasteriser (Microsoft Basic Render
Driver), feature level 11.0. The scene renders four frames through the wrappers and captures
frame 3. It uses immutable vertex and index buffers, a mip-mapped texture updated with a boxed
`UpdateSubresource` and `GenerateMips`, a dynamic constant buffer mapped with `WRITE_DISCARD` for
every draw, an offscreen target that is rendered and then sampled, a depth buffer, blend,
depth-stencil and rasterizer states, a compute pass that writes a UAV texture, an occlusion
query, a `CopySubresourceRegion` into a history texture, and a texture created inside the frame.
Frame 3 depends on state, depth and texture contents that frame 2 left behind.

| Check | Result |
|---|---|
| Capture | 716,456 bytes, 262 records, 694,432 content bytes, 29 objects, no undefined contents |
| Replay on a fresh WARP device | final image bit-identical to the directly rendered frame: 0 of 262,144 bytes differ |
| Same replay with `uploadThroughStaging` | bit-identical; 12 subresources restored by staging copy, the depth buffer among them |
| One content byte changed in the file | replay runs; image differs (4 bytes) |
| Recorded depth contents zeroed | replay runs; image differs (123,288 bytes) |
| Replay with `skipInitialState` | image differs (190,758 bytes) |
| File truncated at seven lengths, one on a record boundary | every one refused with an error message, no crash |
| `d3d11_stream_dump` on the capture | exit code 0, 320 lines |

Not checked: any device other than WARP, a D3D11-to-D3D12 translation layer, a real game frame,
the integration steps above, tessellation and geometry shaders, stream output, indirect draws,
predication, 1D and 3D textures, texture arrays, block-compressed formats, multisampled targets,
and deferred contexts. The code paths for those exist and follow the documented D3D11 calls, and
no test runs them yet.
