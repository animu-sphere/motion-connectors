#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionConnectorWebSocket's boundary.

WORKSPACE.md §2.1 gives this library three edges -- `motionConnectorCore`,
`motionConnectorWire` and `motionConnectorTransport` -- and `motionCore`
through the first. WEBSOCKET_CONNECTOR §2 decided that RFC 6455 is implemented
here, with no third-party library. The check therefore reads three things.

* **The sources**, `tests/` included, for the first sign of a library this
  connector must not reach: another connector, a sampling or recording
  package, an OpenUSD API beyond the Gf value types `motionCore` carries, a
  WebSocket, HTTP, TLS or compression library, or a producer's name. A
  connector's code from another family is refused too: this one's namespace is
  `WEBSOCKET_*`, and the codec's `WIRE_*` passes through.
* **CMakeLists.txt**, as an allowlist of what may be linked and found.
* **The test executable's imports**, which must hold no stage, composition or
  registration library and nothing past the OS's own sockets. A static archive
  records no imports, so the binary argument is an executable.

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


# Every workspace library but this one and its three edges, and every
# usd-motion-plugins library but motionCore.
_FORBIDDEN_WORKSPACE = re.compile(
    r"\b(?:motionConnector(?!(?:WebSocket|Wire|Core|Transport)(?![A-Za-z0-9]))\w+|"
    r"motionSampling|motionRecording|motionRetarget|motionUsd|"
    r"motionSource|motionBvh|motionRuntime|execMotion|"
    r"liveTransport|vrmAdapter\w*)\b|"
    r"\bconnectors::(?!(?:websocket|wire|core|transport)(?![A-Za-z0-9]))\w+",
    re.IGNORECASE)

# A third-party WebSocket, HTTP, TLS or compression library (§2, §4.3).
_FORBIDDEN_THIRD_PARTY = re.compile(
    r"#\s*include\s*[<\"](?:ixwebsocket|websocketpp|boost|asio|beast|libwebsockets|"
    r"uwebsockets|openssl|zlib|curl|httplib|nlohmann)[/.>\"]",
    re.IGNORECASE)

_FORBIDDEN_USD = re.compile(
    r"pxr/(?:usd|base/(?:tf|plug|js|work|trace)|imaging|exec)/|PXR_NAMESPACE|"
    r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|EXEC_REGISTER_COMPUTATIONS|"
    r"\b(?:UsdStage|SdfLayer|PlugRegistry|JsValue)\b")

# A producer, a protocol or an SDK. A frame on this wire may have come from
# any of them, which is exactly why this connector names none.
_PRODUCER_NAMES = (
    "vmc", "mocopi", "vrchat", "ardy", "vrm", "vroid", "unity", "sony",
    "waidayo", "virtualmotioncapture", "steamvr", "openvr", "mediapipe",
    "webxr", "openxr",
)
_FORBIDDEN_PRODUCER = re.compile(
    r"\b(?:" + "|".join(re.escape(n) for n in _PRODUCER_NAMES) + r")",
    re.IGNORECASE)

_FORBIDDEN_CODE = re.compile(r"\b(?:VMC|MOCOPI|VRCHAT_OSC|VRM)_[A-Z0-9_]+\b")

_FORBIDDEN_USD_LIBRARY = re.compile(
    r"\b(?:lib)?usd_(?:ms|usd|usdGeom|usdSkel|usdImaging|sdf|pcp|plug|ar|ndr|"
    r"sdr|hd|hdSt|hdx|hio|glf|garch|exec|esf|ef|js)\b",
    re.IGNORECASE)
_FORBIDDEN_THIRD_PARTY_LIBRARY = re.compile(
    r"\b(?:lib)?(?:ssl|crypto|z|zlib1?|ixwebsocket|websockets|boost_\w+)"
    r"(?:\.so[.\d]*|\.dylib|\.dll|-\d+\.dll)\b",
    re.IGNORECASE)


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("motionConnectorWebSocket boundary check passed")
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
         "motionConnectorWebSocket's edges are motionConnectorCore, motionConnectorWire "
         "and motionConnectorTransport; this names another library"),
        (_FORBIDDEN_THIRD_PARTY,
         "RFC 6455 is implemented here, with no third-party library (§2)"),
        (_FORBIDDEN_USD, "OpenUSD beyond the Gf value types is forbidden"),
        (_FORBIDDEN_PRODUCER,
         "a producer, protocol or SDK name is forbidden in motionConnectorWebSocket, "
         "tests included"),
        (_FORBIDDEN_CODE,
         "another connector's diagnostic code is forbidden; this one's namespace is "
         "WEBSOCKET_*"),
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
        "motionconnectorwebsocket", "motionconnectorcore::motionconnectorcore",
        "motionconnectorwire::motionconnectorwire",
        "motionconnectortransport::motionconnectortransport",
        "ws2_32", "public", "private", "interface",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake, re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "motionConnectorWebSocket links its three edges and the OS's sockets "
                    f"only; CMakeLists.txt links `{token}`")
    for package in re.findall(r"find_package\s*\(\s*([A-Za-z0-9_]+)", cmake):
        if package not in {"pxr", "motionCore", "motionConnectorCore", "motionConnectorWire",
                           "motionConnectorTransport", "Python3"}:
            errors.append(f"motionConnectorWebSocket may not find_package({package})")

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
        errors.append(f"{binary.name} imports {match}; the connector links Gf values only")
    for match in sorted(set(
            m.group(0) for m in _FORBIDDEN_THIRD_PARTY_LIBRARY.finditer(dependencies))):
        errors.append(f"{binary.name} imports {match}; no third-party library (§2)")
    return _report(errors)


if __name__ == "__main__":
    sys.exit(main())
