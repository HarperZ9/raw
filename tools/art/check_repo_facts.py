"""Bind the drawn card to the code that backs it.

The artwork gate settles whether the card fits its columns and matches its
spec. It cannot settle whether the card is true. This reads the engine's own
bytes, rebuilds every number the card draws, and fails when a drawing and its
source disagree.

The engine is C++ and HLSL, so there is nothing to import and run. Each number
is read by parsing the file that declares it: the render-phase and weather
enums, the tonemapper array in the debug GUI, the README effect tables and
disabled list, the shader directory, the proxy headers, the CMake project and
the vcpkg manifest. Standard library only, no build required.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs" / "art" / "raw.art.json"
README = ROOT / "README.md"

WORDS = {
    1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six",
    7: "seven", 8: "eight", 9: "nine", 10: "ten", 11: "eleven", 12: "twelve",
    13: "thirteen", 14: "fourteen", 15: "fifteen", 16: "sixteen",
    17: "seventeen",
}


def _read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="ignore")


def _spec_fields() -> dict[str, str]:
    card = json.loads(SPEC.read_text(encoding="utf-8"))["cards"][0]
    return {row["key"]: row["value"] for row in card["fields"]}


def _enum_members(relative: str, name: str) -> list[str]:
    text = _read(relative)
    body = re.search(r"enum\s+class\s+" + re.escape(name) + r"[^{]*\{(.*?)\}",
                     text, re.S)
    members = []
    for part in body.group(1).split(","):
        token = part.split("//")[0].split("=")[0].strip()
        found = re.match(r"[A-Za-z_]\w*", token)
        if found:
            members.append(found.group(0))
    return members


def _md_table(head: str) -> list[list[str]]:
    """The rows of the markdown table under one `## head`, header dropped."""
    text = _read("README.md")
    rest = text[text.index(head) + len(head):]
    cut = rest.find("\n## ")
    block = rest if cut < 0 else rest[:cut]
    rows = []
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if all(cell and set(cell) <= set("-: ") for cell in cells):
            continue  # the |---|---| separator
        rows.append(cells)
    return rows[1:]


def _tone_names() -> list[str]:
    body = re.search(r"toneNames\[\]\s*=\s*\{(.*?)\}", _read("src/core/DebugGUI.cpp"),
                     re.S)
    return re.findall(r'"([^"]*)"', body.group(1))


def _tone_combo_count() -> int:
    match = re.search(r"Combo\([^)]*toneNames,\s*(\d+)\)",
                      _read("src/core/DebugGUI.cpp"))
    return int(match.group(1))


def _disabled_names() -> list[str]:
    blob = re.search(
        r"renderers removed from compilation pending individual polish:\s*(.+?)\.\s*\n",
        _read("README.md"), re.S)
    return [name.strip() for name in blob.group(1).replace("\n", " ").split(",")
            if name.strip()]


def _disabled_leading() -> int:
    return int(re.search(r"(\d+)\s+renderers removed", _read("README.md")).group(1))


def _cxx_standard() -> int:
    return int(re.search(r"CMAKE_CXX_STANDARD\s+(\d+)", _read("CMakeLists.txt")).group(1))


def _license() -> str:
    return json.loads(_read("vcpkg.json"))["license"]


def _perf_csvs() -> list[str]:
    return sorted(p.relative_to(ROOT).as_posix()
                  for p in ROOT.glob("**/*.csv") if ".git" not in p.parts)


def _verified_effects() -> tuple[int, int]:
    effects = _md_table("## Active Effects")
    verified = sum(1 for row in effects if "verified in-game" in row[-1].lower())
    return verified, len(effects)


def measure() -> dict[str, str]:
    verified, total_effects = _verified_effects()
    shaders = len(list((ROOT / "Shaders").glob("*.hlsl")))
    includes = len(list((ROOT / "Shaders").glob("*.hlsli")))
    proxies = len(list((ROOT / "src" / "d3d11_proxy").glob("Wrapped*.h")))
    tone_curves = len([t for t in _tone_names() if "linear" not in t.lower()])
    weather = [m for m in _enum_members("src/core/WeatherParameterManager.h",
                                        "WeatherCategory") if m != "Count"]
    csvs = _perf_csvs()
    return {
        "render phases": f"{WORDS[len(_enum_members('src/d3d11_proxy/RenderPhaseDetector.h', 'RenderPhase'))]} of them",
        "weather categories": f"{WORDS[len(weather)]} of them",
        "active effects": f"{WORDS[total_effects]} of them",
        "verified in-game": f"{WORDS[verified]} of {WORDS[total_effects]}",
        "post stages": f"{WORDS[len(_md_table('## Active Post-Processing'))]} of them",
        "tone curves": f"{WORDS[tone_curves]} of them",
        "disabled renderers": f"{WORDS[len(_disabled_names())]} of them",
        "proxy objects": f"{WORDS[proxies]} of them",
        "shader files": f"{shaders} on disk",
        "shared include": f"{WORDS[includes]} of them",
        "language standard": f"C++{_cxx_standard()}",
        "license": _license().replace("-or-later", ""),
        "measured frame time": "none in the tree" if not csvs else f"{len(csvs)} recorded",
    }


def check_card_rows_match_the_source() -> None:
    drawn = _spec_fields()
    measured = measure()
    assert set(drawn) == set(measured), (
        f"the card and the source disagree on which rows exist: "
        f"card-only {sorted(set(drawn) - set(measured))}, "
        f"source-only {sorted(set(measured) - set(drawn))}"
    )
    for key, value in measured.items():
        assert drawn[key] == value, (
            f"{key}: the card says {drawn[key]!r}, the source says {value!r}"
        )


def check_the_tonemapper_combo_matches_its_array() -> None:
    """The card counts tone curves off the array; the combo has to draw the
    same length, or the GUI shows fewer entries than the array holds."""
    names = _tone_names()
    assert _tone_combo_count() == len(names), (
        f"the combo draws {_tone_combo_count()} entries and the array holds {len(names)}"
    )
    assert any("linear" in name.lower() for name in names), (
        "no linear passthrough in the tonemapper array, so the curve count is off"
    )


def check_the_disabled_count_is_self_consistent() -> None:
    """The README states a number and then lists the renderers. The list has
    to be that long, or the card's count binds to one and contradicts the other."""
    names = _disabled_names()
    assert _disabled_leading() == len(names), (
        f"the README says {_disabled_leading()} renderers and lists {len(names)}"
    )


