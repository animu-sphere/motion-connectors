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
the single product version. openstrata.toml and every component manifest
mirror it (CMake reads it and restates nothing), and every range a manifest
requires a sibling in admits it. The OpenUSD pin in cmake/PIN_MODULE is the release
docs/architecture/DEPENDENCIES.md names and every CI cell requires. CHANGELOG.md
has a section for VERSION or an `[Unreleased]` one.

The diagnostic catalog is intentionally not checked here: code tables remain
connector-owned, while their cross-connector naming decision is documented in
DIAGNOSTICS.md (DIAG-O1 is resolved). This checker covers links, mirrors,
dependency ranges and documentation ownership; adapter tests cover the catalog.

Ownership -- roadmap prose holds unfinished work only; the root README and
architecture pages link to the sole capability matrix. Scheduling phrases in
other current docs warn for review. Published matrix release availability must
match VERSION, finalized changelog headings and frozen release records.

Release lane -- .github/workflows/release.yml is hand-authored, so it must
bootstrap the `ost` openstrata.ci.yaml pins at every site that decides
behaviour.

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
ROADMAP_DONE = re.compile(
    r"✅|\[[xX]\]|\b(?:implemented|completed)\b|"
    r"^\s*[-*+]\s+(?:done|complete)\b", re.IGNORECASE)
SCHEDULE_STATUS = re.compile(
    r"\b(?:planned\s+for\s+v\d|next\s+release|currently\s+implementing|"
    r"in\s+progress)\b", re.IGNORECASE)
RELEASE_HEADING = re.compile(r"^## \[(\d+\.\d+\.\d+)\] - \d{4}-\d{2}-\d{2}$", re.M)


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


def check_doc_ownership(root: pathlib.Path, path: pathlib.Path) -> tuple[list[str], list[str]]:
    """Narrow ownership gates, not a ban on words describing protocol state.

    Frozen release/report/archive prose and contributor policy are excluded
    from phrase warnings. Fenced examples and inline code follow the existing
    Markdown scanner. Status vocabulary alone cannot prove semantic drift.
    """
    where = path.relative_to(root).as_posix()
    errors: list[str] = []
    warnings: list[str] = []
    if where.startswith(("docs/releases/", "docs/reports/", "docs/archive/",
                         "docs/contributing/")):
        return errors, warnings
    if where.startswith("docs/roadmap/"):
        for number, line in markdown_lines(path):
            if ROADMAP_DONE.search(line):
                errors.append(f"{where}:{number}: finished roadmap work must "
                              "leave roadmap in the same PR")
    elif (path.name == "README.md" or where.startswith(
            ("docs/architecture/", "docs/design/", "docs/reference/"))):
        # Join adjacent prose lines so wrapping cannot hide a phrase.
        paragraph: list[str] = []
        first = 0
        for number, line in [*markdown_lines(path), (0, "")]:
            if line.strip():
                if not paragraph:
                    first = number
                paragraph.append(line)
            else:
                match = SCHEDULE_STATUS.search(" ".join(paragraph))
                if match:
                    warnings.append(f"{where}:{first}: scheduling/progress phrase "
                                    f"{match.group(0)!r}; use the roadmap or matrix")
                paragraph = []
    if where == "README.md" or where.startswith("docs/architecture/"):
        has_matrix_link = any(
            target.partition("#")[0].endswith("CAPABILITY_MATRIX.md")
            and (path.parent / target.partition("#")[0]).resolve()
            == (root / "docs/reference/CAPABILITY_MATRIX.md").resolve()
            for _, line in markdown_lines(path) for target in LINK.findall(line))
        if not has_matrix_link:
            errors.append(f"{where}: link to the canonical CAPABILITY_MATRIX.md "
                          "for implementation status")
    return errors, warnings


