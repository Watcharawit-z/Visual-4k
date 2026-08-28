"""Does rendering at 4K and resolving to 1440p actually beat rendering at 1440p?

This is the experiment the whole project rests on, so it is run against an
analytic scene rather than a photo: a scene we can sample at *any* rate lets
us build a true band-limited reference, which a fixed-resolution test image
cannot give us.

Three renderings of the same scene are compared:

  reference   scene sampled at NxN per panel pixel and averaged -- the image a
              physically perfect display pipeline would show.  Ground truth.
  native      scene sampled once per panel pixel.  This is what a 1440p
              monitor shows today: one sample, no sub-pixel information.
  visual4k    scene sampled once per 4K pixel, then resolved to panel
              resolution with our kernel + RCAS.  What this project ships.

If "visual4k" is not measurably closer to the reference than "native", the
premise of the project is wrong and we should say so.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from visual4k_ref.metrics import aliasing_energy, gradient_energy, psnr, ssim
from visual4k_ref.pipeline import PipelineConfig, supersample
from visual4k_ref.resample import resample


def scene(u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """An analytic test scene in normalised [0,1]^2 coordinates.

    Deliberately full of the frequencies that break naive rendering:
      * a zone plate, whose local frequency grows without bound toward the
        edges -- the classic aliasing torture test;
      * a fan of thin radial lines, standing in for UI hairlines and text
        stems, which is where 1440p desktops visibly break down;
      * a converging checkerboard, standing in for texture minification.
    """
    u = np.asarray(u, dtype=np.float64)
    v = np.asarray(v, dtype=np.float64)

    x = (u - 0.5) * 2.0
    y = (v - 0.5) * 2.0
    r2 = x * x + y * y

    zone = 0.5 + 0.5 * np.sin(90.0 * r2)

    theta = np.arctan2(y, x)
    fan = 0.5 + 0.5 * np.sign(np.sin(48.0 * theta))

    checker = ((np.floor(u * 140.0) + np.floor(v * 140.0)) % 2.0)

    # Blend the three regions by position so each occupies its own band.
    w_fan = np.clip((u - 0.30) * 12.0, 0.0, 1.0) * np.clip((0.70 - u) * 12.0, 0.0, 1.0)
    w_chk = np.clip((u - 0.72) * 12.0, 0.0, 1.0)
    w_zone = np.clip(1.0 - w_fan - w_chk, 0.0, 1.0)

    return np.clip(w_zone * zone + w_fan * fan + w_chk * checker, 0.0, 1.0)


def sample_grid(h: int, w: int) -> np.ndarray:
    """One sample at the centre of each of h x w pixels."""
    v = (np.arange(h, dtype=np.float64) + 0.5) / h
    u = (np.arange(w, dtype=np.float64) + 0.5) / w
    return scene(u[None, :], v[:, None])


def reference_render(h: int, w: int, ss: int, block: int = 64) -> np.ndarray:
    """Ground truth: ss x ss samples per pixel, box-averaged, computed in bands
    so the intermediate never has to fit in memory all at once."""
    out = np.empty((h, w), dtype=np.float64)
    u = (np.arange(w * ss, dtype=np.float64) + 0.5) / (w * ss)
    for y0 in range(0, h, block):
        y1 = min(y0 + block, h)
        v = (np.arange(y0 * ss, y1 * ss, dtype=np.float64) + 0.5) / (h * ss)
        tile = scene(u[None, :], v[:, None])
        tile = tile.reshape(y1 - y0, ss, w, ss).mean(axis=(1, 3))
        out[y0:y1] = tile
    return out


def to_rgb(gray: np.ndarray) -> np.ndarray:
    return np.repeat(gray[..., None], 3, axis=2)


def main() -> int:
    # Proportionally identical to 3840x2160 -> 2560x1440 (a 1.5x linear ratio),
    # scaled down so the 6x6 ground-truth render stays inside a few hundred MB.
    panel_h, panel_w = 720, 1280
    over_h, over_w = 1080, 1920
    ss = 6

    print(f"panel      : {panel_w}x{panel_h}")
    print(f"oversampled: {over_w}x{over_h}  ({over_w/panel_w:.2f}x linear, "
          f"{(over_w*over_h)/(panel_w*panel_h):.2f}x pixels)")
    print(f"reference  : {ss}x{ss} samples/pixel\n")

    t0 = time.time()
    ref = reference_render(panel_h, panel_w, ss)
    print(f"ground truth rendered in {time.time()-t0:.1f}s")

    native = sample_grid(panel_h, panel_w)
    over = sample_grid(over_h, over_w)
    print(f"scene sampled at native and oversampled rates\n")

    rows = []
    rows.append(("native 1440p (1 spp)", native))

    for kernel in ("triangle", "catrom", "gaussian", "lanczos2", "lanczos3", "lanczos4"):
        res = resample(over, panel_h, panel_w, kernel)
        rows.append((f"visual4k / {kernel}", np.clip(res, 0.0, 1.0)))

    # The shipping configuration, with the sharpening pass included.
    cfg = PipelineConfig(kernel="lanczos3", sharpness=0.25, denoise=False)
    shipped = supersample(to_rgb(over), panel_h, panel_w, cfg)[..., 0]
    rows.append(("visual4k / lanczos3 + RCAS", shipped))

    ref_grad = gradient_energy(ref)

    hdr = f"{'rendering':<28}{'PSNR dB':>9}{'SSIM':>8}{'alias err':>12}{'sharpness':>11}"
    print(hdr)
    print("-" * len(hdr))
    for name, img in rows:
        print(f"{name:<28}{psnr(img, ref):>9.2f}{ssim(img, ref):>8.4f}"
              f"{aliasing_energy(img, ref):>12.4f}"
              f"{gradient_energy(img)/ref_grad:>10.2f}x")

    print("\nsharpness is relative to ground truth: 1.00x is correct, "
          "\nmuch above 1.00x with a poor PSNR means aliasing, not detail.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
