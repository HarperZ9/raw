#!/usr/bin/env python3
"""raw_eyes.py - host-side perception for RAW rendering validation.

Turns frame / per-pass captures (PNG) into objective numeric metrics and
viewable diff heatmaps, so a coding assistant with no spatial perception can
*see* and *attribute* rendering artifacts instead of relying on verbal
description. No game required; runs anywhere numpy + Pillow are installed.

Subcommands:
  analyze   IMG                 -> JSON metrics for one capture
  compare   A B [--out diff.png] -> diff metrics + heatmap PNG
  attribute DIR                 -> per-pass table + artifact attribution
  selftest                      -> generate synthetic captures, verify metrics

Capture naming for `attribute` (case-insensitive substring match):
  scene/vanilla, ao/gtao, gi/ssgi, ssr, shadow/contact, sky/skylight,
  bloom, final/composite
"""
import sys, os, json, argparse, glob
import numpy as np
from PIL import Image

EPS = 1e-6

def load(path):
    """Load an image as float HxWx3 in [0,1]."""
    im = Image.open(path).convert("RGB")
    return np.asarray(im).astype(np.float32) / 255.0

def luma(a):
    """Rec.709 luminance."""
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722

def heat_rgb(t):
    """Map t in [0,1] to a black->red->yellow->white heat ramp (uint8 HxWx3)."""
    t = np.clip(t, 0, 1)
    r = np.clip(t * 3.0, 0, 1)
    g = np.clip(t * 3.0 - 1.0, 0, 1)
    b = np.clip(t * 3.0 - 2.0, 0, 1)
    return (np.stack([r, g, b], axis=-1) * 255.0).astype(np.uint8)

def cast_index(r, g, b):
    """>1 means green-biased, <1 means green-deficient (magenta)."""
    return float(g / max((r + b) / 2.0, EPS))

def flags(r, g, b, L):
    f = []
    ci = cast_index(r, g, b)
    if ci > 1.15: f.append("GREEN_CAST")
    if ci < 0.85: f.append("MAGENTA_CAST")
    if float((L < 0.004).mean()) > 0.60: f.append("MOSTLY_BLACK")
    if float((L > 0.996).mean()) > 0.30: f.append("BLOWN_OUT")
    if float(L.std()) < 0.01: f.append("FLAT_NO_SIGNAL")
    return f

