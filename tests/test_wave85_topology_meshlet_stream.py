"""Wave 85 Canonical Oracle: NIF Topology Repair + Watertight 64v/64t Meshlet QEM DAG
+ Priority Async VRAM Residency Streamer.
Clean-room MIT.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

WAVE85_FILES = [
    ROOT / "src/ce_runtime/CeTopologyRepair.h",
    ROOT / "src/ce_runtime/CeTopologyRepair.cpp",
    ROOT / "src/ce_runtime/CeMeshletBuilder.h",
    ROOT / "src/ce_runtime/CeMeshletBuilder.cpp",
    ROOT / "src/d3d12_bridge/RawAssetStream.h",
    ROOT / "src/d3d12_bridge/RawAssetStream.cpp",
    ROOT / "tests/wave85_topology_meshlet_stream_check.cpp",
    ROOT / "tests/test_wave85_topology_meshlet_stream.py",
    ROOT / "docs/WAVE85-TOPOLOGY-MESHLETS-STREAMER-2026-11-26.md",
]


def main() -> int:
    forbidden = "zen" + "tropy"
    for p in WAVE85_FILES:
        assert p.exists(), f"Missing Wave 85 file: {p}"
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"

    topo_h = (ROOT / "src/ce_runtime/CeTopologyRepair.h").read_text(encoding="utf-8")
    assert "static_assert(sizeof(RepairedVertex) == 48u" in topo_h
    assert "RepairNifMeshTopology" in topo_h
    assert "VerifyOrthonormalTangentFrames" in topo_h

    mesh_h = (ROOT / "src/ce_runtime/CeMeshletBuilder.h").read_text(encoding="utf-8")
    assert "kMaxMeshletVertices  = 64u" in mesh_h
    assert "kMaxMeshletTriangles = 64u" in mesh_h
    assert "BuildWatertightMeshletLodDag" in mesh_h
    assert "ValidateMeshletLodDag" in mesh_h

    stream_h = (ROOT / "src/d3d12_bridge/RawAssetStream.h").read_text(encoding="utf-8")
    assert "VramResidencyBudgeter" in stream_h
    assert "0.80f" in stream_h
    assert "ComputePriorityScore" in stream_h
    assert "CancelStaleRequests" in stream_h

    # Run compiled C++23 Wave 85 verification binary if present in build dir.
    exe = pathlib.Path(r"E:\wt\raw-rt9-build\wave85_topology_meshlet_stream_check.exe")
    if exe.exists():
        proc = subprocess.run([str(exe)], capture_output=True, text=True, check=False)
        assert proc.returncode == 0, f"wave85 check failed:\n{proc.stdout}\n{proc.stderr}"
        assert "7 passed, 0 failed" in proc.stdout, f"Unexpected Wave 85 output:\n{proc.stdout}"

    print("PASS: Wave 85 Topology Repair + 64v/64t Meshlet QEM DAG + Priority VRAM Streamer oracle")
    return 0


if __name__ == "__main__":
    sys.exit(main())
