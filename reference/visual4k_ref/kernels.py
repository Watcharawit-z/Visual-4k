"""Resampling kernels used by the Visual-4k pipeline.

Every kernel here is defined in *destination pixel* units and is mirrored
1:1 by an HLSL implementation under ``shaders/``.  The Python version is the
normative reference: if the shader and this file disagree, the shader is wrong.

All functions are vectorised over NumPy arrays and return 0 outside their
support, so a caller can evaluate them on a rectangular tap grid without
masking first.
"""

from __future__ import annotations

import numpy as np

__all__ = [
    "box",
    "triangle",
    "catmull_rom",
    "mitchell",
    "lanczos",
    "gaussian",
    "KERNELS",
    "kernel_by_name",
    "dsr_smoothness_to_sigma",
]


def _sinc(x: np.ndarray) -> np.ndarray:
    """Normalised sinc, sin(pi x) / (pi x), with sinc(0) = 1."""
    x = np.asarray(x, dtype=np.float64)
    out = np.ones_like(x)
    nz = x != 0.0
    px = np.pi * x[nz]
    out[nz] = np.sin(px) / px
    return out


def box(x: np.ndarray) -> np.ndarray:
    """Nearest-neighbour / area kernel.  Support 0.5."""
    x = np.abs(np.asarray(x, dtype=np.float64))
    return np.where(x < 0.5, 1.0, 0.0)


def triangle(x: np.ndarray) -> np.ndarray:
    """Bilinear kernel.  Support 1.0."""
    x = np.abs(np.asarray(x, dtype=np.float64))
    return np.where(x < 1.0, 1.0 - x, 0.0)


def _cubic(x: np.ndarray, b: float, c: float) -> np.ndarray:
    """Mitchell-Netravali cubic family.  Support 2.0."""
    x = np.abs(np.asarray(x, dtype=np.float64))
    x2 = x * x
    x3 = x2 * x

    inner = ((12.0 - 9.0 * b - 6.0 * c) * x3
             + (-18.0 + 12.0 * b + 6.0 * c) * x2
             + (6.0 - 2.0 * b)) / 6.0
    outer = ((-b - 6.0 * c) * x3
             + (6.0 * b + 30.0 * c) * x2
             + (-12.0 * b - 48.0 * c) * x
             + (8.0 * b + 24.0 * c)) / 6.0

    return np.where(x < 1.0, inner, np.where(x < 2.0, outer, 0.0))


def catmull_rom(x: np.ndarray) -> np.ndarray:
    """Catmull-Rom cubic (B=0, C=0.5).  Sharp, mild ringing.  Support 2.0."""
    return _cubic(x, 0.0, 0.5)


def mitchell(x: np.ndarray) -> np.ndarray:
    """Mitchell cubic (B=C=1/3).  Softer, no visible ringing.  Support 2.0."""
    return _cubic(x, 1.0 / 3.0, 1.0 / 3.0)


def lanczos(a: int = 3):
    """Lanczos-a windowed sinc.  Support ``a``.

    a=2 is soft and ringing-free enough for video, a=3 is the default for the
    desktop supersampling path, a=4 is sharper still but shows halos on
    high-contrast text edges.
    """
    if a < 1:
        raise ValueError("lanczos order must be >= 1")

    def _k(x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=np.float64)
        ax = np.abs(x)
        return np.where(ax < a, _sinc(x) * _sinc(x / a), 0.0)

    _k.support = float(a)
    _k.__name__ = f"lanczos{a}"
    return _k


def gaussian(sigma: float = 0.5, truncate: float = 3.25):
    """Gaussian kernel, sigma in destination-pixel units.

    This is the DSR-style path: NVIDIA's Dynamic Super Resolution resolves its
    oversampled buffer with a 13-tap Gaussian whose width is driven by the
    "DSR smoothness" slider.  ``truncate=3.25`` gives 13 taps at a 2.0x
    downsample ratio, which is where the 13-tap number comes from.

    Note: this reproduces the *shape* of NVIDIA's filter, not its exact
    coefficients, which are not published.  See ``dsr_smoothness_to_sigma``.
    """
    if sigma <= 0.0:
        raise ValueError("sigma must be > 0")

    inv_two_sigma_sq = 1.0 / (2.0 * sigma * sigma)
    support = float(truncate * sigma)

    def _k(x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=np.float64)
        ax = np.abs(x)
        return np.where(ax < support, np.exp(-(x * x) * inv_two_sigma_sq), 0.0)

    _k.support = support
    _k.__name__ = f"gaussian{sigma:g}"
    return _k


def dsr_smoothness_to_sigma(smoothness: float) -> float:
    """Map a DSR-style smoothness percentage (0..100) to a Gaussian sigma.

    NVIDIA does not publish the mapping behind its slider, so this is our own
    curve, chosen so that the default (33%) lands on sigma = 0.5 destination
    pixels -- visually very close to the stock DSR look, and just under the
    Nyquist limit of the destination grid.
    """
    s = float(np.clip(smoothness, 0.0, 100.0)) / 100.0
    return 0.25 + 0.75 * s


# Support radii for the plain (non-parameterised) kernels.
box.support = 0.5
triangle.support = 1.0
catmull_rom.support = 2.0
mitchell.support = 2.0

KERNELS = {
    "box": box,
    "triangle": triangle,
    "catrom": catmull_rom,
    "mitchell": mitchell,
    "lanczos2": lanczos(2),
    "lanczos3": lanczos(3),
    "lanczos4": lanczos(4),
    "gaussian": gaussian(0.5),
}


def kernel_by_name(name: str):
    """Look up a kernel by name, accepting ``gaussian:<sigma>`` for a custom width."""
    if name.startswith("gaussian:"):
        return gaussian(float(name.split(":", 1)[1]))
    if name.startswith("lanczos:"):
        return lanczos(int(name.split(":", 1)[1]))
    try:
        return KERNELS[name]
    except KeyError:
        raise ValueError(
            f"unknown kernel {name!r}; known: {', '.join(sorted(KERNELS))}"
        ) from None
