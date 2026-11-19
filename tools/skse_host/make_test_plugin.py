"""The game-side half of RAW's SKSE host test: a light plugin whose quest calls the test plugin's natives.
Original work, MIT.

Two compiled scripts (tools/papyrus/pex.py), no Bethesda file read or copied:
  RAWSKSETest       declares Int Add(Int, Int) and Report(Int), both global native; RAWSKSETest.dll binds them
  RAWSKSETestQuest  extends Quest; OnInit calls RAWSKSETest.Report(RAWSKSETest.Add(2, 3))
The quest starts with the game, so loading a save with the plugin active runs OnInit once.

    python -I tools/skse_host/make_test_plugin.py --out <folder>

Writes <out>/RAWSKSETest.esp and <out>/Scripts/RAWSKSETest.pex, RAWSKSETestQuest.pex.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "papyrus"))
import esp  # noqa: E402
import pex  # noqa: E402
from pex import IDENT, INT, NONE, OP  # noqa: E402

GLOBAL, NATIVE = 0x01, 0x02


class Strings:
    def __init__(self):
        self.list: list[str] = []

    def __call__(self, s: str) -> int:
        if s not in self.list:
            self.list.append(s)
        return self.list.index(s)


def wrap(S: Strings, name: str, parent: str, functions: list) -> bytes:
    obj = {"name": S(name), "parent": S(parent), "doc": S(""), "user_flags": 0, "auto_state": S(""),
           "vars": [], "props": [], "states": [(S(""), functions)]}
    s = {"major": 3, "minor": 2, "game": 1, "time": int(time.time()), "source": name + ".psc", "user": "raw",
         "machine": "raw", "strings": S.list, "debug": None,
         "user_flags": [(S("conditional"), 1), (S("hidden"), 0)], "objects": [obj]}
    data = pex.write(s)
    if pex.write(pex.read(data)) != data:
        raise SystemExit(f"{name}: the written script does not read back")
    return data


def natives() -> bytes:
    S = Strings()
    for s in ("RAWSKSETest", "", "conditional", "hidden"):
        S(s)
    int_t, none = S("Int"), S("None")
    add = pex.Function(ret=int_t, flags=GLOBAL | NATIVE, params=[(S("a"), int_t), (S("b"), int_t)])
    report = pex.Function(ret=none, flags=GLOBAL | NATIVE, params=[(S("value"), int_t)])
    return wrap(S, "RAWSKSETest", "", [(S("Add"), add), (S("Report"), report)])


def quest() -> bytes:
    S = Strings()
    for s in ("RAWSKSETestQuest", "Quest", "", "conditional", "hidden"):
        S(s)
    int_t, none = S("Int"), S("None")

    def I(s: str):
        return (IDENT, S(s))

    code = [
        (OP["callstatic"], [I("RAWSKSETest"), I("Add"), I("sum"), (INT, 2), (INT, 2), (INT, 3)]),
        (OP["callstatic"], [I("RAWSKSETest"), I("Report"), I("::NoneVar"), (INT, 1), I("sum")]),
        (OP["return"], [(NONE, None)]),
    ]
    on_init = pex.Function(ret=none, locals=[(S("sum"), int_t), (S("::NoneVar"), none)], code=code)
    return wrap(S, "RAWSKSETestQuest", "Quest", [(S("OnInit"), on_init)])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    out = Path(ap.parse_args().out)
    (out / "Scripts").mkdir(parents=True, exist_ok=True)
    (out / "Scripts" / "RAWSKSETest.pex").write_bytes(natives())
    (out / "Scripts" / "RAWSKSETestQuest.pex").write_bytes(quest())
    (out / "RAWSKSETest.esp").write_bytes(esp.plugin("RAW", [("RAWSKSETestQuest", ["RAWSKSETestQuest"])]))
    print(f"{out}: RAWSKSETest.esp, Scripts/RAWSKSETest.pex, Scripts/RAWSKSETestQuest.pex")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
