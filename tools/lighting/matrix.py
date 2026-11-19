"""The all-times lighting and tone-map acceptance matrix for one harness run (tests/rt8matrix.txt).

usage: python -I matrix.py <run folder> [--sheet out.png] [--json out.json] [--candidate raw]

Every matrix cell is a harness scene named <place>_<time>_<weather> (riverwood_dawn_clear, inn_night_any, ...) with
the keyframe `m` (steps game and improved: RAW's lighting through the game's tone map) and `m_raw` (step improved,
RAW's lighting through RAW's tone map). A cell may also carry `m_ref` and `m_ref_raw`: the path-traced reference
through each tone map (RtDebug=6, View=7), and `m_irr` / `m_irr_raw`: RAW's received light (View=13) for FLIP.

Each frame is judged over the whole picture (sky, distance, characters, foliage, water and the interface), and by
bands: the top third, the middle third, the bottom third, and the compass strip.
  mean      mean of the sRGB channels, 0 to 255
  clipped   share of pixels with any channel at 250 or more
  crushed   share of pixels whose brightest channel is 4 or less
  detail    the standard deviation of luma, 0 to 255
  flip      FLIP between RAW's received light and the reference through the same tone map, where both exist

A candidate (RAW's lighting through RAW's tone map by default) matches or beats the game in a cell when all hold:
  mean within 20 % of the game's (the scene's intended brightness kept)
  clipped no more than the game's plus 0.5 points, crushed no more than the game's plus 0.5 points
  detail at least 0.95 of the game's
  flip no worse than the game tone map's, where a reference exists
Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

LUMA = np.array([0.2126, 0.7152, 0.0722])
COLUMNS = (("game", "m", "game"), ("improved", "m", "improved"), ("raw", "m_raw", "improved"))


def find(run: Path, scene: str, kf: str, step: str) -> Path | None:
    for ext in (".png", ".bmp"):
        p = run / f"{scene}_{kf}_{step}{ext}"
        if p.exists():
            return p
    return None


def rgb(p: Path) -> np.ndarray:
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float32)


def stats(a: np.ndarray) -> dict:
    y = a @ LUMA
    mx = a.max(axis=2)
    return {"mean": float(a.mean()), "clipped": float((mx >= 250).mean()), "crushed": float((mx <= 4).mean()), "detail": float(y.std())}


def bands(a: np.ndarray) -> dict:
    h, w = a.shape[:2]
    out = {"full": stats(a), "top": stats(a[: h // 3]), "middle": stats(a[h // 3: 2 * h // 3]), "bottom": stats(a[2 * h // 3:])}
    out["compass"] = stats(a[int(h * 0.02): int(h * 0.075), int(w * 0.35): int(w * 0.65)])
    return out


def flip_of(run: Path, scene: str, tm: str) -> float | None:
    suf = "" if tm == "game" else "_raw"
    ref, irr = find(run, scene, f"m_ref{suf}", "improved"), find(run, scene, f"m_irr{suf}", "improved")
    if not ref or not irr:
        return None
    import flip
    def small(p):
        img = flip.load(str(p), 1.0)
        h, w = img.shape[0] // 4 * 4, img.shape[1] // 4 * 4
        return img[:h, :w].reshape(h // 4, 4, w // 4, 4, 3).mean((1, 3))
    return float(flip.flip(small(ref), small(irr), 16.75).mean())


def verdict(game: dict, cand: dict, fg: float | None, fc: float | None) -> tuple[bool, list[str]]:
    g, c, why = game["full"], cand["full"], []
    if abs(c["mean"] - g["mean"]) > 0.2 * max(g["mean"], 1.0):
        why.append(f"mean {c['mean']:.1f} vs {g['mean']:.1f}")
    if c["clipped"] > g["clipped"] + 0.005:
        why.append(f"clipped {c['clipped'] * 100:.2f} % vs {g['clipped'] * 100:.2f} %")
    if c["crushed"] > g["crushed"] + 0.005:
        why.append(f"crushed {c['crushed'] * 100:.2f} % vs {g['crushed'] * 100:.2f} %")
    if c["detail"] < 0.95 * g["detail"]:
        why.append(f"detail {c['detail']:.1f} vs {g['detail']:.1f}")
    if fg is not None and fc is not None and fc > fg:
        why.append(f"flip {fc:.3f} vs {fg:.3f}")
    return not why, why


def scenes(run: Path) -> list[str]:
    seen = []
    for p in sorted(run.iterdir()):
        m = re.match(r"^(.+)_m_game\.(png|bmp)$", p.name)
        if m and m.group(1) not in seen:
            seen.append(m.group(1))
    return seen


def sheet(run: Path, rows: list[dict], out: Path, tw: int = 640) -> None:
    th = tw * 9 // 16
    img = Image.new("RGB", (tw * len(COLUMNS) + 260, th * len(rows) + 30), (16, 16, 16))
    d = ImageDraw.Draw(img)
    for k, (name, _, _) in enumerate(COLUMNS):
        d.text((260 + k * tw + 8, 8), {"game": "game", "improved": "RAW light, game tone map", "raw": "RAW light, RAW tone map"}[name], fill=(230, 230, 230))
    for r, row in enumerate(rows):
        y = 30 + r * th
        lines = [row["scene"]] + [f"{n}: {row[n]['full']['mean']:.0f} / {row[n]['full']['clipped'] * 100:.1f}% / {row[n]['full']['crushed'] * 100:.1f}% / {row[n]['full']['detail']:.1f}"
                                  for n, _, _ in COLUMNS if n in row]
        lines.append(("PASS" if row.get("pass") else "FAIL: " + "; ".join(row.get("why", []))[:80]) if "pass" in row else "")
        for i, t in enumerate(lines):
            d.text((8, y + 8 + i * 16), t, fill=(230, 230, 230) if i else (255, 210, 120))
        for k, (name, kf, step) in enumerate(COLUMNS):
            p = find(run, row["scene"], kf, step)
            if p:
                img.paste(Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS), (260 + k * tw, y))
    img.save(out)


def main() -> int:
    a = argparse.ArgumentParser()
    a.add_argument("run")
    a.add_argument("--sheet")
    a.add_argument("--json")
    a.add_argument("--candidate", default="raw", choices=("improved", "raw"))
    o = a.parse_args()
    run = Path(o.run)
    rows = []
    for s in scenes(run):
        row = {"scene": s}
        for name, kf, step in COLUMNS:
            p = find(run, s, kf, step)
            if p:
                row[name] = bands(rgb(p))
        row["flip_game"], row["flip_raw"] = flip_of(run, s, "game"), flip_of(run, s, "raw")
        if "game" in row and o.candidate in row:
            fc = row["flip_raw"] if o.candidate == "raw" else row["flip_game"]
            row["pass"], row["why"] = verdict(row["game"], row[o.candidate], row["flip_game"], fc)
        rows.append(row)
    print("scene\tcolumn\tmean\tclipped%\tcrushed%\tdetail\tflip\tverdict")
    for row in rows:
        for name, _, _ in COLUMNS:
            if name not in row:
                continue
            f = row[name]["full"]
            fl = row["flip_raw"] if name == "raw" else row["flip_game"] if name == "improved" else None
            v = ("PASS" if row.get("pass") else "FAIL " + "; ".join(row.get("why", []))) if name == o.candidate and "pass" in row else ""
            print(f"{row['scene']}\t{name}\t{f['mean']:.1f}\t{f['clipped'] * 100:.2f}\t{f['crushed'] * 100:.2f}\t{f['detail']:.2f}\t"
                  f"{'' if fl is None else f'{fl:.3f}'}\t{v}")
    n = sum(1 for r in rows if r.get("pass"))
    print(f"{n} of {sum(1 for r in rows if 'pass' in r)} cells pass ({o.candidate})")
    if o.json:
        Path(o.json).write_text(json.dumps(rows, indent=1))
    if o.sheet and rows:
        sheet(run, rows, Path(o.sheet))
    return 0


if __name__ == "__main__":
    sys.exit(main())
