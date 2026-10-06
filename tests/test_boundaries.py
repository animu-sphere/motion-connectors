#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Mutation tests for the acquisition boundary gates; no SDK/compiler needed."""
from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
from check_boundaries import check, code_only

DOWNSTREAM = ("motionSampling", "motionRecording", "motionRetarget", "motionUsd")
MODULE = REPO / "cmake" / "MotionConnectorsBoundaries.cmake"


class SourceBoundaries(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="connector-boundary-")
        self.addCleanup(self.scratch.cleanup)
        self.root = pathlib.Path(self.scratch.name)
        (self.root / "libs").mkdir()

    def write(self, relative: str, text: str):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_downstream_at_each_declaration_surface(self):
        for name in DOWNSTREAM:
            for file, text in (
                ("src/input.cpp", f'#include "{name}/api.h"'),
                ("CMakeLists.txt", f'set(edge {name}::{name})\n'
                 'target_link_libraries(motionConnectorFuture PRIVATE ${edge})'),
                ("cmake/config.cmake.in", f'find_dependency({name} CONFIG)'),
                ("openstrata.library.yaml", f'requires:\n  libraries:\n    - id: {name}\n'),
            ):
                with self.subTest(name=name, file=file):
                    path = self.write(f"libs/motionConnectorFuture/{file}", text)
                    self.assertTrue(check(self.root))
                    path.unlink()

    def test_stage_and_exec_headers(self):
        for header in ("pxr/usd/usd/stage.h", "pxr/usd/usdSkel/skeleton.h",
                       "pxr/exec/exec/computation.h"):
            with self.subTest(header=header):
                self.write("libs/motionConnectorFuture/src/input.cpp", f'#include <{header}>')
                self.assertTrue(check(self.root))

    def test_literal_core_and_transport_semantics(self):
        for owner, text in (
            ("motionConnectorOsc", 'const auto address = "/VMC/Ext/Bone/Pos";'),
            ("motionConnectorTransport", 'const auto address = "/tracking/trackers/1";'),
            ("motionConnectorTransport", 'MotionFrame frame;'),
            ("motionConnectorTransport", 'VmcMessage message;'),
            ("motionConnectorCore", 'DecodeVmc(); VmcMessage value;'),
            ("motionConnectorCore", '#include <winsock2.h>'),
            ("motionConnectorCore", '#include "motionConnectorOsc/OscPacket.h"'),
        ):
            with self.subTest(owner=owner, text=text):
                path = self.write(f"libs/{owner}/src/input.cpp", text)
                self.assertTrue(check(self.root))
                path.unlink()

    def test_tools_tests_and_comments_are_permitted(self):
        text = '#include "motionRecording/api.h"\nconst char* p = "/VMC/Ext";'
        self.write("tools/record/main.cpp", text)
        self.write("libs/motionConnectorFuture/tests/consumer/main.cpp", text)
        self.write("libs/motionConnectorCore/src/input.cpp", '// VmcMessage\n/* /VMC/Ext */')
        self.write("libs/motionConnectorCore/CMakeLists.txt", '#[=[\nmotionRecording\n]=]')
        self.assertEqual(check(self.root), [])

    def test_comment_delimiters_inside_strings_do_not_hide_code(self):
        for literal in ('"http://example"', '"/*"', 'R"tag(// /*)tag"'):
            text = f'const auto s = {literal}; const auto p = "/VMC/Ext";'
            self.write("libs/motionConnectorOsc/src/input.cpp", text)
            self.assertTrue(check(self.root))
        self.assertIn('"#motionRecording"', code_only('"#motionRecording" # ok', cmake=True))

    def test_installed_header_and_config(self):
        for relative, text in (
            ("include/motionConnectorFuture/Input.h", '#include <pxr/usd/usd/stage.h>'),
            ("lib64/cmake/motionConnectorFuture/motionConnectorFutureConfig.cmake",
             'find_dependency(motionRecording CONFIG)'),
            ("lib/cmake/motionConnectorFuture/motionConnectorFutureTargets.cmake",
             'set_target_properties(x PROPERTIES INTERFACE_LINK_LIBRARIES "motionSampling")'),
        ):
            with self.subTest(relative=relative):
                # Every installed package has its header directory.
                (self.root / "include/motionConnectorFuture").mkdir(parents=True, exist_ok=True)
                path = self.write(relative, text)
                self.assertTrue(check(self.root, prefix=True))
                path.unlink()
        for owner, text in (
            ("motionConnectorCore", 'VmcMessage value;'),
            ("motionConnectorTransport", 'MotionFrame value;'),
            ("motionConnectorTransport", 'const auto p = "/tracking/trackers/1";'),
            ("motionConnectorOsc", 'const auto p = "/VMC/Ext/Bone/Pos";'),
        ):
            with self.subTest(owner=owner, text=text):
                path = self.write(f"include/{owner}/Input.h", text)
                self.assertTrue(check(self.root, prefix=True))
                path.unlink()


