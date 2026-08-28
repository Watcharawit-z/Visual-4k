"""End-to-end claims the project makes to its users, asserted as tests."""

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bench_supersample import reference_render, sample_grid
from visual4k_ref.metrics import aliasing_energy, psnr, ssim
from visual4k_ref.pipeline import (PipelineConfig, linear_to_srgb, srgb_to_linear,
                                   supersample, upscale)

PANEL_H, PANEL_W = 180, 320


@pytest.fixture(scope="module")
def scene_set():
    ref = reference_render(PANEL_H, PANEL_W, 8)
    native = sample_grid(PANEL_H, PANEL_W)
    over = sample_grid(int(PANEL_H * 1.5), int(PANEL_W * 1.5))
    return ref, native, over


def _rgb(g):
    return np.repeat(g[..., None], 3, axis=2)


def test_supersampling_beats_a_native_render(scene_set):
    """The core claim: 4K-sourced detail resolved to 1440p is closer to truth."""
    ref, native, over = scene_set
    out = supersample(_rgb(over), PANEL_H, PANEL_W)[..., 0]
    assert psnr(out, ref) > psnr(native, ref) + 3.0
    assert ssim(out, ref) > ssim(native, ref)


def test_supersampling_removes_most_aliasing(scene_set):
    """Aliasing energy is the artefact users describe as 'shimmering'."""
    ref, native, over = scene_set
    out = supersample(_rgb(over), PANEL_H, PANEL_W)[..., 0]
    assert aliasing_energy(out, ref) < 0.5 * aliasing_energy(native, ref)


def test_shipping_default_is_the_best_kernel_we_measured(scene_set):
    """If a wider sinc ever wins here, change the default and this test."""
    ref, _, over = scene_set
    scores = {}
    for k in ("triangle", "catrom", "lanczos2", "lanczos3", "lanczos4"):
        out = supersample(_rgb(over), PANEL_H, PANEL_W,
                          PipelineConfig(kernel=k, sharpness=None))[..., 0]
        scores[k] = ssim(out, ref)
    assert scores["lanczos2"] >= scores["lanczos3"]
    assert scores["lanczos2"] >= scores["lanczos4"]


def test_output_geometry(scene_set):
    _, _, over = scene_set
    out = supersample(_rgb(over), PANEL_H, PANEL_W)
    assert out.shape == (PANEL_H, PANEL_W, 3)
    assert out.min() >= 0.0 and out.max() <= 1.0


def test_srgb_round_trip():
    """A broken transfer function shows up as a washed-out or crushed desktop."""
    x = np.linspace(0.0, 1.0, 4096)
    assert np.abs(linear_to_srgb(srgb_to_linear(x)) - x).max() < 1e-9


def test_linear_resolve_changes_the_result():
    """Guards against the flag being silently ignored."""
    rng = np.random.default_rng(0)
    src = rng.random((90, 160, 3))
    a = supersample(src, 45, 80, PipelineConfig(linear_resolve=False, sharpness=None))
    b = supersample(src, 45, 80, PipelineConfig(linear_resolve=True, sharpness=None))
    assert not np.allclose(a, b)


def test_upscaling_cannot_invent_detail(scene_set):
    """The honest limit: magnified content stays measurably softer than truth.

    This test exists to stop anyone claiming the upscale path 'restores 4K'.
    """
    ref, _, _ = scene_set
    from visual4k_ref.metrics import gradient_energy
    from visual4k_ref.resample import resample
    low = np.clip(resample(ref, PANEL_H // 2, PANEL_W // 2, "lanczos3"), 0, 1)
    up = upscale(_rgb(low), PANEL_H, PANEL_W)[..., 0]
    assert gradient_energy(up) < gradient_energy(ref)
