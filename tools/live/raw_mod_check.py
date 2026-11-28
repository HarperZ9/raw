"""RAW DX12 Wave 86: Deterministic Headless Mod-Regression CLI (raw_mod_check.py).

Audits mod manifests before game launch for:
  1. SemVer v2.0 ABI version contract (0x00020000 major == 2)
  2. Cross-title adapter RTTI contract mismatches (SkyrimSE / Fallout4 / Starfield / SuperframeNative)
  3. FormID / hook-slot load-order collisions
  4. Cyclic frame-graph render-pass dependencies
  5. Per-mod CPU-microsecond and VRAM-byte hard budget breaches

Clean-room MIT.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Sequence

EXPECTED_ABI_MAJOR = 2
TITLE_CONTRACT_TAGS: dict[str, str] = {
    "SkyrimSE": "0x534B5952494D3137",
    "Fallout4": "0x464F344E47323032",
    "Starfield": "0x5354415246434532",
    "SuperframeNative": "0x535550455246524D",
}


def audit_mod_manifest(manifest: dict[str, Any]) -> dict[str, Any]:
    """Fail-closed static audit of a mod manifest dict."""
    errors: list[dict[str, Any]] = []
    title = str(manifest.get("title", "SkyrimSE"))
    expected_tag = TITLE_CONTRACT_TAGS.get(title)
    if expected_tag is None:
        errors.append({
            "code": "ADAPTER_CONTRACT_MISMATCH",
            "message": f"Unknown target title '{title}'.",
        })

    mods = manifest.get("mods", [])
    seen_form_ids: dict[str, str] = {}
    seen_debug_slots: dict[int, str] = {}
    pass_graph: dict[int, int | None] = {}

    for mod in mods:
        mod_id = mod.get("mod_id")
        mod_name = str(mod.get("name", f"mod_{mod_id}"))
        abi_ver = int(mod.get("abi_version", 0))
        abi_major = (abi_ver >> 16) & 0xFFFF
        if abi_major != EXPECTED_ABI_MAJOR:
            errors.append({
                "code": "ABI_MISMATCH",
                "mod_id": mod_id,
                "mod_name": mod_name,
                "message": f"Mod '{mod_name}' targets ABI major {abi_major} (expected {EXPECTED_ABI_MAJOR}).",
            })

        rtti_tag = mod.get("rtti_contract_tag")
        if expected_tag is not None and rtti_tag is not None and str(rtti_tag) != expected_tag:
            errors.append({
                "code": "ADAPTER_CONTRACT_MISMATCH",
                "mod_id": mod_id,
                "mod_name": mod_name,
                "message": f"Mod '{mod_name}' RTTI contract {rtti_tag} does not match {title} ({expected_tag}).",
            })

        cpu_us = int(mod.get("cpu_us", 0))
        hard_cpu_us = int(mod.get("hard_cpu_cap_us", 2000))
        vram_bytes = int(mod.get("vram_bytes", 0))
        hard_vram_bytes = int(mod.get("hard_vram_cap_bytes", 256 * 1024 * 1024))
        if cpu_us > hard_cpu_us or vram_bytes > hard_vram_bytes:
            errors.append({
                "code": "MOD_BUDGET_EXCEEDED",
                "mod_id": mod_id,
                "mod_name": mod_name,
                "message": (
                    f"Mod '{mod_name}' (id={mod_id}) exceeded budget: "
                    f"cpu={cpu_us}/{hard_cpu_us} us, vram={vram_bytes}/{hard_vram_bytes} B."
                ),
            })

        for fid in mod.get("form_id_overrides", []):
            fid_s = str(fid).upper()
            if fid_s in seen_form_ids:
                errors.append({
                    "code": "FORMID_CONFLICT",
                    "mod_id": mod_id,
                    "mod_name": mod_name,
                    "message": f"FormID {fid_s} collision between '{seen_form_ids[fid_s]}' and '{mod_name}'.",
                })
            else:
                seen_form_ids[fid_s] = mod_name

        for slot in mod.get("debug_view_slots", []):
            s_int = int(slot)
            if s_int < 100 or s_int > 255:
                errors.append({
                    "code": "INVALID_DEBUG_SLOT",
                    "mod_id": mod_id,
                    "mod_name": mod_name,
                    "message": f"Mod '{mod_name}' debug slot {s_int} violates reserved core range [0..8] (must be 100..255).",
                })
            elif s_int in seen_debug_slots:
                errors.append({
                    "code": "DEBUG_SLOT_COLLISION",
                    "mod_id": mod_id,
                    "mod_name": mod_name,
                    "message": f"Debug slot {s_int} collision between '{seen_debug_slots[s_int]}' and '{mod_name}'.",
                })
            else:
                seen_debug_slots[s_int] = mod_name

        for p in mod.get("render_passes", []):
            pid = int(p["pass_id"])
            dep = p.get("depends_on")
            pass_graph[pid] = int(dep) if dep is not None else None

    # Cycle audit on render pass dependencies.
    for start_pid in pass_graph:
        seen: set[int] = {start_pid}
        cur = pass_graph[start_pid]
        while cur is not None:
            if cur in seen:
                errors.append({
                    "code": "FRAMEGRAPH_CYCLE",
                    "message": f"Cyclic render-pass dependency involving pass_id {cur}.",
                })
                break
            seen.add(cur)
            cur = pass_graph.get(cur)

    ok = len(errors) == 0
    return {
        "schema": "raw.mod-check/1",
        "title": title,
        "mods_audited": len(mods),
        "ok": ok,
        "verdict": "PASS" if ok else "FAIL",
        "errors": errors,
    }


def run_self_test() -> dict[str, Any]:
    clean_manifest = {
        "title": "SkyrimSE",
        "mods": [
            {
                "mod_id": 101,
                "name": "NordicMistVolumetrics",
                "abi_version": 0x00020000,
                "rtti_contract_tag": TITLE_CONTRACT_TAGS["SkyrimSE"],
                "cpu_us": 180,
                "hard_cpu_cap_us": 1000,
                "vram_bytes": 16 * 1024 * 1024,
                "hard_vram_cap_bytes": 64 * 1024 * 1024,
                "form_id_overrides": ["0x00012345"],
                "debug_view_slots": [101],
                "render_passes": [{"pass_id": 10, "depends_on": None}],
            },
            {
                "mod_id": 102,
                "name": "WhiterunCrowdPatrols",
                "abi_version": 0x00020000,
                "rtti_contract_tag": TITLE_CONTRACT_TAGS["SkyrimSE"],
                "cpu_us": 240,
                "hard_cpu_cap_us": 1000,
                "vram_bytes": 8 * 1024 * 1024,
                "hard_vram_cap_bytes": 64 * 1024 * 1024,
                "form_id_overrides": ["0x00012346"],
                "debug_view_slots": [102],
                "render_passes": [{"pass_id": 11, "depends_on": 10}],
            },
        ],
    }
    clean_res = audit_mod_manifest(clean_manifest)
    assert clean_res["ok"], f"Clean manifest failed: {clean_res}"

    bad_manifest = {
        "title": "Fallout4",
        "mods": [
            {
                "mod_id": 201,
                "name": "BadCrossTitleMod",
                "abi_version": 0x00030000,
                "rtti_contract_tag": TITLE_CONTRACT_TAGS["SkyrimSE"],
                "cpu_us": 9500,
                "hard_cpu_cap_us": 1000,
                "form_id_overrides": ["0x00099999"],
                "render_passes": [{"pass_id": 1, "depends_on": 2}],
            },
            {
                "mod_id": 202,
                "name": "ConflictingMod",
                "abi_version": 0x00020000,
                "rtti_contract_tag": TITLE_CONTRACT_TAGS["Fallout4"],
                "form_id_overrides": ["0x00099999"],
                "render_passes": [{"pass_id": 2, "depends_on": 1}],
            },
        ],
    }
    bad_res = audit_mod_manifest(bad_manifest)
    codes = {e["code"] for e in bad_res["errors"]}
    for req in ("ABI_MISMATCH", "ADAPTER_CONTRACT_MISMATCH", "MOD_BUDGET_EXCEEDED", "FORMID_CONFLICT", "FRAMEGRAPH_CYCLE"):
        assert req in codes, f"Missing expected failure code {req}: {codes}"
    return {"self_test_ok": True, "clean": clean_res, "caught_codes": sorted(codes)}


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="RAW DX12 Wave 86 Headless Mod-Regression Auditor")
    ap.add_argument("--manifest", type=Path, required=False, help="JSON mod manifest file to audit")
    ap.add_argument("--self-test", action="store_true", help="Run built-in clean + conflict self-test")
    args = ap.parse_args(argv)

    if args.self_test or not args.manifest:
        res = run_self_test()
        print(json.dumps(res, indent=2))
        return 0

    data = json.loads(args.manifest.read_text(encoding="utf-8"))
    res = audit_mod_manifest(data)
    print(json.dumps(res, indent=2))
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
