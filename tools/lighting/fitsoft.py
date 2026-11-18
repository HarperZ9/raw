import sys, numpy as np
from pathlib import Path
from model import Scene, shade, score
sc = Scene(Path(sys.argv[1]), 4)
m = sc.sel & (sc.cls == 5)
g = sc.game[m]
for k in (0.0, 0.1, 0.2, 0.3, 0.386):
    print(f"soft k {k}:", score(shade(sc, "mask", soft_k=k)[m], g))
m6 = sc.sel & (sc.cls == 6)
print("parallax, lobe on ", score(shade(sc, "mask", parallax_lobe=True)[m6], sc.game[m6]))
print("parallax, lobe off", score(shade(sc, "mask")[m6], sc.game[m6]))
