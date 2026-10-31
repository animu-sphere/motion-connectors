# Acquisition C ABI

> Contract: **accepted**, 2026-10-31 (CC-O7). Implementation status and test
> evidence live only in the
> [capability matrix](../reference/CAPABILITY_MATRIX.md).
>
> This document owns the **in-process, language-neutral boundary** over the
> connectors: its package, its versioning, its ownership and lifetime rules, its
> errors and its views. On that area it wins over
> [DESIGN_POLICY.md](DESIGN_POLICY.md) §38. What a frame *means* is
> [CONNECTOR_CONTRACT.md](CONNECTOR_CONTRACT.md)'s, and the pose inside it is
> `usd-motion-plugins`' `MOTION_CONTRACT.md`'s; neither is restated here.
> Declarations belong to the public header,
> `libs/motionConnectorCAbi/include/motionConnectorCAbi/MotionConnector.h`.
> Section numbers are stable; open questions are `CABI-O<n>` and never reused.

---

## 1. Scope

- **In:** opening and closing a connector, polling frames and diagnostics, state,
  capabilities, buffer statistics, source profile identifiers, frame views, and
  a hardware-free input path.
- **Out:** filtering, smoothing, tracker assignment, semantic solve, retargeting,
  recording, USD, avatars and runtime loops. Raw capture stays with the C++
  connectors and the tools. So does sending: the ABI reads acquisition.