def metrics(a):
    L = luma(a)
    flat = a.reshape(-1, 3)
    r, g, b = (float(flat[:, 0].mean()), float(flat[:, 1].mean()), float(flat[:, 2].mean()))
    hist, _ = np.histogram(L, bins=16, range=(0.0, 1.0))
    hist = (hist / max(L.size, 1)).round(4).tolist()
    gy, gx = np.gradient(L)
    edge = float(np.sqrt(gx * gx + gy * gy).mean())
    h, w = L.shape
    def q(y0, y1, x0, x1): return round(float(L[y0:y1, x0:x1].mean()), 4)
    quad = {"TL": q(0, h // 2, 0, w // 2), "TR": q(0, h // 2, w // 2, w),
            "BL": q(h // 2, h, 0, w // 2), "BR": q(h // 2, h, w // 2, w)}
    return {
        "dims": [int(w), int(h)],
        "mean_rgb": [round(r, 4), round(g, 4), round(b, 4)],
        "luma_mean": round(float(L.mean()), 4),
        "luma_min": round(float(L.min()), 4),
        "luma_max": round(float(L.max()), 4),
        "luma_std": round(float(L.std()), 4),
        "pct_black": round(float((L < 0.004).mean() * 100), 2),
        "pct_clipped": round(float((L > 0.996).mean() * 100), 2),
        "green_index": round(cast_index(r, g, b), 3),
        "edge_density": round(edge, 4),
        "luma_hist16": hist,
        "quadrant_luma": quad,
        "flags": flags(r, g, b, L),
    }

def compare(path_a, path_b, out_png=None):
    """Diff two captures. Writes a heatmap PNG and returns metrics."""
    a, b = load(path_a), load(path_b)
    if a.shape != b.shape:
        b_img = Image.fromarray((np.clip(b, 0, 1) * 255).astype(np.uint8)).resize(
            (a.shape[1], a.shape[0]))
        b = np.asarray(b_img).astype(np.float32) / 255.0
    d = np.abs(a - b)
    mse = float(((a - b) ** 2).mean())
    psnr = 99.0 if mse < 1e-12 else round(float(10.0 * np.log10(1.0 / mse)), 2)
    La, Lb = luma(a), luma(b)
    h, w = La.shape
    def qd(y0, y1, x0, x1):
        return round(float((Lb[y0:y1, x0:x1].mean() - La[y0:y1, x0:x1].mean())), 4)
    quad_delta = {"TL": qd(0, h // 2, 0, w // 2), "TR": qd(0, h // 2, w // 2, w),
                  "BL": qd(h // 2, h, 0, w // 2), "BR": qd(h // 2, h, w // 2, w)}
    mag = d.mean(axis=2)
    norm = mag / max(float(mag.max()), EPS)
    if out_png:
        Image.fromarray(heat_rgb(norm)).save(out_png)
    return {
        "a": os.path.basename(path_a), "b": os.path.basename(path_b),
        "mean_abs_diff": round(float(d.mean()), 4),
        "max_abs_diff": round(float(d.max()), 4),
        "psnr_db": psnr,
        "luma_delta_mean": round(float(Lb.mean() - La.mean()), 4),
        "changed_pixels_pct": round(float((mag > 0.02).mean() * 100), 2),
        "quadrant_luma_delta": quad_delta,
        "diff_png": out_png,
        "verdict": "identical" if mse < 1e-9 else (
            "subtle" if d.mean() < 0.02 else "significant"),
    }

PASS_KEYS = [("scene", ["scene", "vanilla"]), ("ao", ["ao", "gtao"]),
             ("gi", ["gi", "ssgi"]), ("ssr", ["ssr"]),
             ("shadow", ["shadow", "contact"]), ("skylight", ["sky", "skylight"]),
             ("bloom", ["bloom"]), ("final", ["final", "composite"])]

def classify(name):
    low = name.lower()
    for key, subs in PASS_KEYS:
        if any(s in low for s in subs):
            return key
    return "other"

def attribute(folder):
    """Analyze every capture and attribute EVERY visual-bug class to its pass.

    Covers the full taxonomy: colour cast, no-output, clipping, flatness, noise,
    fireflies, banding, haloing, blur. The point: any visual bug -> a named pass."""
    exts = ("*.png", "*.bmp", "*.jpg", "*.jpeg")
    paths = sorted(p for e in exts for p in glob.glob(os.path.join(folder, e)))
    # one human-readable phrase per artifact flag, pulling the localizing number
    MSG = {
        "GREEN_CAST":     lambda m, q: "GREEN_CAST (green_index=%.2f)" % m["green_index"],
        "MAGENTA_CAST":   lambda m, q: "MAGENTA_CAST (green_index=%.2f)" % m["green_index"],
        "MOSTLY_BLACK":   lambda m, q: "%.0f%%-black, no output" % m["pct_black"],
        "BLOWN_OUT":      lambda m, q: "%.0f%% clipped white" % m["pct_clipped"],
        "FLAT_NO_SIGNAL": lambda m, q: "flat, no signal (luma_std=%.3f)" % m["luma_std"],
        "NOISY":          lambda m, q: "NOISY (noise_rms=%.3f)" % q["noise_rms"],
        "FIREFLIES":      lambda m, q: "FIREFLIES (%.2f%% bright outliers)" % q["fireflies_pct"],
        "BANDING":        lambda m, q: "BANDING (index=%.2f)" % q["banding_index"],
        "HALOING":        lambda m, q: "HALOING (index=%.3f)" % q["halo_index"],
        "BLURRY":         lambda m, q: "BLURRY (sharpness=%.5f)" % q["sharpness"],
    }
    passes, summary = {}, []
    for path in paths:
        key = classify(os.path.basename(path))
        arr = load(path)
        m = metrics(arr); q = quality(arr)
        passes[os.path.basename(path)] = {"pass": key, **m, "quality": q}
        for fl in (m["flags"] + q["quality_flags"]):
            desc = MSG.get(fl, lambda m, q: fl)(m, q)
            summary.append("%s: %s -> pass %s" % (os.path.basename(path), desc, key.upper()))
    return {"passes": passes, "attribution": summary or ["no flagged artifacts"]}


def _synth_scene(w=256, h=256):
    """Deterministic synthetic 'scene': gradient sky + ground + a bright sun."""
    rng = np.random.default_rng(0)
    y = np.linspace(0, 1, h)[:, None]
    x = np.linspace(0, 1, w)[None, :]
    sky = np.stack([0.30 + 0.20 * y, 0.45 + 0.25 * y, 0.70 + 0.20 * y], -1) * np.ones((h, w, 1))
    ground = np.stack([0.18 + 0.10 * x, 0.14 + 0.08 * x, 0.10 + 0.05 * x], -1) * np.ones((h, w, 1))
    horizon = (np.arange(h)[:, None] > h * 0.55)
    img = np.where(horizon[..., None], ground, sky)
    cx, cy = int(w * 0.7), int(h * 0.25)
    yy, xx = np.mgrid[0:h, 0:w]
    sun = np.exp(-(((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * (w * 0.04) ** 2)))
    img = np.clip(img + sun[..., None] * 0.9, 0, 1)
    img += (rng.random((h, w, 3)) - 0.5) * 0.01
    return np.clip(img, 0, 1).astype(np.float32)

def _save(arr, path):
    Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8)).save(path)

def selftest(outdir):
    os.makedirs(outdir, exist_ok=True)
    scene = _synth_scene()
    green = np.clip(scene * np.array([0.9, 1.5, 0.9]), 0, 1)      # SSGI-style green cast
    black = np.zeros_like(scene)                                  # dead pass output
    blown = np.ones_like(scene)                                   # blown highlights
    cases = {"scene": scene, "gi_green": green, "ssr_black": black, "final_blown": blown}
    for name, arr in cases.items():
        _save(arr, os.path.join(outdir, name + ".png"))
    checks = []
    def expect(cond, label):
        checks.append((bool(cond), label))
    m_scene = metrics(scene); m_green = metrics(green)
    m_black = metrics(black); m_blown = metrics(blown)
    expect("GREEN_CAST" not in m_scene["flags"], "scene has no false green cast")
    expect("GREEN_CAST" in m_green["flags"], "green capture flagged GREEN_CAST")
    expect(m_green["green_index"] > m_scene["green_index"] + 0.2, "green_index rises on tint")
    expect("MOSTLY_BLACK" in m_black["flags"], "black capture flagged MOSTLY_BLACK")
    expect("BLOWN_OUT" in m_blown["flags"], "blown capture flagged BLOWN_OUT")
    cmp = compare(os.path.join(outdir, "scene.png"), os.path.join(outdir, "gi_green.png"),
                  os.path.join(outdir, "diff_scene_vs_green.png"))
    expect(cmp["verdict"] == "significant", "scene vs green diff is significant")
    expect(cmp["luma_delta_mean"] > 0, "green tint raises luma")
    attr = attribute(outdir)
    expect(any("GREEN" in s for s in attr["attribution"]), "attribution names green source")
    # quality discernment
    yy, xx = np.mgrid[0:256, 0:256]
    smooth_ramp = np.repeat((xx / 255.0)[..., None], 3, axis=2).astype(np.float32)
    banded = (np.round(smooth_ramp * 12) / 12).astype(np.float32)   # 12-step staircase
    rng2 = np.random.default_rng(1)
    noisy = np.clip(0.5 + (rng2.random((256, 256, 3)) - 0.5) * 0.5, 0, 1).astype(np.float32)
    flies = scene.copy(); flies[rng2.integers(0,256,40), rng2.integers(0,256,40)] = 1.0
    _save(banded, os.path.join(outdir, "q_banded.png"))
    _save(noisy, os.path.join(outdir, "q_noisy.png"))
    _save(flies, os.path.join(outdir, "q_fireflies.png"))
    expect("BANDING" in quality(banded)["quality_flags"], "banding detected on staircase ramp")
    expect("NOISY" in quality(noisy)["quality_flags"], "noise detected on random field")
    expect(quality(flies)["fireflies_pct"] > quality(scene)["fireflies_pct"], "fireflies raise firefly metric")
    expect(quality(smooth_ramp)["banding_index"] < quality(banded)["banding_index"], "smooth ramp less banded than staircase")
    # --- Tier 3.6: resources.jsonl create-side VRAM watch --------------
    with open(os.path.join(outdir, "resources.jsonl"), "w", encoding="utf-8") as rf:
        for rec in [{"op": "create", "type": "buffer", "ptr": "0x1", "size": 1024},
                    {"op": "create", "type": "buffer", "ptr": "0x2", "size": 2048},
                    {"op": "create", "type": "texture2d", "ptr": "0x3", "size": 4194304}]:
            rf.write(json.dumps(rec) + "\n")
    res = read_resources(outdir)
    expect(res["creates"] == 3, "3.6: resources reader counts 3 creates")
    expect(res["total_bytes"] == 1024 + 2048 + 4194304, "3.6: total bytes summed")
    expect(res["by_type"]["buffer"]["count"] == 2, "3.6: per-type count (2 buffers)")
    # --- Tier 3.2: shader_variants.jsonl permutation reader ------------
    with open(os.path.join(outdir, "shader_variants.jsonl"), "w", encoding="utf-8") as sf:
        for rec in [{"hash": "0xaa", "file": "ssr.hlsl", "entry": "main", "profile": "cs_5_0", "defines": "QUALITY=1", "ndefs": 1, "succeeded": True},
                    {"hash": "0xbb", "file": "ssr.hlsl", "entry": "main", "profile": "cs_5_0", "defines": "QUALITY=2", "ndefs": 1, "succeeded": True},
                    {"kind": "shader_used", "ps_hash": "0x123", "vs_hash": "0x456"}]:
            sf.write(json.dumps(rec) + "\n")
    sv = read_shader_variants(outdir)
    expect(sv["compiled"] == 2, "3.2: 2 compile-time variant records")
    expect(sv["distinct_shaders"] == 1, "3.2: grouped to 1 shader by file/entry/profile")
    expect(sv["bound_shaders"] == 1, "3.2: 1 bound shader_used hash")
    expect("QUALITY=1" in sv["permutations"][0] and "QUALITY=2" in sv["permutations"][0], "3.2: both permutations listed")
    # --- Tier 1.2-p3: cb_meta.jsonl slot/stage bind attribution --------
    with open(os.path.join(outdir, "cb_meta.jsonl"), "w", encoding="utf-8") as cf:
        cf.write(json.dumps({"frame": 5, "ptr": "0xCB", "bind": "PS b3", "size": 64,
                             "has_nan": True, "has_inf": False, "nan_at": 2,
                             "min": 0.0, "max": 1.0}) + "\n")
    cbd = read_cb_dump(outdir)
    expect(cbd["nan_inf"] == 1 and not cbd["clean"], "1.2-p3: cb_meta NaN record read")
    expect(any("PS b3" in s for s in cbd["findings"]), "1.2-p3: CB slot/stage bind surfaced")
    # --- Tier 1.3: ranges reader + sign-flip reduction math mirror -----
    with open(os.path.join(outdir, "ranges.jsonl"), "w", encoding="utf-8") as rgf:
        for rec in [{"frame": 64, "pass": "gtao", "output": "gtao", "min": 0.0, "max": 1.0, "nan": 0, "inf": 0, "verdict": "ok"},
                    {"frame": 70, "pass": "ssr", "output": "ssr", "min": -1.0, "max": 2.0, "nan": 3, "inf": 1, "verdict": "CORRUPT"}]:
            rgf.write(json.dumps(rec) + "\n")
    rg = read_ranges(outdir)
    expect(rg["outputs_seen"] == 2, "1.3: ranges reader sees 2 outputs")
    expect(rg["corrupt"] == 1 and not rg["ranges_ok"], "1.3: corrupt range flagged")
    expect(any("ssr" in f and "nan=3" in f for f in rg["findings"]), "1.3: corrupt finding names pass + nan count")
    # mirror the exact order-preserving sign-flip the HLSL/C++ use, prove it round-trips
    import struct as _st
    def _flip(v):
        b = _st.unpack("<I", _st.pack("<f", v))[0]
        return (~b) & 0xFFFFFFFF if (b & 0x80000000) else (b | 0x80000000)
    def _unflip(o):
        b = (o & 0x7FFFFFFF) if (o & 0x80000000) else (~o) & 0xFFFFFFFF
        return _st.unpack("<f", _st.pack("<I", b))[0]
    vals = [-42.0, -3.5, -0.0, 0.0, 1.0, 2.75, 100.0]
    omin, omax = 0xFFFFFFFF, 0
    for v in vals:
        o = _flip(v); omin = min(omin, o); omax = max(omax, o)
    expect(abs(_unflip(omin) - min(vals)) < 1e-6, "1.3: sign-flip min round-trips to true min")
    expect(abs(_unflip(omax) - max(vals)) < 1e-6, "1.3: sign-flip max round-trips to true max")
    # --- reader coverage: read_compile_log (0.2) + read_texture_inspect (1.1) + watch (0.1) ---
    import tempfile as _tfr
    with _tfr.TemporaryDirectory() as wtd:
        with open(os.path.join(wtd, "compile_errors.jsonl"), "w", encoding="utf-8") as f:
            print(json.dumps({"file": "ok.hlsl", "succeeded": True}), file=f)
            print(json.dumps({"file": "bad.hlsl", "succeeded": False,
                              "errors": [{"line": 12, "col": 3, "code": "X3000", "msg": "syntax error"}]}), file=f)
        cl = read_compile_log(wtd)
        expect((not cl["all_built"]) and cl["failing"] == 1, "0.2: read_compile_log flags a failed compile")
        expect(any("bad.hlsl" in e and "X3000" in e for e in cl["errors"]), "0.2: compile error line formatted")
        with open(os.path.join(wtd, "texture_inspect.jsonl"), "w", encoding="utf-8") as f:
            print(json.dumps({"file": "frame0001_gtao", "has_nan": False, "has_inf": False}), file=f)
            print(json.dumps({"file": "frame0001_ssr", "has_nan": True, "has_inf": False,
                              "nan_count": 5, "first_nan": [10, 20]}), file=f)
        ti = read_texture_inspect(wtd)
        expect((not ti["numeric_ok"]) and ti["corrupt"] == 1, "1.1: read_texture_inspect flags a NaN pass")
        expect(any("ssr" in fnd for fnd in ti["findings"]), "1.1: texture_inspect finding names the pass")
        with open(os.path.join(wtd, "metrics.jsonl"), "w", encoding="utf-8") as f:
            for k in range(4):
                print(json.dumps({"luma_mean": 0.4 + k * 0.01, "gpu_ms": 5.0, "fps": 60}), file=f)
        wr = watch(wtd, tail=10)
        expect(wr["samples"] == 4 and "luma_mean" in wr["avg"], "0.1: watch reads the metrics trend")
        expect("green_index" not in wr["avg"], "0.1: watch omits never-emitted metrics (no false trend)")
    # --- reader coverage: temporal() frame-to-frame flicker metric -----
    _ts = os.path.join(outdir, "_t_static.png"); _save(scene, _ts)
    _tk = os.path.join(outdir, "_t_flick.png"); _save(green, _tk)
    expect(temporal(_ts, _ts)["verdict"] == "stable", "temporal: identical frames -> stable")
    expect(temporal(_ts, _tk)["flicker_mean"] > temporal(_ts, _ts)["flicker_mean"],
           "temporal: scene-vs-green flickers more than scene-vs-itself")
    ok = all(c for c, _ in checks)
    for c, label in checks:
        print(("PASS " if c else "FAIL ") + label)
    print("\nselftest: %d/%d checks passed" % (sum(c for c, _ in checks), len(checks)))
    print("artifacts written to: " + outdir)
    print("\nattribution on synthetic set:")
    for s in attr["attribution"]:
        print("  - " + s)
    return 0 if ok else 1

def main(argv=None):
    ap = argparse.ArgumentParser(prog="raw_eyes", description="host-side eyes for RAW")
    sub = ap.add_subparsers(dest="cmd", required=True)
    pa = sub.add_parser("analyze"); pa.add_argument("img")
    pc = sub.add_parser("compare"); pc.add_argument("a"); pc.add_argument("b")
    pc.add_argument("--out", default=None)
    pt = sub.add_parser("attribute"); pt.add_argument("dir")
    pq = sub.add_parser("quality"); pq.add_argument("img")
    pm = sub.add_parser("temporal"); pm.add_argument("prev"); pm.add_argument("cur")
    pw = sub.add_parser("watch"); pw.add_argument("live_dir"); pw.add_argument("--tail", type=int, default=30)
    pcl = sub.add_parser("compile"); pcl.add_argument("live_dir")
    pn = sub.add_parser("numeric"); pn.add_argument("live_dir")
    pcb = sub.add_parser("cb"); pcb.add_argument("live_dir")
    prs = sub.add_parser("resources"); prs.add_argument("live_dir")
    psv = sub.add_parser("shader-variants"); psv.add_argument("live_dir")
    prg = sub.add_parser("ranges"); prg.add_argument("live_dir")
    pctl = sub.add_parser("control"); pctl.add_argument("live_dir")
    pctl.add_argument("--on", nargs="*", default=[]); pctl.add_argument("--off", nargs="*", default=[])
    pctl.add_argument("--set", dest="sets", nargs="*", default=[])
    pctl.add_argument("--reload", action="store_true"); pctl.add_argument("--reset", action="store_true")
    ps = sub.add_parser("selftest"); ps.add_argument("--out", default=None)
    args = ap.parse_args(argv)
    if args.cmd == "analyze":
        a = load(args.img)
        print(json.dumps({**metrics(a), "quality": quality(a)}, indent=2))
    elif args.cmd == "quality":
        print(json.dumps(quality(load(args.img)), indent=2))
    elif args.cmd == "temporal":
        print(json.dumps(temporal(args.prev, args.cur), indent=2))
    elif args.cmd == "watch":
        print(json.dumps(watch(args.live_dir, args.tail), indent=2))
    elif args.cmd == "compile":
        print(json.dumps(read_compile_log(args.live_dir), indent=2))
    elif args.cmd == "numeric":
        print(json.dumps(read_texture_inspect(args.live_dir), indent=2))
    elif args.cmd == "cb":
        print(json.dumps(read_cb_dump(args.live_dir), indent=2))
    elif args.cmd == "resources":
        print(json.dumps(read_resources(args.live_dir), indent=2))
    elif args.cmd == "shader-variants":
        print(json.dumps(read_shader_variants(args.live_dir), indent=2))
    elif args.cmd == "ranges":
        print(json.dumps(read_ranges(args.live_dir), indent=2))
    elif args.cmd == "control":
        print(json.dumps(write_control(args.live_dir, args.on, args.off, args.sets,
                                       args.reload, args.reset), indent=2))
    elif args.cmd == "compare":
        print(json.dumps(compare(args.a, args.b, args.out), indent=2))
    elif args.cmd == "attribute":
        print(json.dumps(attribute(args.dir), indent=2))
    elif args.cmd == "selftest":
        outdir = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), "_selftest")
        return selftest(outdir)
    return 0


# ---------------------------------------------------------------------------
# Shader-quality discernment: fidelity metrics beyond simple anomaly flags.
# ---------------------------------------------------------------------------

def _box(L, k):
    """Fast k x k box blur via summed-area table (edge-padded)."""
    pad = k // 2
    Lp = np.pad(L, pad, mode="edge")
    c = np.cumsum(np.cumsum(Lp, 0), 1)
    c = np.pad(c, ((1, 0), (1, 0)))
    S = c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]
    return S / (k * k)

def quality(a):
    """Per-frame fidelity metrics — the discernment layer.

    Pairs with the assistant reading the frame itself: numbers localize the
    defect, the eyes confirm it perceptually."""
    L = luma(a)
    blur = _box(L, 5)
    hp = L - blur                                  # high-pass: detail + noise
    gy, gx = np.gradient(blur)
    grad = np.sqrt(gx * gx + gy * gy)
    flat = grad < np.percentile(grad, 40)          # smooth regions
    noise = float(np.sqrt((hp[flat] ** 2).mean())) if flat.any() else 0.0
    fire = (L > blur + 0.25) & (L > 0.6)           # isolated bright outliers
    fireflies = float(fire.mean() * 100.0)
    # banding: in smooth bright regions, staircasing => runs of exactly-flat gradient
    smooth = (blur > 0.4) & (grad < np.percentile(grad, 60))
    banding = float((grad[smooth] < 1e-4).mean()) if smooth.sum() > 100 else 0.0
    lap = _box(L, 3) - L                            # laplacian-ish
    sharpness = float(lap.var())
    edge = grad > np.percentile(grad, 95)          # strong edges
    halo = float(np.abs(hp[edge]).mean()) if edge.any() else 0.0
    q = {
        "noise_rms": round(noise, 4),
        "fireflies_pct": round(fireflies, 3),
        "banding_index": round(banding, 3),
        "sharpness": round(sharpness, 5),
        "halo_index": round(halo, 4),
        "contrast": round(float(L.std()), 4),
        "dynamic_range": round(float(np.percentile(L, 99) - np.percentile(L, 1)), 4),
    }
    qf = []
    if noise > 0.03: qf.append("NOISY")
    if fireflies > 0.10: qf.append("FIREFLIES")
    if banding > 0.55: qf.append("BANDING")
    if halo > 0.06: qf.append("HALOING")
    if sharpness < 1e-4: qf.append("BLURRY")
    q["quality_flags"] = qf
    return q

def temporal(prev_path, cur_path):
    """Frame-to-frame stability — flicker, ghosting, convergence."""
    a, b = load(prev_path), load(cur_path)
    if a.shape != b.shape:
        b = np.asarray(Image.fromarray((np.clip(b, 0, 1) * 255).astype(np.uint8))
                       .resize((a.shape[1], a.shape[0]))).astype(np.float32) / 255.0
    d = np.abs(luma(b) - luma(a))
    static = d < 0.05
    static_flicker = float(d[static].mean()) if static.any() else 0.0
    return {
        "flicker_mean": round(float(d.mean()), 4),
        "static_flicker": round(static_flicker, 5),
        "moving_pct": round(float((d > 0.1).mean() * 100), 2),
        "verdict": "stable" if static_flicker < 0.01 else "flickering/ghosting",
    }

# ---------------------------------------------------------------------------
# Live bridge: RAW writes telemetry every frame; we read it on demand (always
# fresh) and write a control file RAW polls — so we tune while the game runs,
# no alt-tab, no copy-paste.
#
#   <live>/metrics.jsonl   RAW appends one JSON object per emitted frame
#   <live>/frame.bmp       RAW overwrites a downsampled latest frame (~1/sec)
#   <live>/control.json    we write; RAW polls mtime and applies
# ---------------------------------------------------------------------------

def _tail_jsonl(path, n):
    if not os.path.exists(path):
        return []
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()[-n:]
    out = []
    for ln in lines:
        ln = ln.strip()
        if ln:
            try: out.append(json.loads(ln))
            except Exception: pass
    return out

def watch(live_dir, tail=30):
    """Snapshot of the live game state: recent telemetry trend + latest frame."""
    rows = _tail_jsonl(os.path.join(live_dir, "metrics.jsonl"), tail)
    frame = os.path.join(live_dir, "frame.bmp")
    report = {"samples": len(rows), "frame_present": os.path.exists(frame)}
    if rows:
        last = rows[-1]
        def trend(k):
            vals = [r[k] for r in rows if k in r]
            return round(sum(vals) / len(vals), 4) if vals else None
        report["latest"] = last
        report["avg"] = {k: trend(k) for k in ("luma_mean", "green_index",
                          "gpu_ms", "fps", "noise_rms", "fireflies_pct") if any(k in r for r in rows)}
        # stability of luma across the window = is the image flickering live?
        lm = [r["luma_mean"] for r in rows if "luma_mean" in r]
        if len(lm) > 2:
            report["luma_jitter"] = round(float(np.std(lm)), 4)
    if os.path.exists(frame):
        try:
            a = load(frame)
            report["frame_metrics"] = {**metrics(a), **quality(a)}
        except Exception as e:
            report["frame_error"] = str(e)
    return report

def _read_ini(path):
    d = {}
    if os.path.exists(path):
        for ln in open(path, encoding="utf-8", errors="ignore"):
            ln = ln.strip()
            if ln and not ln.startswith("#") and "=" in ln:
                k, _, v = ln.partition("=")
                d[k.strip()] = v.strip()
    return d

def write_control(live_dir, on=None, off=None, sets=None, reload=False, reset=False):
    """Write/merge a flat control.ini that RAW polls and applies next frame.

    Schema (flat key=value): effect toggles `<effect>=0|1`, params `<effect>.<p>=<f>`,
    plus `seq`, `reload=1`, `reset=1`. Flat so RAW parses it with its existing INI code."""
    os.makedirs(live_dir, exist_ok=True)
    path = os.path.join(live_dir, "control.ini")
    cur = _read_ini(path)
    seq = int(cur.get("seq", "0")) + 1
    for e in (off or []): cur[e] = "0"
    for e in (on or []):  cur[e] = "1"
    for kv in (sets or []):
        k, _, v = kv.partition("=")
        cur[k.strip()] = v.strip()
    cur["seq"] = str(seq)
    cur["reload"] = "1" if reload else "0"
    cur["reset"] = "1" if reset else "0"
    with open(path, "w", encoding="utf-8") as fh:
        print("# raw control - written by raw_eyes; RAW polls this each frame", file=fh)
        for k in sorted(cur):
            print("%s=%s" % (k, cur[k]), file=fh)
    return cur

def read_compile_log(live_dir):
    """live/compile_errors.jsonl -> latest compile status per shader + failures.

    The write-side oracle for 'did the shader build' (Tier 0.2). Last attempt per
    file wins; a shader is failing if its most recent compile did not succeed."""
    rows = _tail_jsonl(os.path.join(live_dir, "compile_errors.jsonl"), 4000)
    latest = {}
    for r in rows:
        latest[r.get("file", "?")] = r
    failing = [r for r in latest.values() if not r.get("succeeded", True)]
    out = {"shaders_seen": len(latest), "failing": len(failing),
           "all_built": len(failing) == 0, "errors": []}
    for r in sorted(failing, key=lambda x: x.get("file", "")):
        errs = r.get("errors", [])
        if not errs:
            out["errors"].append("%s: FAILED (no parsed error lines)" % r.get("file", "?"))
        for e in errs:
            out["errors"].append("%s:%s:%s [%s] %s" % (
                r.get("file", "?"), e.get("line", "?"), e.get("col", "?"),
                e.get("code", ""), e.get("msg", "")))
    return out


def read_texture_inspect(live_dir):
    """live/texture_inspect.jsonl -> per-pass float NaN/Inf witness (Tier 1.1).

    Source-level: the proxy scans float bits BEFORE the 8-bit clamp, so this sees
    corruption that metrics() (which reads a clamped 8-bit image) cannot."""
    rows = _tail_jsonl(os.path.join(live_dir, "texture_inspect.jsonl"), 4000)
    latest = {}
    for r in rows:
        latest[r.get("file", "?")] = r
    bad = [r for r in latest.values() if r.get("has_nan") or r.get("has_inf")]
    out = {"passes_seen": len(latest), "corrupt": len(bad),
           "numeric_ok": len(bad) == 0, "findings": []}
    for r in sorted(bad, key=lambda x: x.get("file", "")):
        kinds = []
        if r.get("has_nan"):
            kinds.append("NaN x%d @%s" % (r.get("nan_count", 0), r.get("first_nan")))
        if r.get("has_inf"):
            kinds.append("Inf")
        out["findings"].append("%s: %s" % (r.get("file", "?"), ", ".join(kinds)))
    return out


def read_cb_dump(live_dir):
    """live/cb_meta.jsonl -> constant-buffer corruption witnesses (Tier 1.2).

    NaN/Inf in committed CB bytes (scanned on the CPU shadow, no GPU readback) plus
    CB_SIZE_MISMATCH overflow from UpdateSubresource. Emitted only on corruption.
    NOTE: NaN/Inf is HEURISTIC -- the scan has no CB layout, so integer/bitfield members
    with all-exponent-bits (0xFFFFFFFF, -1, packed colors) read as NaN/Inf. The write-gate
    treats NaN/Inf as advisory and blocks only on CB_SIZE_MISMATCH (audit B)."""
    rows = _tail_jsonl(os.path.join(live_dir, "cb_meta.jsonl"), 4000)
    nan = [r for r in rows if r.get("has_nan") or r.get("has_inf")]
    mism = [r for r in rows if r.get("assert") == "CB_SIZE_MISMATCH"]
    out = {"records": len(rows), "nan_inf": len(nan), "size_mismatch": len(mism),
           "clean": (len(nan) == 0 and len(mism) == 0), "findings": []}
    for r in nan:
        kind = ("NaN@%s" % r.get("nan_at")) if r.get("has_nan") else "Inf"
        out["findings"].append("CB %s @ %s (size %s): %s [min %s max %s]" % (
            r.get("ptr"), r.get("bind", "?"), r.get("size"), kind, r.get("min"), r.get("max")))
    for r in mism:
        out["findings"].append("CB %s: OVERFLOW wrote %s > capacity %s" % (
            r.get("ptr"), r.get("wrote"), r.get("capacity")))
    return out


def read_resources(live_dir):
    """live/resources.jsonl -> create-side VRAM allocation watch (Tier 3.6).

    CREATE-SIDE ONLY: sums approximate bytes of every resource the proxy saw
    created. Release is NOT tracked (vtable-wrap follow-on), so this is a
    monotonic high-water of allocation, not true live residency."""
    rows = _tail_jsonl(os.path.join(live_dir, "resources.jsonl"), 200000)
    creates = [r for r in rows if r.get("op") == "create"]
    by_type, total = {}, 0
    for r in creates:
        t = r.get("type", "?"); sz = int(r.get("size", 0) or 0)
        bt = by_type.setdefault(t, {"count": 0, "bytes": 0})
        bt["count"] += 1; bt["bytes"] += sz; total += sz
    return {"creates": len(creates), "total_bytes": total,
            "total_mb": round(total / (1024 * 1024), 2),
            "by_type": {t: {"count": v["count"], "mb": round(v["bytes"] / (1024 * 1024), 2)}
                        for t, v in sorted(by_type.items())},
            "note": "create-side only; Release not tracked (follow-on)"}


def read_shader_variants(live_dir):
    """live/shader_variants.jsonl -> #define permutations per compiled shader (Tier 3.2).

    Compile-time records (defines present) come from the D3DCompile hooks where
    pDefines is in scope; defines are NEVER reflected from the DXBC blob
    (compiler-erased). 'kind':'shader_used' records are bind-time witnesses
    (PSSetShader) of which bytecode hash is actually bound. Stock Skyrim shaders
    arrive precompiled via CreatePixelShader, so they appear only as shader_used
    with no defines (expected). The variant 'hash' (source+defines) and the
    shader_used 'ps_hash' (bytecode) are different domains -> correlate by
    file/entry, not by hash."""
    rows = _tail_jsonl(os.path.join(live_dir, "shader_variants.jsonl"), 8000)
    variants = [r for r in rows if r.get("kind") != "shader_used"]
    used = [r for r in rows if r.get("kind") == "shader_used"]
    perms = {}
    for r in variants:
        k = (r.get("file", "?"), r.get("entry", "?"), r.get("profile", "?"))
        perms.setdefault(k, set()).add(r.get("defines", ""))
    out = {"compiled": len(variants),
           "bound_shaders": len(set(u.get("ps_hash") for u in used)),
           "distinct_shaders": len(perms), "permutations": []}
    for (f, e, pr), defs in sorted(perms.items()):
        shown = " ; ".join(sorted(d for d in defs if d))[:300] or "<none>"
        out["permutations"].append("%s:%s [%s] x%d: %s" % (f, e, pr, len(defs), shown))
    return out


def read_ranges(live_dir):
    """live/ranges.jsonl -> per-pass GPU min/max/NaN/Inf (Tier 1.3, pre-clamp).

    A GPU reduction over each opted-in pass output BEFORE the 8-bit clamp. GATED
    ([Diagnostics] GpuReadback, default off) and the GPU path is operator-validated
    (see RangeOracle.cpp). Emit-on-corruption (else throttled) -> may be empty."""
    rows = _tail_jsonl(os.path.join(live_dir, "ranges.jsonl"), 4000)
    latest = {}
    for r in rows:
        latest[(r.get("pass", "?"), r.get("output", "?"))] = r
    bad = [r for r in latest.values() if r.get("nan", 0) or r.get("inf", 0) or r.get("verdict") == "CORRUPT"]
    out = {"outputs_seen": len(latest), "corrupt": len(bad),
           "ranges_ok": len(bad) == 0, "findings": []}
    for r in sorted(bad, key=lambda x: (x.get("pass", ""), x.get("output", ""))):
        out["findings"].append("%s/%s: nan=%s inf=%s [min %s max %s]" % (
            r.get("pass", "?"), r.get("output", "?"), r.get("nan", 0), r.get("inf", 0),
            r.get("min"), r.get("max")))
    return out


if __name__ == "__main__":
    sys.exit(main())
