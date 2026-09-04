"""Check that the front-page card still tells the truth, and say so in a receipt.

    python tools/art/check_repo_art.py           # a readable summary
    python tools/art/check_repo_art.py --json    # the same run as a receipt

A picture in a README is never diffed, so it drifts from the text silently:
somebody edits a number, nobody re-renders, and the card now describes a
version of the engine that no longer exists. Here the picture is a pure
function of a spec that IS diffable, so this re-renders it and compares bytes.

Whether the drawn numbers are TRUE of the engine is a separate question, asked
by tools/art/check_repo_facts.py, which reads the shaders, the source enums,
the README tables and the build manifest and rebuilds each count.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import render_repo_art as RENDER  # noqa: E402
import check_repo_card as CARD_GATE  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "docs" / "art"
SCHEMA = "raw.repo-art/v1"

# Where an illustration lives. .github/assets/ is deliberately outside this
# set: it holds the social-preview banner, which is a hand-drawn asset rather
# than a rendered-from-spec drawing.
SHOWN_DIRS = ("docs/art",)

EM_DASH = "—"


def _rel(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace("\\", "/")


def _specs() -> list[Path]:
    return sorted(ART.glob("*.art.json"))


def check_spec_present(specs: list[Path]) -> list[str]:
    return [] if specs else ["docs/art holds no *.art.json spec"]


def check_artwork_matches_spec(specs: list[Path]) -> list[str]:
    """Bytes, not text. read_text decodes with universal newlines, so a file
    that a Windows writer turned into CRLF reads back identical and the drift
    goes unnoticed until the diff shows every line changed."""
    bad = []
    for spec_path in specs:
        for path, text in RENDER.rendered(spec_path).items():
            if not path.exists():
                bad.append(f"{_rel(path)} was never rendered")
            elif path.read_bytes() != (text + "\n").encode("utf-8"):
                bad.append(f"{_rel(path)} is stale; run python tools/art/render_repo_art.py")
    return bad


def check_render_is_deterministic(specs: list[Path]) -> list[str]:
    """The card is a pure function of its spec. Rendered twice, it is equal."""
    return [f"{p.name} renders differently every time"
            for p in specs if RENDER.rendered(p) != RENDER.rendered(p)]


def check_no_local_paths_or_em_dashes(_unused: list[Path]) -> list[str]:
    bad = []
    for path in sorted(ART.glob("*.svg")):
        text = path.read_text(encoding="utf-8")
        if EM_DASH in text:
            bad.append(f"{path.name} carries an em-dash")
        if re.search(r"[A-Z]:[\\/]", text):
            bad.append(f"{path.name} names a local path")
    return bad


def check_every_illustration_is_shown(_unused: list[Path]) -> list[str]:
    """No orphans. An image nobody links to is an image nobody sees."""
    haystack = (ROOT / "README.md").read_text(encoding="utf-8")
    haystack += "".join(p.read_text(encoding="utf-8", errors="ignore")
                        for p in ROOT.glob("docs/**/*.md"))
    images = sorted(p for d in SHOWN_DIRS for p in (ROOT / d).glob("*")
                    if p.suffix.lower() in {".svg", ".png"})
    return [f"committed but never shown: {_rel(p)}"
            for p in images if _rel(p) not in haystack]


def check_the_gate_can_fail(_unused: list[Path]) -> list[str]:
    """A gate that cannot fail is not a gate. Every card check with a budget in
    it gets handed input it has to reject, and anything that passes is named."""
    return CARD_GATE.control_failures()


CHECKS = [
    ("spec.present", check_spec_present),
    ("art.matches_spec", check_artwork_matches_spec),
    ("art.render_is_deterministic", check_render_is_deterministic),
    ("art.no_local_paths_or_em_dashes", check_no_local_paths_or_em_dashes),
    ("art.every_illustration_is_shown", check_every_illustration_is_shown),
] + CARD_GATE.checks() + [
    ("art.the_gate_can_fail", check_the_gate_can_fail),
]


def _outputs(specs: list[Path]) -> list[dict]:
    seen = []
    for spec_path in specs:
        for path in RENDER.rendered(spec_path):
            body = path.read_bytes() if path.exists() else b""
            seen.append({
                "file": _rel(path),
                "spec": _rel(spec_path),
                "bytes": len(body),
                "sha256": hashlib.sha256(body).hexdigest(),
            })
    return sorted(seen, key=lambda item: item["file"])


def receipt() -> dict:
    specs = _specs()
    results = [{"name": name, "passed": not failures, "failures": failures}
               for name, failures in ((n, f(specs)) for n, f in CHECKS)]
    return {
        "schema": SCHEMA,
        "mode": "check",
        "specs": [_rel(p) for p in specs],
        "outputs": _outputs(specs),
        "checks": results,
        "passed": all(item["passed"] for item in results),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true",
                        help="emit the run as a receipt instead of a summary")
    args = parser.parse_args(argv)
    report = receipt()
    if args.json:
        print(json.dumps(report, indent=2))
        return 0 if report["passed"] else 1
    for item in report["checks"]:
        print(f"{'ok  ' if item['passed'] else 'FAIL'} {item['name']}")
        for failure in item["failures"]:
            print(f"       {failure}")
    print(f"{len(report['outputs'])} files from "
          f"{len(report['specs'])} spec files")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
