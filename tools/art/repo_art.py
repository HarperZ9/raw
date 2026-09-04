"""repo_art.py -- shared primitives for the repository's front-page card.

The card renderer draws from a spec and needs three things from here: the two
type families the whole design uses, and the two helpers that keep a render
byte-stable. Byte stability is the point of the split. The card is a pure
function of its spec, so a test can re-render it and compare against what is
committed, and both halves of that comparison round numbers and escape markup
exactly the same way.

Two faces only, a grotesk and a mono. Hierarchy comes from weight and size, so
there is no third family to reach for.
"""
from __future__ import annotations

GROTESK = "Hanken Grotesk, Segoe UI, ui-sans-serif, system-ui, sans-serif"
MONO = "Conso, ui-monospace, Cascadia Mono, Consolas, monospace"


def _num(value: float) -> str:
    """Two decimal places, no trailing noise. Byte-stability starts here."""
    return f"{value:.2f}".rstrip("0").rstrip(".") or "0"


def _esc(text: str) -> str:
    return (str(text).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))
