"""Reader for the .rawtex files the proxy's FrameLedger writes, and preview images.

Layout (little endian): b"RAWTEX1\\0", ten uint32 (width, height, depth, arraySize,
mipLevels, DXGI format, dimension, rowBytes, cube, samples), then arraySize * depth
images of `height` rows, `rowBytes` each, top row first. Only mip 0 is stored.

Original work, MIT.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

# DXGI_FORMAT values this reader decodes.
R32G32B32A32_FLOAT, R16G16B16A16_TYPELESS, R16G16B16A16_FLOAT = 2, 9, 10
R10G10B10A2_UNORM, R11G11B10_FLOAT = 24, 26
R8G8B8A8_TYPELESS, R8G8B8A8_UNORM, R8G8B8A8_UNORM_SRGB = 27, 28, 29
R16G16_FLOAT, R32_TYPELESS, D32_FLOAT, R32_FLOAT = 34, 39, 40, 41
R24G8_TYPELESS, D24_UNORM_S8_UINT = 44, 45
R8G8_UNORM, R16_TYPELESS, R16_FLOAT, D16_UNORM, R16_UNORM, R8_UNORM = 49, 53, 54, 55, 56, 61
B8G8R8A8_UNORM = 87

FORMAT_NAMES = {
    2: "R32G32B32A32_FLOAT", 9: "R16G16B16A16_TYPELESS", 10: "R16G16B16A16_FLOAT", 24: "R10G10B10A2_UNORM",
    26: "R11G11B10_FLOAT", 27: "R8G8B8A8_TYPELESS", 28: "R8G8B8A8_UNORM", 29: "R8G8B8A8_UNORM_SRGB",
    34: "R16G16_FLOAT", 39: "R32_TYPELESS", 40: "D32_FLOAT", 41: "R32_FLOAT", 44: "R24G8_TYPELESS",
    45: "D24_UNORM_S8_UINT", 49: "R8G8_UNORM", 53: "R16_TYPELESS", 54: "R16_FLOAT", 55: "D16_UNORM",
    56: "R16_UNORM", 61: "R8_UNORM", 87: "B8G8R8A8_UNORM",
}


@dataclass
class RawTex:
    width: int
    height: int
    depth: int
    array: int
    mips: int
    format: int
    dimension: int
    row_bytes: int
    cube: bool
    data: np.ndarray  # raw bytes, shape (images, height, row_bytes)

    @property
    def images(self) -> int:
        return self.array * self.depth

    @property
    def format_name(self) -> str:
        return FORMAT_NAMES.get(self.format, str(self.format))


def read(path: str | Path) -> RawTex:
    raw = Path(path).read_bytes()
    if raw[:8] != b"RAWTEX1\0":
        raise ValueError(f"{path}: not a RAWTEX1 file")
    w, h, d, a, mips, fmt, dim, row, cube, _samples = struct.unpack_from("<10I", raw, 8)
    body = np.frombuffer(raw, dtype=np.uint8, offset=48)
    want = a * d * h * row
    if body.size != want:
        raise ValueError(f"{path}: {body.size} bytes of texels, the header says {want}")
    return RawTex(w, h, d, a, mips, fmt, dim, row, bool(cube), body.reshape(a * d, h, row))


def _unpack_11_11_10(u: np.ndarray) -> np.ndarray:
    """R11G11B10_FLOAT: unsigned floats with 5 exponent bits and 6, 6 and 5 mantissa bits."""
    def part(bits: np.ndarray, mant_bits: int) -> np.ndarray:
        e = (bits >> mant_bits).astype(np.int32)
        m = (bits & ((1 << mant_bits) - 1)).astype(np.float64)
        scale = float(1 << mant_bits)
        normal = (1.0 + m / scale) * np.exp2(e - 15.0)
        denorm = (m / scale) * np.exp2(-14.0)
        out = np.where(e == 0, denorm, normal)
        return np.where(e == 31, np.inf, out).astype(np.float32)
    return np.stack([part(u & 0x7FF, 6), part((u >> 11) & 0x7FF, 6), part((u >> 22) & 0x3FF, 5)], axis=-1)


def channels(t: RawTex, image: int = 0) -> np.ndarray:
    """One image as float32 channels, shape (height, width, n). Depth-stencil gives
    (depth 0..1, stencil 0..255)."""
    rows = t.data[image]
    f, w = t.format, t.width
    if f in (R8G8B8A8_TYPELESS, R8G8B8A8_UNORM, R8G8B8A8_UNORM_SRGB, B8G8R8A8_UNORM):
        c = rows[:, : w * 4].reshape(t.height, w, 4).astype(np.float32) / 255.0
        return c[..., [2, 1, 0, 3]] if f == B8G8R8A8_UNORM else c
    if f == R8G8_UNORM:
        return rows[:, : w * 2].reshape(t.height, w, 2).astype(np.float32) / 255.0
    if f == R8_UNORM:
        return rows[:, :w].reshape(t.height, w, 1).astype(np.float32) / 255.0
    if f in (R16G16B16A16_TYPELESS, R16G16B16A16_FLOAT):
        return rows[:, : w * 8].copy().view(np.float16).reshape(t.height, w, 4).astype(np.float32)
    if f == R16G16_FLOAT:
        return rows[:, : w * 4].copy().view(np.float16).reshape(t.height, w, 2).astype(np.float32)
    if f == R16_FLOAT:
        return rows[:, : w * 2].copy().view(np.float16).reshape(t.height, w, 1).astype(np.float32)
    if f in (R16_TYPELESS, D16_UNORM, R16_UNORM):
        return rows[:, : w * 2].copy().view(np.uint16).reshape(t.height, w, 1).astype(np.float32) / 65535.0
    if f in (R32_TYPELESS, D32_FLOAT, R32_FLOAT):
        return rows[:, : w * 4].copy().view(np.float32).reshape(t.height, w, 1)
    if f == R32G32B32A32_FLOAT:
        return rows[:, : w * 16].copy().view(np.float32).reshape(t.height, w, 4)
    if f in (R24G8_TYPELESS, D24_UNORM_S8_UINT):
        u = rows[:, : w * 4].copy().view(np.uint32).reshape(t.height, w)
        depth = (u & 0xFFFFFF).astype(np.float64) / 16777215.0
        return np.stack([depth.astype(np.float32), (u >> 24).astype(np.float32)], axis=-1)
    if f == R10G10B10A2_UNORM:
        u = rows[:, : w * 4].copy().view(np.uint32).reshape(t.height, w)
        return np.stack([(u & 1023) / 1023.0, ((u >> 10) & 1023) / 1023.0, ((u >> 20) & 1023) / 1023.0,
                         (u >> 30) / 3.0], axis=-1).astype(np.float32)
    if f == R11G11B10_FLOAT:
        return _unpack_11_11_10(rows[:, : w * 4].copy().view(np.uint32).reshape(t.height, w))
    raise ValueError(f"no decoder for DXGI format {f}")


def depth24_exact(t: RawTex, image: int = 0) -> np.ndarray:
    """D24 depth as float64, with no float32 rounding (24 bits do not fit in float32's 24-bit
    mantissa only at exactly 1.0, but the linearisation is steep there)."""
    u = t.data[image][:, : t.width * 4].copy().view(np.uint32).reshape(t.height, t.width)
    return (u & 0xFFFFFF).astype(np.float64) / 16777215.0


def to_srgb8(linear: np.ndarray) -> np.ndarray:
    x = np.clip(np.nan_to_num(linear, nan=0.0, posinf=1.0, neginf=0.0), 0.0, 1.0)
    s = np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)
    return (s * 255.0 + 0.5).astype(np.uint8)


def tonemap(hdr: np.ndarray) -> np.ndarray:
    """A display preview of scene-linear values: x / (1 + x), then the sRGB curve."""
    x = np.clip(np.nan_to_num(hdr, nan=0.0, posinf=64.0, neginf=0.0), 0.0, None)
    return to_srgb8(x / (1.0 + x))
