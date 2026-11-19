# Hot reload: shaders and parameters reach the running game

While Skyrim runs with the bridge's lighting pass, an edit to its HLSL or to the `[Lighting]`
keys of `d3d11_proxy.ini` shows up in the next frames. You don't restart the game or press a key.

```
[HotReload]
Enabled=1
ShaderDir=<a folder holding Lighting.hlsl, LightFog.hlsl, LightingCommon.hlsli>
Dxc=<dxcompiler.dll; default: beside the game, then the Windows SDK>
```

## What happens

| Change | Path | Time from the write to the new frame (one run, 2160p, the inn) |
|---|---|---|
| a shader file | Every 15 presents the proxy reads the newest write time in `ShaderDir`. A change starts a compile on a worker thread with dxcompiler.dll and the build's own flags (`-HV 2021 -Gis -O3`). At a later present the lighting and fog pipelines are rebuilt from the new DXIL and swapped in. Both pipelines must build before either is swapped. | 764 ms for the first reload (dxcompiler.dll loads), 204 ms later on |
| a broken shader | The compile fails. The last good pipelines stay, the compiler's text goes to the log and to `raw_hot_reload.txt`, and an overlay over the top-left of the frame shows it. The overlay is laid out by GDI and copied in before Present, so it needs no shader. The next good compile removes it. | 102 ms to the overlay |
| `[Lighting]` in the ini | A new write time reloads the section and applies it at once (modes, every switch, fog, AO and shadow constants, view modes). | 53 ms |

`raw_hot_reload.txt` beside the game holds the counts (`reloads`, `failures`, `params`), the
last reload's time and the first line of the last error.

## In the harness

`run.py ... --shaders src/d3d12_bridge/shaders` copies the shader folder beside the game as
`raw_shaders` and turns hot reload on. The folder is removed after the run. Two script commands
edit the running game and time the answer:

```
reload shader Lighting.hlsl "<old text>" "<new text>"
reload ini View=2 Mode=2
```

`tests/hotreload.txt` is the proof: a warm tint edited into the lighting shader, a broken edit,
the fix, then a parameter edit. Its frames are in the plan folder as
`ingame-hotreload-2026-11-18-{before,warm,broken,fixed,sunview}-2160.png`. The `broken` frame shows the
overlay over the warm frame, because the last good shader stayed. The camera drifted between the
first two keyframes, so `before` and `warm` are different views.

Edit to frame against a relaunch: a relaunch of the same test to the same cell takes 65 s here
(launch to `cell up` in the run log). A shader reload takes 0.2 s.

## Not built yet

- **Code reload:** the engine as a separate DLL behind the proxy. The plan: drain the GPU,
  serialise pass state, load a versioned copy, restore. The bridge's passes are still linked into
  `d3d11.dll`, so this needs that split first.
- **Coverage:** shader reload covers the lighting and fog pipelines. The real-time AO pass's shaders
  are superframe's `ao_rt.hlsl`, and its pass has no reload entry yet.
- **Overlay controls:** parameters change from the ini only. No overlay widgets yet.

## Ray-tracing shaders (2026-11-19)

`RtShadow.hlsl` (the sun, light, sky and reflection rays) and `RtSkyFilter.hlsl` (the sky and bounce
filter) reload with the lighting shaders when the folder holds them and the ray-tracing scene runs
(`RtShadows=1` on raw::tx). They compile as `cs_6_6`; the new pipelines replace the old ones once the
GPU has finished the frames in flight. A refused or broken shader leaves every old pipeline in place
and shows the error in the overlay, as for the lighting shaders.
