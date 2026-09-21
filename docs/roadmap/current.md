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
- ⬜ Agree on the `MotionStream` intake boundary with `usd-motion-plugins`
  (`CC-O3`).
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
- ⬜ Resolve the shared change-of-basis primitive and VMC translation-channel
  evidence (`CS-O1`, `CS-O2`, `CS-O3`).
- ⬜ Reconcile per-joint tracking loss and actor identity with the shared
  contract (`CC-O4`, `CC-O5`).

### Tools and evidence

- ✅ Add `motion_connect dump`, `list` and `inspect` over `MotionFrame`; the
  remaining CLI work is the later `record` / `bridge` scope.
- ⬜ Re-run the imported capture/replay evidence through the unified contract
  and verify the packages from a clean installed prefix.
- ⬜ Confirm that `usd-vrm-plugins` no longer consumes the imported libraries
  after its migration cleanup.
- ⬜ Audit the imported `--export-trace` paths. Keep raw packet/session capture
  here, and move or redefine semantic `motion-capture-trace` export under
  `usd-motion-plugins` so it does not become a second recording boundary.
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
