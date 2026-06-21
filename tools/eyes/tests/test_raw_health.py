from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

EYES = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[6]
GPU_VALIDATOR_SRC = ROOT / "public" / "gpu-trace-validator" / "src"
TRACE_PASS = ROOT / "public" / "gpu-trace-validator" / "tests" / "fixtures" / "trace_pass.json"

sys.path.insert(0, str(EYES))

import raw_health  # noqa: E402
import build_stamp  # noqa: E402


def _jsonl(path: Path, rows: list[dict[str, object]]) -> None:
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )


def _populate_live(live: Path) -> None:
    live.mkdir()
    _jsonl(
        live / "bindings.jsonl",
        [
            {
                "frame": 1,
                "pass": "gtao",
                "op": "Draw",
                "stage": "GS",
                "srv": {"t0": "depth"},
                "rtv": [],
                "dsv": None,
                "uav": [],
                "asserts": [],
            }
        ],
    )
    _jsonl(
        live / "restores.jsonl",
        [
            {
                "phase": 3,
                "assert": "STATE_NOT_RESTORED",
                "dirty": 1,
                "tid": 99,
                "fields": ["dsv"],
            }
        ],
    )
    _jsonl(
        live / "resources.jsonl",
        [{"op": "create", "type": "texture2d", "ptr": "0x1", "size": 4096}],
    )
    _jsonl(
        live / "shader_variants.jsonl",
        [
            {"kind": "shader_used", "ps_hash": "0x1", "vs_hash": "0x2"},
            {
                "hash": "0xa",
                "file": "gtao.hlsl",
                "entry": "main",
                "profile": "cs_5_0",
                "defines": "Q=1",
            },
        ],
    )
    _jsonl(
        live / "cb_meta.jsonl",
        [
            {
                "frame": 1,
                "ptr": "0xC",
                "bind": "PS b3",
                "size": 64,
                "has_nan": True,
                "has_inf": False,
                "nan_at": 0,
                "min": 0.0,
                "max": 1.0,
            }
        ],
    )
    _jsonl(
        live / "temporal.jsonl",
        [
            {
                "frame": 0,
                "buffer": "taa.history",
                "read_idx": 0,
                "write_idx": 0,
                "warmup": True,
            },
            {
                "frame": 1,
                "buffer": "taa.history",
                "read_idx": 1,
                "write_idx": 0,
                "warmup": False,
            },
        ],
    )
    _jsonl(
        live / "ranges.jsonl",
        [
            {
                "frame": 64,
                "pass": "gtao",
                "output": "gtao",
                "min": 0.0,
                "max": 1.0,
                "nan": 0,
                "inf": 0,
                "verdict": "ok",
            }
        ],
    )
    _jsonl(
        live / "metrics.jsonl",
        [{"frame": 1, "luma_mean": 0.42, "gpu_ms": 5.0, "fps": 60.0}],
    )
    Image.new("RGB", (8, 8), (80, 96, 112)).save(live / "frame.bmp")


def test_empty_live_dir_reports_attention_without_leaking_path(tmp_path: Path) -> None:
    live = tmp_path / "live"
    live.mkdir()

    receipt = raw_health.build_receipt(live)
    rendered = json.dumps(receipt, sort_keys=True)

    assert receipt["receipt_version"] == "raw-health-v0"
    assert receipt["organ"] == "eye.raw_rendering"
    assert receipt["mode"] == "runtime-live"
    assert receipt["scope"] == "read-only"
    assert receipt["status"] == "fail"
    assert receipt["runtime"]["attention"] >= 1
    assert str(live) not in rendered


def test_source_root_reports_source_state_without_runtime_failure(tmp_path: Path) -> None:
    root = tmp_path / "raw"
    (root / "src").mkdir(parents=True)
    (root / "Shaders").mkdir()
    (root / "tools" / "eyes").mkdir(parents=True)
    (root / "build" / "Release").mkdir(parents=True)
    (root / "CMakeLists.txt").write_text("project(RAW)\n", encoding="utf-8")
    (root / "README.md").write_text("# RAW\n", encoding="utf-8")
    (root / "src" / "main.cpp").write_text("// source\n", encoding="utf-8")
    (root / "Shaders" / "main.hlsl").write_text("// shader\n", encoding="utf-8")
    (root / "tools" / "eyes" / "raw_eyes.py").write_text("# eyes\n", encoding="utf-8")
    (root / "tools" / "eyes" / "verify_runtime.py").write_text("# verify\n", encoding="utf-8")
    source_sha, source_files = build_stamp.hash_sources([root / "src", root / "Shaders"])
    (root / "tools" / "eyes" / "build_manifest.json").write_text(
        json.dumps({"source_sha": source_sha, "files": source_files}),
        encoding="utf-8",
    )
    (root / "build" / "Release" / "RAW.dll").write_bytes(b"raw")
    (root / "build" / "Release" / "d3d11.dll").write_bytes(b"d3d11")

    receipt = raw_health.build_receipt(root)
    rendered = json.dumps(receipt, sort_keys=True)

    assert receipt["mode"] == "source-state"
    assert receipt["status"] == "warn"
    assert receipt["summary"] == "RAW source state warn"
    assert receipt["sources"]["source_state"] is True
    assert receipt["sources"]["live"] is False
    assert receipt["runtime"]["status"] == "not-applicable"
    assert receipt["source_state"]["status"] == "pass"
    assert receipt["source_state"]["essential_present"] is True
    assert receipt["source_state"]["build"]["built"] is True
    assert receipt["source_state"]["manifest"]["present"] is True
    assert str(root) not in rendered


def test_populated_live_and_passing_trace_emit_pass_receipt(tmp_path: Path) -> None:
    live = tmp_path / "live"
    _populate_live(live)

    receipt = raw_health.build_receipt(
        live,
        trace_path=TRACE_PASS,
        gpu_validator_src=GPU_VALIDATOR_SRC,
    )

    assert receipt["status"] == "pass"
    assert receipt["mode"] == "runtime-live"
    assert receipt["runtime"]["attention"] == 0
    assert receipt["runtime"]["verified"] >= 5
    assert receipt["eyes"]["watch"]["frame_present"] is True
    assert receipt["gpu_trace"]["status"] == "pass"
    assert receipt["gpu_trace"]["assertions"]["failure_count"] == 0
    assert "raw_eyes.watch" in receipt["commands"]
    assert "verify_runtime.verify" in receipt["commands"]
