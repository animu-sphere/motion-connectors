#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce DESIGN_POLICY §47 on library code and package declarations.

Only library production files are scanned; tools and consumer tests own
downstream policy. MotionConnectorsBoundaries.cmake also checks actual target
closures in workspace, standalone and installed-consumer builds.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
DOWNSTREAM = re.compile(r"motion(?:Sampling|Recording|Retarget|Usd)\w*", re.I)
COMPONENT = re.compile(r"\bmotion(?:Connector\w+|Core|Sampling|Recording|Retarget|Usd)\b", re.I)
STAGE_INCLUDE = re.compile(r'#\s*include\s*[<"]pxr/(?:usd/|exec/|imaging/)', re.I)
ADDRESSES = re.compile(r"/(?:VMC|tracking)(?:/|\b)", re.I)
PROTOCOL = re.compile(
    r"\b(?:Vmc\w*|Mocopi\w*|Vrchat\w*|OscPacket\w*|OscMessage\w*|"
    r"WebSocket\w*|OpenXR\w*|WebXR\w*|MediaPipe\w*|UdpReceiver\w*)\b", re.I)
TRANSPORT_PROTOCOL = re.compile(
    r"\b(?:Vmc\w*|Mocopi\w*|Vrchat\w*|OscPacket\w*|OscMessage\w*|"
    r"OpenXR\w*|WebXR\w*|MediaPipe\w*)\b", re.I)
NETWORK_INCLUDE = re.compile(
    r'#\s*include\s*[<"](?:winsock\w*|ws2tcpip\.h|sys/socket\.h|'
    r'netinet/|arpa/|openxr/|boost/asio|asio[/.]|websocket)', re.I)
SOURCE_SUFFIXES = {".h", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl"}
# Preserve literals, including raw strings: URLs and embedded comment
# delimiters must not hide a later include or a forbidden address.
CPP_PARTS = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"|'
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.S)
CMAKE_PARTS = re.compile(
    r'"(?:\\.|[^"\\])*"|#\[(?P<comment_eq>=*)\[.*?\](?P=comment_eq)\]|'
    r'\[(?P<eq>=*)\[.*?\](?P=eq)\]|#[^\n]*', re.S)


def code_only(text: str, cmake: bool = False) -> str:
    parts = CMAKE_PARTS if cmake else CPP_PARTS
    def replace(match: re.Match) -> str:
        value = match.group()
        if value.startswith("#" if cmake else ("//", "/*")):
            return re.sub(r"[^\n]", " ", value)
        return value
    return parts.sub(replace, text)


def check_files(owner: str, paths: list[pathlib.Path]) -> tuple[list[str], set[str]]:
    errors: list[str] = []
    edges: set[str] = set()
    for path in paths:
        is_source = path.suffix.lower() in SOURCE_SUFFIXES
        text = code_only(path.read_text(encoding="utf-8"), cmake=not is_source)
        rules = [(DOWNSTREAM, "downstream motion dependency")]
        if is_source:
            rules.append((STAGE_INCLUDE, "stage/UsdSkel/OpenExec include"))
            if owner.lower() in {"motionconnectorcore", "motionconnectorosc",
                                 "motionconnectortransport"}:
                rules.append((ADDRESSES, "protocol address literal"))
            if owner.lower() == "motionconnectorcore":
                rules.extend([(PROTOCOL, "protocol implementation"),
                              (NETWORK_INCLUDE, "network/device SDK include")])
                for name in COMPONENT.findall(text):
                    if name.lower().split("_")[0] not in {owner.lower(), "motioncore"}:
                        errors.append(f"{path}: core reaches {name}")
            if owner.lower() == "motionconnectortransport":
                rules.append((TRANSPORT_PROTOCOL, "protocol implementation in transport"))
                rules.append((re.compile(r"\b(?:MotionFrame|MotionPose|TrackerObservation)\b"),
                              "motion semantics in transport"))
                for name in COMPONENT.findall(text):
                    if name.lower().split("_")[0] != owner.lower():
                        errors.append(f"{path}: transport reaches {name}")
        else:
            edges.update(name.lower() for name in COMPONENT.findall(text)
                         if name.lower() != owner.lower())
        for pattern, reason in rules:
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                errors.append(f"{path}:{line}: {reason}: {match.group()}")
    return errors, edges


def check(root: pathlib.Path, prefix: bool = False) -> list[str]:
    errors: list[str] = []
    graph: dict[str, set[str]] = {}
    owners = (sorted((root / "include").glob("motionConnector*")) if prefix
              else sorted((root / "libs").iterdir()))
    for area in owners:
        if not area.is_dir():
            continue
        owner = area.name
        if prefix:
            paths = [p for p in area.rglob("*") if p.suffix.lower() in SOURCE_SUFFIXES]
            paths += sorted(root.glob(f"lib*/cmake/{owner}/*.cmake"))
        else:
            paths = [p for p in area.rglob("*") if p.is_file()
                     and not {"tests", "examples", "build", ".strata"}.intersection(
                         p.relative_to(area).parts)
                     and (p.suffix.lower() in SOURCE_SUFFIXES
                          or p.name == "CMakeLists.txt" or p.suffix == ".cmake"
                          or p.name.endswith(".cmake.in")
                          or p.name == "openstrata.library.yaml")]
        failures, edges = check_files(owner, sorted(paths))
        errors.extend(failures)
        graph[owner.lower()] = edges
    for owner in graph:
        pending = [(owner, [owner])]
        visited = set()
        while pending:
            name, route = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            if DOWNSTREAM.fullmatch(name):
                errors.append("library declaration closure: " + " -> ".join(route))
            pending.extend((edge, route + [edge]) for edge in graph.get(name, ()))
    if not prefix:
        errors.extend(check_web(root))
    return errors


def check_web(root: pathlib.Path) -> list[str]:
    """Browser acquisition imports browser/MediaPipe APIs, never native packages."""
    errors: list[str] = []
    imports = re.compile(r'''(?:\bfrom\s*|\bimport\s*(?:\(\s*)?)["']([^"']+)["']''')
    for area in sorted((root / "web").glob("motionConnector*")):
        allowed = {"@mediapipe/tasks-vision"} if area.name == "motionConnectorMediaPipe" else set()
        package = area / "package.json"
        if not package.is_file():
            errors.append(f"{area}: missing browser package declaration")
            continue
        try:
            manifest = json.loads(package.read_text(encoding="utf-8"))
        except (ValueError, OSError) as error:
            errors.append(f"{package}: invalid browser package: {error}")
            continue
        for key in ("dependencies", "peerDependencies", "optionalDependencies"):
            for name in manifest.get(key, {}):
                if name not in allowed:
                    errors.append(f"{package}: forbidden browser dependency: {name}")
        for path in sorted((area / "src").rglob("*")):
            if path.suffix not in {".js", ".mjs", ".ts"}:
                continue
            content = code_only(path.read_text(encoding="utf-8"))
            for match in DOWNSTREAM.finditer(content):
                errors.append(f"{path}: downstream motion dependency: {match.group()}")
            for target in imports.findall(content):
                if target in allowed:
                    continue
                if target.startswith("./") or target.startswith("../"):
                    resolved = (path.parent / target).resolve()
                    if resolved.is_relative_to(area.resolve()) and resolved.is_file():
                        continue
                errors.append(f"{path}: forbidden browser import: {target}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path, default=REPO)
    parser.add_argument("--prefix", type=pathlib.Path)
    args = parser.parse_args()
    errors = check(args.prefix or args.root, prefix=args.prefix is not None)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("ok  connector library source/package boundaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
