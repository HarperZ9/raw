"""RAW's script-heavy test scene for Papyrus: a light plugin whose quests run scripts every frame. Original work, MIT.

Two scripts, both written here as compiled Papyrus (pex.py), extend Quest:
  RAWLoadArith   OnUpdate runs a loop of N iterations of integer arithmetic (cmp_lt, jmpf, imul, iadd, iadd, jmp),
                 then registers for the next update.
  RAWLoadNative  the same loop with a native call per iteration (Form.GetFormID), the shape of typical mod scripts.
Each quest starts with the game (Start Game Enabled), so loading a save with the plugin active starts them, and its
OnInit registers the first update. No Bethesda asset or record is copied: the plugin holds only RAW's own records.

    python -I tools/papyrus/make_load_plugin.py --out <folder> [--quests 32] [--iterations 500] [--interval 0.01]

Writes <out>/RAWPapyrusLoad.esp and <out>/Scripts/RAWLoadArith.pex, RAWLoadNative.pex.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import esp  # noqa: E402
import pex  # noqa: E402
from pex import BOOL, FLOAT, IDENT, INT, NONE, OP  # noqa: E402,F401


class Strings:
    def __init__(self):
        self.list: list[str] = []

    def __call__(self, s: str) -> int:
        if s not in self.list:
            self.list.append(s)
        return self.list.index(s)


def script(name: str, iterations: int, interval: float, native: bool, counter: bool = True) -> bytes:
    S = Strings()
    I = lambda s: (IDENT, S(s))  # noqa: E731
    for s in (name, "Quest", "", "conditional", "hidden"):
        S(s)
    none, int_t, bool_t, float_t, glob_t = S("None"), S("Int"), S("Bool"), S("Float"), S("GlobalVariable")
    register = (OP["callmethod"], [I("RegisterForSingleUpdate"), I("self"), I("::NoneVar"), (INT, 1), (FLOAT, interval)])
    on_init = pex.Function(ret=none, locals=[(S("::NoneVar"), none)],
                           code=[register, (OP["return"], [(NONE, None)])])
    work = (OP["callmethod"], [I("GetFormID"), I("self"), I("m"), (INT, 0)]) if native else \
           (OP["imul"], [I("m"), I("i"), (INT, 3)])
    code = [
        (OP["assign"], [I("i"), (INT, 0)]),                 # 0
        (OP["assign"], [I("acc"), (INT, 0)]),               # 1
        (OP["cmp_lt"], [I("t"), I("i"), (INT, iterations)]),  # 2
        (OP["jmpf"], [I("t"), (INT, 5)]),                   # 3 -> 8
        work,                                               # 4
        (OP["iadd"], [I("acc"), I("acc"), I("m")]),         # 5
        (OP["iadd"], [I("i"), I("i"), (INT, 1)]),           # 6
        (OP["jmp"], [(INT, -5)]),                           # 7 -> 2
    ]
    if counter:
        # the running count of completed updates, written to the quest's own global (one native call per update)
        code += [
            (OP["iadd"], [I("::count_var"), I("::count_var"), (INT, 1)]),                       # 8
            (OP["cast"], [I("f"), I("::count_var")]),                                            # 9
            (OP["callmethod"], [I("SetValue"), I("::Counter_var"), I("::NoneVar"), (INT, 1), I("f")]),  # 10
        ]
    code += [register, (OP["return"], [(NONE, None)])]
    on_update = pex.Function(ret=none, locals=[(S("i"), int_t), (S("acc"), int_t), (S("m"), int_t), (S("t"), bool_t),
                                                (S("f"), float_t), (S("::NoneVar"), none)], code=code)
    vars_, props = [], []
    if counter:
        vars_ = [(S("::count_var"), int_t, 0, (INT, 0)), (S("::Counter_var"), glob_t, 0, (NONE, None))]
        props = [{"name": S("Counter"), "type": glob_t, "doc": S(""), "user_flags": 0, "flags": 7,
                  "auto_var": S("::Counter_var")}]
    obj = {"name": S(name), "parent": S("Quest"), "doc": S(""), "user_flags": 0, "auto_state": S(""),
           "vars": vars_, "props": props, "states": [(S(""), [(S("OnInit"), on_init), (S("OnUpdate"), on_update)])]}
    s = {"major": 3, "minor": 2, "game": 1, "time": int(time.time()), "source": name + ".psc", "user": "raw",
         "machine": "raw", "strings": S.list, "debug": None,
         "user_flags": [(S("conditional"), 1), (S("hidden"), 0)], "objects": [obj]}
    data = pex.write(s)
    if pex.write(pex.read(data)) != data:
        raise SystemExit(f"{name}: the written script does not read back")
    return data


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--quests", type=int, default=32, help="quests in all, half of each script")
    ap.add_argument("--iterations", type=int, default=500)
    ap.add_argument("--interval", type=float, default=0.01, help="seconds between updates")
    ap.add_argument("--no-counter", action="store_true", help="leave out the per-quest update counters")
    a = ap.parse_args()
    out = Path(a.out)
    (out / "Scripts").mkdir(parents=True, exist_ok=True)
    for name, native in (("RAWLoadArith", False), ("RAWLoadNative", True)):
        (out / "Scripts" / f"{name}.pex").write_bytes(script(name, a.iterations, a.interval, native, not a.no_counter))
    quests = [(f"RAWLoadQuest{i:02d}", ["RAWLoadNative" if i % 2 else "RAWLoadArith"]) for i in range(a.quests)]
    (out / "RAWPapyrusLoad.esp").write_bytes(esp.plugin("RAW", quests, counters=not a.no_counter))
    print(f"{out}: RAWPapyrusLoad.esp with {a.quests} quests, {a.iterations} iterations per update, every {a.interval} s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
