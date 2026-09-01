"""Can a resolve keep the subpixel detail that makes small text legible?

The complaint that started this: raising the desktop resolution and letting the
GPU downsample gives more room on screen, but the text goes soft. It does, and
the reason is specific rather than general.

An LCD pixel is three emitters side by side: red at one third left of centre,
green at centre, blue at one third right. ClearType exploits that, computing
how much of a glyph covers each *emitter* rather than each pixel, which buys
three times the horizontal resolution for free. Every ordinary resolve destroys
it -- one value per output pixel, sent to all three emitters, and the structure
ClearType built is averaged away.

So: sample each channel where its emitter actually is. Red a third of a pixel
left, blue a third right. That is what subpixel_resample does, and this
measures whether it is worth anything.

THE SCENE is vertical stems at glyph widths and sub-pixel phases, because that
is what text is: the hard part of rendering a letter is placing its vertical
strokes. Coverage is computed analytically -- a stem is an interval, a subpixel
is an interval, and the overlap of two intervals is exact -- so the ground
truth here carries no sampling error at all.

THREE METRICS, because one is not enough to tell improvement from fringing:

  subpixel PSNR  per channel against per-emitter ground truth. This is
                 effective resolution: how much of the real detail each
                 emitter is showing.
  luma PSNR      channel mean against whole-pixel ground truth. Overall
                 brightness must stay correct; a method that wins on
                 resolution by darkening the image has not won.
  fringe         how far the output's colour departs from what correct
                 subpixel rendering has. Note that this counts having too
                 little colour as well as too much: a plain resolve emits no
                 chroma at all where the ground truth has some, and scores
                 badly for that, not for fringing.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from visual4k_ref.resample import RGB_STRIPE_OFFSETS, resample, subpixel_resample

PANEL_W = 1280
PANEL_H = 16


def stems() -> "tuple[np.ndarray, np.ndarray]":
    """Vertical strokes at the widths and phases real glyphs have.

    Widths from 0.75 to 2.0 panel pixels: a 0.75-pixel stem is the hairline of
    a small serif face, and 2.0 is a bold stem at a comfortable reading size.
    Each width is repeated at three sub-pixel phases, since where a stem falls
    inside a pixel is exactly what decides whether it renders crisp or muddy.
    """
    lo, hi = [], []
    x = 8.0
    for width in (0.75, 1.0, 1.25, 1.5, 2.0):
        for phase in (0.0, 1.0 / 3.0, 2.0 / 3.0):
            for _ in range(6):
                lo.append(x + phase)
                hi.append(x + phase + width)
                x += width + 4.0
    return np.asarray(lo), np.asarray(hi)


STEM_LO, STEM_HI = stems()


def ink(p: np.ndarray, q: np.ndarray) -> np.ndarray:
    """Exact ink coverage of each interval [p, q), as a fraction of its width."""
    overlap = np.clip(np.minimum(STEM_HI[None, :], q[:, None]) -
                      np.maximum(STEM_LO[None, :], p[:, None]), 0.0, None)
    return overlap.sum(axis=1) / (q - p)


def row_at(width: int, subpixels: bool) -> np.ndarray:
    """One row of the scene, rendered at `width` pixels.

    With ``subpixels`` the three emitters of each pixel are integrated
    separately, which is what ClearType computes. Without it, each pixel gets
    one coverage value -- ordinary greyscale antialiasing.
    """
    scale = PANEL_W / width
    if not subpixels:
        edges = np.arange(width + 1, dtype=np.float64) * scale
        value = 1.0 - ink(edges[:-1], edges[1:])
        return np.repeat(value[:, None], 3, axis=1)

    out = np.empty((width, 3), dtype=np.float64)
    for channel in range(3):
        p = (np.arange(width, dtype=np.float64) + channel / 3.0) * scale
        q = p + scale / 3.0
        out[:, channel] = 1.0 - ink(p, q)
    return out


def as_image(row: np.ndarray, height: int) -> np.ndarray:
    return np.repeat(row[None, :, :], height, axis=0)


def psnr_of(img: np.ndarray, ref: np.ndarray) -> float:
    mse = float(np.mean((np.clip(img, 0.0, 1.0) - ref) ** 2))
    return float("inf") if mse == 0.0 else 10.0 * np.log10(1.0 / mse)


def fringe_of(img: np.ndarray, ref: np.ndarray) -> float:
    """RMS colour error beyond the ground truth's own subpixel colouring."""
    img = np.clip(img, 0.0, 1.0)
    img_chroma = img - img.mean(axis=2, keepdims=True)
    ref_chroma = ref - ref.mean(axis=2, keepdims=True)
    return float(np.sqrt(np.mean((img_chroma - ref_chroma) ** 2)))


