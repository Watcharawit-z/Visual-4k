"""The offline CLI is the only part of this project a user can run without
Windows, so it is the part most likely to be someone's first contact with it."""

import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tools" / "visual4k.py"

pytest.importorskip("PIL")
from PIL import Image  # noqa: E402


def _write_image(path, h=180, w=320):
    rng = np.random.default_rng(0)
    data = (rng.random((h, w, 3)) * 255).astype(np.uint8)
    Image.fromarray(data).save(path)
    return path


def _run(*args):
    return subprocess.run([sys.executable, str(CLI), *args],
                          capture_output=True, text=True)


def test_resolve_writes_requested_geometry(tmp_path):
    src = _write_image(tmp_path / "in.png")
    dst = tmp_path / "out.png"
    r = _run("resolve", str(src), str(dst), "--width", "160", "--height", "90")
    assert r.returncode == 0, r.stderr
    assert Image.open(dst).size == (160, 90)


def test_resolve_keeps_aspect_when_given_one_axis(tmp_path):
    src = _write_image(tmp_path / "in.png", h=180, w=320)
    dst = tmp_path / "out.png"
    r = _run("resolve", str(src), str(dst), "--width", "160")
    assert r.returncode == 0, r.stderr
    assert Image.open(dst).size == (160, 90)


def test_compare_stacks_three_renderings(tmp_path):
    src = _write_image(tmp_path / "in.png")
    dst = tmp_path / "cmp.png"
    r = _run("compare", str(src), str(dst), "--width", "160", "--height", "90")
    assert r.returncode == 0, r.stderr
    w, h = Image.open(dst).size
    assert w == 160
    assert h == 90 * 3 + 8 * 2          # three panels plus two separators
    assert "visual4k" in r.stdout


def test_demo_needs_no_input(tmp_path):
    dst = tmp_path / "demo.png"
    r = _run("demo", str(dst), "--size", "72")
    assert r.returncode == 0, r.stderr
    assert dst.exists()


def test_magnification_is_flagged_not_silently_done(tmp_path):
    """Upscaling cannot add detail; the CLI must say so rather than imply it can."""
    src = _write_image(tmp_path / "in.png", h=90, w=160)
    dst = tmp_path / "out.png"
    r = _run("resolve", str(src), str(dst), "--width", "320", "--height", "180")
    assert r.returncode == 0, r.stderr
    assert "cannot add detail" in r.stderr


def test_rejects_unknown_kernel(tmp_path):
    src = _write_image(tmp_path / "in.png")
    r = _run("resolve", str(src), str(tmp_path / "o.png"), "--kernel", "bogus")
    assert r.returncode != 0
