# SPDX-License-Identifier: Apache-2.0
"""The C ABI as ctypes declares it, and finding the library that implements it.

Everything here mirrors `motionConnectorCAbi/MotionConnector.h`, which owns the
declarations; tests/test_binding.py holds this file to that header. Nothing
outside this package sees a pointer.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import sys
import threading
from ctypes import (POINTER, c_char, c_char_p, c_double, c_float, c_int32, c_uint8, c_uint16,
                    c_uint32, c_uint64, c_void_p)

ABI_MAJOR = 1
LIBRARY_ENV = "MOTION_CONNECTORS_LIBRARY"

STATUS_OK = 0
STATUS_EMPTY = 1
STATUS_OPEN_FAILED = -4
STATUS_NOT_AVAILABLE = -6

_Status = c_int32


class _Struct(ctypes.Structure):
    @classmethod
    def new(cls):
        value = cls()
        value.struct_size = ctypes.sizeof(cls)
        return value


class StringView(ctypes.Structure):
    _fields_ = [("data", POINTER(c_char)), ("size", c_uint64)]


class SourceInfo(_Struct):
    _fields_ = [("struct_size", c_uint32), ("capabilities", c_uint32), ("id", StringView),
                ("default_profile", StringView), ("default_port", c_uint16),
                ("reserved", c_uint8 * 6)]


class WebSocketOptions(_Struct):
    _fields_ = [("struct_size", c_uint32), ("role", c_uint32), ("path", c_char_p),
                ("allowed_origins", POINTER(c_char_p)), ("allowed_origin_count", c_uint64),
                ("max_message_bytes", c_uint64), ("read_budget_messages", c_uint64),
                ("handshake_timeout_seconds", c_double), ("silence_timeout_seconds", c_double),
                ("reconnect_initial_seconds", c_double), ("reconnect_max_seconds", c_double)]


class ConnectorConfig(_Struct):
    _fields_ = [("struct_size", c_uint32), ("buffer_mode", c_uint32), ("address", c_char_p),
                ("source_profile", c_char_p), ("coordinate_conversion", c_char_p),
                ("buffer_capacity", c_uint64), ("diagnostic_capacity", c_uint64),
                ("websocket", POINTER(WebSocketOptions)), ("port", c_uint16),
                ("reserved", c_uint8 * 6)]


class ConnectorStats(_Struct):
    _fields_ = [("struct_size", c_uint32), ("reserved", c_uint32), ("pushed_frames", c_uint64),
                ("polled_frames", c_uint64), ("dropped_frames", c_uint64),
                ("skipped_frames", c_uint64), ("rejected_frames", c_uint64),
                ("refused_frames", c_uint64), ("dropped_diagnostics", c_uint64)]


class FrameInfo(_Struct):
    _fields_ = [("struct_size", c_uint32), ("source_clock", c_uint32), ("frame_number", c_uint64),
                ("actor_count", c_uint64), ("receive_timestamp", c_double),
                ("source_timestamp", c_double), ("sequence", c_uint64),
                ("source_profile", StringView), ("has_source_timestamp", c_uint8),
                ("has_sequence", c_uint8), ("reserved", c_uint8 * 6)]


class ActorView(_Struct):
    _fields_ = [("struct_size", c_uint32), ("reserved0", c_uint32), ("actor_id", StringView),
                ("tracker_count", c_uint64), ("channel_count", c_uint64), ("has_pose", c_uint8),
                ("reserved1", c_uint8 * 7)]


class PoseView(_Struct):
    _fields_ = [("struct_size", c_uint32), ("source_kind", c_uint32), ("timestamp", c_double),
                ("root_world_position", c_float * 3), ("root_world_orientation", c_float * 4),
                ("root_linear_velocity", c_float * 3), ("root_angular_velocity", c_float * 3),
                ("look_at_target", c_float * 3), ("joint_count", c_uint64),
                ("local_rotations", POINTER(c_float * 4)), ("valid_rotations", POINTER(c_uint8)),
                ("confidence", POINTER(c_float)), ("provider", StringView),
                ("protocol", StringView), ("source_id", StringView),
                ("metadata_source_timestamp", c_double), ("metadata_sequence_number", c_uint64),
                ("has_root_position", c_uint8), ("has_root_orientation", c_uint8),
                ("has_root_linear_velocity", c_uint8), ("has_root_angular_velocity", c_uint8),
                ("has_look_at_target", c_uint8), ("has_contacts", c_uint8),
                ("left_foot_contact", c_uint8), ("right_foot_contact", c_uint8),
                ("has_metadata_source_timestamp", c_uint8),
                ("has_metadata_sequence_number", c_uint8), ("reserved", c_uint8 * 6)]


class ChannelView(_Struct):
    _fields_ = [("struct_size", c_uint32), ("value", c_float), ("name", StringView)]


class TrackerView(_Struct):
    _fields_ = [("struct_size", c_uint32), ("confidence", c_float), ("tracker_id", StringView),
                ("position", c_float * 3), ("rotation", c_float * 4), ("has_position", c_uint8),
                ("has_rotation", c_uint8), ("has_confidence", c_uint8), ("reserved", c_uint8 * 1)]


class DiagnosticView(_Struct):
    _fields_ = [("struct_size", c_uint32), ("severity", c_uint32), ("code", StringView),
                ("source", StringView), ("subject", StringView), ("detail", StringView),
                ("timestamp", c_double), ("sequence", c_uint64), ("recoverable", c_uint8),
                ("has_timestamp", c_uint8), ("has_sequence", c_uint8), ("reserved", c_uint8 * 5)]


# Opaque handles are c_void_p: only this package holds them.
PROTOTYPES = {
    "motion_abi_version": (c_uint32, ()),
    "motion_status_name": (c_char_p, (_Status,)),
    "motion_joint_count": (c_uint64, ()),
    "motion_joint_vocabulary_version": (c_uint32, ()),
    "motion_joint_name": (_Status, (c_uint64, POINTER(StringView))),
    "motion_source_count": (c_uint64, ()),
    "motion_source_get_info": (_Status, (c_uint64, POINTER(SourceInfo))),
    "motion_connector_create": (_Status, (c_char_p, POINTER(c_void_p))),
    "motion_connector_destroy": (None, (c_void_p,)),
    "motion_connector_open": (_Status, (c_void_p, POINTER(ConnectorConfig))),
    "motion_connector_close": (_Status, (c_void_p,)),
    "motion_connector_get_state": (_Status, (c_void_p, POINTER(c_uint32))),
    "motion_connector_get_capabilities": (_Status, (c_void_p, POINTER(c_uint32))),
    "motion_connector_get_source_profile": (_Status, (c_void_p, POINTER(StringView))),
    "motion_connector_get_endpoint": (_Status, (c_void_p, POINTER(StringView))),
    "motion_connector_last_error": (_Status, (c_void_p, POINTER(StringView))),
    "motion_connector_get_stats": (_Status, (c_void_p, POINTER(ConnectorStats))),
    "motion_connector_poll": (_Status, (c_void_p, POINTER(c_void_p))),
    "motion_connector_poll_diagnostic": (_Status, (c_void_p, POINTER(c_void_p))),
    "motion_connector_push_input":
        (_Status, (c_void_p, POINTER(c_uint8), c_uint64, c_double, c_char_p)),
    "motion_connector_flush_input": (_Status, (c_void_p, c_double)),
    "motion_frame_release": (None, (c_void_p,)),
    "motion_frame_get_info": (_Status, (c_void_p, POINTER(FrameInfo))),
    "motion_frame_get_actor": (_Status, (c_void_p, c_uint64, POINTER(ActorView))),
    "motion_frame_get_pose": (_Status, (c_void_p, c_uint64, POINTER(PoseView))),
    "motion_frame_get_channel": (_Status, (c_void_p, c_uint64, c_uint64, POINTER(ChannelView))),
    "motion_frame_get_tracker": (_Status, (c_void_p, c_uint64, c_uint64, POINTER(TrackerView))),
    "motion_diagnostic_release": (None, (c_void_p,)),
    "motion_diagnostic_get_view": (_Status, (c_void_p, POINTER(DiagnosticView))),
}


class LibraryError(RuntimeError):
    """The C ABI library could not be loaded, or speaks another major version."""


def _file_name() -> str:
    if sys.platform == "win32":
        return "motionConnectorCAbi.dll"
    if sys.platform == "darwin":
        return f"libmotionConnectorCAbi.{ABI_MAJOR}.dylib"
    return f"libmotionConnectorCAbi.so.{ABI_MAJOR}"


def _candidates() -> list[str]:
    explicit = os.environ.get(LIBRARY_ENV)
    if explicit:
        return [explicit]
    # An installed package sits at <prefix>/<libdir>/python/motion_connectors.
    package = pathlib.Path(__file__).resolve().parent
    libdir, prefix = package.parents[1], package.parents[2]
    beside = [prefix / "bin" / _file_name(), libdir / _file_name()]
    return [str(path) for path in beside if path.is_file()] + [_file_name()]


def _load_dll(path: str) -> ctypes.CDLL:
    if sys.platform != "win32":
        return ctypes.CDLL(path)
    # Python does not search PATH for a DLL's dependencies, and the library's
    # OpenUSD closure is found there in an activated runtime (C_ABI §2). This is
    # what OpenUSD's own `pxr` package does. A relative entry is left out, so the
    # working directory never supplies a DLL.
    directories = [entry for entry in os.environ.get("PATH", "").split(os.pathsep)
                   if os.path.isabs(entry) and os.path.isdir(entry)]
    cookies = [os.add_dll_directory(entry) for entry in directories]
    try:
        return ctypes.CDLL(path)
    finally:
        for cookie in cookies:
            cookie.close()


def _load() -> ctypes.CDLL:
    failures = []
    for candidate in _candidates():
        try:
            library = _load_dll(candidate)
            break
        except OSError as error:
            failures.append(f"{candidate}: {error}")
    else:
        raise LibraryError(
            f"cannot load the motion-connectors C ABI ({'; '.join(failures)}). Set {LIBRARY_ENV} "
            "to its path, and make its OpenUSD libraries loadable.")
    try:
        for name, (restype, argtypes) in PROTOTYPES.items():
            function = getattr(library, name)
            function.restype = restype
            function.argtypes = argtypes
    except AttributeError as error:
        raise LibraryError(f"{candidate} is not the motion-connectors C ABI: {error}") from error
    version = library.motion_abi_version()
    if version >> 16 != ABI_MAJOR:
        raise LibraryError(f"{candidate} speaks C ABI {version >> 16}.{version & 0xFFFF}; "
                           f"this binding needs major {ABI_MAJOR}")
    return library


_library = None
_library_lock = threading.Lock()


def library() -> ctypes.CDLL:
    """The loaded library. Loading waits for first use, so `import` cannot fail on it."""
    global _library
    with _library_lock:
        if _library is None:
            _library = _load()
        return _library
