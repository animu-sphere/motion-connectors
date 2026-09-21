#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the facts documentation and manifests restate, which rot silently.

Links -- docs/contributing/documentation.md's change checklist: "Relative
links and heading anchors resolve." For every `[text](target)` outside code in
every Markdown file, this fails when:

  * a relative path names a file or directory that does not exist;
  * a `#fragment` names no heading in the target Markdown file, using
    GitHub's heading-slug rules (so `§14.1 First release — done` is
    `#141-first-release--done`);
  * a link to an absolute path or a machine-local path (`C:\\...`, `/home/...`)
    appears at all -- documents never commit one.

External links (`http:`, `https:`, `mailto:`) are not fetched.

Mirrors -- docs/architecture/WORKSPACE.md §4: the repository-root VERSION is
the single product version. openstrata.toml, every component manifest and
every CMake fallback mirror it, and every range a manifest requires a sibling
in admits it. The OpenUSD pin in cmake/PIN_MODULE is the release
docs/architecture/DEPENDENCIES.md names and every CI cell requires. CHANGELOG.md
has a section for VERSION or an `[Unreleased]` one.

The diagnostic catalog is intentionally not checked here: code tables remain
connector-owned, while their cross-connector naming decision is documented in
DIAGNOSTICS.md (DIAG-O1 is resolved). This checker covers links, mirrors and
dependency ranges; the adapter tests cover the catalog entries.

  check_docs.py             check the repository
  check_docs.py --selftest  check the slug and link rules against known cases
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys
import unicodedata

REPO = pathlib.Path(__file__).resolve().parents[1]
# The one file that pins OpenUSD, and the variable in it that names the release.
PIN_MODULE = "MotionConnectorsOpenUsd.cmake"
PIN_VARIABLE = "MOTIONCONNECTORS_OPENUSD_REQUIRED_RELEASE"
SKIP_DIRS = {".git", "build", "dist", ".strata", "node_modules", "__pycache__",
             ".claude", "local", ".ost-ci", ".ost-ci-home"}

