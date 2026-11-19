# Live session: one game, tuned in place by every worker

One Skyrim SE (1.7.104) stays up for hours. Workers don't launch their own sessions. They send requests to the
session's service, which runs them one at a time, in arrival order, in the running game. A shader edit, an ini
change, a new engine-module build, a time of day, a weather, a cell, a capture or a measure each take
milliseconds to seconds. A relaunch to the same cell takes about 65 s.

## Asking for something

```
python tools/live/req.py --as <you> <kind> key=value ...
```

`req.py` queues the request, waits for its answer and prints the result as JSON. It exits 0 when the request
succeeded, 1 when it failed (the reason is in `error`), and 3 when no answer came within `--timeout` seconds.
`--no-wait` prints the id and returns at once. `req.py state` prints the session's live state, and
`req.py queue` prints what is waiting.

| Kind | Args | What happens |
|---|---|---|
| `reload_shader` | `file`, and `source` (a whole file to copy in) or `old` and `new` (one edit) | Writes the session's shader copy and waits for the proxy's hot reload. A compile error fails the request with the compiler's line, and the last good pipelines stay. |
| `reload_ini` | `Key=value` pairs | Edits `[Lighting]` in the deployed ini. The proxy applies it in about 50 ms. |
| `ini_set` | `section`, `key`, `value` | One key in any section. `[Lighting]` applies live. `[EngineRT]` takes effect at the next `reload_module`. Any other section takes effect at the next `restart`. |
| `reload_module` | `source` (a built raw_engine.dll), `name` (default raw_engine.dll) | The new build replaces the running module with no game restart (below). |
| `console` | `command` | Typed into the console. `coc`, `cow` and `load` wait for the cell, and a missed change is typed once more. |
| `set_time` | `hour` | `set gamehour to`. The answer carries the hour the engine module reads back. |
| `set_weather` | `id` (hex form id), `force` (default true: `fw`; false: `sw`, the sky blends) | The answer carries the engine's weather read. |
| `move` | `cell`, or `worldspace` with `x` and `y` | `coc` or `cow`, waited for. |
| `capture` | `kind=frame` (`name`) | The backbuffer through the engine module, as PNG in the result folder. |
| | `kind=keyframe` (`name`, `steps`, `hold`) | The proxy's capture set. Its steps are the lighting steps and the debug views (`game`, `improved`, `view_classes`, `view_lod_normal`, ...), with match statistics and GPU times. |
| | `kind=gbuffer` (`history` 0, 1 or 2) | One frame ledger: every target after its last write. Read it with `tools/frame_buffers/inventory.py` or `gbuffer.py`. |
| `measure` | `frames` (300), `name` | Frame time over N frames, Papyrus, and the proxy's per-frame counters. GPU time per pass needs `GpuTiming=1`, which turns the bridge off for now (Findings). |
| `engine` | `commands` (`;`-separated) | Any engine-control command (`set`, `grids`, `cascades`, `atlas`, `papyrus`, `view`, `weather`, `lights`, ...). The answer carries the result lines it wrote. |
| `script` | `lines` (a list) | Harness script lines (`tools/harness/run.py`), so command files written for `listen` move over unchanged. |
| `restart` | `resolution`, `dll`, `why` (all optional) | Ends the game, restores the install, and starts again. This is the way to change the resolution or the proxy build. |
| `status`, `stop` | | The session's state; or end the session and restore the install. |
| `cancel` | the request id (req.py writes `<root>/cancel/<id>`, nothing is queued) | The running `script` request stops before its next line and answers as failed. |

A capture runs at the session's resolution. A different one needs `restart resolution=WxH`.

Each answer is `E:/raw-live/results/<id>/result.json`, next to the files the request made: `ok`, `error`,
`data`, `files`, the requester, and the timings (`queue_wait_ms`, `run_ms`, `latency_ms`). It also carries
`session_uptime_s` and the restart count.

## The service

```
node gpu-lock.mjs run --lock gpu "raw live session service" -- python tools/live/service.py
    --game <Skyrim folder> --enb-backup <ENB backup with SHA256SUMS> --settings-backup <folder>
    --dll <built d3d11.dll> --module <built raw_engine.dll> --shaders <shader folder> --ini <d3d11_proxy.ini>
    --resolution 3840x2160
```