- **Not** a mirror of the C++ interface, **not** the frame wire format, and
  **not** the WASM data ABI. Three representations stay distinct
  ([DESIGN_POLICY §38](DESIGN_POLICY.md#38-abi-considerations)).

## 2. Package and sources

`motionConnectorCAbi` is its own package, target and artifact, with one public
header, `motionConnectorCAbi/MotionConnector.h`, which includes only `<stdint.h>`
and its export macros.

It is the one **shared** library in this repository, for two reasons. A binding
loads it by name. And its connectors link privately and statically, so their C++
types stop inside it, and only `motion_*` symbols are exported.

A connector is chosen by string id: `vmc`, `mocopi`, `vrchat-osc` and `websocket`,
the same ids as `motion_connect`. `motion_source_get_info` lists the ones the
library was built with, with each default profile, conventional port and
capability set. A port is never applied implicitly (Rule: configuration is
explicit, [CONNECTOR §2](CONNECTOR_CONTRACT.md#2-imotionconnector)).

**OpenXR is not a version 1 source.** Its connector borrows the caller's
instance, spaces and trackers, and acquires inside the caller's frame loop. None
of that is a configuration string, and a binding that owned those handles would
be a second session owner. Adding it means adding a source and its borrowed-handle
contract here (`CABI-O1`).

The library's runtime closure still contains OpenUSD's `gf`, because `motionCore`
does ([WORKSPACE §1.2](../architecture/WORKSPACE.md#12-web-modules)). A consumer
compiles against C alone, but its process must be able to load those libraries.

## 3. Types

- **Integers are fixed width**, including every enumeration: a value travels as
  its `typedef`, such as `motion_status_t`, and never as a C `enum` type, whose
  size is the compiler's.
- **Timestamps are `double` seconds and counters are `uint64_t`**, exactly as in
  the frame. Neither is narrowed.
- **A quaternion is `[w, x, y, z]`**, as in the frame wire format.
- **Struct layouts are defined for 64-bit targets.** The library refuses to build
  for anything else, and `motionConnectorCAbi_cConsumer` freezes every size and
  offset. A 32-bit or WASM consumer belongs to the separate WASM data ABI.
- **No field is an STL or USD type, and no exception crosses** (§5).

## 4. Versioning

`motion_abi_version()` returns `(major << 16) | minor`, independent of the package
version. A consumer refuses a library whose major differs. A minor release only
appends: a new function, or a new field at the end of a struct. Changing or
removing anything is a new major.

Every struct starts with `uint32_t struct_size`, set by the caller to the
`sizeof` it was compiled with (`MOTION_INIT`).

| Struct direction | `struct_size` below the version 1 size | `struct_size` larger than this library knows |
| --- | --- | --- |
| Output: the library fills it | `MOTION_STATUS_UNSUPPORTED_STRUCT`, nothing written | written up to what it knows, and `struct_size` is set to that; the rest is left alone |
| Input: the caller fills it | `MOTION_STATUS_UNSUPPORTED_STRUCT` | `MOTION_STATUS_UNSUPPORTED_STRUCT`: a field it does not know could not be honoured |

## 5. Errors

Every function returns without throwing. A `motion_status_t` of zero or more is
success, and `MOTION_STATUS_EMPTY` means there is nothing yet. A negative value is
a failure, and on failure an output handle is `NULL`. An exception inside the
library stops at the boundary as `MOTION_STATUS_OUT_OF_MEMORY` or
`MOTION_STATUS_INTERNAL`.

**A failed open is a state, not a fault**
([CONNECTOR §2](CONNECTOR_CONTRACT.md#2-imotionconnector)):
`MOTION_STATUS_OPEN_FAILED` leaves the connector in `MOTION_STATE_ERROR`, the
reason in `motion_connector_last_error`, and the caller free to open again. A
malformed request, by contrast, is refused before the connector is touched.

`motion_connector_last_error` belongs to its handle, not to a thread, so that a
binding never has to pair a call with a thread-local read.

## 6. Ownership and lifetimes

| Handle or view | Owner | Valid until |
| --- | --- | --- |
| `motion_connector_t*` | the caller | `motion_connector_destroy` |
| `motion_frame_t*` | the caller, from `motion_connector_poll` | `motion_frame_release` |
| `motion_diagnostic_t*` | the caller, from `motion_connector_poll_diagnostic` | `motion_diagnostic_release` |
| a view of a frame or diagnostic, every pointer and string in it | the handle | that handle's release |
| a connector's profile, endpoint and last error | the connector | its next non-const call |
| source and joint names | the library | always |

**A frame is a copy the consumer owns.** It is independent of later polls, of
`motion_connector_close` and of `motion_connector_destroy`. This is the choice
that makes a binding safe by construction: a garbage-collected frame object can
outlive anything, and no rule about the next poll can be forgotten. It costs one
allocation set per delivered frame, which the connectors' `MotionFrame` already
pays, and a consumer that keeps up with the source can release each frame at once.

The connector handle is used by **one thread at a time**, as `IMotionConnector`
is. Distinct connectors may be used concurrently. A frame or diagnostic is
immutable, so it may be read from any number of threads and released on any one
of them, once.

`motion_connector_poll` and `motion_connector_poll_diagnostic` never block. Open,
close and destroy return without waiting for a peer.

### 6.1 Diagnostics

Diagnostics are a separate read from frames, so a frame is the same value whether
or not anything went wrong producing it
([CONNECTOR §9](CONNECTOR_CONTRACT.md#9-diagnostics)). A single "last diagnostic"
was rejected: one poll can raise many, and it would lose all but the last.

Each connector's typed code leaves as its stable string, and the queue is bounded.
`diagnostic_capacity` sets the bound, 1024 by default. An overflow drops the
oldest and counts it in `dropped_diagnostics`, because a consumer that never reads
them must not grow without bound. `motion_connector_close` keeps what has not been
delivered, so the reason a session ended can still be read after it ends.

### 6.2 The hardware-free input path

`motion_connector_push_input` and `motion_connector_flush_input` take one received
datagram or message through the connector's own decode and assembly, as capture
replay does ([CONNECTOR §12](CONNECTOR_CONTRACT.md#12-raw-capture)). They are in
the ABI so that a binding and its tests run with no socket and no device, on the
same committed corpus as the C++ tests (design policy Rule 7).

They need no open. An unopened connector uses its default buffer, which holds one
frame in `Latest` mode, so a replay drains after each push. They are an input path
only: nothing reads a capture file through this ABI.

## 7. Strings

Every string is valid UTF-8, delivered as `motion_string_view_t`: `data` is never
`NULL` and is followed by a NUL, while `size` is the length, because a value may
itself contain NUL. Connectors carry names as received, so the boundary has to
enforce this, and it does so two ways.

- **A frame with a field that is not UTF-8 is refused, whole.** It is not
  delivered, `refused_frames` counts it, and `C_ABI_FRAME_NOT_UTF8` names the
  field. These strings are identities: repairing one could make two names collide
  or change which tracker an operator assigned. The frame wire writer refuses the
  same frame for the same reason
  ([FRAME_WIRE_FORMAT §6](FRAME_WIRE_FORMAT.md#6-what-a-reader-refuses)), and only
  that frame is lost.
- **A diagnostic's text is repaired**, with U+FFFD for each invalid byte. It is a
  report, not an identity, and it may be quoting the very bytes that were refused.
  Dropping it would lose the reason. A connector's open-failure message is treated
  the same way.

## 8. Absence

Availability is explicit, never inferred from a value
([FRAME_WIRE_FORMAT §4.1](FRAME_WIRE_FORMAT.md#41-absence-is-an-absent-key) states
the same rule for bytes).

- A scalar, vector or quaternion has a `has_*` flag. An absent one's storage is
  zero, which is not a valid quaternion, so reading it by mistake is visible.
- An array that may be absent is `NULL`, as `confidence` is.
- A joint is present only where `valid_rotations[joint]` is non-zero. An absent
  joint's quaternion is all zeros, never identity
  ([CONNECTOR §3](CONNECTOR_CONTRACT.md#3-motionframe)).
- An actor without a pose answers `MOTION_STATUS_NOT_AVAILABLE`.

## 9. Views

One function per level, each taking indices: `motion_frame_get_info`, then
`_get_actor`, then `_get_pose`, `_get_channel` and `_get_tracker`. Because each
element is copied into a caller-sized struct, an element can grow under §4. A
pointer to an array of structs could not grow without breaking every consumer's
stride. Fixed per-joint data is the exception and is exposed as plain arrays,
`local_rotations`, `valid_rotations` and `confidence`, indexed by the shared
vocabulary (`motion_joint_count`, `motion_joint_name`), because a binding wants
them whole and their element type will not change.

A tracker id is delivered as the source named it and is not a body role.
Assignment and solve are not ABI operations
([DESIGN_POLICY §47.5.1](DESIGN_POLICY.md#4751-tracking-ownership-review)).

## 10. Evidence

Replay parity against the C++ connectors, on the committed corpus of every source,
is the conformance rule: the same input must read as the same values, field by
field, frame by frame and diagnostic by diagnostic. A consumer written in C,
linking this library alone, also runs against the installed package, and the
exported symbol set must equal what the header declares. The tests are named in
the [capability matrix](../reference/CAPABILITY_MATRIX.md#3-tools-and-bindings).

## 11. Open questions

| Id | Question | Resolve by |
| --- | --- | --- |
| CABI-O1 | Whether and how to expose OpenXR's borrowed session handles and caller-driven `Acquire` | a binding consumer that owns an XR session |
