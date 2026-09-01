"""What does the DSR smoothness slider actually cost you, and where is its best setting?

NVIDIA's DSR resolves its oversampled buffer with a Gaussian whose width comes
from the "DSR - Smoothness" slider. Advice about that slider is almost always
"try some values and see" -- including the advice I gave. But the scene in
bench_supersample.py has an analytic ground truth, so the question has an
answer rather than an opinion, and this measures it.

Two things come out of it:

  1. Where the Gaussian's own optimum sits, and how far the default is from
     it. That is directly actionable on a machine already using DSR.

  2. How much the best possible Gaussian still leaves on the table against a
     windowed-sinc resolve. That is the honest size of the remaining gap --
     the only number that says whether a better filter is worth having at all.

TWO LIMITS, STATED UP FRONT BECAUSE THEY BOUND EVERY NUMBER BELOW:

  * NVIDIA does not publish the mapping from the slider's percentage to a
    filter width, so kernels.dsr_smoothness_to_sigma is our own curve, anchored
    so the 33% default lands on sigma = 0.5. The sigma results are real; the
    percentages that go with them inherit that assumption and are an estimate.

  * This models legacy DSR. DLDSR uses a learned downscaler, not a Gaussian,
    so it is not this curve at all. What carries over to DLDSR is only the
    direction: a wider filter is a softer image.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bench_supersample import reference_render, sample_grid, to_rgb
from visual4k_ref.kernels import dsr_smoothness_to_sigma
from visual4k_ref.metrics import aliasing_energy, gradient_energy, psnr, ssim
from visual4k_ref.pipeline import PipelineConfig, supersample
from visual4k_ref.resample import resample


def main() -> int:
    # Proportionally identical to 3840x2160 -> 2560x1440, as in the sibling
    # benchmark, so the two sets of numbers can be read together.
    panel_h, panel_w = 720, 1280
    over_h, over_w = 1080, 1920
    ss = 6

    print(f"panel      : {panel_w}x{panel_h}   "
          f"(proportional to 2560x1440)")
    print(f"oversampled: {over_w}x{over_h}   "
          f"(proportional to 3840x2160, a 1.50x linear ratio)")
    print(f"reference  : {ss}x{ss} samples/pixel\n")

    ref = reference_render(panel_h, panel_w, ss)
    native = sample_grid(panel_h, panel_w)
    over = sample_grid(over_h, over_w)
    ref_grad = gradient_energy(ref)

    def report(name: str, img: np.ndarray) -> "tuple[str, float, float, float, float]":
        img = np.clip(img, 0.0, 1.0)
        return (name, psnr(img, ref), ssim(img, ref),
                aliasing_energy(img, ref), gradient_energy(img) / ref_grad)

    hdr = (f"{'smoothness':>11}{'sigma':>8}{'PSNR dB':>10}{'SSIM':>8}"
           f"{'alias err':>12}{'sharpness':>11}")
    print(hdr)
    print("-" * len(hdr))

    best = None
    for percent in range(0, 101, 5):
        sigma = dsr_smoothness_to_sigma(percent)
        img = resample(over, panel_h, panel_w, f"gaussian:{sigma}")
        _, p, s, a, g = report("", img)

        marker = ""
        if percent == 33 or (percent == 35 and True):
            marker = ""
        print(f"{percent:>10}%{sigma:>8.3f}{p:>10.2f}{s:>8.4f}{a:>12.1f}"
              f"{g:>10.2f}x{marker}")

        if best is None or p > best[1]:
            best = (percent, p, sigma, s, a, g)

    # The default, called out separately: it is not on the 5% grid.
    default_sigma = dsr_smoothness_to_sigma(33)
    default_img = resample(over, panel_h, panel_w, f"gaussian:{default_sigma}")
    _, default_psnr, _, default_alias, _ = report("", default_img)

    print()
    print(f"DSR default (33%, sigma {default_sigma:.3f}): "
          f"PSNR {default_psnr:.2f} dB, aliasing {default_alias:.1f}")
    print(f"best Gaussian ({best[0]}%, sigma {best[2]:.3f}): "
          f"PSNR {best[1]:.2f} dB, aliasing {best[4]:.1f}")
    print(f"  moving the slider is worth {best[1] - default_psnr:+.2f} dB")

    print()
    print("Against the alternatives, all at the same 1.50x ratio and the same cost:")
    print()
    hdr2 = f"{'rendering':<34}{'PSNR dB':>10}{'SSIM':>8}{'alias err':>12}{'sharpness':>11}"
    print(hdr2)
    print("-" * len(hdr2))

    rows = [
        report("native 1440p, no supersampling", native),
        report(f"DSR-style Gaussian, default 33%", default_img),
        report(f"DSR-style Gaussian, best {best[0]}%",
               resample(over, panel_h, panel_w, f"gaussian:{best[2]}")),
        report("lanczos2", resample(over, panel_h, panel_w, "lanczos2")),
    ]
    cfg = PipelineConfig(kernel="lanczos2", sharpness=0.25, denoise=False)
    rows.append(report("lanczos2 + RCAS (what we ship)",
                       supersample(to_rgb(over), panel_h, panel_w, cfg)[..., 0]))

    for name, p, s, a, g in rows:
        print(f"{name:<34}{p:>10.2f}{s:>8.4f}{a:>12.1f}{g:>10.2f}x")

    ours = rows[-1][1]
    print()
    print(f"Headroom left by the best Gaussian: {ours - best[1]:+.2f} dB")
    print()
    print("sharpness is measured against ground truth: 1.00x is correct.")
    print("Above 1.00x with a poor PSNR is aliasing being mistaken for detail.")

    ratio_sweep()
    return 0


def ratio_sweep() -> None:
    """How much the oversampling ratio is worth, which is the comparison that
    actually decides anything.

    Measured at three panel sizes rather than one. A single size gave 1.75x a
    3 dB lead over 2.00x, which is not a real effect: it is where this scene's
    frequencies happened to land against that particular sampling grid. The
    same measurement at 1066x600 and 1366x768 put 1.75x more than 3 dB *below*
    2.00x. Any ratio conclusion drawn from one grid is a coincidence waiting to
    be quoted, so all three are printed and only their agreement is worth
    trusting.
    """
    panels = [(720, 1280), (600, 1066), (768, 1366)]
    ratios = [1.25, 1.50, 1.75, 2.00, 2.25]

    print()
    print()
    print("Oversampling ratio, lanczos2 resolve, PSNR dB at three sampling phases")
    print()
    header = f"{'ratio':>7}{'cost':>8}"
    for ph, pw in panels:
        header += f"{f'{pw}x{ph}':>13}"
    header += f"{'mean':>9}"
    print(header)
    print("-" * len(header))

    references = [(ph, pw, reference_render(ph, pw, 6)) for ph, pw in panels]

    for ratio in ratios:
        values = []
        for ph, pw, ref in references:
            over = sample_grid(int(round(ph * ratio)), int(round(pw * ratio)))
            img = np.clip(resample(over, ph, pw, "lanczos2"), 0.0, 1.0)
            values.append(psnr(img, ref))

        line = f"{ratio:>6.2f}x{ratio * ratio:>7.2f}x"
        for value in values:
            line += f"{value:>13.2f}"
        line += f"{float(np.mean(values)):>9.2f}"
        print(line)

    print()
    print("cost is the pixel count relative to rendering at panel resolution.")
    print("1.50x is what DSR 2.25x and DLDSR 2.25x give on a 1440p panel;")
    print("2.00x is DSR 4.00x, which DLDSR does not offer.")


if __name__ == "__main__":
    raise SystemExit(main())
