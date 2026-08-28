"""RCAS -- Robust Contrast Adaptive Sharpening.

A faithful CPU reimplementation of AMD's FidelityFX FSR 1.0 RCAS pass
(``ffx_fsr1.h``, ``FsrRcasF``), used here as the reference for
``shaders/rcas.hlsl``.

Why RCAS and not unsharp mask: a resolve from 4K to 1440p is a low-pass
filter, so the result is *correctly* a little soft and needs sharpening back.
A plain unsharp mask would also amplify the ringing the Lanczos lobes left
behind and blow out specular highlights.  RCAS instead derives a per-pixel
sharpening strength from the local min/max of a 5-tap cross, so it can never
push a pixel outside the range its neighbours already occupy -- no halos, no
clipping, and it is cheap enough to run every frame at 1440p.

Colour space: RCAS is designed to run in *perceptual* space.  Feed it
gamma-encoded (sRGB) values, not linear light, or dark areas will be
over-sharpened.
"""

from __future__ import annotations

import numpy as np

__all__ = ["rcas", "RCAS_LIMIT"]

# Reciprocal of the maximum permitted lobe strength; from ffx_fsr1.h.
RCAS_LIMIT = 0.25 - (1.0 / 16.0)


def _shift(img: np.ndarray, dy: int, dx: int) -> np.ndarray:
    """Neighbour fetch with clamp-to-edge addressing, matching the shader."""
    h, w = img.shape[0], img.shape[1]
    ys = np.clip(np.arange(h) + dy, 0, h - 1)
    xs = np.clip(np.arange(w) + dx, 0, w - 1)
    return img[ys][:, xs]


def rcas(img: np.ndarray, sharpness: float = 0.25,
         denoise: bool = True) -> np.ndarray:
    """Sharpen an (H, W, 3) image in [0, 1].

    Parameters
    ----------
    sharpness
        FSR's "sharpness stops": 0.0 is maximum sharpening, and each +1.0
        halves the strength (the shader constant is ``exp2(-sharpness)``).
        0.25 is a good default for a 4K -> 1440p resolve; text-heavy desktops
        tolerate 0.0, film-grain-heavy video wants 0.5 or more.
    denoise
        Enable FSR's noise-aware attenuation, which backs the filter off in
        regions whose local contrast is dominated by a single outlier pixel.
        Leave it on for camera video and film; turning it off gives slightly
        crisper synthetic UI edges.
    """
    img = np.asarray(img, dtype=np.float64)
    if img.ndim != 3 or img.shape[2] < 3:
        raise ValueError("rcas expects (H, W, C>=3)")

    rgb = img[..., :3]

    #     b
    #   d e f
    #     h
    b = _shift(rgb, -1, 0)
    d = _shift(rgb, 0, -1)
    e = rgb
    f = _shift(rgb, 0, 1)
    h = _shift(rgb, 1, 0)

    # FSR's cheap luma proxy: 2*luma = r*0.5 + g + b*0.5.
    def _luma(x: np.ndarray) -> np.ndarray:
        return x[..., 0] * 0.5 + x[..., 1] + x[..., 2] * 0.5

    bL, dL, eL, fL, hL = (_luma(x) for x in (b, d, e, f, h))

    # Per-channel min/max over the 4-tap ring.
    mn4 = np.minimum(np.minimum(b, d), np.minimum(f, h))
    mx4 = np.maximum(np.maximum(b, d), np.maximum(f, h))

    # Limiters.  hit_min is how far the filter may push a pixel down before it
    # would undershoot black; hit_max the same for the 1.0 peak.  Both are
    # expressed as lobe weights, so the smallest of them bounds the filter.
    eps = 1e-12
    hit_min = np.minimum(mn4, e) / (4.0 * mx4 + eps)
    hit_max = (1.0 - np.maximum(mx4, e)) / (4.0 * mn4 - 4.0 + eps)

    lobe_rgb = np.maximum(-hit_min, hit_max)
    lobe = lobe_rgb.max(axis=-1)

    # Clamp to the safe negative range, then scale by the sharpness stops.
    lobe = np.clip(lobe, -RCAS_LIMIT, 0.0) * np.exp2(-float(sharpness))

    if denoise:
        ring_max = np.maximum(np.maximum(np.maximum(bL, dL), np.maximum(eL, fL)), hL)
        ring_min = np.minimum(np.minimum(np.minimum(bL, dL), np.minimum(eL, fL)), hL)
        nz = 0.25 * (bL + dL + fL + hL) - eL
        nz = np.clip(np.abs(nz) / (ring_max - ring_min + eps), 0.0, 1.0)
        nz = 1.0 - 0.5 * nz
        lobe = lobe * nz

    lobe = lobe[..., None]
    out = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0)
    out = np.clip(out, 0.0, 1.0)

    if img.shape[2] > 3:
        out = np.concatenate([out, img[..., 3:]], axis=-1)
    return out
