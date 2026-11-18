"""A table of a harness report: match statistics, coverage and GPU times per keyframe and step.

usage: python tools/harness/summary.py <report.json> [...]

Original work, MIT.
"""
from __future__ import annotations

import json
import sys


def main() -> None:
    for path in sys.argv[1:]:
        r = json.load(open(path))
        print(f"# {r['test']} at {r['resolution']}: ENB {r['install_after']['game_after']}, settings restored "
              f"{r['install_after']['settings_restored']}, quit cleanly {r.get('quit_cleanly')}, error {r.get('error')}")
        print(f"  GPU before: {r['gpu_before']['load']}; compute apps: {len(r['gpu_before']['compute_apps'])}")
        if "minimised" in r:
            print(f"  minimised: {r['minimised']}")
        print(f"  {'scene':16s} {'keyframe':9s} {'step':22s} {'rel':>7s} {'dE':>6s} {'dE<2':>6s} {'cover':>6s} {'pass ms':>8s} "
              f"{'ao':>6s} {'light':>6s} {'fog':>6s} {'bridge':>7s}")
        for x in r.get("results", []):
            rel = f"{x['rel_mean_abs']:.4f}" if "rel_mean_abs" in x else "-"
            de = f"{x['mean_dE76']:.3f}" if "mean_dE76" in x else "-"
            d2 = f"{x['dE_under_2']:.3f}" if "dE_under_2" in x else "-"
            cov = ("ok" if x["covered"] else "FAIL") if "covered" in x else "-"
            print(f"  {x['scene']:16s} {x['keyframe']:9s} {x['step']:22s} {rel:>7s} {de:>6s} {d2:>6s} {cov:>6s} {x['pass_ms']:8.3f} "
                  f"{x['ao_ms']:6.3f} {x['lighting_ms']:6.3f} {x['fog_ms']:6.3f} {x['bridge_ms']:7.3f}")


if __name__ == "__main__":
    main()
