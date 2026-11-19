# SKSE plugin hosting

`raw_skse.dll` loads SKSE plugins from `Data/SKSE/Plugins` with no SKSE installed. The game starts as
`SkyrimSE.exe`; RAW's `d3d11.dll` proxy loads the host, and the host gives each plugin the interface table an SKSE
plugin expects. Built and tested for Skyrim Special Edition 1.6.1170.

```
d3d11_proxy.ini
[Modules]
Load=raw_skse.dll          ; beside raw_engine.dll if that runs too: Load=raw_engine.dll;raw_skse.dll
[SKSE]
Host=1                     ; 0 turns the host off
Plugins=                   ; a;b.dll loads only these; empty loads every .dll in Data/SKSE/Plugins
AllowLegacy=0              ; 1 also loads plugins that export only SKSEPlugin_Query
ReportVersion=2.2.6        ; the SKSE version plugins are told
OutDir=raw_skse_out        ; skse_host.log and skse_host.json, beside the game
```

When SKSE itself is in the process (`skse64_1_6_1170.dll` loaded), the host loads nothing and says so in its log.
SKSE then owns the plugins, so no hook runs twice.

## When things happen

| Moment | SKSE | RAW's host |
|---|---|---|
| Plugins load | before the game's own start-up code | at the game's first `D3D11CreateDevice`, on the main thread, after the proxy's start-up |
| PostLoad, PostPostLoad | after every plugin loaded | the same |
| InputLoaded | after the input manager is made | the first present that finds `BSInputDeviceManager` made |
| DataLoaded | right after the data files load | the first present that finds the main menu made (the game builds it after the data loads) |
| PreLoadGame, PostLoadGame | around a save load | around `BGSSaveLoadManager::Load_Impl`, with the co-save's revert and load callbacks between them |
| SaveGame | as a save is written | before `BGSSaveLoadManager::Save_Impl`, with the co-save written at that point |
| NewGame, DeleteGame | new game, deleted save | not sent yet |

A plugin that patches the game's early start-up (before the renderer exists) loads too late under the host for
those patches to take. Plugins that hook gameplay, data loading, saves or the renderer's per-frame work load in time.

## The interface

The host implements the binary contract that CommonLibSSE-NG (MIT) declares for plugins: the interface table, the
`SKSEPlugin_Version` data layout, the `SKSEPlugin_Query` and `SKSEPlugin_Load` exports, and each interface's
function table. `src/skse_host/skse_abi.h` holds the layouts with the offsets checked at compile time.

| Interface | Function | State |
|---|---|---|
| Load | `SKSEPlugin_Version` data: runtime list, Address Library and signature-scanning flags, the 1.6.629 structure flags, minimum SKSE version | implemented |
| Load | `SKSEPlugin_Query` (plugins built before 1.6.629) | implemented, off by default (`AllowLegacy=1`) |
| Load | `SKSEPlugin_Load` | implemented; a fault inside it is caught and the plugin left unloaded |
| SKSEInterface | `skseVersion`, `runtimeVersion` (read from the exe), `isEditor` | implemented |
| SKSEInterface | `QueryInterface`, `GetPluginHandle`, `GetPluginInfo` | implemented |
| SKSEInterface | `GetReleaseIndex` | stub: returns 0 |
| Messaging (v2) | `RegisterListener` (one sender, or every sender), `Dispatch` (plugin to plugin, to one receiver or all) | implemented |
| Messaging | host messages | PostLoad, PostPostLoad, InputLoaded, DataLoaded, PreLoadGame, PostLoadGame, SaveGame implemented; NewGame, DeleteGame not sent |
| Messaging | `GetEventDispatcher` (mod, camera, crosshair, action, NiNode update) | stub: returns a valid empty event source; plugins can add sinks; no event is sent yet |
| Task (v2) | `AddTask`, `AddUITask` | implemented: run and disposed at the next present, on the presenting thread |
| Trampoline (v1) | `AllocateFromBranchPool` | implemented: executable memory within 2 GB of the game's image |
| Trampoline | `AllocateFromLocalPool` | implemented: executable memory within 2 GB of the plugin's image |
| Serialization (v4) | `SetUniqueID`, `SetSaveCallback`, `SetLoadCallback`, `SetRevertCallback` | implemented |
| Serialization | `WriteRecord`, `OpenRecord`, `WriteRecordData`, `GetNextRecordInfo`, `ReadRecordData` | implemented |
| Serialization | `ResolveFormId`, `ResolveHandle` | stub: return their input (a changed load order is not remapped) |
| Serialization | `SetFormDeleteCallback` | stored, never called |
| Papyrus (v1) | `Register` | implemented: the callback gets the game's `IVirtualMachine` once the VM exists |
| Scaleform (v1) | `Register`, `RegisterForInventory` | not provided: `Register` returns false |
| Object | delay functors, object registry, persistent storage | not provided: `QueryInterface` returns null |
| SKSE's own Papyrus script extensions | the natives SKSE adds to `Game`, `Form`, `StringUtil` and the rest | not provided |

