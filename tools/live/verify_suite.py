#!/usr/bin/env python3
"""RAW Multi-Condition Live Verification Suite (Skyrim SE 1.7.104.0 DX12 + SM 6.9 + DXR 1.2).
Captures 3 distinct live regimes (Natural, Heavy Rainstorm + Wet Puddles, Alpine Snowstorm)
with full GPU telemetry and outputs resized inspection JPEGs for automated visual audit.
"""
import json, os, subprocess, sys, glob
from PIL import Image

PY = r"C:\Users\Zain\AppData\Local\Programs\Python\Python312\python.exe"
REQ = r"E:\wt\raw-live\tools\live\req.py"
OUT_DIR = r"C:\Users\Zain\AppData\Local\Temp\opencode"

BASE_INI = [
    "Quality=4", "Tonemap=1", "ToneCurve=4", "ToneX1=4.5", "ToneX2=1",
    "ToneX3=1.35", "ToneX4=0.18", "WeatherDrive=1", "LutIndex=0",
    "LutStrength=0.85", "OverlayMask=263", "DlssMode=2"
]

CONDITIONS = [
    ("natural", [("console", ["command=player.setangle x 0"])], ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
    ("river_closeup", [("set_time", ["hour=14.0"]), ("console", ["command=player.setangle z 355"]), ("console", ["command=player.setangle x 22"])], ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
    ("overhead_sky_clear", [("set_weather", ["id=00081a"]), ("console", ["command=player.setangle z 90"]), ("console", ["command=player.setangle x -38"])], ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
    ("sunset", [("console", ["command=player.setangle x 0"]), ("set_time", ["hour=18.3"])], ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
    ("night", [("set_time", ["hour=23.0"])], ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
    ("rainstorm", [("set_time", ["hour=12.5"])], ["View=0", "WeatherDrive=0", "Wetness=0.95", "Puddles=0.85", "Rain=1.0", "Snow=0.0"]),
    ("snowstorm", [], ["View=0", "WeatherDrive=0", "Wetness=0.0", "Puddles=0.0", "Rain=0.0", "Snow=0.92"]),
    ("interior_inn", [("move", ["cell=RiverwoodSleepingGiantInn"]), ("console", ["command=player.setangle x 0"])], ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
    ("diag_gi", [("move", ["cell=Riverwood"]), ("console", ["command=player.setangle x 0"])], ["View=5", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"]),
]

def run_req(args, timeout=90):
    cmd = [PY, REQ, "--as", "lead-live", "--timeout", str(timeout)] + args
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr

def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else "suite"
    summary = []
    for name, pre_cmds, extra in CONDITIONS:
        for pc, pa in pre_cmds:
            run_req([pc] + pa, timeout=90)
        run_req(["reload_ini"] + BASE_INI + extra, timeout=30)
        cap_name = f"verify_{tag}_{name}"
        rc, out, err = run_req(["capture", "kind=keyframe", f"name={cap_name}", "steps=improved", "hold=16"], timeout=90)
        row = {}
        try:
            j = json.loads(out)
            rows = j.get("data", {}).get("rows", [])
            if rows:
                row = rows[0]
        except Exception:
            pass
        matches = sorted(glob.glob(rf"E:\raw-live\results\**\*verify_{tag}_{name}*improved.png", recursive=True),
                         key=os.path.getmtime, reverse=True)
        jpg = os.path.join(OUT_DIR, f"verify_{tag}_{name}.jpg")
        if matches:
            Image.open(matches[0]).convert("RGB").resize((1280, 720), Image.Resampling.BILINEAR).save(jpg, quality=92)
        summary.append({
            "condition": name,
            "covered": row.get("covered"),
            "lighting_ms": row.get("lighting_ms"),
            "pass_ms": row.get("pass_ms"),
            "rt_trace_ms": row.get("rt_trace_ms"),
            "rt_water_ms": row.get("rt_water_ms"),
            "rt_instances": row.get("rt_instances"),
            "jpg": jpg if matches else None,
        })
    # Restore natural dynamic weather drive & View=0 cleanly
    run_req(["reload_ini"] + BASE_INI + ["View=0", "WeatherDrive=1", "WeatherRain=-1", "WeatherSnow=-1"], timeout=30)
    print(json.dumps(summary, indent=2))

if __name__ == "__main__":
    main()
