#!/usr/bin/env python3
"""visual4k -- run the Visual-4k pipeline on image files, with no driver needed.

This is the pipeline the GPU compositor runs, applied offline. Use it to see
what the compositor will do to your own screenshots before installing anything,
and to A/B kernels and sharpness settings on content you actually care about.

    # Resolve a 4K screenshot down to your panel, the way the compositor would
    visual4k.py resolve shot-4k.png out.png --width 2560 --height 1440

    # Side-by-side against a naive downscale, with quality numbers
    visual4k.py compare shot-4k.png comparison.png --width 2560 --height 1440

    # No screenshot handy? Render the synthetic benchmark scene instead
    visual4k.py demo demo.png

Requires: numpy, pillow
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reference"))

from visual4k_ref.metrics import gradient_energy, psnr, ssim      # noqa: E402
from visual4k_ref.pipeline import PipelineConfig, supersample      # noqa: E402
from visual4k_ref.resample import resample                         # noqa: E402

try:
    from PIL import Image
except ImportError:                                                # pragma: no cover
    sys.exit("visual4k needs pillow: pip install pillow numpy")


# ---------------------------------------------------------------------------
# I/O
# ---------------------------------------------------------------------------

def load_image(path: str) -> np.ndarray:
    """Load an image as float RGB in [0, 1]."""
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.float64) / 255.0


def save_image(array: np.ndarray, path: str) -> None:
    """Save float RGB in [0, 1], rounding rather than truncating.

    Truncation here would cost half a code value across the whole image, which
    is small but systematic -- exactly the kind of bias that makes an A/B
    comparison lie.
    """
    data = np.clip(array, 0.0, 1.0)
    Image.fromarray(np.round(data * 255.0).astype(np.uint8)).save(path)


def build_config(args) -> PipelineConfig:
    return PipelineConfig(
        kernel=args.kernel,
        sharpness=None if args.sharpness < 0 else args.sharpness,
        denoise=args.denoise,
        linear_resolve=args.linear,
    )


def target_size(src_h: int, src_w: int, args) -> tuple[int, int]:
    """Destination geometry, defaulting to the 1440p panel this project targets."""
    if args.width and args.height:
        return args.height, args.width
    if args.width:
        return int(round(src_h * args.width / src_w)), args.width
    if args.height:
        return args.height, int(round(src_w * args.height / src_h))
    return 1440, 2560


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_resolve(args) -> int:
    src = load_image(args.input)
    dst_h, dst_w = target_size(src.shape[0], src.shape[1], args)

    if dst_h > src.shape[0] or dst_w > src.shape[1]:
        print(f"note: {src.shape[1]}x{src.shape[0]} -> {dst_w}x{dst_h} is a "
              f"magnification.\n"
              f"      This path cannot add detail, only spread the existing "
              f"detail more gracefully.", file=sys.stderr)

    out = supersample(src, dst_h, dst_w, build_config(args))
    save_image(out, args.output)

    ratio = src.shape[1] / dst_w
    print(f"{src.shape[1]}x{src.shape[0]} -> {dst_w}x{dst_h} "
          f"({ratio:.2f}x linear), kernel {args.kernel}, "
          f"sharpness {args.sharpness}")
    print(f"wrote {args.output}")
    return 0


def cmd_compare(args) -> int:
    """Render the same source three ways and stack them into one image.

    The point is the middle panel: a naive bilinear downscale is what most
    software does, and it is the honest thing to beat, not a nearest-neighbour
    straw man.
    """
    src = load_image(args.input)
    dst_h, dst_w = target_size(src.shape[0], src.shape[1], args)

    # What you see today: one sample per destination pixel, no filtering. This
    # is the aliasing a native render at panel resolution produces.
    ys = np.clip(((np.arange(dst_h) + 0.5) * src.shape[0] / dst_h).astype(int),
                 0, src.shape[0] - 1)
    xs = np.clip(((np.arange(dst_w) + 0.5) * src.shape[1] / dst_w).astype(int),
                 0, src.shape[1] - 1)
    naive = src[ys][:, xs]

    bilinear = np.clip(resample(src, dst_h, dst_w, "triangle"), 0.0, 1.0)
    ours = supersample(src, dst_h, dst_w, build_config(args))

    # Ground truth: box-average every source pixel that falls in a destination
    # pixel. Only exact for integer ratios, so it is reported as a reference
    # rather than used to declare a winner at arbitrary scales.
    truth = np.clip(resample(src, dst_h, dst_w, "box"), 0.0, 1.0)

    print(f"{src.shape[1]}x{src.shape[0]} -> {dst_w}x{dst_h}\n")
    header = f"{'method':<22}{'PSNR dB':>9}{'SSIM':>9}{'sharpness':>12}"
    print(header)
    print("-" * len(header))
    ref_grad = gradient_energy(truth)
    for name, img in (("point sample (today)", naive),
                      ("bilinear", bilinear),
                      (f"visual4k / {args.kernel}", ours)):
        print(f"{name:<22}{psnr(img, truth):>9.2f}{ssim(img, truth):>9.4f}"
              f"{gradient_energy(img) / ref_grad:>11.2f}x")

    print("\nsharpness is measured against the band-limited reference: 1.00x is\n"
          "correct. Well above 1.00x with a poor PSNR is aliasing, not detail.")

    # Stack the three vertically with a thin separator so the crop lines up.
    gap = np.zeros((8, dst_w, 3))
    stacked = np.concatenate([naive, gap, bilinear, gap, ours], axis=0)
    save_image(stacked, args.output)
    print(f"\nwrote {args.output} (point sample / bilinear / visual4k, top to bottom)")
    return 0


def cmd_demo(args) -> int:
    """Render the benchmark scene and its comparison without needing an input."""
    from bench_supersample import reference_render, sample_grid

    panel_h, panel_w = args.size, args.size * 16 // 9
    over_h, over_w = int(panel_h * 1.5), int(panel_w * 1.5)

    print(f"rendering the benchmark scene at {panel_w}x{panel_h} "
          f"(oversampled {over_w}x{over_h})...")

    truth = reference_render(panel_h, panel_w, 6)
    native = sample_grid(panel_h, panel_w)
    over = sample_grid(over_h, over_w)

    rgb = lambda g: np.repeat(g[..., None], 3, axis=2)     # noqa: E731
    ours = supersample(rgb(over), panel_h, panel_w, build_config(args))[..., 0]

    header = f"{'rendering':<26}{'PSNR dB':>9}{'SSIM':>9}"
    print(f"\n{header}")
    print("-" * len(header))
    print(f"{'native (1 sample/pixel)':<26}{psnr(native, truth):>9.2f}"
          f"{ssim(native, truth):>9.4f}")
    print(f"{'visual4k':<26}{psnr(ours, truth):>9.2f}{ssim(ours, truth):>9.4f}")

    gap = np.zeros((8, panel_w))
    stacked = np.concatenate([native, gap, ours, gap, truth], axis=0)
    save_image(rgb(stacked), args.output)
    print(f"\nwrote {args.output}")
    print("top to bottom: native 1440p / visual4k / ground truth")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="visual4k",
        description="Run the Visual-4k resolve pipeline on image files.")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p, with_io=True):
        if with_io:
            p.add_argument("input")
            p.add_argument("output")
        p.add_argument("--width", type=int, default=0,
                       help="destination width (default: 2560)")
        p.add_argument("--height", type=int, default=0,
                       help="destination height (default: 1440)")
        p.add_argument("--kernel", default="lanczos2",
                       help="triangle|catrom|mitchell|lanczos2|lanczos3|"
                            "lanczos4|gaussian (default: lanczos2)")
        p.add_argument("--sharpness", type=float, default=0.25,
                       help="RCAS stops; 0 is strongest, negative disables")
        p.add_argument("--denoise", action="store_true",
                       help="RCAS noise attenuation (video and film)")
        p.add_argument("--linear", action="store_true",
                       help="resolve in linear light (video; see docs)")

    p_resolve = sub.add_parser("resolve", help="resolve one image to panel size")
    add_common(p_resolve)
    p_resolve.set_defaults(func=cmd_resolve)

    p_compare = sub.add_parser("compare",
                               help="stack point-sample / bilinear / visual4k")
    add_common(p_compare)
    p_compare.set_defaults(func=cmd_compare)

    p_demo = sub.add_parser("demo", help="render the synthetic benchmark scene")
    p_demo.add_argument("output")
    p_demo.add_argument("--size", type=int, default=720,
                        help="panel height; width follows at 16:9")
    add_common(p_demo, with_io=False)
    p_demo.set_defaults(func=cmd_demo)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
