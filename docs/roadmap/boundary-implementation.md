# Connector boundary implementation

Status: 🚧 **implementation in progress**, 2026-10-06.
This page holds incomplete work for
[DESIGN_POLICY §47](../design/DESIGN_POLICY.md#47-external-acquisition-boundary).
The target dependency graph is
[WORKSPACE §2](../architecture/WORKSPACE.md#2-dependency-directions);
current capability claims remain in the
[capability matrix](../reference/CAPABILITY_MATRIX.md).
Release assignment is maintained only in
[the roadmap status table](README.md#status-at-a-glance).

Use **Boundary Phase A–E** when referring to this plan, so it cannot be
confused with the older Connector Phases or a sibling's migration phases.
Remaining execution order is B, E, C, D, followed by the tracking placement
review. Enforce the invariant before expanding the
source set.

## Boundary Phase A — Thin VMC and mocopi libraries

Implemented acquisition boundaries and replay/installed-consumer evidence are
recorded in the [capability matrix](../reference/CAPABILITY_MATRIX.md#1-contract)
and [changelog](../../CHANGELOG.md#unreleased). Recorder raw/export separation
continues in Phase B; workspace-wide enforcement continues in Phase E.

## Boundary Phase B — Recorder integration

- ⬜ Separate raw capture/source replay from canonical trace transcription in
  `vmc_record`, `mocopi_record` and `vrchat_osc_record`.
- ⬜ Declare tool-level `motionRecording` dependencies for semantic export;
  use it only on the export path. Keep raw capture as acquisition bytes and
  source/session provenance.
- ⬜ Route `--export-trace` through the downstream writer, with no copied
  `motion-capture-trace` implementation. Preserve source-specific CLI
  options, session reports and deterministic capture-to-trace evidence.
- ⬜ Keep `MotionFrame`-to-motion-intake orchestration outside connector
  libraries. Put the reusable runtime bridge in `usd-avatar-runtime` when
  that integration is implemented.

Completion: each recorder's composition is `tool → connector`, plus
`tool → motionRecording` for semantic export; no connector library depends
on the recorder or its downstream library. Existing inspect/export and raw
capture tests pass, and export consumes faithfully emitted observations.
Making export an optional build feature is an integration choice; it must
not become a required connector dependency.

## Boundary Phase C — OpenXR reference implementation

- ⬜ Implement the reserved `motionConnectorOpenXR` against the new boundary:
  OpenXR loader → head/controller/hand/body observations → source
  normalization → `MotionFrame`.
- ⬜ Declare SDK requirements only in that optional connector. Keep filters,
  retargeting, recording, `UsdStage` and avatar semantics outside it.
- ⬜ Add hardware-independent decode/normalization/contract tests and use
  OpenXR as the reference for later sources.

Completion: the connector emits normalized observations through the shared
contract, passes Boundary Phase E's gates, and its deterministic tests need
no XR hardware. Device-only validation does not create a supported capability
claim by itself.

## Boundary Phase D — WebXR and MediaPipe

- ⬜ Implement `motionConnectorWebXR`: browser acquisition of viewer,
  controllers and hands → source normalization → `MotionFrame`.
- ⬜ Implement `motionConnectorMediaPipe`: landmarks → source-specific
  normalization → observations / `MotionFrame`.
- ⬜ Resolve `WS-O4`, `WS-O6`, `CC-O2` and the source-profile representation
  before freezing the browser boundary. Keep generic body solve downstream,
  rather than fixing a reconstruction algorithm inside a connector.

Completion: both sources follow the same acquisition contract as OpenXR,
declare their basis and units, and have deterministic contract/normalization
tests. Browser delivery never imports motion intake, filters, retargeting or
semantic recording into the connector.

## Boundary Phase E — CI enforcement

- ⬜ Add dependency graph checks rejecting `motionSampling`,
  `motionRecording`, `motionRetarget` and `motionUsd` in connector library
  closures. Check CMake links, installed package dependencies and manifests
  so an installed consumer cannot reintroduce a hidden edge.
- ⬜ Extend include scans to reject `pxr/usd/usd/`, `pxr/usd/usdSkel/` and
  OpenExec includes in connector library code.
- ⬜ Keep protocol implementation out of `motionConnectorCore`; reject
  VMC/VRChat address literals in `motionConnectorOsc` and transport layers.
  Transport must also remain independent of `MotionFrame` semantics.
- ⬜ Scope the gates to library ownership. Permit deliberate downstream
  dependencies in tools, examples and consumer integration tests; no
  allowlist may leave a forbidden edge in a connector library.

Completion: CI and clean installed-consumer checks fail on each forbidden
dependency/header/literal case and accept the intended library/tool split.
The source replay evidence from Phase A still passes. Execute this phase
after A/B and before C/D to prevent regressions during source expansion.

## Follow-up — Generic tracking placement

- ⬜ Reassess `motionConnectorTracking` generic assignment, anatomical solve,
  pose reconstruction, confidence fusion and multi-tracker synthesis under
  [DESIGN_POLICY §47.5](../design/DESIGN_POLICY.md#475-tracking-and-placement-decisions).
  Keep `TrackerObservation`, source IDs/hints, availability, raw observations
  and tracking-space normalization here. Move only algorithms that satisfy
  all three criteria: no source/device name required, reusable across
  connectors, and generic `MotionPose` production.

Completion: record the ownership decision in the owning repositories and
resolve shared types without a reverse dependency or a duplicate contract
before moving any implementation.
