#!/usr/bin/env python3
"""Checks that the driver is compiled for the IddCx version its INF requests.

These two facts live in different files, in different notations, and neither
build step can see the other:

    Visual4kDisplay.vcxproj:  IDDCX_VERSION_MAJOR=1, IDDCX_VERSION_MINOR=2
    Visual4kDisplay.inf:      UmdfExtensions = IddCx0102

The macros decide the size and layout of IDD_CX_CLIENT_CONFIG. The INF decides
which class extension Windows loads. When they disagree, the driver hands the
extension a structure it does not recognise, IddCxDeviceInitConfig refuses it,
and EvtDeviceAdd returns that failure. The device then installs perfectly --
package staged, node created, driver bound -- and fails to start with
CM_PROB_FAILED_ADD, which reads like a driver bug rather than a build
configuration mismatch.

That is exactly what happened, and it cost a full install cycle on a real
machine to find, because every individual step reported success.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VCXPROJ = ROOT / "driver" / "Visual4kDisplay" / "Visual4kDisplay.vcxproj"
INF = ROOT / "driver" / "Visual4kDisplay" / "Visual4kDisplay.inf"


def project_version() -> "tuple[int, int]":
    text = VCXPROJ.read_text(encoding="utf-8")
    major = re.search(r"<Visual4kIddCxMajor[^>]*>(\d+)</Visual4kIddCxMajor>", text)
    minor = re.search(r"<Visual4kIddCxMinor[^>]*>(\d+)</Visual4kIddCxMinor>", text)
    if not major or not minor:
        raise SystemExit(f"could not find the IddCx version in {VCXPROJ.name}")
    return int(major.group(1)), int(minor.group(1))


def inf_version() -> "tuple[int, int]":
    text = INF.read_text(encoding="utf-8")
    # UmdfExtensions = IddCx0102  ->  major 01, minor 02
    match = re.search(r"UmdfExtensions\s*=\s*IddCx(\d{2})(\d{2})", text,
                      re.IGNORECASE)
    if not match:
        raise SystemExit(f"could not find UmdfExtensions in {INF.name}")
    return int(match.group(1)), int(match.group(2))


def main() -> int:
    project = project_version()
    inf = inf_version()

    print(f"  compiled for : IddCx {project[0]}.{project[1]}"
          f"  ({VCXPROJ.name})")
    print(f"  INF requests : IddCx {inf[0]}.{inf[1]}"
          f"  ({INF.name})")

    if project != inf:
        print()
        print("These must be equal.")
        print("A driver compiled for one IddCx version and installed against")
        print("another starts up, fails IddCxDeviceInitConfig, and reports")
        print("CM_PROB_FAILED_ADD -- with every build step reporting success.")
        print()
        print(f"Either set Visual4kIddCxMinor to {inf[1]} in {VCXPROJ.name},")
        print(f"or set UmdfExtensions to IddCx{project[0]:02d}{project[1]:02d} "
              f"in {INF.name}.")
        return 1

    print()
    print("The compiled IddCx version matches the one the INF requests.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
