#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The installed-consumer lane (docs/architecture/WORKSPACE.md §4).

Installs a built workspace into a clean prefix outside the repository, then
proves the prefix works on its own:

  1. the prefix holds what the workspace installs -- its documentation, and
     for every package tests/installed_consumer/packages.json lists, one
     config, its version file and its public header -- and no text file in
     it names the source tree or the build tree;
  2. tests/installed_consumer/, copied out of the repository, configures
     against the prefix and OpenUSD alone, finds every listed package at the
     repository's major.minor, links its exported target, builds, and reports
     that it consumed exactly as many packages as the list names.

The list is empty until the first import, and the lane runs anyway: what it
proves before then is that the install, the leak scan and a consumer outside
the repository work, so the first package lands in a lane that already runs.

  check_installed_consumer.py --build-dir build/windows-msvc --config Release
      --usd-root C:/usd/openusd-26.08 [--generator Ninja --make-program ...]
      [--keep DIR]
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
CONSUMER = REPO / "tests" / "installed_consumer"
DOC_DIR = pathlib.Path("share", "doc", "motion-connectors")


def run(command: list, **kwargs) -> subprocess.CompletedProcess:
    print("$ " + " ".join(str(c) for c in command), flush=True)
    return subprocess.run([str(c) for c in command], check=True, text=True,
                          encoding="utf-8", errors="replace", **kwargs)


def executable(name: str) -> str:
    return f"{name}.exe" if sys.platform == "win32" else name


def check_prefix(prefix: pathlib.Path, build_dir: pathlib.Path,
                 packages: list[dict]) -> list[str]:
    errors: list[str] = []
    for name in ("LICENSE", "README.md"):
        if not (prefix / DOC_DIR / name).is_file():
            errors.append(f"the prefix has no {(DOC_DIR / name).as_posix()}")

    # Libraries install under CMAKE_INSTALL_LIBDIR, which GNUInstallDirs makes
    # lib64 on some Linux distributions.
    for package in packages:
        name = package["name"]
        config_dirs = sorted(p.parent for p in prefix.glob(
            f"lib*/cmake/{name}/{name}Config.cmake"))
        if len(config_dirs) != 1:
            errors.append(f"the prefix has {len(config_dirs)} {name} package "
                          f"configs under lib*/cmake/{name}, expected one")
        elif not (config_dirs[0] / f"{name}ConfigVersion.cmake").is_file():
            errors.append(f"the prefix has no {name}ConfigVersion.cmake")
        header = prefix / "include" / package["header"]
        if not header.is_file():
            errors.append(f"the prefix has no include/{package['header']}")

    # Nothing installed may point back at where it was built.
    forbidden = set()
    for root in (REPO, build_dir):
        text = str(root.resolve())
        forbidden.update({text, text.replace("\\", "/"),
                          text.replace("\\", "\\\\")})
    for path in prefix.rglob("*"):
        if path.suffix.lower() not in {".json", ".cmake", ".yaml", ".txt", ".h",
                                       ".md"}:
            continue
        lowered = path.read_text(encoding="utf-8", errors="replace").lower()
        for needle in forbidden:
            if needle.lower() in lowered:
                errors.append(f"{path.relative_to(prefix).as_posix()} names "
                              f"{needle}")
                break
    return errors


def main() -> int:
    # Paths are printed, and a console's code page may not spell them.
    sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--usd-root", required=True, type=pathlib.Path)
    parser.add_argument("--external-prefix", action="append", default=[],
                        type=pathlib.Path,
                        help="a package this workspace consumes from another "
                             "repository, materialized outside the tree; the "
                             "build was given the same prefixes")
    parser.add_argument("--generator")
    parser.add_argument("--make-program")
    parser.add_argument("--cxx-compiler")
    parser.add_argument("--keep", type=pathlib.Path,
                        help="work here instead of a deleted temporary directory")
    args = parser.parse_args()

    version = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    major_minor = ".".join(version.split(".")[:2])
    packages = json.loads((CONSUMER / "packages.json").read_text(
        encoding="utf-8"))["packages"]

    scratch_owner = None
    if args.keep:
        work = args.keep.resolve()
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
    else:
        scratch_owner = tempfile.TemporaryDirectory(prefix="motionconnectors-consumer-")
        work = pathlib.Path(scratch_owner.name).resolve()

    try:
        prefix = work / "prefix"
        run(["cmake", "--install", args.build_dir, "--prefix", prefix,
             "--config", args.config])

        errors = check_prefix(prefix, args.build_dir, packages)
        if errors:
            print("\n".join(errors), file=sys.stderr)
            return 1
        print(f"the prefix holds the documentation and {len(packages)} "
              f"package(s), and names no build location")

        source = work / "consumer-src"
        shutil.copytree(CONSUMER, source)
        build = work / "consumer-build"
        # The consumer sees the install prefix, OpenUSD, and every package
        # this workspace consumes from another repository. The last of those
        # is not optional: an installed motionConnectorTracking names
        # motionCore, and a consumer that cannot find it cannot link -- which
        # is exactly what a downstream consumer of THIS repository will face.
        prefix_path = ";".join(
            [prefix.as_posix(), args.usd_root.as_posix()]
            + [external.as_posix() for external in args.external_prefix])
        configure = ["cmake", "-S", source, "-B", build,
                     f"-DCMAKE_PREFIX_PATH={prefix_path}",
                     f"-DMOTIONCONNECTORS_CONSUMER_VERSION={major_minor}",
                     f"-DCMAKE_BUILD_TYPE={args.config}"]
        if args.generator:
            configure += ["-G", args.generator]
        if args.make_program:
            configure.append(f"-DCMAKE_MAKE_PROGRAM={args.make_program}")
        if args.cxx_compiler:
            configure.append(f"-DCMAKE_CXX_COMPILER={args.cxx_compiler}")
        run(configure)
        run(["cmake", "--build", build, "--config", args.config])

        probes = sorted(build.rglob(executable("installed_consumer")))
        if not probes:
            print("the consumer built no installed_consumer", file=sys.stderr)
            return 1
        env = dict(os.environ)
        env["PATH"] = os.pathsep.join([str(prefix / "bin"),
                                       str(args.usd_root / "bin"),
                                       str(args.usd_root / "lib"),
                                       env.get("PATH", "")])
        result = run([probes[0]], env=env, stdout=subprocess.PIPE)
        want = f"consumed {len(packages)} package(s)"
        if want not in result.stdout.splitlines():
            print(f"the consumer printed {result.stdout!r}, expected {want!r}",
                  file=sys.stderr)
            return 1
        print(f"ok  {want} from outside the repository")
        print("installed-consumer lane passed")
        return 0
    except subprocess.CalledProcessError as error:
        print(f"command failed with exit status {error.returncode}", file=sys.stderr)
        return 1
    finally:
        if scratch_owner:
            scratch_owner.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
