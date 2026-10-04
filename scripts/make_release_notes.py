#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Render GitHub release notes for one version from the changelog + template.

Fills ``docs/contributing/RELEASE_NOTES_TEMPLATE.md`` with:

* ``{version}`` / ``{tag}``  -- the release version (default: repo ``VERSION``).
* ``{changelog}``            -- that version's ``## [X.Y.Z]`` section of
                               ``CHANGELOG.md`` (heading dropped, body kept).
* ``{pins}``                 -- the consumer pin table the release workflow
                               builds from the published artifact digests.
* ``{checksums}``            -- contents of a ``SHA256SUMS`` file, when the
                               release workflow passes one.

The changelog section heading must be *finalized* (``## [X.Y.Z] - <date>``)
unless ``--allow-unreleased`` is given -- tagging with an unfinished changelog
is the mistake this guard exists to catch. A dry run before the version has a
section of its own renders the ``## [Unreleased]`` section instead.

Usage (what .github/workflows/release.yml runs):

    python scripts/make_release_notes.py --version 0.1.0 \\
        --pins dist-release/external-library-pins.md \\
        --checksums dist-release/SHA256SUMS --out release-notes.md
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


def changelog_section(changelog: str, name: str) -> tuple[str, str] | None:
    """Return (heading, body) of the ``## [name]`` section, or None."""

    lines = changelog.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(rf"^## \[{re.escape(name)}\]", line):
            start = i
            break
    if start is None:
        return None
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if lines[j].startswith("## ") or re.match(r"^\[[^\]]+\]:", lines[j]):
            end = j
            break
    body = "\n".join(lines[start + 1:end]).strip()
    return lines[start], body


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version",
                        default=(REPO_ROOT / "VERSION").read_text().strip(),
                        help="release version X.Y.Z (default: repo VERSION file)")
    parser.add_argument("--changelog", default=str(REPO_ROOT / "CHANGELOG.md"))
    parser.add_argument("--template",
                        default=str(REPO_ROOT / "docs" / "contributing" /
                                    "RELEASE_NOTES_TEMPLATE.md"))
    parser.add_argument("--pins",
                        help="consumer pin table to inline into the notes")
    parser.add_argument("--checksums",
                        help="SHA256SUMS file to inline into the notes")
    parser.add_argument("--allow-unreleased", action="store_true",
                        help="render an unfinished section (a dry run)")
    parser.add_argument("--out", help="write the notes here (default: stdout)")
    args = parser.parse_args(argv)

    changelog = pathlib.Path(args.changelog).read_text(encoding="utf-8")
    section = changelog_section(changelog, args.version)
    if section is None and args.allow_unreleased:
        section = changelog_section(changelog, "Unreleased")
    if section is None:
        raise SystemExit(f"ERROR: CHANGELOG.md has no '## [{args.version}]' section")
    heading, body = section
    finalized = re.match(rf"^## \[{re.escape(args.version)}\] - \d{{4}}-\d{{2}}-\d{{2}}$",
                         heading)
    if not finalized and not args.allow_unreleased:
        raise SystemExit(
            f"ERROR: changelog heading is not finalized: {heading!r}\n"
            f"Write it as '## [{args.version}] - YYYY-MM-DD' before tagging "
            f"(or pass --allow-unreleased for a dry run).")

    def inline(path: str | None, placeholder: str) -> str:
        if not path:
            return placeholder
        return pathlib.Path(path).read_text(encoding="utf-8").strip()

    notes = pathlib.Path(args.template).read_text(encoding="utf-8")
    for key, value in {
        "{version}": args.version,
        "{tag}": f"v{args.version}",
        "{changelog}": body,
        "{pins}": inline(args.pins, "(appended by the release workflow)"),
        "{checksums}": inline(args.checksums, "(appended by the release workflow)"),
    }.items():
        notes = notes.replace(key, value)

    if args.out:
        pathlib.Path(args.out).write_text(notes, encoding="utf-8", newline="\n")
        print(f"wrote release notes for v{args.version} -> {args.out}")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