**Co-save.** RAW writes its own file, `<save name>.rawcosave`, beside the save: `RAWC`, a format number, then per
plugin its unique id and its records (type, version, length, bytes). SKSE's `.skse` file is neither read nor
written, so a plugin's data saved under SKSE does not carry over to the host, and the reverse.

**Addresses.** The host needs eight game addresses: the VM, data handler, UI, input manager and save manager
singletons and the save and load functions. `src/skse_host/skse_addresses.h` holds them for 1.6.1170, generated
once by `tools/skse_host/addrlib.py` from an Address Library database. At run time the host needs no database.
When one is installed, the host reads it, checks each built-in address against it (the log names any mismatch)
and answers `RAW_SKSE_AddressOf(id)` for RAW's own code. Most plugins built on CommonLibSSE-NG read the database
themselves, so those still need Address Library installed.

**What a plugin sees.** `skseVersion` is `ReportVersion` (2.2.6 by default), so plugins that check a minimum SKSE
version load. Handle 0 is the host; plugins get 1, 2 and up in folder order. Messages from the host carry the
sender `SKSE`.

## Results on 1.6.1170

Filled in from the test sessions below.

## Test

`tools/harness/run.py` runs the test with no SKSE in the process (`--launch direct`):

```
python -I tools/skse_host/make_test_plugin.py --out <esp folder>
cmake -S tests/skse_host/plugin -B <plugin build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<CommonLibSSE-NG prefix>
python tools/harness/run.py skse_host --launch direct --game <Skyrim> --enb-backup <ENB backup> --settings-backup <folder>
    --dll <d3d11.dll> --module <raw_skse.dll> --plugin <esp folder> --skse-file <RAWSKSETest.dll>
    --skse-file <versionlib-1-6-1170-0.bin> --collect raw_skse_out --out <results> --resolution 1920x1080
```

`RAWSKSETest.dll` (`tests/skse_host/plugin`) is RAW's own plugin. It writes one PASS, FAIL or INFO line per check to
`RAWSKSETest.log`: load and every interface, each message, a plugin-to-plugin message, a task, a trampoline write,
two Papyrus natives called by a quest script (`tools/skse_host/make_test_plugin.py`), and a co-save round trip
(save by name, load it back, compare the counter). The harness removes every file it placed (the proxy, the host,
the plugins, the Address Library file, the test esp and scripts), writes `Plugins.txt` back, moves the test saves
and co-saves to the results, removes the SKSE logs the run created, and checks all ENB hashes after.

## Licences

| What | Licence | How it is used |
|---|---|---|
| SKSE64 | no open licence: the readme forbids rehosting; `src/skse64/skse64_license.txt` names one team barred from using the files and grants nothing | not used. No SKSE file is read for the implementation, copied or loaded. The interface was written from CommonLibSSE-NG's declarations and from what plugins do |
| CommonLibSSE-NG | MIT | its declarations are the reference for `skse_abi.h` and the Address Library format; the test plugin links it |
| Address Library for SKSE Plugins (meh321) | no open licence in the archive | read once to generate eight RVAs; placed in the game folder only during tests; never shipped |
| Third-party test plugins | per row in the results | run only inside test sessions; never redistributed |
