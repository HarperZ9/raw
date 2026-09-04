"""Render the repository's front-page card from its spec.

    python tools/art/render_repo_art.py            # write the SVG
    python tools/art/render_repo_art.py --check    # fail if it is stale

The check mode is the point. A picture in a README is never diffed, so it
drifts from the words it illustrates the moment someone edits one and not the
other. Here the picture is a pure function of a spec that IS diffable, so a
test can re-render it and compare the result against what is committed.

This repository draws one card: the counts the engine declares, each bound to
the file it is read from. No generative header, no flow diagram. A curated
public surface publishes numbers, not the shape of a private pipeline.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from repo_card import card_svg  # noqa: E402

ART = Path(__file__).resolve().parents[2] / "docs" / "art"


def rendered(spec_path: Path) -> dict[Path, str]:
    """Every file one spec produces, as path to text."""
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    out: dict[Path, str] = {}
    for card in spec.get("cards", []):
        out[spec_path.parent / card["file"]] = card_svg(card)
    return out


def specs() -> list[Path]:
    return sorted(ART.glob("*.art.json"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="report stale artwork instead of rewriting it")
    args = parser.parse_args(argv)

    stale: list[str] = []
    for spec_path in specs():
        for path, text in rendered(spec_path).items():
            body = text + "\n"
            if args.check:
                current = path.read_text(encoding="utf-8") if path.exists() else ""
                if current != body:
                    stale.append(str(path.relative_to(ART.parents[1])))
                continue
            # newline="" so a Windows run writes the same bytes a Linux run
            # does. The whole point of this file is that committed artwork and
            # a fresh render are comparable.
            path.write_text(body, encoding="utf-8", newline="")
            print(f"wrote {path.relative_to(ART.parents[1])} ({len(body)} bytes)")

    if stale:
        print("stale artwork, re-run tools/art/render_repo_art.py:", file=sys.stderr)
        for name in stale:
            print(f"  {name}", file=sys.stderr)
        return 1
    if args.check:
        print(f"artwork matches its spec ({len(specs())} spec files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
