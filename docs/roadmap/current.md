# Current priorities

The next milestone is the JS/TS and WASM consumer boundary for v0.3.0.
Implementation status and test evidence live in the
[capability matrix](../reference/CAPABILITY_MATRIX.md);
landed changes live in the [changelog](../../CHANGELOG.md).

## Preserve the acquisition boundary

Every change must preserve
[DESIGN_POLICY §47](../design/DESIGN_POLICY.md#47-external-acquisition-boundary)
and the [workspace dependency gates](../architecture/WORKSPACE.md#24-enforcement):
source and standalone builds, installed consumers and public headers,
package-config closure, forbidden includes and target links, and mutation
self-tests. A connector ends at observations / `MotionFrame`. Filtering,
smoothing, skeleton solve, retargeting, semantic recording, USD authoring and
runtime scheduling belong downstream.

## v0.3.0 consumer boundary

- Add a shared JS/TS consumer API under `bindings/js/` for browser acquisition,
  WebSocket sources and native/WASM consumers. `web/motionConnectorWebXR` and
  `web/motionConnectorMediaPipe` are acquisition providers, not native bindings.
- Design a WASM-friendly data ABI with explicit copy/ownership rules, 64-bit
  integer handling and string lifetimes. Keep the in-process C ABI, JS object
  representation and serialized frame wire format distinct.

New connector additions are not release gates. Acquisition references are
evaluated through their matrix evidence; hardware validation remains operator
work.

## Deferred work

- Coordinate the remaining [generic solve migration](boundary-implementation.md)
  after motion-owned input and parity contracts are adopted.
- Publish `motionConnectorOpenXR` when a descriptor can declare the OpenXR
  loader ([WORKSPACE §4.1](../architecture/WORKSPACE.md#41-distribution)).
- Revisit actor identity with the first multi-actor source (`CC-O5`).
- Schedule advanced mocap / IMU / optical / depth sources, generation adapters,
  binary encoding, TLS, multi-peer receive and runtime abstraction only after
  the current release blockers, with a concrete requirement.

## Operator evidence

These tasks require a session and manifest, or an explicit decision that a
device cannot produce the evidence. They do not gate a feature PR or a release.
Replay recorded sessions against their manifests with
`scripts/check_recorded_sessions.py`
([building guide](../guides/building.md#replaying-the-recorded-device-sessions)).

- A VMC relay or sender session compared at the canonical layer, including
  evidence for `CS-O2`. The prior cross-source method is recorded in
  [`usd-vrm-plugins` report motion/01](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/01-2026-08-15-mocopi-cross-source.md).
- A device-produced recovery case or a decision that the device cannot emit
  it, for the reserved `MOCOPI_TRACKING_LOST` diagnostic
  ([CONNECTOR §5](../design/CONNECTOR_CONTRACT.md#5-state)).
- A labelled rolled VRChat OSC take to distinguish the remaining Euler
  compositions: hold a head tilt or foot roll and record its side. See the
  measurement method in
  [`usd-vrm-plugins` report motion/03](https://github.com/animu-sphere/usd-vrm-plugins/blob/main/docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md).
- A redistributable mocopi capture from the vendor's `BVH Sender`, with terms
  permitting publication.
- Labelled OpenXR/WebXR device observations and MediaPipe world-axis
  measurements under [COORDINATE_SYSTEMS §5](../design/COORDINATE_SYSTEMS.md#5-known-sources).
- A live session reaching an avatar using release artifacts alone; coordinate
  the composed integration run with `usd-avatar-runtime`.
