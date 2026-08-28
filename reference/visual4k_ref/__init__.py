"""Visual-4k reference implementation.

Normative CPU model of the GPU compositor in ``host/visual4k-host``.  Every
shader in ``shaders/`` has its behaviour pinned by a test in
``reference/tests`` that compares it against the functions exported here.
"""

from .kernels import KERNELS, dsr_smoothness_to_sigma, kernel_by_name
from .metrics import aliasing_energy, gradient_energy, psnr, ssim
from .pipeline import PipelineConfig, supersample, upscale
from .rcas import rcas
from .resample import build_taps, resample

__version__ = "0.1.0"

__all__ = [
    "KERNELS", "kernel_by_name", "dsr_smoothness_to_sigma",
    "build_taps", "resample", "rcas",
    "PipelineConfig", "supersample", "upscale",
    "psnr", "ssim", "gradient_energy", "aliasing_energy",
]
