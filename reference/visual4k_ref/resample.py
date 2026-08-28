"""Separable polyphase resampler -- the reference for the HLSL downsample pass.

The GPU shader evaluates exactly this filter, so any question of "is the
compositor losing detail?" can be answered here first, on the CPU, with
metrics instead of eyeballs.

Coordinate convention (the one thing that is easy to get wrong):
pixel ``i`` of an image of length ``N`` covers ``[i, i+1)`` and has its centre
at ``i + 0.5``.  Destination pixel ``j`` therefore samples the source at
``(j + 0.5) * src_len / dst_len``.  Getting this off by half a pixel is the
classic cause of a "slightly blurry, slightly shifted" supersampled desktop.
"""

from __future__ import annotations

from typing import Callable

import numpy as np

from .kernels import kernel_by_name

__all__ = ["build_taps", "resample_axis", "resample", "TapTable"]


class TapTable:
    """Precomputed polyphase tap indices and weights for one axis.

    ``indices`` is (dst_len, n_taps) int64, ``weights`` is (dst_len, n_taps)
    float64 with each row summing to 1.  This is exactly the table the shader
    bakes into a constant buffer.
    """

    __slots__ = ("indices", "weights", "src_len", "dst_len", "n_taps")

    def __init__(self, indices, weights, src_len, dst_len):
        self.indices = indices
        self.weights = weights
        self.src_len = src_len
        self.dst_len = dst_len
        self.n_taps = indices.shape[1]

    def __repr__(self) -> str:
        return (f"TapTable({self.src_len} -> {self.dst_len}, "
                f"{self.n_taps} taps/pixel)")


def build_taps(src_len: int, dst_len: int, kernel: Callable,
               support: float | None = None) -> TapTable:
    """Build the polyphase tap table mapping ``src_len`` samples to ``dst_len``.

    When downsampling the kernel is *widened* by the scale ratio.  Skipping
    that step is what turns a 4K -> 1440p resolve into an aliased mess: the
    filter has to be low-pass for the destination grid, not the source grid.
    """
    if src_len < 1 or dst_len < 1:
        raise ValueError("lengths must be >= 1")

    if support is None:
        support = getattr(kernel, "support", None)
        if support is None:
            raise ValueError("kernel has no .support; pass support= explicitly")

    scale = src_len / dst_len            # source pixels consumed per dest pixel
    filter_scale = max(scale, 1.0)       # widen only when minifying
    ksupport = support * filter_scale

    centres = (np.arange(dst_len, dtype=np.float64) + 0.5) * scale
    lo = np.floor(centres - ksupport + 0.5).astype(np.int64)
    hi = np.ceil(centres + ksupport + 0.5).astype(np.int64)

    n_taps = int((hi - lo).max())
    offsets = np.arange(n_taps, dtype=np.int64)[None, :]
    idx = lo[:, None] + offsets

    # Rows whose tap span is shorter than n_taps get their surplus taps zeroed.
    in_span = idx < hi[:, None]

    # Kernel argument is measured from the destination pixel centre, in units
    # of (widened) destination pixels.  Tap k covers source pixel centre k+0.5.
    arg = (idx + 0.5 - centres[:, None]) / filter_scale
    w = kernel(arg) * in_span

    # Clamp-to-edge addressing: out-of-range taps fold onto the border pixel
    # and keep their weight, which preserves energy at the image border.
    idx = np.clip(idx, 0, src_len - 1)

    total = w.sum(axis=1, keepdims=True)
    total[total == 0.0] = 1.0
    w = w / total

    return TapTable(idx, w, src_len, dst_len)


def resample_axis(img: np.ndarray, taps: TapTable, axis: int) -> np.ndarray:
    """Apply a tap table along one axis of an (H, W[, C]) array."""
    if img.shape[axis] != taps.src_len:
        raise ValueError(
            f"axis {axis} has length {img.shape[axis]}, "
            f"tap table expects {taps.src_len}")

    moved = np.moveaxis(img, axis, 0)             # (src_len, ...)
    gathered = moved[taps.indices]                # (dst_len, n_taps, ...)

    # einsum keeps this a single pass and avoids materialising a big product.
    w = taps.weights
    out = np.einsum("dt...,dt->d...", gathered, w, optimize=True)
    return np.moveaxis(out, 0, axis)


def resample(img: np.ndarray, dst_h: int, dst_w: int,
             kernel: str | Callable = "lanczos3") -> np.ndarray:
    """Resample an (H, W[, C]) float image to (dst_h, dst_w[, C]).

    Works for both minification (the 4K -> 1440p resolve) and magnification
    (the video upscale path); the kernel widening is chosen per axis, so
    anamorphic ratios behave correctly.
    """
    if isinstance(kernel, str):
        kernel = kernel_by_name(kernel)

    img = np.asarray(img, dtype=np.float64)
    if img.ndim not in (2, 3):
        raise ValueError("expected (H, W) or (H, W, C)")

    src_h, src_w = img.shape[0], img.shape[1]

    # Resample the axis with the larger reduction first: it shrinks the working
    # set before the second, more expensive pass runs.
    do_rows_first = (dst_h / src_h) <= (dst_w / src_w)

    if do_rows_first:
        out = resample_axis(img, build_taps(src_h, dst_h, kernel), axis=0)
        out = resample_axis(out, build_taps(src_w, dst_w, kernel), axis=1)
    else:
        out = resample_axis(img, build_taps(src_w, dst_w, kernel), axis=1)
        out = resample_axis(out, build_taps(src_h, dst_h, kernel), axis=0)

    return out
