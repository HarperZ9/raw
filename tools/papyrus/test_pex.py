"""Checks tools/papyrus/pex.py against compiled scripts in place: every file read and written again must come back
byte for byte, and the object size field's offset from the body length is reported. Reads the files where they are;
copies nothing. Original work, MIT.

    python -I tools/papyrus/test_pex.py <folder of .pex files> [--limit N]
"""
from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pex  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    files = sorted(Path(a.folder).glob("*.pex"))
    if a.limit:
        files = files[:a.limit]
    same = 0
    extra = collections.Counter()
    ops = collections.Counter()
    bad = []
    for f in files:
        data = f.read_bytes()
        try:
            s = pex.read(data)
        except Exception as e:  # report every file that does not parse, with the reason
            bad.append(f"{f.name}: {e}")
            continue
        for o in s["objects"]:
            extra[o["size_minus_body"]] += 1
            for _, funcs in o["states"]:
                for _, fn in funcs:
                    for op, _ in fn.code:
                        ops[pex.OPCODES[op][0]] += 1
        if pex.write(s) == data:
            same += 1
        else:
            bad.append(f"{f.name}: written differs")
    print(f"{same} of {len(files)} files read and written back byte for byte")
    print("object size minus body length:", dict(extra))
    print("opcodes:", ", ".join(f"{k} {v}" for k, v in ops.most_common()))
    for b in bad[:20]:
        print("  ", b)
    return 0 if same == len(files) and files else 1


if __name__ == "__main__":
    raise SystemExit(main())
