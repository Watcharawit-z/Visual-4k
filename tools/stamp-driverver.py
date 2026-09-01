#!/usr/bin/env python3
"""Gives each build its own DriverVer, so an install actually replaces the old one.

Windows ranks driver packages, and a package whose DriverVer is not newer than
the one already in the store does not have to win. Every build so far shipped
`01/01/2025,1.0.0.0`, so reinstalling over a previous version could leave the
earlier binary bound to the device -- an update that silently updates nothing.

That is not hypothetical. A fix for an IddCx version mismatch was shipped, the
setup program reported "device already exists; updating its driver", and the
device stayed broken in exactly its old way. The fix may never have been
loaded.

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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="w.x.y.z; default is derived from the date")
    parser.add_argument("--check", action="store_true",
                        help="only report the current value, change nothing")
    args = parser.parse_args()

    text = INF.read_text(encoding="utf-8")
    match = PATTERN.search(text)
    if not match:
        print(f"no DriverVer line found in {INF.name}", file=sys.stderr)
        return 1

    print(f"current: {match.group('date')},{match.group('version')}")
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
    INF.write_text(PATTERN.sub(replacement, text, count=1), encoding="utf-8")
    print(f"stamped: {date},{version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
