#!/usr/bin/env python3
"""RAW health receipt emitter.

This is a read-only host-side bridge: it summarizes RAW live artifacts, RAW eye
metrics, and optional GPU trace validation into a compact receipt other organs
can ingest. It does not launch the game, mutate RAW state, or certify authority.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import raw_eyes  # noqa: E402
import verify_runtime  # noqa: E402

RECEIPT_VERSION = "raw-health-v0"
ORGAN_ID = "eye.raw_rendering"
FAIL_STATUSES = {"STALE", "ABSENT"}
SOFT_STATUSES = {"PENDING", "GATED"}
COMMAND_VERIFY = "verify_runtime.verify"
COMMAND_WATCH = "raw_eyes.watch"
COMMAND_ATTRIBUTE = "raw_eyes.attribute"
COMMAND_GPU_TRACE = "gpu_trace_validator.build_payload"
COMMAND_SOURCE_STATE = "build_stamp.hash_sources"
SOURCE_MARKERS = (
    ("src", "dir"),
    ("Shaders", "dir"),
    ("CMakeLists.txt", "file"),
    ("tools/eyes/raw_eyes.py", "file"),
    ("tools/eyes/verify_runtime.py", "file"),
)
BUILD_ARTIFACTS = ("RAW.dll", "d3d11.dll")


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _runtime_summary(checks: list[dict[str, Any]]) -> dict[str, Any]:
    verified = sum(1 for check in checks if check.get("status") == "VERIFIED")
    soft = sum(1 for check in checks if check.get("status") in SOFT_STATUSES)
    attention = sum(1 for check in checks if check.get("status") in FAIL_STATUSES)
    status = "fail" if attention else "warn" if soft else "pass"
    return {
        "status": status,
        "verified": verified,
        "soft": soft,
        "attention": attention,
        "checks": checks,
    }


def _not_applicable_runtime(reason: str) -> dict[str, Any]:
    return {
        "status": "not-applicable",
        "verified": 0,
        "soft": 0,
        "attention": 0,
        "checks": [],
        "reason": reason,
    }


def _watch_status(report: dict[str, Any]) -> str:
    if report.get("frame_error"):
        return "warn"
    if report.get("frame_present"):
        return "pass"
    if int(report.get("samples") or 0) > 0:
        return "warn"
    return "unverified"


def _attribute_status(report: dict[str, Any] | None) -> str:
    if report is None:
        return "not-applicable"
    attribution = report.get("attribution")
    if attribution == ["no flagged artifacts"]:
        return "pass"
    return "warn" if attribution else "unverified"


def _find_workspace_root(start: Path) -> Path | None:
    for candidate in (start, *start.parents):
        if (candidate / ".repomap.toml").exists() or (
            candidate / "WORKSPACE-REPO-MAP.json"
        ).exists():
            return candidate
    return None


def _is_raw_source_root(path: Path) -> bool:
    return (
        (path / "CMakeLists.txt").is_file()
        and (path / "src").is_dir()
        and (path / "Shaders").is_dir()
        and (path / "tools" / "eyes").is_dir()
    )


def _marker_state(root: Path) -> dict[str, bool]:
    state: dict[str, bool] = {}
    for rel, kind in SOURCE_MARKERS:
        candidate = root / rel
        state[rel] = candidate.is_dir() if kind == "dir" else candidate.is_file()
    return state


def _source_hash(root: Path) -> tuple[str, int]:
    import build_stamp  # noqa: PLC0415

    return build_stamp.hash_sources([root / "src", root / "Shaders"])


def _source_manifest(root: Path, source_sha: str, source_files: int) -> dict[str, Any]:
    path = root / "tools" / "eyes" / "build_manifest.json"
    if not path.is_file():
        return {"present": False, "fresh": None}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except ValueError:
        return {"present": True, "fresh": False, "error": "invalid-json"}
    built_sha = str(data.get("source_sha") or "")
    return {
        "present": True,
        "fresh": built_sha == source_sha,
        "built_sha": built_sha[:12],
        "current_sha": source_sha[:12],
        "built_files": data.get("files"),
        "current_files": source_files,
    }


def _build_state(root: Path) -> dict[str, Any]:
    release = root / "build" / "Release"
    artifacts: dict[str, dict[str, Any]] = {}
    for name in BUILD_ARTIFACTS:
        path = release / name
        artifacts[name] = {
            "present": path.is_file(),
            "bytes": path.stat().st_size if path.is_file() else 0,
        }
    return {
        "built": all(item["present"] for item in artifacts.values()),
        "artifacts": artifacts,
    }


def _source_state(root: Path) -> dict[str, Any]:
    markers = _marker_state(root)
    essential_present = all(markers.values())
    source_sha, source_files = _source_hash(root) if essential_present else ("", 0)
    manifest = _source_manifest(root, source_sha, source_files)
    build = _build_state(root)
    status = _source_state_status(essential_present, build, manifest)
    return {
        "status": status,
        "essential_present": essential_present,
        "markers": markers,
        "source": {"sha256": source_sha, "files": source_files},
        "build": build,
        "manifest": manifest,
    }


def _source_state_status(
    essential_present: bool,
    build: dict[str, Any],
    manifest: dict[str, Any],
) -> str:
    if not essential_present:
        return "fail"
    if not build.get("built") or manifest.get("fresh") is False:
        return "warn"
    return "pass"


def _ensure_gpu_validator(gpu_validator_src: str | Path | None) -> None:
    candidates: list[Path] = []
    if gpu_validator_src is not None:
        candidates.append(Path(gpu_validator_src))
    root = _find_workspace_root(HERE)
    if root is not None:
        candidates.append(root / "public" / "gpu-trace-validator" / "src")
    for candidate in candidates:
        if candidate.exists() and str(candidate) not in sys.path:
            sys.path.insert(0, str(candidate))


def _gpu_trace_payload(
    trace_path: str | Path,
    *,
    expected_failures: int | None = None,
    gpu_validator_src: str | Path | None = None,
) -> dict[str, Any]:
    _ensure_gpu_validator(gpu_validator_src)
    from gpu_trace_validator.validator import (  # noqa: PLC0415
        DEFAULT_SCHEMA,
        build_payload,
        load_json,
        load_trace,
    )

    trace = load_trace(trace_path)
    schema_errors: list[str] = []
    try:
        from jsonschema import Draft202012Validator  # noqa: PLC0415

        validator = Draft202012Validator(load_json(DEFAULT_SCHEMA))
        schema_errors = [
            "; ".join(str(part) for part in error.absolute_path) + f": {error.message}"
            for error in validator.iter_errors(trace)
        ]
    except Exception as error:  # pragma: no cover - exercised when jsonschema is absent
        schema_errors = [str(error)]
    return build_payload(trace, schema_errors, expected_failures)


def _combined_status(
    runtime: dict[str, Any],
    watch: dict[str, Any],
    attribute: dict[str, Any] | None,
    gpu_trace: dict[str, Any] | None,
) -> str:
    statuses = [
        str(runtime["status"]),
        _watch_status(watch),
        _attribute_status(attribute),
    ]
    if gpu_trace is not None:
        statuses.append(str(gpu_trace.get("status") or "unverified"))
    if "fail" in statuses:
        return "fail"
    if "warn" in statuses:
        return "warn"
    if all(status in {"pass", "not-applicable"} for status in statuses):
        return "pass"
    return "unverified"


def _source_receipt(root: Path) -> dict[str, Any]:
    source_state = _source_state(root)
    status = "fail" if source_state["status"] == "fail" else "warn"
    return {
        "receipt_version": RECEIPT_VERSION,
        "generated_at": _utc_now(),
        "organ": ORGAN_ID,
        "mode": "source-state",
        "status": status,
        "summary": f"RAW source state {status}",
        "scope": "read-only",
        "sources": {
            "live": False,
            "captures": False,
            "gpu_trace": False,
            "source_state": True,
        },
        "runtime": _not_applicable_runtime("source root provided; no live runtime telemetry measured"),
        "eyes": {
            "watch_status": "not-applicable",
            "watch": None,
            "attribute_status": "not-applicable",
            "attribute": None,
        },
        "gpu_trace": None,
        "source_state": source_state,
        "commands": [COMMAND_SOURCE_STATE],
    }


def build_receipt(
    live_dir: str | Path,
    *,
    captures_dir: str | Path | None = None,
    trace_path: str | Path | None = None,
    expected_failures: int | None = None,
    gpu_validator_src: str | Path | None = None,
) -> dict[str, Any]:
    live_path = Path(live_dir)
    if _is_raw_source_root(live_path):
        return _source_receipt(live_path)

    commands = [COMMAND_VERIFY, COMMAND_WATCH]
    runtime = _runtime_summary(verify_runtime.verify(str(live_dir)))
    watch = raw_eyes.watch(str(live_dir))
    attribute = None
    if captures_dir is not None:
        attribute = raw_eyes.attribute(str(captures_dir))
        commands.append(COMMAND_ATTRIBUTE)

    gpu_trace = None
    if trace_path is not None:
        gpu_trace = _gpu_trace_payload(
            trace_path,
            expected_failures=expected_failures,
            gpu_validator_src=gpu_validator_src,
        )
        commands.append(COMMAND_GPU_TRACE)

    status = _combined_status(runtime, watch, attribute, gpu_trace)
    return {
        "receipt_version": RECEIPT_VERSION,
        "generated_at": _utc_now(),
        "organ": ORGAN_ID,
        "mode": "runtime-live",
        "status": status,
        "summary": f"RAW health {status}",
        "scope": "read-only",
        "sources": {
            "live": True,
            "captures": captures_dir is not None,
            "gpu_trace": trace_path is not None,
            "source_state": False,
        },
        "runtime": runtime,
        "eyes": {
            "watch_status": _watch_status(watch),
            "watch": watch,
            "attribute_status": _attribute_status(attribute),
            "attribute": attribute,
        },
        "gpu_trace": gpu_trace,
        "commands": commands,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="raw_health", description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    receipt = sub.add_parser("receipt", help="emit a RAW health receipt")
    receipt.add_argument("live_dir")
    receipt.add_argument("--captures")
    receipt.add_argument("--trace")
    receipt.add_argument("--expect-failures", type=int, default=None)
    receipt.add_argument("--gpu-validator-src")
    receipt.add_argument("--output")
    args = parser.parse_args(argv)

    if args.cmd == "receipt":
        payload = build_receipt(
            args.live_dir,
            captures_dir=args.captures,
            trace_path=args.trace,
            expected_failures=args.expect_failures,
            gpu_validator_src=args.gpu_validator_src,
        )
        text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
        if args.output:
            Path(args.output).write_text(text, encoding="utf-8")
        else:
            print(text, end="")
        return 1 if payload["status"] == "fail" else 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
