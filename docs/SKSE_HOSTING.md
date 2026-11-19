# SKSE plugin hosting

`raw_skse.dll` loads SKSE plugins from `Data/SKSE/Plugins` with no SKSE installed. The game starts as
`SkyrimSE.exe`; RAW's `d3d11.dll` proxy loads the host, and the host gives each plugin the interface table an SKSE
plugin expects. It runs on every build RAW's version layer names (docs/VERSIONS.md): Skyrim Special Edition
1.6.1170 and 1.7.104 today. The host reads its game addresses from the same tables as the rest of RAW, so a new
build needs only a new table.

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
SKSE then owns the plugins, so no hook runs twice. SKSE has its own build for each game build (2.2.6 for 1.6.1170,
2.3.1 for 1.7.104); the host needs no SKSE install on either.

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

## Which plugins load

The host reads each plugin's `SKSEPlugin_Version` data and refuses, with the reason in `skse_host.json`, a plugin
that:

- names neither this build nor a version-independent way to find addresses (Address Library or signatures);
- is version-independent but built against game structures from before 1.6.629;
- finds its addresses through the Address Library, does not name this build, and was built before the library's
  format 5, on 1.7.99 and later. Such a plugin reads `versionlib-1-7-104-0.bin` itself, cannot parse it, and stops
  the game with a message box. The flag that says it can is bit 1 of the word at 0x304 in `SKSEPlugin_Version`.
  Every CommonLibSSE-NG plugin built before format 5 lacks it, so most plugins released for 1.6.1170 are refused on
  1.7.104 until their authors rebuild them;
- needs a newer SKSE than `ReportVersion`;
- exports only `SKSEPlugin_Query` (built before 1.6.629), unless `AllowLegacy=1`.

## The interface

The host implements the binary contract that CommonLibSSE-NG declares for plugins: the interface table, the
`SKSEPlugin_Version` data layout, the `SKSEPlugin_Query` and `SKSEPlugin_Load` exports, and each interface's
function table. `src/skse_host/skse_abi.h` holds the layouts with the offsets checked at compile time.

| Interface | Function | State |
|---|---|---|
| Load | `SKSEPlugin_Version` data: runtime list, Address Library, format-5 and signature flags, the 1.6.629 structure flags, minimum SKSE version | implemented |
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
| Papyrus (v1) | `Register` | implemented: the callback gets the game's `IVirtualMachine` once the VM exists and its vtable lies in the game's image |
| Scaleform (v1) | `Register`, `RegisterForInventory` | not provided: `Register` returns false |
| Object | delay functors, object registry, persistent storage | not provided: `QueryInterface` returns null |
| SKSE's own Papyrus script extensions | the natives SKSE adds to `Game`, `Form`, `StringUtil` and the rest | not provided |

**Co-save.** RAW writes its own file, `<save name>.rawcosave`, beside the save: `RAWC`, a format number, then per
plugin its unique id and its records (type, version, length, bytes). SKSE's `.skse` file is neither read nor
written, so a plugin's data saved under SKSE does not carry over to the host, and the reverse.

**Addresses.** The host needs seven game addresses (the VM, data handler, UI, input manager and save manager
singletons, `BGSSaveLoadManager::Save_Impl` and `Load_Impl`) and two structure offsets (`SkyrimVM::impl`,
`UI::menuMap`). `src/skse_host/skse_addresses.h` names them; the values come from the version layer's table for
the running exe (`data/versions/skyrimse-<build>.tsv`, deployed as `raw_versions/` beside the game). An address the
table lacks comes from an installed Address Library database for the same build (formats 2 and 5,
`skse_addrlib.cpp`). With no table naming the exe the host hooks nothing and sends no timed messages; plugins still
load. Any value still unknown turns its feature off, and the log says which. `RAW_SKSE_AddressOf(id)` answers
Address Library ids for RAW's own code when a database is installed. Plugins built on CommonLibSSE-NG read the
database themselves, so those still need Address Library installed.

**What a plugin sees.** `skseVersion` is `ReportVersion` (2.2.6 by default), so plugins that check a minimum SKSE
version load. Handle 0 is the host; plugins get 1, 2 and up in folder order. Messages from the host carry the
sender `SKSE`.

## Results

Filled in from the test sessions below.

## Test

`tools/harness/run.py` starts `SkyrimSE.exe` directly, so no SKSE is in the process:

```
python -I tools/skse_host/make_test_plugin.py --out <esp folder>
python tools/harness/run.py skse_host --game <Skyrim> --enb-backup <ENB backup> --settings-backup <folder>
    --dll <d3d11.dll> --module <raw_skse.dll> --versions data/versions --plugin <esp folder>
    --skse-file <RAWSKSEProbe.dll> --skse-file <RAWSKSETest.dll> --skse-file <versionlib-<build>.bin>
    --collect raw_skse_out --out <results> --resolution 1920x1080
```

Two test plugins, both RAW's own:

- `RAWSKSEProbe.dll` (`tests/skse_host/probe`, built with RAW) speaks the interface with no library under it, reads
  no Address Library and touches no game structure, so it runs on any build the host supports. It writes PASS, FAIL
  or INFO lines to `raw_skse_out/RAWSKSEProbe.log`: load and every interface, each message, a plugin-to-plugin
  message, a task, a trampoline write, the Papyrus callback's VM pointer, and a co-save round trip.
- `RAWSKSETest.dll` (`tests/skse_host/plugin`) is built on CommonLibSSE-NG, the way most plugins are, and adds two
  Papyrus natives that a quest script calls (`tools/skse_host/make_test_plugin.py`). Built against CommonLibSSE-NG
  3.7.0 (MIT) it reads Address Library format 2 only, so it is for 1.6.x. For 1.7.x the same source is built
  locally against the maintained CommonLibSSE-NG, which reads format 5 and is GPL-3.0-or-later; that binary is kept
  outside this repository and never shipped.

The harness removes every file it placed (the proxy, the host, the plugins, the Address Library file, the address
tables, the test esp and scripts), writes `Plugins.txt` back, moves the test saves and co-saves to the results,
removes the SKSE logs the run created, and checks all ENB hashes after.

## Licences

| What | Licence | How it is used |
|---|---|---|
| SKSE64 | no open licence: the readme forbids rehosting; `src/skse64/skse64_license.txt` names one team barred from using the files and grants nothing | not used. No SKSE file is read for the implementation, copied or loaded. The interface was written from CommonLibSSE-NG's declarations and from what plugins do |
| CommonLibSSE-NG 3.7.0 and earlier | MIT | its declarations are the reference for `skse_abi.h` and the Address Library format 2; the 1.6.x test plugin links it |
| CommonLibSSE-NG, maintained line (alandtse) | GPL-3.0-or-later since 2026-08 | the layout facts only: Address Library format 5, the format-5 plugin flag, `SkyrimVM::impl` at 0x210 on 1.7.x. No code is copied. The 1.7.x test plugin links it locally and is never shipped |
| Address Library for SKSE Plugins (meh321) | Nexus permissions: asset use allowed, no rehosting | read to fill and check the address tables; placed in the game folder only during tests; never shipped |
| Third-party test plugins | per row in the results | run only inside test sessions; never redistributed |