def check_release_docs(root: pathlib.Path) -> list[str]:
    """Availability is a shipped release or 'unreleased', never a target date.

    Finalizing a VERSION changelog heading requires its release record in the
    same PR. An Unreleased section allows development without inventing a
    future release record or editing historical scope.
    """
    errors: list[str] = []
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    finalized = set(RELEASE_HEADING.findall(changelog))
    records = root / "docs/releases"
    matrix_path = root / "docs/reference/CAPABILITY_MATRIX.md"
    for heading in re.findall(r"^## \[\d+\.\d+\.\d+\].*$", changelog, re.M):
        if not RELEASE_HEADING.fullmatch(heading):
            errors.append(f"CHANGELOG.md: release heading must use "
                          f"## [X.Y.Z] - YYYY-MM-DD: {heading}")

    def key(value: str) -> tuple[int, ...]:
        return tuple(int(p) for p in value.split("."))

    for release in sorted(finalized):
        if key(release) > key(version):
            errors.append(f"CHANGELOG.md: finalized release {release} exceeds "
                          f"VERSION {version}")
        if not (records / f"v{release}.md").is_file():
            errors.append(f"docs/releases/v{release}.md: missing record for "
                          "finalized changelog release")
    for record in sorted(records.glob("v*.md")):
        release = record.stem.removeprefix("v")
        if not re.fullmatch(r"\d+\.\d+\.\d+", release):
            errors.append(f"docs/releases/{record.name}: invalid release filename")
            continue
        if release not in finalized:
            errors.append(f"docs/releases/{record.name}: no finalized changelog heading")
        if not re.search(rf"^# v{re.escape(release)}(?:\s|$)",
                         record.read_text(encoding="utf-8"), re.M):
            errors.append(f"docs/releases/{record.name}: title must name v{release}")
    availability_table = False
    for number, line in enumerate(matrix_path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.startswith("|"):
            availability_table = False
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if cells[-1] == "Release availability":
            availability_table = True
            continue
        if not availability_table or all(re.fullmatch(r"[-: ]+", c) for c in cells):
            continue
        value = cells[-1]
        if value in {"unreleased", "—"}:
            continue
        match = re.fullmatch(r"v(\d+\.\d+\.\d+)", value)
        if not match or match.group(1) not in finalized:
            errors.append(f"docs/reference/CAPABILITY_MATRIX.md:{number}: release "
                          f"availability {value!r} has no finalized release; "
                          "use unreleased or — and schedule in roadmap")
    if not any("Release availability" in line for line in matrix_path.read_text(
            encoding="utf-8").splitlines() if line.startswith("|")):
        errors.append("docs/reference/CAPABILITY_MATRIX.md: no Release availability column")
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
    # No CMake project restates the number: every CMakeLists.txt takes
    # MOTIONCONNECTORS_VERSION from cmake/MotionConnectorsProject.cmake, which
    # reads VERSION and has no fallback to drift.

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

        # Mutate ownership and release facts, not just regex examples.
        roadmap = root / "docs/roadmap/current.md"
        roadmap.parent.mkdir(parents=True)
        roadmap.write_text(
            "# Remaining work\n\n- [ ] Decide ABI\n\n"
            "```text\nimplemented example\n```\n", encoding="utf-8")
        if check_doc_ownership(root, roadmap) != ([], []):
            failures.append("unfinished roadmap or fenced example was refused")
        for marker in ("- [x] ABI", "- [X] ABI", "- ✅ done", "Implemented ABI",
                       "completed phase", "- done ABI", "- complete ABI"):
            roadmap.write_text(marker, encoding="utf-8")
            if len(check_doc_ownership(root, roadmap)[0]) != 1:
                failures.append(f"roadmap mutation escaped: {marker}")

        reference = root / "docs/reference"
        reference.mkdir(parents=True)
        matrix = reference / "CAPABILITY_MATRIX.md"
        matrix.write_text("# Capabilities\n", encoding="utf-8")
        architecture = root / "docs/architecture/WORKSPACE.md"
        architecture.parent.mkdir(parents=True)
        for current_page, link in (
                (root / "README.md", "docs/reference/CAPABILITY_MATRIX.md"),
                (architecture, "../reference/CAPABILITY_MATRIX.md")):
            current_page.write_text(f"[status]({link})\n", encoding="utf-8")
            if check_doc_ownership(root, current_page) != ([], []):
                failures.append("canonical matrix link was refused")
            current_page.write_text("# Structure\n", encoding="utf-8")
            if not check_doc_ownership(root, current_page)[0]:
                failures.append("missing matrix link escaped")
            current_page.write_text("[status](https://example.org/CAPABILITY_MATRIX.md)\n",
                                    encoding="utf-8")
            if not check_doc_ownership(root, current_page)[0]:
                failures.append("noncanonical matrix link escaped")

        design = root / "docs/design/contract.md"
        design.parent.mkdir(parents=True)
        for phrase in ("planned for v0.2.0", "next release",
                       "currently implementing", "in\nprogress"):
            design.write_text(phrase, encoding="utf-8")
            errors, warnings = check_doc_ownership(root, design)
            if errors or len(warnings) != 1:
                failures.append(f"progress phrase did not warn: {phrase!r}")
        design.write_text("```\nin progress\n```\nThe next receiver reads a complete frame.\n",
                          encoding="utf-8")
        if check_doc_ownership(root, design) != ([], []):
            failures.append("protocol prose or fenced sample caused a warning")

        records = root / "docs/releases"
        records.mkdir(parents=True)
        record = records / "v0.1.0.md"
        record.write_text("# v0.1.0\n\nin progress\n", encoding="utf-8")
        if check_doc_ownership(root, record) != ([], []):
            failures.append("frozen release prose caused a warning")
        (root / "VERSION").write_text("0.1.0\n", encoding="utf-8")
        changelog = root / "CHANGELOG.md"
        changelog.write_text("## [Unreleased]\n\n## [0.1.0] - 2026-10-04\n",
                             encoding="utf-8")
        matrix_template = (
            "| Capability | Status | Release availability |\n"
            "| --- | --- | --- |\n| example | supported | {} |\n")
        for availability in ("v0.1.0", "unreleased", "—"):
            matrix.write_text(matrix_template.format(availability), encoding="utf-8")
            if check_release_docs(root):
                failures.append(f"valid release availability was refused: {availability}")
        matrix.write_text(matrix_template.format("v0.2.0"), encoding="utf-8")
        if not check_release_docs(root):
            failures.append("unpublished matrix release escaped")
        matrix.write_text(matrix_template.format("unreleased"), encoding="utf-8")
        record.unlink()
        if not check_release_docs(root):
            failures.append("missing finalized release record escaped")
        record.write_text("# v0.2.0\n", encoding="utf-8")
        if not check_release_docs(root):
            failures.append("release record title drift escaped")
        record.write_text("# v0.1.0\n", encoding="utf-8")
        changelog.write_text("## [Unreleased]\n", encoding="utf-8")
        if not check_release_docs(root):
            failures.append("release record without finalized changelog escaped")
        changelog.write_text("## [0.1.0] - 2026-10-04\n## [0.2.0] - 2026-10-08\n",
                             encoding="utf-8")
        (records / "v0.2.0.md").write_text("# v0.2.0\n", encoding="utf-8")
        if not check_release_docs(root):
            failures.append("finalized release newer than VERSION escaped")
        (root / "VERSION").write_text("0.2.0\n", encoding="utf-8")
        if check_release_docs(root):
            failures.append("consistent release preparation was refused")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("check_docs selftest passed")
    return 0


def check_release_lane_ost_pin(root: pathlib.Path) -> list[str]:
    """`.github/workflows/release.yml` bootstraps the `ost` the contract pins.

    The release workflow is hand-authored, because the CI contract cannot
    express the verbs a release turns on: packaging every member, and pushing
    each library to the registry consumers pin it from. So `ost ci generate`
    never touches it and `ost ci validate` says nothing about it, and a green
    PR lane proves nothing about the lane that publishes. usd-vrm-plugins let
    exactly this drift across three releases (its ost report 39) and added the
    same check afterwards.

    Only the three sites that decide behaviour are read: the asset URL, the
    assertion that checks what was installed, and the registry cache key. The
    header comment names versions on purpose -- it is prose about this file's
    history, and a check that read it would forbid the file from having one.
    """
    errors: list[str] = []
    lane_path = root / ".github" / "workflows" / "release.yml"
    if not lane_path.is_file():
        return errors
    contract = (root / "openstrata.ci.yaml").read_text(encoding="utf-8")
    m = re.search(r'^bootstrap:\s*$.*?^\s+version:\s*"([^"]+)"',
                  contract, re.M | re.S)
    if not m:
        return ["openstrata.ci.yaml declares no bootstrap.ost.version"]
    pinned = m.group(1)
    lane = lane_path.read_text(encoding="utf-8")
    sites = {
        "the release asset URL":
            r"open-strata/releases/download/v(\d+\.\d+\.\d+)",
        "the post-install assertion":
            r'!=\s*"ost (\d+\.\d+\.\d+)"',
        "the registry cache key":
            r"key: ost-registry-(\d+\.\d+\.\d+)-",
    }
    for what, pattern in sites.items():
        found = set(re.findall(pattern, lane))
        if not found:
            errors.append(
                f".github/workflows/release.yml: {what} names no `ost` "
                f"version. It bootstraps one by hand, so every pin site must "
                f"stay readable or this check silently stops checking it")
            continue
        wrong = sorted(v for v in found if v != pinned)
        if wrong:
            errors.append(
                f".github/workflows/release.yml: {what} says ost "
                f"{', '.join(wrong)} but openstrata.ci.yaml pins {pinned}. It "
                f"is hand-authored, so regeneration will not fix it and no PR "
                f"lane will report it")
    return errors


def main() -> int:
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    cache: dict[pathlib.Path, set[str]] = {}
    files = markdown_files(REPO)
    errors = [e for path in files for e in check_file(path, cache)]
    warnings: list[str] = []
    for path in files:
        ownership_errors, ownership_warnings = check_doc_ownership(REPO, path)
        errors.extend(ownership_errors)
        warnings.extend(ownership_warnings)
    errors += check_mirrors(REPO)
    errors += check_release_lane_ost_pin(REPO)
    errors += check_release_docs(REPO)
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        print(f"{len(errors)} problem(s)", file=sys.stderr)
        return 1
    print(f"{len(files)} Markdown file(s): every relative link and anchor "
          f"resolves; version/pin mirrors and documentation ownership agree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
