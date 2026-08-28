"""RCAS invariants.  Violating any of these produces visible halos or clipping."""

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from visual4k_ref.rcas import rcas


def test_flat_field_untouched():
    """No contrast means no lobe; anything else is noise amplification."""
    flat = np.full((32, 32, 3), 0.5)
    assert np.abs(rcas(flat) - flat).max() == 0.0


@pytest.mark.parametrize("sharpness", [0.0, 0.25, 1.0, 2.0])
def test_output_stays_in_range(sharpness):
    """Overshoot past 1.0 clips to white and eats specular highlights."""
    rng = np.random.default_rng(0)
    img = rng.random((48, 48, 3))
    out = rcas(img, sharpness)
    assert out.min() >= 0.0 and out.max() <= 1.0


def test_no_halo_on_a_step_edge():
    """The result may not leave the range its own 5-tap neighbourhood spans."""
    img = np.zeros((32, 32, 3))
    img[:, 16:, :] = 1.0
    out = rcas(img, 0.0)
    assert set(np.unique(np.round(out, 6))) <= {0.0, 1.0}


def test_sharpness_is_monotone():
    """Higher 'stops' must mean weaker sharpening, matching FSR's convention."""
    rng = np.random.default_rng(1)
    from visual4k_ref.resample import resample
    img = np.clip(resample(rng.random((32, 32, 3)), 128, 128, "lanczos3"), 0, 1)

    def energy(x):
        return float((np.diff(x, axis=0) ** 2).mean() + (np.diff(x, axis=1) ** 2).mean())

    e = [energy(rcas(img, s)) for s in (0.0, 0.5, 1.0, 2.0, 4.0)]
    assert all(e[i] > e[i + 1] for i in range(len(e) - 1))
    assert e[-1] > energy(img) * 0.999      # still sharpening, just barely


def test_alpha_is_passed_through():
    rng = np.random.default_rng(2)
    img = rng.random((16, 16, 4))
    out = rcas(img)
    assert np.array_equal(out[..., 3], img[..., 3])


def test_denoise_never_strengthens_the_filter():
    """The noise term is an attenuator; it must only ever back the filter off."""
    rng = np.random.default_rng(3)
    img = rng.random((64, 64, 3))
    base = rcas(img, 0.0, denoise=False)
    den = rcas(img, 0.0, denoise=True)
    assert np.abs(den - img).sum() <= np.abs(base - img).sum() + 1e-9


def test_rejects_greyscale():
    with pytest.raises(ValueError):
        rcas(np.zeros((8, 8)))
