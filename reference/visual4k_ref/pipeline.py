"""The Visual-4k supersampling pipeline, end to end, on the CPU.

This mirrors what ``visual4k-host`` does per frame on the GPU:

    4K virtual display frame
      -> (optional) sRGB decode
      -> separable Lanczos/Gaussian resolve to the panel's native resolution
      -> (optional) sRGB encode
      -> RCAS sharpening
      -> present on the physical 1440p panel

The only difference is precision and speed: the shader runs in fp16/fp32 on
the GPU, this runs in fp64.  Use this to answer "what *should* frame N look
like?" when the compositor output looks wrong.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .rcas import rcas
from .resample import resample

__all__ = ["PipelineConfig", "supersample", "upscale"]


def srgb_to_linear(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(x: np.ndarray) -> np.ndarray:
    x = np.clip(np.asarray(x, dtype=np.float64), 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1.0 / 2.4) - 0.055)


@dataclass
class PipelineConfig:
    """Everything the compositor needs to resolve one frame.

    The kernel default is ``lanczos2`` rather than the more obvious
    ``lanczos3``: ``reference/bench_supersample.py`` measures lanczos3 as
    *worse* at every supersampling ratio this compositor actually runs at
    (1.25x-2.0x linear).  A wider sinc only pays off when there is a lot of
    stopband to suppress, and at 1.5x there is not; the extra lobes just add
    ringing.  Re-run that benchmark before changing this.

    ``linear_resolve`` is the interesting knob.  Averaging pixels is only
    physically correct in linear light, and doing it in gamma space measurably
    darkens thin bright features -- white text on black is the worst case.
    But *most* desktop content is authored to be resampled in gamma space, and
    linear resolve makes antialiased text look thinner than the same text on a
    real 4K panel.  Default: linear off for the desktop, on for video.
    """

    kernel: str = "lanczos2"      # measured best at 1.25x-2.0x; see docs/ALGORITHMS.md
    sharpness: float = 0.25       # RCAS stops; None disables the pass
    denoise: bool = False         # off for synthetic UI, on for camera video
    linear_resolve: bool = False


def supersample(frame: np.ndarray, dst_h: int, dst_w: int,
                config: PipelineConfig | None = None) -> np.ndarray:
    """Resolve an oversampled frame down to the panel's native resolution.

    This is the operation that makes the whole project worth building: the
    detail in the output came from geometry that was genuinely rasterised at
    4K, so edges carry sub-pixel information a native 1440p render never had.
    """
    config = config or PipelineConfig()
    frame = np.asarray(frame, dtype=np.float64)

    work = srgb_to_linear(frame) if config.linear_resolve else frame
    work = resample(work, dst_h, dst_w, config.kernel)
    if config.linear_resolve:
        work = linear_to_srgb(work)

    work = np.clip(work, 0.0, 1.0)

    if config.sharpness is not None and work.ndim == 3 and work.shape[2] >= 3:
        work = rcas(work, config.sharpness, config.denoise)

    return work


def upscale(frame: np.ndarray, dst_h: int, dst_w: int,
            config: PipelineConfig | None = None) -> np.ndarray:
    """Magnify low-resolution content (video, older games) toward the panel grid.

    Be clear about what this can and cannot do: magnification invents no new
    detail, it only chooses how gracefully the existing detail is spread over
    more pixels.  Lanczos-3 plus a light RCAS pass is the honest ceiling for a
    real-time spatial filter; anything sharper than this is a neural upscaler,
    which is a different project with a different latency budget.
    """
    config = config or PipelineConfig(kernel="lanczos3", sharpness=0.4,
                                      denoise=True, linear_resolve=True)
    frame = np.asarray(frame, dtype=np.float64)

    work = srgb_to_linear(frame) if config.linear_resolve else frame
    work = resample(work, dst_h, dst_w, config.kernel)
    if config.linear_resolve:
        work = linear_to_srgb(work)

    work = np.clip(work, 0.0, 1.0)

    if config.sharpness is not None and work.ndim == 3 and work.shape[2] >= 3:
        work = rcas(work, config.sharpness, config.denoise)

    return work