The service holds the shared gpu lock for its whole life, so it is the only process that holds a game session.
Other workers send it requests and never take the lock themselves.

| When | What it does |
|---|---|
| Start | Needs 6 GB available, and waits until it is. Then it runs the harness's checks: the ENB backup and the game folder against `SHA256SUMS`, and both settings inis against their backups. It hashes and copies Plugins.txt. It deploys the proxy, its ini (plus `[Modules]`, `[HotReload]`, `[EngineRT] GpuTiming=0`), the module, the address tables and the shader copy. It sets the resolution for the session, starts SkyrimSE.exe directly (SteamAppId 489830) and loads `rawharness`. |
| Each request | It claims the oldest request with an exclusive create, then moves it to `running/`. It runs that request and nothing else, writes the answer, and checks the game's health. |
| Every 2 s | It writes `state.json`: up, uptime, resolution, scene, modules, module reloads, hot reload counters, the proxy heartbeat, available memory, restarts, counts, and the running and last request. |
| Game gone | It answers the request in flight as failed, restores the install, and starts again. Each restart is recorded in `state.json` with its reason. After 6 restarts the service stops instead of looping. |
| Below 3 GB available | It ends the game, restores the install, and starts again at 6 GB. |
| `stop`, `E:/raw-live/STOP`, Ctrl-C | It quits the game, restores the install, writes `report.json` in its session folder, and exits 0 only when every check passed. |
| After a killed service | `service.py ... --restore-only` restores the install. Requests left in `running/` are answered as failed at the next start. |

The restore always runs `restore-enb.cmd` and checks the 232 hashes. Both settings inis and Plugins.txt are
checked by hash. Every placed file goes: the proxy files, the module and its versioned copies, `raw_versions`,
`raw_shaders`, and `raw_engine_out` (copied to the session folder first). The answer lists anything left over. A
file the dying game still holds, such as the proxy log, is retried for 60 s and never skipped.

## Reloading the engine module

`reload_module` copies the new build beside the game as `raw_engine.r<N>.dll` and writes `raw_module_go.txt`. At
its next poll (every 15 presents, on the render thread, before the present callbacks run) the proxy does four
things in order (`src/d3d11_proxy/ModuleHost.*`):

1. It calls the running copy's `RAW_ModuleStop`. The module restores everything it wrote in the game's code and
   vtables, newest first, from the patch ledger (`src/engine_rt/rt_patch.*`). The ledger covers inline detours,
   vtable slots and rel32 call sites. While the sampler or the call graph thread runs, the module refuses the
   stop and changes nothing.
