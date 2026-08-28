"""Image quality metrics used to justify the pipeline's design choices.

These exist so that "does supersampling actually help?" and "which kernel
should ship as the default?" are answered with numbers rather than opinion.
"""

from __future__ import annotations

import numpy as np

__all__ = ["psnr", "ssim", "gradient_energy", "aliasing_energy"]


def psnr(a: np.ndarray, b: np.ndarray, peak: float = 1.0) -> float:
    """Peak signal-to-noise ratio in dB.  Higher is closer to the reference."""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    mse = float(((a - b) ** 2).mean())
    if mse == 0.0:
        return float("inf")
    return 10.0 * np.log10((peak ** 2) / mse)


def _gauss1d(sigma: float, radius: int) -> np.ndarray:
    x = np.arange(-radius, radius + 1, dtype=np.float64)
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    return k / k.sum()


def _blur(img: np.ndarray, sigma: float = 1.5) -> np.ndarray:
    """Separable Gaussian blur with reflect padding."""
    radius = int(np.ceil(3.0 * sigma))
    k = _gauss1d(sigma, radius)
    out = np.pad(img, ((radius, radius), (radius, radius)), mode="reflect")
    out = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 0, out)
    out = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 1, out)
    return out


def ssim(a: np.ndarray, b: np.ndarray, peak: float = 1.0) -> float:
    """Mean structural similarity, Gaussian-weighted (Wang et al. 2004).

    Operates on luma if given colour input.  Returns 1.0 for identical images.
    """
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    if a.ndim == 3:
        a = a[..., :3] @ np.array([0.2126, 0.7152, 0.0722])
    if b.ndim == 3:
        b = b[..., :3] @ np.array([0.2126, 0.7152, 0.0722])

    c1 = (0.01 * peak) ** 2
    c2 = (0.03 * peak) ** 2

    mu_a, mu_b = _blur(a), _blur(b)
    mu_aa, mu_bb, mu_ab = mu_a * mu_a, mu_b * mu_b, mu_a * mu_b

    sig_aa = _blur(a * a) - mu_aa
    sig_bb = _blur(b * b) - mu_bb
    sig_ab = _blur(a * b) - mu_ab

    num = (2.0 * mu_ab + c1) * (2.0 * sig_ab + c2)
    den = (mu_aa + mu_bb + c1) * (sig_aa + sig_bb + c2)
    return float((num / den).mean())


def gradient_energy(img: np.ndarray) -> float:
    """Mean squared first difference -- a blunt but reliable sharpness proxy.

    Useful for comparing two renderings of the *same* content; meaningless
    across different content, and it happily rewards aliasing, so always read
    it next to ``aliasing_energy``.
    """
    img = np.asarray(img, dtype=np.float64)
    if img.ndim == 3:
        img = img[..., :3] @ np.array([0.2126, 0.7152, 0.0722])
    gy = np.diff(img, axis=0)
    gx = np.diff(img, axis=1)
    return float((gy ** 2).mean() + (gx ** 2).mean())


def aliasing_energy(img: np.ndarray, reference: np.ndarray) -> float:
    """Energy of the error that sits above half the destination Nyquist rate.

    Aliasing folds high source frequencies down as *structured* error.  By
    measuring only the high-frequency half of the error spectrum we separate
    "sharp" (error concentrated nowhere) from "aliased" (error concentrated in
    the top octave), which plain PSNR cannot distinguish.
    """
    img = np.asarray(img, dtype=np.float64)
    reference = np.asarray(reference, dtype=np.float64)
    if img.ndim == 3:
        img = img[..., :3] @ np.array([0.2126, 0.7152, 0.0722])
    if reference.ndim == 3:
        reference = reference[..., :3] @ np.array([0.2126, 0.7152, 0.0722])

    err = img - reference
    spec = np.abs(np.fft.fftshift(np.fft.fft2(err))) ** 2

    h, w = spec.shape
    cy, cx = h / 2.0, w / 2.0
    ys, xs = np.indices(spec.shape)
    r = np.sqrt(((ys - cy) / (h / 2.0)) ** 2 + ((xs - cx) / (w / 2.0)) ** 2)

    high = spec[r > 0.5].sum()
    return float(high / spec.size)