def check_the_shared_include_is_sharedraw() -> None:
    """The card draws one shared include. Its name is checked too, so the row
    cannot pass on a second .hlsli that is not the shared utility header."""
    includes = sorted(p.name for p in (ROOT / "Shaders").glob("*.hlsli"))
    assert includes == ["SharedRAW.hlsli"], (
        f"the shared HLSL include is not SharedRAW.hlsli alone: {includes}"
    )


def check_the_marked_row_is_still_an_honest_null() -> None:
    """The card says no measured per-pass frame time is committed here."""
    csvs = _perf_csvs()
    assert not csvs, (
        "a committed CSV capture now exists, so the marked row is stale and "
        f"the card should draw the count: {csvs}"
    )


CHECKS = [
    check_card_rows_match_the_source,
    check_the_tonemapper_combo_matches_its_array,
    check_the_disabled_count_is_self_consistent,
    check_the_shared_include_is_sharedraw,
    check_the_marked_row_is_still_an_honest_null,
]


def main() -> int:
    failures = 0
    for check in CHECKS:
        name = check.__name__.replace("check_", "")
        try:
            check()
        except AssertionError as problem:
            failures += 1
            print(f"FAIL facts.{name}")
            for line in str(problem).splitlines():
                print(f"       {line}")
        else:
            print(f"ok   facts.{name}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
