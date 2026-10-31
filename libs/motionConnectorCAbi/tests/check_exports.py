#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The shared library exports exactly what the public header declares.

A missing symbol breaks a binding when it loads. An extra one means a connector's
C++ symbol leaked through the ABI, where it would be an unversioned surface.

  check_exports.py HEADER LIBRARY [--linker LINK_EXE]
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

DECLARATION = re.compile(r"MOTIONCONNECTORCABI_API\s[^;{]*?\b(motion_\w+)\s*\(", re.S)
MSVC_EXPORT = re.compile(r"^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)", re.M)


def declared(header: pathlib.Path) -> set[str]:
    return set(DECLARATION.findall(header.read_text(encoding="utf-8")))


def exported(library: pathlib.Path, linker: str | None) -> set[str]:
    if sys.platform == "win32":
        if not linker:
            raise SystemExit("--linker is required on Windows")
        output = subprocess.run([linker, "/dump", "/exports", str(library)], check=True,
                                capture_output=True, text=True).stdout
        return set(MSVC_EXPORT.findall(output))
    if sys.platform == "darwin":
        output = subprocess.run(["nm", "-gU", str(library)], check=True,
                                capture_output=True, text=True).stdout
        return {line.split()[-1].removeprefix("_") for line in output.splitlines()
                if line.strip()}
    output = subprocess.run(["nm", "-D", "--defined-only", str(library)], check=True,
                            capture_output=True, text=True).stdout
    return {line.split()[-1] for line in output.splitlines() if line.strip()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("header", type=pathlib.Path)
    parser.add_argument("library", type=pathlib.Path)
    parser.add_argument("--linker")
    args = parser.parse_args()

    want = declared(args.header)
    if not want:
        print(f"{args.header} declares nothing", file=sys.stderr)
        return 1
    have = exported(args.library, args.linker)
    missing = sorted(want - have)
    extra = sorted(have - want)
    if missing:
        print("declared and not exported: " + ", ".join(missing), file=sys.stderr)
    if extra:
        print("exported and not declared: " + ", ".join(extra), file=sys.stderr)
    if missing or extra:
        return 1
    print(f"ok  {len(want)} exported symbol(s), all declared")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
