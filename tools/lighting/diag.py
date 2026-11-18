import sys, numpy as np
from pathlib import Path
from model import Scene, shade, score
sc = Scene(Path(sys.argv[1]), 4)
cls = np.round(sc.spec[..., 2] * 255).astype(int) & 127
full = shade(sc, "mask"); nos = shade(sc, "mask", spec_on=False)
for c in range(1, 8):
    m = sc.sel & (cls == c)
    if m.sum() < 500: continue
    g = sc.game[m]
    r = np.median((g.sum(-1) + 1e-4) / (full[m].sum(-1) + 1e-4))
    print(f"class {c}: {m.sum():6d} px  ratio {r:.3f}  spec {score(full[m], g)}  | no spec {score(nos[m], g)}  mask>0 {np.mean(sc.spec[m][:,0]>0.01):.2f} gloss {np.median(sc.spec[m][:,1]):.3f}")
from PIL import Image
d = np.zeros(sc.game.shape); d[sc.sel] = (full - sc.game)[sc.sel]
lum = d.sum(-1) / 3
img = np.zeros(sc.game.shape[:2] + (3,), np.uint8)
img[..., 0] = np.clip(lum * 400, 0, 255); img[..., 2] = np.clip(-lum * 400, 0, 255); img[..., 1] = (~sc.sel) * 60
Image.fromarray(img).save(sys.argv[2])
