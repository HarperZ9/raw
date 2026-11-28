"""Bind the drawn RAW v1.0 facts card directly to the source tree."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs" / "art" / "raw.art.json"

WORDS = {
    1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six",
    7: "seven", 8: "eight", 9: "nine", 10: "ten",
}


def _read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="ignore")


def _spec_fields() -> dict[str, str]:
    card = json.loads(SPEC.read_text(encoding="utf-8"))["cards"][0]
    return {row["key"]: row["value"] for row in card["fields"]}


def _cxx_standard() -> int:
    return int(re.search(r"CMAKE_CXX_STANDARD\s+(\d+)", _read("CMakeLists.txt")).group(1))


def _license() -> str:
    return json.loads(_read("vcpkg.json"))["license"]


def measure() -> dict[str, str]:
    dx12_hlsl = len(list((ROOT / "src" / "d3d12_bridge" / "shaders").glob("*.hlsl")))
    dx12_hlsli = len(list((ROOT / "src" / "d3d12_bridge" / "shaders").glob("*.hlsli")))
    proxies = len(list((ROOT / "src" / "d3d11_proxy").glob("Wrapped*.h")))
    cpp_files = len(list((ROOT / "src").rglob("*.cpp")))
    h_files = len(list((ROOT / "src").rglob("*.h")))
    abi_ok = (ROOT / "include" / "raw_mod_sdk_abi.h").exists() and (ROOT / "sdk" / "raw_mod_sdk.bld").exists()
    return {
        "dx12 hlsl shaders": f"{dx12_hlsl} on disk",
        "dx12 shared includes": f"{dx12_hlsli} on disk",
        "proxy objects": f"{WORDS.get(proxies, str(proxies))} of them",
        "cpp source files": f"{cpp_files} on disk",
        "header files": f"{h_files} on disk",
        "mod sdk": "ABI v2.0" if abi_ok else "missing",
        "language standard": f"C++{_cxx_standard()}",
        "license": _license().replace("-or-later", ""),
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


def check_mod_sdk_present() -> None:
    assert (ROOT / "include" / "raw_mod_sdk_abi.h").exists(), "missing include/raw_mod_sdk_abi.h"
    assert (ROOT / "sdk" / "raw_mod_sdk.bld").exists(), "missing sdk/raw_mod_sdk.bld"


CHECKS = [
    check_card_rows_match_the_source,
    check_mod_sdk_present,
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
