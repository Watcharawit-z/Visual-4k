"""The GPU compositor and the reference must filter with identical coefficients.

Skipped when no C++ toolchain is present, so the suite still runs on a bare
Python checkout -- but on CI this is the test that stops the shader and the
reference silently drifting apart.
"""

import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "reference"))

from visual4k_ref.kernels import kernel_by_name
from visual4k_ref.resample import build_taps

CXX = shutil.which("g++") or shutil.which("clang++")
pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler available")


@pytest.fixture(scope="module")
def selftest_binary(tmp_path_factory):
    out = tmp_path_factory.mktemp("cpp") / "taptable_selftest"
    subprocess.run(
        [CXX, "-O2", "-std=c++17", "-I", str(ROOT / "host/visual4k-host/src"),
         str(ROOT / "tools/taptable_selftest.cpp"),
         str(ROOT / "host/visual4k-host/src/TapTable.cpp"),
         "-o", str(out)],
        check=True, capture_output=True)
    return out


def _cpp_table(binary, src, dst, kernel):
    out = subprocess.run([str(binary), str(src), str(dst), kernel],
                         capture_output=True, text=True, check=True).stdout
    rows = [ln for ln in out.splitlines() if ln and not ln.startswith("#")]
    first = np.array([int(r.split(",")[0]) for r in rows], dtype=np.int64)
    weights = np.array([[float(v) for v in r.split(",")[1:]] for r in rows])
    return first, weights


@pytest.mark.parametrize("src,dst,kernel", [
    (3840, 2560, "lanczos2"),      # the shipping desktop resolve, horizontal
    (2160, 1440, "lanczos2"),      # ... and vertical
    (5120, 2560, "lanczos3"),      # a 2.0x DSR-style ratio
    (2646, 1440, "catrom"),        # deliberately awkward, non-integer ratio
    (1280, 2560, "lanczos3"),      # the video magnification path
    (1440, 1440, "lanczos2"),      # 1:1 passthrough
    (17, 5, "triangle"),
    (5, 17, "mitchell"),
    (64, 63, "lanczos4"),
])
def test_cpp_matches_reference(selftest_binary, src, dst, kernel):
    c_first, c_w = _cpp_table(selftest_binary, src, dst, kernel)
    py = build_taps(src, dst, kernel_by_name(kernel))

    assert c_w.shape == py.weights.shape, "tap count differs"

    c_idx = np.clip(c_first[:, None] + np.arange(c_w.shape[1]), 0, src - 1)
    assert np.array_equal(c_idx, py.indices), "tap source indices differ"

    # float32 storage on the C++ side sets the tolerance floor.
    assert np.abs(c_w - py.weights).max() < 1e-6


def test_cpp_rows_normalise(selftest_binary):
    """A row that does not sum to 1 is visible as banding on the panel."""
    _, w = _cpp_table(selftest_binary, 3840, 2560, "lanczos2")
    assert np.abs(w.sum(axis=1) - 1.0).max() < 1e-6


def test_virtual_monitor_edid_is_valid(tmp_path_factory):
    """A malformed EDID appears on Windows only as 'monitor has no modes'."""
    out = tmp_path_factory.mktemp("edid") / "edid_selftest"
    subprocess.run(
        [CXX, "-O2", "-std=c++17", "-I", str(ROOT / "driver/Visual4kDisplay"),
         str(ROOT / "tools/edid_selftest.cpp"),
         str(ROOT / "driver/Visual4kDisplay/Edid.cpp"), "-o", str(out)],
        check=True, capture_output=True)
    result = subprocess.run([str(out)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
