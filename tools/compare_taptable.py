"""Cross-check the compositor's C++ tap tables against the Python reference.

Run as part of the test suite (reference/tests/test_cpp_parity.py) whenever a
C++ toolchain is available.  A mismatch here means the GPU is filtering with
different coefficients than the reference the project's quality claims were
measured with, which would make every benchmark in docs/ALGORITHMS.md a lie.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference"))

from visual4k_ref.kernels import kernel_by_name          # noqa: E402
from visual4k_ref.resample import build_taps             # noqa: E402

CASES = [
    # The shipping desktop path: a 4K virtual display on a 1440p panel.
    (3840, 2560, "lanczos2"),
    (2160, 1440, "lanczos2"),
    # A 2.0x DSR-style ratio, and a non-integer awkward one.
    (5120, 2560, "lanczos3"),
    (2646, 1440, "catrom"),
    # Magnification (the video path) and the 1:1 passthrough.
    (1280, 2560, "lanczos3"),
    (1440, 1440, "lanczos2"),
    # Small sizes, where off-by-one tap counts show up first.
    (17, 5, "triangle"),
    (5, 17, "mitchell"),
    (64, 63, "lanczos4"),
]


def run_cpp(binary: Path, src: int, dst: int, kernel: str):
    out = subprocess.run([str(binary), str(src), str(dst), kernel],
                         capture_output=True, text=True, check=True).stdout
    rows = [ln for ln in out.splitlines() if ln and not ln.startswith("#")]
    first = np.array([int(r.split(",")[0]) for r in rows], dtype=np.int64)
    weights = np.array([[float(v) for v in r.split(",")[1:]] for r in rows],
                       dtype=np.float64)
    return first, weights


def main() -> int:
    binary = ROOT / "build" / "taptable_selftest"
    if not binary.exists():
        print(f"missing {binary}; build it first (see tools/taptable_selftest.cpp)")
        return 77                       # skip, not fail

    worst = 0.0
    failures = 0

    for src, dst, kernel in CASES:
        c_first, c_w = run_cpp(binary, src, dst, kernel)
        py = build_taps(src, dst, kernel_by_name(kernel))

        label = f"{src:>5} -> {dst:<5} {kernel:<9}"

        if c_w.shape != py.weights.shape:
            print(f"{label} FAIL tap count: C++ {c_w.shape[1]} vs Python "
                  f"{py.weights.shape[1]}")
            failures += 1
            continue

        # Compare the *effective* source index each tap reads, since the C++
        # side clamps in the shader while the reference clamps in the table.
        c_idx = np.clip(c_first[:, None] + np.arange(c_w.shape[1]), 0, src - 1)
        if not np.array_equal(c_idx, py.indices):
            bad = int((c_idx != py.indices).sum())
            print(f"{label} FAIL {bad} tap indices differ")
            failures += 1
            continue

        # float32 storage on the C++ side sets the achievable tolerance.
        err = float(np.abs(c_w - py.weights).max())
        worst = max(worst, err)
        status = "ok" if err <= 1e-6 else "FAIL"
        if err > 1e-6:
            failures += 1
        print(f"{label} {status}  taps={c_w.shape[1]:<3} max weight delta {err:.3e}")

    print(f"\nworst weight delta across all cases: {worst:.3e}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
