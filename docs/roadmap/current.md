# Current: converge imported connectors onto the shared contract

Status: 🚧 v0.1.0 convergence in progress (2026-09-21).

PRs #1–#8 imported the transport, OSC, tracker, VMC, mocopi and VRChat OSC
implementations with their tests, capture corpora and record tools. Those
source-specific paths are described in the [capability matrix](../reference/CAPABILITY_MATRIX.md)
and the import history is in the [changelog](../../CHANGELOG.md). The current
milestone is no longer an import exercise: it is the work that makes the
imported paths conform to one connector API without moving source semantics
into the shared layer.

## Work remaining

### Shared contract

- ✅ Add `motionConnectorCore` with `IMotionConnector`, `ConnectorConfig`,
  `ConnectorState`, `ConnectorCapabilities`, `MotionFrame`, `FrameTiming`,
  actor identity and the bounded `Latest` / `Ordered` / `Lossless` buffer.
- ✅ Resolve `Poll` result and skipped-frame semantics (`CC-O6`): one frame per
  poll, cumulative buffer counters, oldest-drop `Ordered` and back-pressure
  `Lossless` behavior.
- ✅ Agree on the `MotionStream` intake boundary with `usd-motion-plugins`
  (`CC-O3`): `Poll(MotionFrame&)` feeds actor-scoped `LiveCaptureSource::Push`,
  and `IMotionSource::Sample` reads at evaluation time.
- ✅ Resolve the profile identifier and representation rules (`SP-O1`, `SP-O2`)
  with connector-owned JSON profiles installed under
  `share/motion-connectors/profiles/`.
- ✅ Resolve `DIAG-O1` before v0.1.0: all three imported diagnostic families
  use `VMC_*`, `MOCOPI_*` and `VRCHAT_OSC_*` consistently in the catalog,
  tests, recorders and recorded-session manifests.

### Source convergence

- ✅ Adapt VMC, mocopi and VRChat OSC Trackers to the shared connector contract
  without replacing their tested source-specific assembly. `VmcConnector`,
  `MocopiConnector` and `VrchatOscConnector` now wrap their tested source
  paths; tracker frames remain observations and are not assigned to an avatar.
- ✅ Implement installed source profiles for `vmc.v1`, `mocopi.body.v1` and
  `vrchat-osc.trackers.v1`; profile IDs and JSON data are validated while
  target-avatar mapping remains outside this repository.
- ⬜ Adopt `motionCore`'s shared basis operation in VMC and VRChat OSC after
  its 0.5.2 package is published and update the pinned digests. Measure VMC's
  two translation channels on two senders (`CS-O2`).
- ⬜ Revisit actor identity with the first multi-actor source (`CC-O5`).

### Tools and evidence

- ✅ Add `motion_connect dump`, `list` and `inspect` over `MotionFrame`; the
  remaining CLI work is the later `record` / `bridge` scope.
- ✅ Re-run the imported capture/replay evidence through the unified contract
  and verify the packages from a clean installed prefix. The three connector
  corpus tests exercise the hardware-free `PushDatagram` → `Poll` path for
  every committed capture, using each protocol's required end-of-capture
  handling, and `workspace_installed_consumer` consumes the installed package
  set outside the repository.
- ✅ Audit the imported `--export-trace` paths. All three tools delegate the
  file format to `motionRecording::WriteCaptureTraceFile`; mocopi and VRChat
  OSC export only from `--inspect`, while VMC also accumulates poses and
  exports during live capture with a second `--max-frames` bound.
- ⬜ Remove or move VMC's live semantic export and settle where offline
  capture-to-trace conversion is invoked. Raw packet/session capture remains
  here; a semantic `motion-capture-trace` must have one recording owner in
  `usd-motion-plugins`.
- ⬜ Decide whether the three raw capture tools remain separate or are folded
  into `motion_connect record` (`WS-O3`). A future `record` command must remain
  a connector capture/replay tool, not a semantic motion recorder.

## Completion criteria

v0.1.0 is complete when:

- the three imported source categories produce the same `MotionFrame` contract;
- VMC loopback and all imported captures replay deterministically through that
  path in CI;
- source profiles, timing, state, diagnostics and buffer behavior have tests;
- the three imported diagnostic families have been renamed consistently before
  v0.1.0;
- `motion_connect dump`, `list` and `inspect` work from an installed prefix;
- malformed input remains refused without hardware or a live peer; and
- the sibling repository consumes none of the moved implementations.

## Later releases

The later sequence remains: WebSocket transport and Python bindings, browser
MediaPipe and the JS/WASM boundary, WebXR, OpenXR and advanced sources. Their
open decisions stay in the owning design or architecture document and are
scheduled in [roadmap/README.md](README.md); this page does not duplicate them.
