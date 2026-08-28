"""Properties the resampler must hold for the compositor to be trustworthy.

Each failure here corresponds to a visible artefact on the panel, noted in
the test's docstring, so a red test says what the user would have seen.
"""

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from visual4k_ref.kernels import KERNELS, kernel_by_name
from visual4k_ref.resample import build_taps, resample

ALL = sorted(KERNELS)
INTERPOLATING = ["triangle", "catrom", "lanczos2", "lanczos3", "lanczos4"]


@pytest.mark.parametrize("name", ALL)
def test_weights_sum_to_one(name):
    """Rows that do not sum to 1 show up as bright or dark banding."""
    taps = build_taps(1080, 720, KERNELS[name])
    assert np.allclose(taps.weights.sum(axis=1), 1.0, atol=1e-12)


@pytest.mark.parametrize("name", ALL)
def test_constant_field_is_preserved(name):
    """Any deviation here is a flat-grey desktop that pulses when it scrolls."""
    src = np.full((216, 384), 0.42)
    out = resample(src, 144, 256, name)
    assert np.abs(out - 0.42).max() < 1e-12


@pytest.mark.parametrize("name", INTERPOLATING)
def test_same_size_is_identity(name):
    """A 1:1 path must be bit-clean, or the compositor softens native content."""
    rng = np.random.default_rng(0)
    src = rng.random((48, 64))
    out = resample(src, 48, 64, name)
    assert np.abs(out - src).max() < 1e-9


@pytest.mark.parametrize("name", ALL)
def test_no_half_pixel_shift(name):
    """A shifted resolve is the classic 'sharp but subtly wrong' bug."""
    src = np.zeros((64, 64))
    src[31:33, 31:33] = 0.25          # straddles the exact image centre
    out = resample(src, 32, 32, name)
    ys, xs = np.indices(out.shape)
    total = out.sum()
    assert abs((ys * out).sum() / total - 15.5) < 1e-9
    assert abs((xs * out).sum() / total - 15.5) < 1e-9


@pytest.mark.parametrize("name", ALL)
def test_downsample_widens_the_kernel(name):
    """Without widening, the resolve aliases instead of low-passing."""
    narrow = build_taps(100, 100, KERNELS[name])
    wide = build_taps(400, 100, KERNELS[name])
    assert wide.n_taps > narrow.n_taps


def test_separable_matches_explicit_two_pass():
    """Guards the axis-ordering optimisation in resample()."""
    rng = np.random.default_rng(3)
    src = rng.random((80, 120, 3))
    k = kernel_by_name("lanczos3")
    a = resample(src, 40, 90, "lanczos3")
    b = np.moveaxis(src, 0, 0)
    from visual4k_ref.resample import resample_axis
    b = resample_axis(src, build_taps(120, 90, k), axis=1)
    b = resample_axis(b, build_taps(80, 40, k), axis=0)
    assert np.allclose(a, b, atol=1e-12)


def test_channels_are_independent():
    """Cross-channel bleed would tint the whole desktop."""
    rng = np.random.default_rng(4)
    src = rng.random((60, 60, 3))
    out = resample(src, 30, 30, "lanczos3")
    for c in range(3):
        assert np.allclose(out[..., c], resample(src[..., c], 30, 30, "lanczos3"))


def test_edges_do_not_darken():
    """Clamp-to-edge addressing must not lose energy at the border."""
    src = np.ones((64, 64))
    out = resample(src, 32, 32, "lanczos3")
    assert np.abs(out - 1.0).max() < 1e-12


def test_rejects_mismatched_axis():
    taps = build_taps(100, 50, KERNELS["lanczos3"])
    from visual4k_ref.resample import resample_axis
    with pytest.raises(ValueError):
        resample_axis(np.zeros((99, 10)), taps, axis=0)
