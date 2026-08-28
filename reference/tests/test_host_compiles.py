"""Syntax-check the Windows-only host sources against the stub headers.

Catches the class of error that would otherwise surface as a wall of compiler
output on a user's first build. It does not validate the real API surface --
see tools/winstub/README.md.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "check-host-compiles.sh"

pytestmark = pytest.mark.skipif(
    shutil.which("g++") is None and shutil.which("clang++") is None,
    reason="no C++ compiler available")


def test_host_sources_type_check():
    result = subprocess.run(["bash", str(SCRIPT)], cwd=ROOT,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
