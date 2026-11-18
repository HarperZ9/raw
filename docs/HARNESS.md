# Test harness: Skyrim runs a test with no one at the keyboard

`tools/harness/run.py` runs a named test script in the game and leaves the install as it found
it. One command:

```
python tools/harness/run.py lighting --game <Skyrim folder> --enb-backup <ENB backup with SHA256SUMS>
    --settings-backup <folder for the settings files> --dll <built d3d11.dll> --out <results>
    --resolution 1920x1080
```

| Phase | What happens |
|---|---|
| Before | The ENB backup and the game folder are checked file by file against `SHA256SUMS`; `SkyrimCustom.ini` and `SkyrimPrefs.ini` must equal their backups (made on first use). Any mismatch stops the run before anything is copied. |
| Deploy | The proxy `d3d11.dll` and a harness ini (`[Lighting] Harness=1`); the resolution (`iSize W/H`), tutorials off and `bAlwaysActive=1` in the settings files. |
| Run | The game starts through SKSE; the script runs line by line. |
| Quit | `qqq` in the console; `taskkill` only if the game is still up 60 s later (`quit_cleanly` in the report says which). |
| After | The proxy log and every capture are collected, captures become PNG, ENB is restored with `restore-enb.cmd`, the settings files are copied back, and every hash is checked again. |
| Report | `report.json`: each step with its time, the GPU load and the compute processes before and after, the install checks, and one record per captured step (match statistics, coverage, GPU times). `tools/harness/summary.py` prints it as a table. |

## The Survival Mode prompt

A new game started from the main menu (`coc` from the menu) shows the Survival Mode message box,
and the earlier session scripts answered it with key presses that the box ignored. What works:

1. Run the game at the display's own resolution. The game's `iSize W/H` was 3840 x 2160 on a
   1920 x 1080 display mode, so the borderless window showed only its top-left quarter and the
   box's buttons were off screen. The frame itself was whole in every capture.
2. The box ignores the arrow keys and Enter sent as scan codes, and its cursor ignores absolute
   mouse positions. It follows relative mouse motion: from where it starts, (0.494, 0.516) of the
   frame, a move of (0.037 w / 1.92, 0.244 h / 1.54) lands on **No** (gains measured in game at
   1080p), and a left click answers it.
3. The harness's `prepare` test does this once and saves the state as `rawharness`. Every other
   test starts with `start load rawharness`, which loads that save from the main menu with the
   console's `load`; the prompt never appears again because its quest has run in that save.

The save lives in the game's save folder (`__MO_Saves` under this profile). Nothing else in the
install changes.

## Script commands

| Command | Effect |
|---|---|
| `start load <save>`, `start new <console command>` | from the main menu |
| `prepare <save>` | answer the Survival Mode prompt and save |
| `scene <name>` | names the captures that follow |
| `console <command>` | typed into the console |
| `wait <seconds>`, `wait load` | `wait load`: the loading screen has passed and 300 frames are drawn |
| `keyframe <name> steps=<list or all> hold=<frames>` | the proxy captures the same view once per step: game lighting, ours matched, controls, each improvement alone, all of them |
| `mouse <dx> <dy>` | turns the camera |
| `walk <key> <seconds> keyframe=<name> ...` | holds a key; with a keyframe the capture runs while the camera moves |
| `minimise <seconds>` | minimises the window and records how many frames the game presented meanwhile |
| `quit` | |

Input goes through `SendInput`, which needs the game window in the foreground. Every key checks
that it is; a key never goes to another window, and a step that cannot type stops the run with
the reason instead of being skipped.

## The proxy's side

With `[Lighting] Harness=1` the proxy writes `raw_harness_status.txt` beside the game every 30
presents (presents, world frames) and polls for `raw_harness_go.txt`: `scene`, `keyframe`,
`steps`, `hold`, `dir`. It runs the steps, holding each for `hold` frames, writes the backbuffer,
the statistics and the GPU times per step to `<dir>/harness_results.jsonl`, and answers with
`raw_harness_done.txt`. The steps of one keyframe follow each other frame by frame, so each
before and after comes from the same view; wind, water and the game's temporal anti-aliasing
still move between them.

## First runs (2026-11-18)

| Run | Result |
|---|---|
| `prepare` at 1920 x 1080 | prompt answered No with the cursor move and click, state saved as `rawharness` |
| `lighting` at 1920 x 1080 | loaded the save with no prompt; 5 keyframes in 3 cells; the inn, the walk and Riverwood by day measured; the night row is void (the `coc` after the inn did not take); minimised for 8 s the game kept drawing (810 presents, 810 world frames) |
| `lighting` at 3840 x 2160 | all 5 keyframes in 3 cells; minimise skipped (the window is larger than the 1080p display and did not come back from a minimise in an earlier try) |
| every run | ENB 232 of 232 hashes after restore, both settings files restored |

`qqq` quit the game in the first 1080p run; in the last two it was still up 60 s later and the
harness ended it with `taskkill` (`quit_cleanly: false` in the report).

## Limits

- The game cannot run without a window. At 1080p it keeps drawing while minimised.
- A `coc` typed right after `tfc` can fail to change the cell; the report shows it only in the
  pictures and in the decoded sun, not as an error yet.
- Console input needs the foreground: someone using the desktop during a run can stop it (it
  stops cleanly and restores the install).
- The cursor gains for the prompt were measured at 1080p.
- Camera keyframes are mouse turns and walks from the save's start position; there is no free
  camera path with coordinates yet.