class CMakeBoundaries(unittest.TestCase):
    def configure(self, body: str, rejected: bool, files: dict[str, str] | None = None,
                  expected: str = "Connector boundary:"):
        with tempfile.TemporaryDirectory(prefix="connector-graph-") as scratch:
            root = pathlib.Path(scratch)
            for relative, text in (files or {}).items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.22)\nproject(BoundaryTest NONE)\n'
                f'include("{MODULE.as_posix()}")\n' + body,
                encoding="utf-8")
            result = subprocess.run(
                # No compilation occurs. An explicit Makefiles generator avoids
                # Visual Studio's SDK discovery even for a project with NONE.
                ["cmake", "-G", "Unix Makefiles",
                 f"-DCMAKE_MAKE_PROGRAM={shutil.which('cmake')}",
                 "-S", str(root), "-B", str(root / "build")],
                capture_output=True, text=True, encoding="utf-8", errors="replace")
            output = result.stdout + result.stderr
            if rejected:
                self.assertNotEqual(result.returncode, 0, output)
                self.assertIn(expected, output)
            else:
                self.assertEqual(result.returncode, 0, output)

    def test_private_alias_and_generator_expression_closures(self):
        for name in DOWNSTREAM:
            for edge in ("helperAlias", "$<LINK_ONLY:helperAlias>",
                         "$<IF:$<CONFIG:Debug>,helperAlias,>",
                         "$<TARGET_NAME_IF_EXISTS:helperAlias>"):
                with self.subTest(name=name, edge=edge):
                    self.configure(
                        'add_library(acquisition INTERFACE)\n'
                        'add_library(helper INTERFACE)\n'
                        'add_library(helperAlias ALIAS helper)\n'
                        f'set(edge "{edge}")\n'
                        'target_link_libraries(acquisition INTERFACE "${edge}")\n'
                        f'target_link_libraries(helper INTERFACE {name}::{name})\n'
                        'motionconnectors_check_target_boundary(acquisition)\n', True)

    def test_imported_hidden_edge_and_library_location(self):
        for property, value in (
            ("INTERFACE_LINK_LIBRARIES", "$<LINK_ONLY:helper>"),
            ("INTERFACE_LINK_LIBRARIES_DIRECT", "helper"),
            ("IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE", "helper"),
            ("IMPORTED_LOCATION_RELEASE", "/sdk/lib/libmotionRecording.so"),
            ("IMPORTED_IMPLIB_RELEASE", "C:/sdk/lib/motionUsd.lib"),
        ):
            with self.subTest(property=property):
                self.configure(
                    'add_library(acquisition INTERFACE)\n'
                    'add_library(opaque UNKNOWN IMPORTED)\n'
                    'add_library(helper INTERFACE)\n'
                    'target_link_libraries(helper INTERFACE motionSampling::motionSampling)\n'
                    'set_target_properties(opaque PROPERTIES IMPORTED_CONFIGURATIONS RELEASE '
                    f'{property} "{value}")\n'
                    'target_link_libraries(acquisition INTERFACE opaque)\n'
                    'motionconnectors_check_target_boundary(acquisition)\n', True)

    def test_private_static_dependency(self):
        # The gate must run during configure, before a compiler is required.
        self.configure(
            'add_library(acquisition STATIC absent.cpp)\n'
            'add_library(helper INTERFACE)\n'
            'target_link_libraries(acquisition PRIVATE helper)\n'
            'target_link_libraries(helper INTERFACE motionRecording::motionRecording)\n'
            'motionconnectors_check_target_boundary(acquisition)\n', True)

    def test_library_tool_split_and_cycles(self):
        self.configure(
            'add_library(acquisition INTERFACE)\n'
            'add_library(motionCore INTERFACE)\n'
            'add_library(motionCore::motionCore ALIAS motionCore)\n'
            'target_link_libraries(acquisition INTERFACE motionCore::motionCore)\n'
            'target_link_libraries(motionCore INTERFACE acquisition)\n'
            'add_library(recorder INTERFACE)\n'
            'target_link_libraries(recorder INTERFACE acquisition motionRecording::motionRecording)\n'
            'motionconnectors_check_target_boundary(acquisition)\n', False)

    def test_deferred_component_gate_without_tests(self):
        self.configure(
            'add_library(acquisition INTERFACE)\n'
            'cmake_language(DEFER CALL motionconnectors_check_target_boundary acquisition)\n'
            'target_link_libraries(acquisition INTERFACE motionRetarget::motionRetarget)\n', True)

    def test_installed_package_discovery_and_hidden_closure(self):
        for name in DOWNSTREAM:
            with self.subTest(name=name, surface="find_dependency"):
                self.configure(
                    f'set(CMAKE_DISABLE_FIND_PACKAGE_{name} TRUE)\n'
                    'find_package(Acquisition CONFIG REQUIRED PATHS '
                    '"${CMAKE_CURRENT_SOURCE_DIR}/prefix" NO_DEFAULT_PATH)\n', True,
                    files={"prefix/AcquisitionConfig.cmake":
                           'include(CMakeFindDependencyMacro)\n'
                           f'find_dependency({name} CONFIG REQUIRED)\n'},
                    expected=f"CMAKE_DISABLE_FIND_PACKAGE_{name}")
            with self.subTest(name=name, surface="hidden imported closure"):
                self.configure(
                    'find_package(Acquisition CONFIG REQUIRED PATHS '
                    '"${CMAKE_CURRENT_SOURCE_DIR}/prefix" NO_DEFAULT_PATH)\n'
                    'motionconnectors_check_target_boundary(Acquisition::Acquisition)\n', True,
                    files={"prefix/AcquisitionConfig.cmake":
                           'add_library(Acquisition::Acquisition INTERFACE IMPORTED)\n'
                           'add_library(helper INTERFACE IMPORTED)\n'
                           'set_target_properties(Acquisition::Acquisition PROPERTIES '
                           'INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:helper>")\n'
                           'set_target_properties(helper PROPERTIES '
                           f'INTERFACE_LINK_LIBRARIES "{name}::{name}")\n'})


if __name__ == "__main__":
    if not shutil.which("cmake"):
        raise SystemExit("cmake is required to test actual dependency closures")
    unittest.main()
