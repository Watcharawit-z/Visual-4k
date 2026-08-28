#!/usr/bin/env python3
"""Parses every XML file in the tree, and says which line broke.

This exists because the same mistake has now cost three CI cycles: a double
hyphen inside an XML comment. It is illegal in XML, and the tools that consume
these files report it uselessly --

    general error c1010070: Failed to load and parse the manifest.
    Windows was unable to parse the requested XML data.

-- naming the file and not the line, on a Windows runner, ten minutes after the
edit. The comment reads perfectly well to a person, which is exactly why it
survives review.

(The paragraph above contains the offending sequence twice, inside a Python
docstring where it is harmless. That is deliberate: a checker nobody can read
the rationale for is a checker somebody deletes.)
"""

import sys
import xml.dom.minidom
from pathlib import Path

# Every XML dialect this project ships. .manifest is not conventionally XML by
# extension, which is part of why it went unchecked.
SUFFIXES = {".xml", ".manifest", ".vcxproj", ".props", ".targets", ".filters"}

SKIP_DIRS = {".git", "build", "__pycache__", ".venv"}


def xml_files(root: Path):
    for path in sorted(root.rglob("*")):
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        if path.is_file() and path.suffix.lower() in SUFFIXES:
            yield path


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    failures = 0
    checked = 0

    for path in xml_files(root):
        checked += 1
        relative = path.relative_to(root)
        try:
            xml.dom.minidom.parse(str(path))
        except Exception as error:  # noqa: BLE001 -- report anything, then fail
            failures += 1
            print(f"FAILED {relative}")
            print(f"    {error}")

            # minidom's message carries a line number but not the text on it,
            # and the text is what makes the mistake obvious.
            line_number = getattr(error, "lineno", None)
            if line_number:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
                if 0 < line_number <= len(lines):
                    print(f"    line {line_number}: {lines[line_number - 1].strip()}")
            continue

        print(f"ok     {relative}")

    print()
    if failures:
        print(f"{failures} of {checked} XML file(s) do not parse.")
        print("A double hyphen inside a <!-- comment --> is the usual cause.")
        return 1

    print(f"All {checked} XML files parse.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
