"""FLIP image difference, written from the paper (Andersson, Nilsson, Akenine-Moller, Oskarsson, Astrom and
Fairchild, "FLIP: A Difference Evaluator for Alternating Images", HPG 2020). LDR form: both images in sRGB,
0 to 1. Not checked against NVIDIA's reference implementation; compare runs of this tool with each other.

usage: python flip.py <reference.png> <test.png> [--ppd 67] [--exposure E] [--map out.png] [--crop x0 y0 x1 y1]
HDR inputs (.npy float32 linear RGB) are tone mapped first with 1 - exp(-E x) for both images.
Prints mean, median and 95th percentile FLIP. Original work, MIT.
"""
import argparse
import sys

import numpy as np
from PIL import Image


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


M_RGB2XYZ = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
M_XYZ2RGB = np.linalg.inv(M_RGB2XYZ)
WHITE = M_RGB2XYZ @ np.ones(3)


def xyz_to_ycxcz(xyz):
    x, y, z = (xyz[..., k] / WHITE[k] for k in range(3))
    return np.stack([116 * y - 16, 500 * (x - y), 200 * (y - z)], -1)


def ycxcz_to_xyz(c):
    y = (c[..., 0] + 16) / 116
    x = c[..., 1] / 500 + y
    z = y - c[..., 2] / 200
    return np.stack([x * WHITE[0], y * WHITE[1], z * WHITE[2]], -1)


def xyz_to_lab(xyz):
    t = xyz / WHITE
    d = 6 / 29
    f = np.where(t > d ** 3, np.cbrt(t), t / (3 * d * d) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def hunt(lab):
    return np.stack([lab[..., 0], 0.01 * lab[..., 0] * lab[..., 1], 0.01 * lab[..., 0] * lab[..., 2]], -1)


def hyab(a, b):
    return np.abs(a[..., 0] - b[..., 0]) + np.sqrt((a[..., 1] - b[..., 1]) ** 2 + (a[..., 2] - b[..., 2]) ** 2)


def conv_sep(img, k):
    """Separable 2D convolution with reflect padding."""
    r = len(k) // 2
    p = np.pad(img, ((r, r), (0, 0)), mode="reflect")
    out = sum(k[i] * p[i:i + img.shape[0]] for i in range(len(k)))
    p = np.pad(out, ((0, 0), (r, r)), mode="reflect")
    return sum(k[i] * p[:, i:i + img.shape[1]] for i in range(len(k)))


def csf_kernel(a1, b1, a2, b2, ppd):
    # radius: three standard deviations of the widest Gaussian
    s = max(np.sqrt(b1 / (2 * np.pi ** 2)), np.sqrt(b2 / (2 * np.pi ** 2)) if a2 else 0)
    r = int(np.ceil(3 * s * ppd))
    x = np.arange(-r, r + 1) / ppd
    g = a1 * np.sqrt(np.pi / b1) * np.exp(-np.pi ** 2 * x * x / b1)
    if a2:
        g = g + a2 * np.sqrt(np.pi / b2) * np.exp(-np.pi ** 2 * x * x / b2)
    return g / g.sum()


def colour_pipeline(srgb, ppd):
    ycc = xyz_to_ycxcz(srgb_to_linear(srgb) @ M_RGB2XYZ.T)
    params = [(1, 0.0047, 0, 1e-5), (1, 0.0053, 0, 1e-5), (34.1, 0.04, 13.5, 0.025)]
    filt = np.stack([conv_sep(ycc[..., k], csf_kernel(*params[k], ppd)) for k in range(3)], -1)
    rgb = np.clip(ycxcz_to_xyz(filt) @ M_XYZ2RGB.T, 0, 1)
    return hunt(xyz_to_lab(rgb @ M_RGB2XYZ.T))


def features(srgb, ppd):
    y = (xyz_to_ycxcz(srgb_to_linear(srgb) @ M_RGB2XYZ.T)[..., 0] + 16) / 116
    sd = 0.5 * 0.082 * ppd
    r = int(np.ceil(3 * sd))
    x = np.arange(-r, r + 1, dtype=np.float64)
    g = np.exp(-x * x / (2 * sd * sd))
    d1 = -x * g
    d2 = (x * x / (sd * sd) - 1) * g
    g /= g.sum()
    d1 /= np.sum(np.abs(d1)) / 2   # a unit step gives 1
    d2 -= d2.mean()
    d2 /= np.sum(d2[d2 > 0])

    def sep(img, kr, kc):
        rr = len(kr) // 2
        p = np.pad(img, ((rr, rr), (0, 0)), mode="reflect")
        t = sum(kr[i] * p[i:i + img.shape[0]] for i in range(len(kr)))
        cc = len(kc) // 2
        p = np.pad(t, ((0, 0), (cc, cc)), mode="reflect")
        return sum(kc[i] * p[:, i:i + img.shape[1]] for i in range(len(kc)))

    edge = np.hypot(sep(y, g, d1), sep(y, d1, g))
    point = np.hypot(sep(y, g, d2), sep(y, d2, g))
    return edge, point


def flip(ref, test, ppd=67.0):
    qc, qf, pc, pt = 0.7, 0.5, 0.4, 0.95
    e = hyab(colour_pipeline(ref, ppd), colour_pipeline(test, ppd)) ** qc
    green, blue = np.array([[[0.0, 1.0, 0.0]]]), np.array([[[0.0, 0.0, 1.0]]])
    cmax = float(hyab(hunt(xyz_to_lab(green @ M_RGB2XYZ.T)), hunt(xyz_to_lab(blue @ M_RGB2XYZ.T)))[0, 0] ** qc)
    ec = np.where(e < pc * cmax, pt / (pc * cmax) * e, pt + (e - pc * cmax) / (cmax - pc * cmax) * (1 - pt))
    er, pr = features(ref, ppd)
    et, ptt = features(test, ppd)
    ef = (np.maximum(np.abs(er - et), np.abs(pr - ptt)) / np.sqrt(2)) ** qf
    return np.clip(ec, 0, 1) ** (1 - np.clip(ef, 0, 1))


def load(path, exposure):
    if path.endswith(".npy"):
        hdr = np.load(path).astype(np.float64)[..., :3]
        return linear_to_srgb(1 - np.exp(-exposure * np.maximum(hdr, 0)))
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ref")
    p.add_argument("test")
    p.add_argument("--ppd", type=float, default=67.0)
    p.add_argument("--exposure", type=float, default=1.0)
    p.add_argument("--map")
    p.add_argument("--crop", type=int, nargs=4)
    p.add_argument("--scale", type=int, default=1, help="downsample both by this factor first")
    a = p.parse_args()
    ref, test = load(a.ref, a.exposure), load(a.test, a.exposure)
    if a.crop:
        x0, y0, x1, y1 = a.crop
        ref, test = ref[y0:y1, x0:x1], test[y0:y1, x0:x1]
    if a.scale > 1:
        s = a.scale
        h, w = ref.shape[0] // s * s, ref.shape[1] // s * s
        ref = ref[:h, :w].reshape(h // s, s, w // s, s, 3).mean((1, 3))
        test = test[:h, :w].reshape(h // s, s, w // s, s, 3).mean((1, 3))
    f = flip(ref, test, a.ppd)
    print(f"FLIP mean {f.mean():.4f} median {np.median(f):.4f} p95 {np.percentile(f, 95):.4f} ({f.shape[1]}x{f.shape[0]})")
    if a.map:
        Image.fromarray((np.clip(f, 0, 1) * 255).astype(np.uint8)).save(a.map)
    return 0


if __name__ == "__main__":
    sys.exit(main())
