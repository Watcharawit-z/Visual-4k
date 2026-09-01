#!/usr/bin/env python3
"""Checks the driver is built for the framework versions its INF asks Windows to load.

Two versions have to agree between two files that no build step compares:

    Visual4kDisplay.vcxproj          Visual4kDisplay.inf
    UMDF_VERSION_MAJOR/MINOR    <->  UmdfLibraryVersion = 2.33.0
    IDDCX_VERSION_MAJOR/MINOR   <->  UmdfExtensions     = IddCx0102

The macros decide the size and layout of the structures the driver hands the
framework. The INF decides which framework binary Windows loads. When they
disagree, the driver is handed to a loader that does not recognise what it
passes, EvtDeviceAdd returns a failure, and the device installs perfectly --
package staged, node created, driver bound -- then refuses to start with
CM_PROB_FAILED_ADD.

That has now happened twice, once for each version, and the second time only
because the first fix was applied to IddCx alone while the identical mistake
sat one line away in the same file. Both are checked here for that reason.

Neither failure is visible to a build. Both halves compile, link, sign and
install; the disagreement only exists at load time on a real machine.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VCXPROJ = ROOT / "driver" / "Visual4kDisplay" / "Visual4kDisplay.vcxproj"
INF = ROOT / "driver" / "Visual4kDisplay" / "Visual4kDisplay.inf"


def find(text: str, pattern: str, what: str) -> "re.Match":
    match = re.search(pattern, text, re.IGNORECASE)
    if not match:
        raise SystemExit(f"could not find {what}")
    return match


def main() -> int:
    project = VCXPROJ.read_text(encoding="utf-8")
    inf = INF.read_text(encoding="utf-8")

    checks = []

    # --- UMDF -------------------------------------------------------------
    major = find(project, r"<UmdfVersionMajor[^>]*>(\d+)</UmdfVersionMajor>",
                 "UmdfVersionMajor in the project")
    minor = find(project, r"<UmdfVersionMinor[^>]*>(\d+)</UmdfVersionMinor>",
                 "UmdfVersionMinor in the project")
    library = find(inf, r"UmdfLibraryVersion\s*=\s*(\d+)\.(\d+)\.(\d+)",
                   "UmdfLibraryVersion in the INF")
    checks.append((
        "UMDF",
        (int(major.group(1)), int(minor.group(1))),
        (int(library.group(1)), int(library.group(2))),
        f"UmdfVersionMinor in {VCXPROJ.name}",
        f"UmdfLibraryVersion in {INF.name}",
    ))

    # --- IddCx ------------------------------------------------------------
    major = find(project, r"<Visual4kIddCxMajor[^>]*>(\d+)</Visual4kIddCxMajor>",
                 "Visual4kIddCxMajor in the project")
    minor = find(project, r"<Visual4kIddCxMinor[^>]*>(\d+)</Visual4kIddCxMinor>",
                 "Visual4kIddCxMinor in the project")
    extension = find(inf, r"UmdfExtensions\s*=\s*IddCx(\d{2})(\d{2})",
                     "UmdfExtensions in the INF")
    checks.append((
        "IddCx",
        (int(major.group(1)), int(minor.group(1))),
        (int(extension.group(1)), int(extension.group(2))),
        f"Visual4kIddCxMinor in {VCXPROJ.name}",
        f"UmdfExtensions in {INF.name}",
    ))

    failures = 0
    for name, built, declared, project_where, inf_where in checks:
        status = "ok  " if built == declared else "WRONG"
        print(f"  {status} {name:<6} built for {built[0]}.{built[1]}, "
              f"INF requests {declared[0]}.{declared[1]}")
        if built != declared:
            failures += 1
            print(f"        {project_where} and {inf_where} disagree.")

    print()
    if failures:
        print(f"{failures} version mismatch(es).")
        print("A driver built for one framework version and loaded against")
        print("another starts, fails in EvtDeviceAdd, and reports")
        print("CM_PROB_FAILED_ADD -- with every build step reporting success.")
        return 1

    print("The driver is built for the versions its INF requests.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