2. It drops every callback whose code lies inside the old image.
3. It loads the new copy and starts it with `ProxyInterface::moduleGeneration` raised. A reloaded module skips
   its startup-only steps (the settings that size the renderer's targets, and the startup atlas).
4. It frees the old copy after 120 presents and at least 2 s, so a game thread still returning through one of
   its hooks has left it. Trampolines and stubs stay allocated for the same reason.

The answer is `raw_module_done.txt`: `ok <ms>: ... N of N patches restored, K callbacks dropped`, or
`refused: <why>`. The proxy's own lookup of `RAW_EngineLights` follows the reload through
`modules::Generation()`.

These survive a reload because they live in the game's memory: settings changed with `set`, a grid length, and
cascade policy values. These don't, because they live in the module: a live atlas resize's viewport fit (send
`fit` again), and the hooks a command installed (`drawstats`, `vtime`, `emitters`; send the command again).

Limits, stated plainly. A thread inside a patched function's first 14 bytes at the instant of the restore is
the residual risk every inline hook carries. Callbacks are swapped on the render thread, so a draw callback
fired from another thread at that instant would race. Skyrim draws on its render thread.

## The next stage: the bridge as a reloadable module

`d3d11.dll` itself can't reload. The wrappers and the D3D12 bridge hold live COM objects that the game holds
too. The split moves the bridge's passes (`raw_d3d12_bridge`, the lighting form, ray tracing) into a module behind
the same ModuleHost. The wrappers stay in the proxy and call the bridge through a table that the proxy swaps at
the reload. The bridge's state that must outlive a reload is the D3D12 device, the shared surfaces and the
history targets. That state moves into the proxy and gets handed to the new copy. The steps are: drain the GPU,
serialise the pass state, load the versioned copy, restore. This PR builds the module host, the ledger and the
generation handshake that the split needs. The split itself is not started. Until it lands, a proxy change goes
through `restart dll=<build>` (about 65 s).

## Findings from the first sessions (2026-11-19)

- **Per-pass GPU timing turns the bridge off.** With `[EngineRT] GpuTiming=1` under raw::tx, the D3D12 bridge
  logged "initialisation failed at queries; bridge stays off". Every lighting keyframe then showed the game's
  own frame, and `Lighting.hlsl` reloads failed with "the bridge is not running the lighting pass". With
  `GpuTiming=0` the bridge attached at the first keyframe (`improved` rt_draws 1978). The ray tracing worker
  found the cause. The service's default ini now sets `GpuTiming=0`, so `measure` reports frame time, Papyrus
  and the proxy counters with no per-pass GPU times. Open item: the bridge's query creation has to tolerate the
  engine's queries, or the engine has to share them.
- **The shader copy must match the proxy build.** A shader folder from one branch with a proxy from another
  compiles, and then the pipeline swap refuses it. `restart dll=... shaders=...` changes both together.
- **Edits outlive a restart.** `ini_set` and `reload_ini` write the deployed ini, and a restart now starts from
  that ini (less `[HotReload]` and `[Modules]`, which it adds again). In the first session a restart had put
  `GpuTiming=1` back.
- **A coc to the cell the player stands in** shows no loading screen, so the harness typed it a second time and
  the keys went into the world (the inventory opened, three times on 2026-11-19). In the live session every coc
  (`move`, `console`, and `script` lines) skips a repeat of the last move or of the save's cell (`--save-cell`,
  default riverwood; `move ... again=true` forces one). A coc that brings no loading screen and no lighting
  change is taken as already there and is not typed again. The log says so. The cost: a coc whose keys were
  lost in a hitch is no longer retried, so check the log line when a cell matters.

## The proof

`python tools/live/prove.py --plan shared` sends 24 requests from a script into the session the ray tracing
worker was using at the same time. Its requests ran between them in arrival order. It makes no cell moves, and
its shader edits change no pixel: a comment in `Lighting.hlsl`, a broken edit and its fix, and `RtShadow.hlsl`
written back as it was. Run 3 (session 20261119-093716, the ray tracing worker's d3d11.dll, `GpuTiming=0`, the
bridge running):

| Request | Count | Run time (ms) | What came back |
|---|---|---|---|
| reload_shader | 4 | 801 to 1158 | edit to new pipelines 644 to 852 ms. The broken edit failed with the compiler's line (`Lighting.hlsl:105:56: use of undeclared identifier`), and the next request ran. |
| reload_module | 3 | 1740 to 1792 | proxy side 42 to 53 ms: "2 of 2 patches restored, 5 callbacks dropped", raw_engine.r1, r2, r3 started, each old copy freed |
| measure (300 frames) | 5 | 2973 to 3245 | mean 8.30 to 8.33 ms before and after each reload; 9.29 ms at the end, after the hour change |
| capture | 6 | 1089 to 4160 | four frames, one keyframe (game and improved), one G-buffer ledger (144 files) |
| set_time, set_weather | 3 | 7880 to 9338 | game hour read back 7.015 and 13.014 |
| reload_ini, ini_set, engine, status | 6 | 244 to 551 | |

24 of 24 as expected, no restart. The session was up 226 s at the last answer. The median run time was 1.5 s,
and the median submit-to-answer time was 2.0 s. The three long waits (75 s, 16 s, 12 s) were queue waits behind
the ray tracing worker's own requests. Set_time and set_weather spend most of their time in the console's
deliberate typing pace (`game.py console`).

Run 1 (31 requests, master proxy with the ray tracing shader folder, `GpuTiming=1`) got 28 of 31 as expected.
The three misses were `Lighting.hlsl` reloads refused because the bridge was off (first finding above). One
restart in it came from the ray tracing worker's `restart dll=` request. That restart took 44 s, from the
request to the save loaded again, with ENB 232 of 232, both inis and Plugins.txt restored before the new start.
Every stop of the service ended with the game quit by `qqq`, ENB 232 of 232, both inis and Plugins.txt by
hash, and nothing left in the game folder.
