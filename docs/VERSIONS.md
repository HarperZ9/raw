# Game versions

RAW runs on Skyrim Special Edition 1.6.1170 and 1.7.104. Each runtime has one address table in `data/versions/`. Supporting the next runtime means adding one more table, with no code change.

## How the module picks a table

`raw_engine.dll` reads the running exe at startup:
- its file version (`VS_FIXEDFILEINFO`, for example 1.7.104.0)
- its PE time stamp and image size, from the loaded headers

It then loads the table under `raw_versions\` (the harness deploys `data/versions` there) whose header names all three. If no table matches, the module hooks nothing and logs the reason in `raw_engine_out/engine_rt.log`:

```
version: SkyrimSE.exe 1.7.105.0 (time stamp 0x..., image 0x...): no address table matches (tables: 1.6.1170.0, 1.7.104.0); RAW hooks nothing in the game
```

The proxy (`d3d11.dll`, raw::tx, the bridge, ray tracing) holds no game address. It runs on any runtime.

## The table

A table is a TSV file. Its header lines are `version`, `timestamp` and `size_of_image`, followed by rows of `name kind value confidence source`:

| kind | what it is | how a new runtime gets it |
|---|---|---|
| `setting` | a Setting object (`fShadowDistance:Display`) | by name, from the exe's `.data` |
| `vtable` | a class's primary vtable | the exe's RTTI |
| `function` | a function: a vtable slot, a detour target | an RTTI slot, or matched by structure |
| `global` | a singleton or global (`TES::Singleton`) | matched by structure |
| `site`, `range` | an instruction address, a code range | through the function holding it |
| `offset` | a structure offset (`off:TESObjectCELL::worldSpace`) | carried from the reference runtime and checked in game |

The code asks for an entry by name (`Addr("fn:GridArray::DetachAll")`). A name the table lacks returns 0, and the feature behind it stays off. The log lists every missing name. Test scripts use the same names (`engine disasm fn:...`), so one test runs on every runtime.

`data/versions/entries.tsv` lists every non-setting entry RAW uses, with its rule and its 1.6.1170 value.

## Making a table for a new runtime

```
python -I tools/enginemap/port_version.py --ref-exe <1.6.1170 exe> --new-exe <new exe> --version <x.y.z.0> \
    --entries data/versions/entries.tsv --ref-table data/versions/skyrimse-1.6.1170.tsv \
    --out data/versions/skyrimse-<x.y.z>.tsv --ref-cg <raw_engine_out of a CallGraph=1 run on 1.6.1170> \
    --new-features <structure_features.py output> [--al-ref <versionlib 1.6.1170> --al-new <versionlib new>] --report <dir>
```

The tool trusts its sources in this order:
1. **RAW's own derivation.**
   - Settings by name.
   - Vtables and their slots from RTTI.
   - Functions and globals paired by structure. This compares the reference's in-process call graph (addresses only) with the new exe's functions: sizes, calls, references and vtable-call slots.
2. **An installed Address Library**, which stays local data and is never committed. It does two jobs:
   - It cross-checks every value of ours. The reference value's id is looked up in the new runtime's file.
   - It fills in entries ours does not yet reach. Those rows carry confidence `al`.

The report gives the share of rows with our own value, and our agreement rate with the Address Library per kind. The aim is that our own values cover every entry and the Address Library is only a check.

## Status per runtime (2026-11-19)

The 1.6.1170 column comes from engine control 1 to 7, measured in game on that runtime. Steam has since replaced it on this machine, so it was not rerun with the version layer. The 1.7.104 column comes from sessions ver-1 to ver-3: the harness tests `engine_version`, `engine_version_b` and `engine_version_c` at 1080p. ENB checked 232 of 232 after each session, and the settings were restored by hash.

| Feature | 1.6.1170 | 1.7.104 | 1.7.104 evidence |
|---|---|---|---|
| Version detection and table | n/a (hardcoded before) | pass | `version: SkyrimSE.exe 1.7.104.0 (time stamp 0x6A8C7046, image 0x3929000): table skyrimse-1.7.104.tsv` |
| raw::tx (the game on D3D12) | pass | pass | reaches the world; Riverwood and the Bannered Mare render |
| D3D12 bridge on the layer | pass | pass | `attached to raw::tx: the layer's D3D12 device and queue` |
| RT scene on the game's geometry | pass | pass | `[RT] scene ready on the layer's device`; the harness checks the scene is not empty |
| RAW lighting, improved mode | pass | pass | keyframe riverwood, game and improved |
| Settings at startup | pass | pass | uGridsToLoad 5 to 7, uExterior Cell Buffer 36 to 64 |
| Weather feed | pass | pass | `fw 10a241` reads back rain 1.0, flags 4 |
| Lights (live state) | pass | pass | Riverwood 6 active lights, the inn at 22:00 16, the same count as 1.6.1170 |
| Emitter lights | pass | pass | 6 glowing emitters in Riverwood |
| The view (camera, cell, worldspace, hour) | pass | pass | `view_ok` 15, worldspace 0x3C through `off:TESObjectCELL::worldSpace` +0x128 |
| Shadow atlas, live rebuild | pass | pass | 4096 to 8192 to 4096, the size global following |
| Live grids through a worldspace change | pass | pass | 7 to 5 to 7 through WhiterunWorld; DetachAll calls 4, 8, 16, the same as 1.6.1170 |
| Papyrus VM timing (vtable hook) | pass | pass | 1,202 VM calls, 9.8 ms over 600 frames |
| Papyrus opcode loop timing | pass | hooked, not exercised | 1 call site; 0 calls with no script load |
| Cascade settings | pass | read only | `cascades` shows the settings; `set` not run on 1.7.104 |
| GPU time per pass | fails under tx (engine control 7) | not run | the fix is in engine control 8 |

Structure offsets raw_engine reads, checked on 1.7.104 by the readings above: the view (PlayerCamera, the player's cell, `TESObjectCELL::worldSpace`), the shader manager state, `TES::gridCells`, Sky and Calendar. `off:SkyrimVM::impl` (0x210 on 1.7.x, CommonLib's layout notes) is in the table but not used by raw_engine and not checked.

The 1.7.104 table against Address Library v13 (`port_version.py --report`):

| kind | rows | RAW's own value | agree with the library |
|---|---|---|---|
| setting | 5,344 | 5,344 | 5,339 of 5,343 |
| vtable | 15 | 15 | 15 of 15 |
| global | 130 | 95 | 95 of 95 |
| function | 11 | 8 | 8 of 8 |
| site | 1 | 1 | 1 of 1 |
| range | 2 | 0 | (library fallback) |
| offset | 3 | carried or cited | checked in game, above |

The structure match pairs 29,952 of 120,080 functions. Of the 29,705 with a library id, 29,659 agree (99.85 %). The four settings that disagree (two `Bethesda.net` settings, `sControlsDefinitionFile`, `fMaxDistanceMoved:Pathfinding`) are not used by RAW and were not investigated. Still on the library fallback: 35 globals, `fn:TES::SetupGrid`, `fn:ShadowMaps::Render`, `fn:ShadowMaps::SizeAtStartup`, `global:ShadowMapSize` and the Papyrus code range.