def main() -> int:
    over_w = int(PANEL_W * 1.5)          # 3840 -> 2560 in miniature
    over_h = int(PANEL_H * 1.5)

    truth_sub = as_image(row_at(PANEL_W, subpixels=True), PANEL_H)
    truth_luma = as_image(row_at(PANEL_W, subpixels=False), PANEL_H)

    # What the desktop hands the resolve: glyphs already antialiased by the
    # font rasteriser at the oversampled resolution, greyscale, because the
    # rasteriser has no idea its output is about to be scaled.
    oversampled = as_image(row_at(over_w, subpixels=False), over_h)

    native_grey = as_image(row_at(PANEL_W, subpixels=False), PANEL_H)

    rows = [
        ("native 1440p, ClearType", truth_sub),
        ("native 1440p, greyscale AA", native_grey),
        ("4K -> plain resolve", resample(oversampled, PANEL_H, PANEL_W, "lanczos2")),
        ("4K -> subpixel resolve", subpixel_resample(oversampled, PANEL_H, PANEL_W,
                                                     "lanczos2")),
    ]

    print(f"panel {PANEL_W} wide, oversampled {over_w} (1.50x, as DSR 2.25x gives)")
    print(f"stems: {len(STEM_LO)} vertical strokes, 0.75 to 2.00 pixels wide,")
    print(f"       at three sub-pixel phases each\n")

    hdr = f"{'rendering':<30}{'subpixel PSNR':>15}{'luma PSNR':>12}{'fringe':>10}"
    print(hdr)
    print("-" * len(hdr))
    for name, img in rows:
        sub = psnr_of(img, truth_sub)
        luma = psnr_of(np.repeat(np.clip(img, 0, 1).mean(axis=2, keepdims=True),
                                 3, axis=2), truth_luma)
        print(f"{name:<30}{sub:>15.2f}{luma:>12.2f}{fringe_of(img, truth_sub):>10.4f}")

    print()
    print("native ClearType is the ground truth for the subpixel column, so its")
    print("infinite PSNR is a definition rather than a result: it is the ceiling")
    print("any resolve is trying to reach, not a method competing with them.")

    fringe_filters(oversampled, truth_sub, truth_luma)
    return 0


def apply_across_subpixels(img: np.ndarray, taps) -> np.ndarray:
    """Convolve along the row of emitters, treating RGB RGB RGB as one signal.

    Subpixel rendering's own correction runs here rather than per channel,
    because the thing being smoothed is the sequence of emitters across the
    panel, not three separate images.
    """
    height, width, _ = img.shape
    flat = img.reshape(height, width * 3)

    kernel = np.asarray(taps, dtype=np.float64)
    kernel = kernel / kernel.sum()
    pad = len(kernel) // 2
    padded = np.pad(flat, ((0, 0), (pad, pad)), mode="edge")

    out = np.zeros_like(flat)
    for i, coefficient in enumerate(kernel):
        out += coefficient * padded[:, i:i + width * 3]
    return out.reshape(height, width, 3)


def fringe_filters(oversampled, truth_sub, truth_luma) -> None:
    """Subpixel rendering normally pairs with a filter that trades some of the
    recovered detail back to suppress coloured edges. Every one of them scores
    worse here, and that result should be read carefully rather than believed.

    The ground truth in this file is *ideal* per-emitter coverage, with no
    correction applied. An unfiltered resolve is therefore closest to it by
    construction, and any filter can only move away. What the filters exist to
    fix -- whether a coloured edge is objectionable to look at -- is not in
    this measurement at all, and cannot be: it needs a real panel and a person.

    So the numbers below say the filters are unnecessary, and the numbers are
    not entitled to that conclusion. They are printed because the experiment
    was run, and because anyone who sees fringing on a real screen should know
    that this was tried and where to start.
    """
    subpixel = subpixel_resample(oversampled, PANEL_H, PANEL_W, "lanczos2")

    print()
    print()
    print("Fringe-reduction filters, applied across the emitters:")
    print()
    hdr = f"{'filter':<34}{'subpixel PSNR':>15}{'luma PSNR':>12}{'fringe':>10}"
    print(hdr)
    print("-" * len(hdr))

    candidates = [
        ("none", None),
        ("[1 2 1] / 4", (1, 2, 1)),
        ("[1 1 1] / 3", (1, 1, 1)),
        ("[1 2 3 2 1] / 9", (1, 2, 3, 2, 1)),
        ("[1 3 4 3 1] / 12", (1, 3, 4, 3, 1)),
        ("[1 1 1 1 1] / 5", (1, 1, 1, 1, 1)),
    ]
    for name, taps in candidates:
        img = subpixel if taps is None else apply_across_subpixels(subpixel, taps)
        sub = psnr_of(img, truth_sub)
        luma = psnr_of(np.repeat(np.clip(img, 0, 1).mean(axis=2, keepdims=True),
                                 3, axis=2), truth_luma)
        print(f"{name:<34}{sub:>15.2f}{luma:>12.2f}"
              f"{fringe_of(img, truth_sub):>10.4f}")

    print()
    print("Read the docstring before drawing a conclusion from these.")


if __name__ == "__main__":
    raise SystemExit(main())
