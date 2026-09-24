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

### Operator evidence

Carried from `usd-vrm-plugins`, which shipped these connectors through its
v0.8.0 without them. None closes by writing code; each is a recorded session
and a manifest, and none gates a pull request.

- ⬜ **A VMC relay or sender session, compared at the canonical layer.** Two
  observation paths of one mocopi session agree to a median 0.084° per bone
  ([`usd-vrm-plugins` report motion/01](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/01-2026-08-15-mocopi-cross-source.md));
  VMC has no recorded sender yet. The same sessions settle `CS-O2`.
- ⬜ **A recovery a device can actually produce, or a decision that it cannot.**
  Removing a mocopi sensor puts the app into re-tracking, so the stream never
  carries a lost sensor, and `MOCOPI_TRACKING_LOST` stays reserved and
  unraised ([CONNECTOR §5](../design/CONNECTOR_CONTRACT.md#5-state)). A source
  restart is recorded.
- ⬜ **A labelled *rolled* VRChat OSC take.** The tracker Euler order is
  measured to three compositions of six, because nobody tilted in the
  2026-08-30 session. Twenty seconds settles it: a head tilted onto one
  shoulder and held, or a foot rolled onto its outer edge, with the side
  written down. Until then the residual is median 0.21°, 12.33° at worst
  ([`usd-vrm-plugins` report motion/03](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)
  §2.3).
- ⬜ **A redistributable mocopi capture.** The device sessions are
  `recorded/manifest.json` rows with no bytes, because a session is a real
  person's motion. A publishable one needs the vendor's `BVH Sender`, not a
  device.
- ⬜ **A live session reaching an avatar from release artifacts alone.** It
  composes this repository's release with an avatar repository's, so it
  waits for this repository's first release; where the composed test runs is
  `usd-avatar-runtime`'s once it exists.

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
