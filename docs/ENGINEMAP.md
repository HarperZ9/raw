# Engine map

A function-level map of SkyrimSE.exe 1.6.1170, built from the executable's data sections, the Address Library database and CommonLibSSE-NG's names. It tells RAW where each engine system lives: functions, vtables, singletons, settings and the fields worth streaming live. It is read-only static analysis: the tool never launches the game and never changes a game file.

## Run it

```
python -I tools/enginemap/build_map.py \
  --exe "<Skyrim Special Edition>/SkyrimSE.exe" \
  --addrlib "<.../SKSE/Plugins>/versionlib-1-6-1170-0.bin" \
  --commonlib <CommonLibSSE-NG checkout> \
  --bridge <skyrimbridge checkout> \
  --out docs/enginemap --public
python -I tools/enginemap/test_enginemap.py
```

Needs Python 3.10+ and `pefile`. A full run takes about 8 seconds on the CPU. `--public` leaves out values copied from the game (setting defaults and string values), so the output holds addresses, names and notes only. `--addrlib-alt` checks a second database against the first.

## What it reads

| source | gives |
|---|---|
| `.pdata` | every function's start and end (120,080) |
| `.rdata` RTTI | 8,544 vtables, class names and class hierarchies |
| `.data` | 5,344 Setting objects: Skyrim.ini, SkyrimPrefs.ini and game settings, with their compiled defaults |
| Address Library | ID to address |
| CommonLibSSE-NG (MIT) | names for IDs, virtual slot names, member offsets |

The executable's code section is encrypted on disk by Steam's DRM wrapper. The tool reports that and leaves it alone, so callers and callees are not in the map. The `refs` column shows which classes' vtables dispatch to each function.

## Output (`docs/enginemap/`)

- one TSV per subsystem: renderer, shaders, lighting and shadows, scene graph, LOD, sky and weather, water, streaming, Papyrus
- `settings.tsv`, `singletons.tsv`, `live_state.tsv`, `control_points.tsv`, `lighting_techniques.tsv`
- `stats.json`: counts and cross-check results

Confidence per row: **named** (CommonLib names it), **inferred** (exact class and slot from RTTI, method unnamed), **unknown** (exact address, stub shared by many classes).

## Checks the tool runs every time

- Address Library IDs for vtables resolve to vtable starts found by the RTTI scanner (8,017 of 8,017 on 1.6.1170).
- IDs for RTTI type descriptors resolve to type descriptors (7,319 of 7,321; the 2 misses exceed the reader's string limit).
- Named functions start a `.pdata` function (536 of 589; the rest are leaf functions with no unwind entry).

## Debt

Written in Python for now; the BuildLang port is owed.
