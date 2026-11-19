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

## Status per runtime

See the pass table in the engine control 8 section of `docs/ENGINEMAP.md`.
