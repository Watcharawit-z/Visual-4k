#!/usr/bin/env python3
"""Stamps the INF with values only the build knows: its DriverVer, and its IddCx version.

Windows ranks driver packages, and a package whose DriverVer is not newer than
the one already in the store does not have to win. Every build so far shipped
`01/01/2025,1.0.0.0`, so reinstalling over a previous version could leave the
earlier binary bound to the device -- an update that silently updates nothing.

That is not hypothetical. A fix for an IddCx version mismatch was shipped, the
setup program reported "device already exists; updating its driver", and the
device stayed broken in exactly its old way. The fix may never have been
loaded.

The IddCx version is stamped for a different reason. The INF's UmdfExtensions
line names the class extension Windows loads, and the driver has to be compiled
against the same one -- but the header gates its *callback tables* on the
version macros while leaving its *data structures* at their newest layout. So a
driver built for an older IddCx passes a correctly-shrunk IDD_CX_CLIENT_CONFIG
and a full-size IDDCX_ADAPTER_CAPS, and the second is rejected with
STATUS_INVALID_PARAMETER while the first sails through. That is exactly what
happened, and hand-writing the version in one file and discovering it in
another is what allowed it.

Both now come from the same place: whatever the kit being built against ships.

Called by CI before the driver is packaged. Without arguments it uses the
current UTC date and a version derived from it, which is enough to make every
build distinguishable and monotonic.
"""

import argparse
import datetime as dt
import re
import sys
from pathlib import Path

INF = Path(__file__).resolve().parent.parent / "driver" / "Visual4kDisplay" / \
      "Visual4kDisplay.inf"

# DriverVer = MM/DD/YYYY,w.x.y.z  with anything after it left alone.
PATTERN = re.compile(
    r"^(?P<lead>\s*DriverVer\s*=\s*)"
    r"(?P<date>\d{2}/\d{2}/\d{4})\s*,\s*"
    r"(?P<version>\d+\.\d+\.\d+\.\d+)"
    r"(?P<tail>.*)$",
    re.MULTILINE,
)

IDDCX_PATTERN = re.compile(
    r"(?P<lead>^\s*UmdfExtensions\s*=\s*)(?P<extension>IddCx\d{4})",
    re.MULTILINE | re.IGNORECASE,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="w.x.y.z; default is derived from the date")
    parser.add_argument("--iddcx", metavar="MAJOR.MINOR",
                        help="IddCx version to write into UmdfExtensions")
    parser.add_argument("--check", action="store_true",
                        help="only report the current values, change nothing")
    args = parser.parse_args()

    text = INF.read_text(encoding="utf-8")
    match = PATTERN.search(text)
    if not match:
        print(f"no DriverVer line found in {INF.name}", file=sys.stderr)
        return 1

    print(f"current: {match.group('date')},{match.group('version')}")

    extension_match = IDDCX_PATTERN.search(text)
    if extension_match:
        print(f"current: UmdfExtensions = {extension_match.group('extension')}")

    if args.check:
        return 0

    now = dt.datetime.now(dt.timezone.utc)
    date = now.strftime("%m/%d/%Y")

    # Minutes since the start of the year fits comfortably in the last field
    # and increases with every build, which is all the ordering needs.
    if args.version:
        version = args.version
    else:
        start = dt.datetime(now.year, 1, 1, tzinfo=dt.timezone.utc)
        minutes = int((now - start).total_seconds() // 60)
        version = f"1.0.{now.year % 100}.{minutes % 65536}"

    replacement = f"\\g<lead>{date},{version}\\g<tail>"
    text = PATTERN.sub(replacement, text, count=1)
    print(f"stamped: {date},{version}")

    if args.iddcx:
        try:
            major, minor = (int(part) for part in args.iddcx.split("."))
        except ValueError:
            print(f"--iddcx wants MAJOR.MINOR, got {args.iddcx!r}", file=sys.stderr)
            return 1

        extension = f"IddCx{major:02d}{minor:02d}"
        text, count = IDDCX_PATTERN.subn(f"\\g<lead>{extension}", text, count=1)
        if count != 1:
            print(f"no UmdfExtensions line found in {INF.name}", file=sys.stderr)
            return 1
        print(f"stamped: UmdfExtensions = {extension}")

    INF.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
