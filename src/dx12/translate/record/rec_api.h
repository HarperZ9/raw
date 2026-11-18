#pragma once
// Public entry points of the D3D11 call-stream recorder and replayer.
// See docs/D3D11_CALL_STREAM.md for the file format and the integration notes.
#include "rec_recorder.h"
#include "rec_replayer.h"

namespace raw::rec
{
// Wraps a real device and its immediate context. On success *outDev and
// *outCtx each hold one reference; the wrappers hold their own references to
// the real objects. Hand the wrappers to the application in place of the
// real ones.
HRESULT Install(ID3D11Device* real, ID3D11DeviceContext* realCtx, ID3D11Device** outDev,
                ID3D11DeviceContext** outCtx);

// The recorder that belongs to a device passed to or returned by Install.
// Null when the device was never installed. Not reference counted: valid
// while the wrappers are alive.
Recorder* GetRecorder(ID3D11Device* device);
} // namespace raw::rec
