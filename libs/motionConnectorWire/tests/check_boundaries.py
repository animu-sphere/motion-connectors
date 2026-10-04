#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionConnectorWire's boundary.

WORKSPACE.md §2.1 gives this library two edges, `motionConnectorCore` and
`motionCore`, and §2.2 forbids the one a reader would most expect: a transport,
in either direction. A wire format needs no socket. The check therefore reads
three things.

* **The sources**, `tests/` included, for the first sign the codec has learned
  something it must not know: another workspace library, a socket header, an
  OpenUSD API beyond the Gf value types `motionCore` carries, a producer's or
  protocol's name, an OSC address literal, or a connector's diagnostic code.
  The codec's own namespace is `WIRE_*`, and a connector's code in it would
  make one connector's frozen set every caller's.
* **CMakeLists.txt**, as an allowlist of what may be linked and found.
* **The test executable's imports**, which must hold no stage, composition or
  registration library and no socket library. A static archive records no
  imports, so the binary argument is an executable, never the `.lib` / `.a`.

Comments are stripped before every scan, because these files document the
boundary in situ.
"""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import sys


def _find_dumpbin() -> str | None:
    tool = shutil.which("dumpbin")
    if tool:
        return tool
    roots = [
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\\Program Files")),
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\\Program Files (x86)")),
    ]
    for root in roots:
        # The release year is a wildcard: an in-place Visual Studio upgrade can
        # leave an empty `2022/` beside a populated `18/`.
        matches = sorted(root.glob(
            "Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"),
            reverse=True)
        if matches:
            return str(matches[0])
    return None


_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT = re.compile(r"//[^\n]*")


def _code_only(text: str) -> str:
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", text))


def _binary_dependencies(binary: pathlib.Path) -> str:
    if sys.platform == "win32":
        tool = _find_dumpbin()
        if not tool:
            raise RuntimeError("dumpbin was not found")
        command = [tool, "/nologo", "/dependents", str(binary)]
    elif sys.platform == "darwin":
        tool = shutil.which("otool")
        if not tool:
            raise RuntimeError("otool was not found")
        command = [tool, "-L", str(binary)]
    else:
        tool = shutil.which("readelf")
        if not tool:
            raise RuntimeError("readelf was not found")
        command = [tool, "-d", str(binary)]
    return subprocess.run(
        command, check=True, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE).stdout


# Every workspace library but this one and the core, and every
# usd-motion-plugins library but motionCore. The exception for a permitted
# name ends in `(?![A-Za-z0-9])` rather than `\b`, because under IGNORECASE
# `MOTIONCONNECTORWIRE_API` would otherwise slip through on its `_`.
_FORBIDDEN_WORKSPACE = re.compile(
    r"\b(?:motionConnector(?!(?:Wire|Core)(?![A-Za-z0-9]))\w+|"
    r"motionSampling|motionRecording|motionRetarget|motionUsd|"
    r"motionSource|motionBvh|motionRuntime|execMotion|"
    r"liveTransport|vrmAdapter\w*)\b|"
    r"\bconnectors::(?!(?:wire|core)(?![A-Za-z0-9]))\w+",
    re.IGNORECASE)

# A socket, in any of the three platforms' spellings.
_FORBIDDEN_SOCKET = re.compile(
    r"#\s*include\s*[<\"](?:winsock2?\.h|ws2tcpip\.h|sys/socket\.h|netinet/[^>\"]+|"
    r"arpa/inet\.h|netdb\.h)[>\"]")

# Gf value types arrive through motionCore; nothing past them may.
_FORBIDDEN_USD = re.compile(
    r"pxr/(?:usd|base/(?:tf|plug|js|work|trace)|imaging|exec)/|PXR_NAMESPACE|"
    r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|EXEC_REGISTER_COMPUTATIONS|"
    r"\b(?:UsdStage|SdfLayer|PlugRegistry|JsValue)\b")

# A producer, a protocol, or an SDK. A leading word boundary and no trailing
# one: the realistic failure is an identifier that begins with the name.
_PRODUCER_NAMES = (
    "vmc", "mocopi", "vrchat", "ardy", "vrm", "vroid", "unity", "sony",
    "waidayo", "virtualmotioncapture", "steamvr", "openvr", "mediapipe",
    "webxr", "openxr",
)
_FORBIDDEN_PRODUCER = re.compile(
    r"\b(?:" + "|".join(re.escape(n) for n in _PRODUCER_NAMES) + r")",
    re.IGNORECASE)

_FORBIDDEN_ADDRESS = re.compile(
    r"\"/(?:VMC|tracking|avatar|com|input|chatbox)\b", re.IGNORECASE)

# Any connector's frozen diagnostic code, by its family prefix.
_FORBIDDEN_CODE = re.compile(r"\b(?:VMC|MOCOPI|VRCHAT_OSC|VRM)_[A-Z0-9_]+\b")

_FORBIDDEN_USD_LIBRARY = re.compile(
    r"\b(?:lib)?usd_(?:ms|usd|usdGeom|usdSkel|usdImaging|sdf|pcp|plug|ar|ndr|"
    r"sdr|hd|hdSt|hdx|hio|glf|garch|exec|esf|ef|js)\b",
    re.IGNORECASE)
_FORBIDDEN_SOCKET_LIBRARY = re.compile(r"\b(?:ws2_32|wsock32)\.dll\b", re.IGNORECASE)


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("motionConnectorWire boundary check passed")
    return 0


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    binary = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    errors: list[str] = []

    forbidden_files = {"openstrata.plugin.yaml", "pluginfo.json"}
    for path in source.rglob("*"):
        if path.is_file() and path.name.lower() in forbidden_files:
            errors.append(f"plugin registration file is forbidden: {path}")

    checks = (
        (_FORBIDDEN_WORKSPACE,
         "motionConnectorWire's edges are motionConnectorCore and motionCore; "
         "this names another library"),
        (_FORBIDDEN_SOCKET, "a wire format needs no socket"),
        (_FORBIDDEN_USD, "OpenUSD beyond the Gf value types is forbidden"),
        (_FORBIDDEN_PRODUCER,
         "a producer, protocol or SDK name is forbidden in motionConnectorWire, "
         "tests included"),
        (_FORBIDDEN_ADDRESS, "an address literal is forbidden in motionConnectorWire"),
        (_FORBIDDEN_CODE,
         "a connector's diagnostic code is forbidden; the codec's namespace is WIRE_*"),
    )
    for area in (source / "include", source / "src", source / "tests"):
        for path in sorted(area.rglob("*")):
            if not path.is_file() or path.suffix not in {".h", ".cpp"}:
                continue
            code = _code_only(path.read_text(encoding="utf-8"))
            for pattern, message in checks:
                found = pattern.search(code)
                if found:
                    errors.append(f"{message}: {path} (`{found.group(0)}`)")

    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {
        "motionconnectorwire", "motionconnectorcore::motionconnectorcore",
        "motioncore::motioncore", "public", "private", "interface",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake, re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "motionConnectorWire links motionConnectorCore and motionCore "
                    f"only; CMakeLists.txt links `{token}`")
    for package in re.findall(r"find_package\s*\(\s*([A-Za-z0-9_]+)", cmake):
        if package not in {"pxr", "motionCore", "motionConnectorCore", "Python3"}:
            errors.append(
                f"motionConnectorWire may not find_package({package})")

    if binary is None:
        return _report(errors)
    if binary.suffix.lower() in {".lib", ".a"}:
        errors.append(
            f"{binary.name} is a static archive and records no imports; point "
            "this check at a linked binary (the test executable)")
        return _report(errors)
    try:
        dependencies = _binary_dependencies(binary)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect {binary.name}: {exc}")
        return _report(errors)
    for match in sorted(set(m.group(0) for m in _FORBIDDEN_USD_LIBRARY.finditer(dependencies))):
        errors.append(f"{binary.name} imports {match}; the codec links Gf values only")
    for match in sorted(set(m.group(0) for m in _FORBIDDEN_SOCKET_LIBRARY.finditer(dependencies))):
        errors.append(f"{binary.name} imports {match}; a wire format needs no socket")
    return _report(errors)


if __name__ == "__main__":
    sys.exit(main())