FENCE = re.compile(r"^\s*(```|~~~)")
INLINE_CODE = re.compile(r"(`+)(?:(?!\1).)+?\1")
LINK = re.compile(r"(?<!\!)\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
IMAGE = re.compile(r"!\[[^\]]*\]\(\s*<?([^)\s>]+)>?\s*\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
EXTERNAL = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.-]*:")
MACHINE_LOCAL = re.compile(r"^(?:[A-Za-z]:[\\/]|/(?:home|Users|tmp)/|\\\\)")


def slug(heading: str) -> str:
    """GitHub's anchor for a heading's text."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # links -> text
    text = re.sub(r"<[^>]+>", "", text)                          # inline HTML
    text = text.replace("`", "").strip().lower()
    kept = []
    for ch in text:
        category = unicodedata.category(ch)
        if ch in " -_" or category[0] in {"L", "N"} or category == "Mn":
            kept.append(ch)
    return "".join(kept).replace(" ", "-")


def markdown_lines(path: pathlib.Path):
    """(line number, text) for every line outside a fenced code block, with
    inline code spans blanked so a link inside backticks is not a link."""
    in_fence = False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if not in_fence:
            yield number, INLINE_CODE.sub(lambda m: " " * len(m.group(0)), line)


def anchors(path: pathlib.Path, cache: dict[pathlib.Path, set[str]]) -> set[str]:
    if path not in cache:
        seen: dict[str, int] = {}
        found: set[str] = set()
        in_fence = False
        for line in path.read_text(encoding="utf-8").splitlines():
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            match = None if in_fence else HEADING.match(line)
            if match:
                base = slug(match.group(2))
                count = seen.get(base, 0)
                found.add(base if count == 0 else f"{base}-{count}")
                seen[base] = count + 1
        cache[path] = found
    return cache[path]


def markdown_files(root: pathlib.Path) -> list[pathlib.Path]:
    """The repository's own Markdown: what git tracks or would track.

    Asking git rather than walking the tree matters in CI, where the checkout
    also holds the bootstrapped `ost` and the materialized runtime, each with
    Markdown of its own whose links point into trees that are not here.
    Walking the directory is the fallback for a tree without git.
    """
    try:
        listed = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
             "--exclude-standard", "--", "*.md"],
            check=True, stdout=subprocess.PIPE).stdout.decode("utf-8")
        files = [root / name for name in listed.split("\0") if name]
        return sorted(path for path in files if path.is_file())
    except (OSError, subprocess.CalledProcessError):
        pass
    files = []
    for path in root.rglob("*.md"):
        if not any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            files.append(path)
    return sorted(files)


def check_file(path: pathlib.Path, cache: dict) -> list[str]:
    errors: list[str] = []
    where = (path.relative_to(REPO).as_posix() if path.is_relative_to(REPO)
             else path.name)
    for number, line in markdown_lines(path):
        for match in [*LINK.finditer(line), *IMAGE.finditer(line)]:
            target = match.group(1)
            if EXTERNAL.match(target) and not MACHINE_LOCAL.match(target):
                continue
            if MACHINE_LOCAL.match(target) or target.startswith("/"):
                errors.append(f"{where}:{number}: absolute or machine-local "
                              f"link {target}")
                continue
            file_part, _, fragment = target.partition("#")
            resolved = (path.parent / file_part).resolve() if file_part else path
            if not resolved.exists():
                errors.append(f"{where}:{number}: {file_part} does not exist")
                continue
            if fragment:
                if resolved.is_dir() or resolved.suffix.lower() != ".md":
                    errors.append(f"{where}:{number}: #{fragment} on a "
                                  f"non-Markdown target {file_part}")
                elif fragment not in anchors(resolved, cache):
                    errors.append(f"{where}:{number}: {file_part or where} has "
                                  f"no heading #{fragment}")
    return errors


# `version: ">=0.1,<0.2"` under a manifest's `requires.libraries`.
REQUIRED_RANGE = re.compile(r'^\s+version:\s*">=([0-9.]+),<([0-9.]+)"', re.MULTILINE)
FIND_PACKAGE_VERSION = re.compile(r"find_package\((\w+)\s+([0-9.]+)\s+CONFIG")


def in_range(version: str, lower: str, upper: str) -> bool:
    def key(text: str) -> tuple[int, ...]:
        parts = [int(p) for p in text.split(".")]
        return tuple(parts + [0] * (3 - len(parts)))
    return key(lower) <= key(version) < key(upper)


def check_ranges(root: pathlib.Path, manifest: pathlib.Path, version: str) -> list[str]:
    """Every SIBLING this workspace requires is built at VERSION, so every
    required range has to admit it -- or the release cannot resolve itself.

    A dependency carrying an `artifact:` pin is not a sibling: it names a
    library from another repository, at that repository's version, and this
    one's VERSION says nothing about it. `motionConnectorTracking` requires
    `motionCore >=0.5,<0.6` while this workspace is 0.1.0, and both are right.
    So the ranges under an `artifact:` block are skipped, and the rest -- which
    are siblings -- are checked exactly as before.
    """
    where = manifest.relative_to(root).as_posix()
    text = manifest.read_text(encoding="utf-8")
    return [f"{where}: required range >={lower},<{upper} excludes {version}"
            for lower, upper in REQUIRED_RANGE.findall(sibling_ranges_only(text))
            if not in_range(version, lower, upper)]


def sibling_ranges_only(text: str) -> str:
    """`text` with every externally pinned dependency's range removed.

    A dependency block is externally pinned when it carries `artifact:`. The
    range line precedes it, so the removal walks the block: from a `- id:` line
    to the next one at the same indentation, dropping the whole block when an
    `artifact:` key appears inside it.
    """
    lines = text.splitlines()
    kept: list[str] = []
    block: list[str] | None = None
    indent = 0

    def flush() -> None:
        nonlocal block
        if block is not None and not any(
                line.strip().startswith("artifact:") for line in block):
            kept.extend(block)
        block = None

    for line in lines:
        stripped = line.lstrip()
        if stripped.startswith("- id:"):
            flush()
            block = [line]
            indent = len(line) - len(stripped)
            continue
        if block is not None:
            bare = line.strip()
            continues = not bare or (len(line) - len(line.lstrip())) > indent
            if continues:
                block.append(line)
                continue
            flush()
        kept.append(line)
    flush()
    return "\n".join(kept)


def check_mirrors(root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    version = (root / "VERSION").read_text(encoding="utf-8").strip()

    def expect(path: pathlib.Path, pattern: str, want: str, what: str) -> None:
        where = path.relative_to(root).as_posix()
        match = re.search(pattern, path.read_text(encoding="utf-8"), re.MULTILINE)
        if not match:
            errors.append(f"{where}: no {what} found")
        elif match.group(1) != want:
            errors.append(f"{where}: {what} is {match.group(1)}, expected {want}")

    expect(root / "openstrata.toml", r'^version\s*=\s*"([^"]+)"', version,
           "project version")
    for manifest in sorted(root.glob("*/*/openstrata.*.yaml")):
        expect(manifest, r"^\s+version:\s*([0-9][^\s#]*)", version, "version")
        errors.extend(check_ranges(root, manifest, version))
    for cmake in sorted(root.glob("*/*/CMakeLists.txt")):
        if "../../VERSION" in cmake.read_text(encoding="utf-8"):
            expect(cmake, r'set\(_\w+_version "([^"]+)"\)', version,
                   "standalone fallback version")

    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## [{version}]" not in changelog and "## [Unreleased]" not in changelog:
        errors.append(f"CHANGELOG.md has neither a [{version}] nor an "
                      f"[Unreleased] section")

    pin = re.search(PIN_VARIABLE + r' "([^"]+)"',
                    (root / "cmake" / PIN_MODULE).read_text(encoding="utf-8"))
    if not pin:
        errors.append(f"cmake/{PIN_MODULE}: no {PIN_VARIABLE}")
        return errors
    release = pin.group(1)
    expect(root / "docs" / "architecture" / "DEPENDENCIES.md",
           r"OpenUSD \*\*([0-9.]+)\*\*", release, "OpenUSD pin")
    ci = root / "openstrata.ci.yaml"
    for found in re.findall(r'require_openusd_version:\s*"([^"]+)"',
                            ci.read_text(encoding="utf-8")):
        if found != release:
            errors.append(f"openstrata.ci.yaml: a cell requires OpenUSD {found}, "
                          f"expected {release}")
    for manifest in sorted(root.glob("plugins/*/openstrata.plugin.yaml")):
        expect(manifest, r'^\s+openusd:\s*"==([^"]+)"', release,
               "runtime.openusd pin")
    return errors


def selftest() -> int:
    cases = {
        "14.1 First substantial release — definition of done":
            "141-first-substantial-release--definition-of-done",
        "1.2 Bundles, tools and data": "12-bundles-tools-and-data",
        "19. Where this document departs from the implementation policy":
            "19-where-this-document-departs-from-the-implementation-policy",
        "Status at a glance": "status-at-a-glance",
        "2. MIG-0 — preparation 🚧": "2-mig-0--preparation-",
        "8. `MotionClip` and sampling": "8-motionclip-and-sampling",
        "左腕 と [link](x.md)": "左腕-と-link",
    }
    failures = [f"slug({h!r}) = {slug(h)!r}, expected {want!r}"
                for h, want in cases.items() if slug(h) != want]
    links = LINK.findall("see [a](b.md#c) and ![i](img.png)")
    if links != ["b.md#c"]:
        failures.append(f"LINK found {links}")
    stripped = INLINE_CODE.sub(lambda m: " " * len(m.group(0)), "`[x](y.md)`")
    if LINK.search(stripped):
        failures.append("a link inside a code span is checked")
    if not MACHINE_LOCAL.match("C:\\dev\\x.md") or not MACHINE_LOCAL.match("/home/u/x"):
        failures.append("machine-local paths are not recognized")

    for version, lower, upper, want in [("0.1.0", "0.1", "0.2", True),
                                        ("0.1.0", "0.0", "0.1", False),
                                        ("0.1.9", "0.1", "0.2", True),
                                        ("1.0.0", "0.1", "1.0", False)]:
        if in_range(version, lower, upper) != want:
            failures.append(f"in_range({version}, {lower}, {upper}) != {want}")
    ranges = REQUIRED_RANGE.findall('    - id: a\n      version: ">=0.1,<0.2"\n')
    if ranges != [("0.1", "0.2")]:
        failures.append(f"REQUIRED_RANGE found {ranges}")

    # The whole rule, on files: one good link, and one of each kind of bad.
    import tempfile
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        (root / "target.md").write_text("# Title\n\n## 2.1 Some `code` — part\n",
                                        encoding="utf-8")
        page = root / "page.md"
        page.write_text(
            "[ok](target.md#21-some-code--part)\n"
            "[no file](missing.md)\n"
            "[no anchor](target.md#nowhere)\n"
            "[local](C:\\dev\\target.md)\n"
            "```\n[in a fence](missing.md)\n```\n",
            encoding="utf-8")
        found = check_file(page, {})
        if len(found) != 3 or not all(
                any(word in e for e in found)
                for word in ("missing.md does not exist", "#nowhere",
                             "machine-local")):
            failures.append(f"check_file reported {found}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("check_docs selftest passed")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    cache: dict[pathlib.Path, set[str]] = {}
    files = markdown_files(REPO)
    errors = [e for path in files for e in check_file(path, cache)]
    errors += check_mirrors(REPO)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        print(f"{len(errors)} problem(s)", file=sys.stderr)
        return 1
    print(f"{len(files)} Markdown file(s): every relative link and anchor "
          f"resolves; every version and pin mirror agrees")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
