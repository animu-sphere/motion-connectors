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
Remaining execution order is D, followed by the tracking placement
review. Boundary Phase E enforcement is recorded in
[WORKSPACE §2.4](../architecture/WORKSPACE.md#24-enforcement).

## Boundary Phase A — Thin VMC and mocopi libraries

Implemented acquisition boundaries and replay/installed-consumer evidence are
recorded in the [capability matrix](../reference/CAPABILITY_MATRIX.md#1-contract)
and [changelog](../../CHANGELOG.md#unreleased). Workspace-wide enforcement
continues in Phase E.

## Boundary Phase B — Recorder integration

Recorder composition and deterministic inspect/export evidence are recorded in
the [capability matrix](../reference/CAPABILITY_MATRIX.md#3-tools-and-bindings)
and [workspace contract](../architecture/WORKSPACE.md#21-inside-the-repository).
Reusable `MotionFrame`-to-motion-intake orchestration remains outside connector
libraries and belongs in `usd-avatar-runtime` when that integration is built.

## Boundary Phase C — OpenXR reference implementation

Acquisition, normalization and hardware-independent SDK/contract evidence are
recorded in the [capability matrix](../reference/CAPABILITY_MATRIX.md#2-sources)
and [component README](../../libs/motionConnectorOpenXR/README.md). The optional
module follows Boundary Phase E's acquisition gates. Browser sources reuse its
tracking-space observation boundary; the caller owns its session and frame loop.
Device measurement remains operator evidence rather than deterministic reference
acquisition's completion gate.

## Boundary Phase D — WebXR and MediaPipe

WebXR acquisition and deterministic contract/normalization evidence are recorded
in the [capability matrix](../reference/CAPABILITY_MATRIX.md#2-sources) and
[component README](../../web/motionConnectorWebXR/README.md). The caller owns
the session, reference space and active animation callback; the module emits
tracking-space observations in the existing frame wire representation.

Remaining:

- ⬜ Implement `motionConnectorMediaPipe`: landmarks → source-specific
  normalization → observations / `MotionFrame`.
- ⬜ Resolve `CC-O2` and the source-profile representation
  before freezing the browser boundary. Keep generic body solve downstream,
  rather than fixing a reconstruction algorithm inside a connector.

Browser layout and native closure decisions (WS-O4, WS-O6) are recorded in
[WORKSPACE §1.2](../architecture/WORKSPACE.md#12-web-modules). Acquisition uses
the frame wire representation without compiling native core to WASM.

Completion: both sources follow the same acquisition contract as OpenXR,
declare their basis and units, and have deterministic contract/normalization
tests. Browser delivery never imports motion intake, filters, retargeting or
semantic recording into the connector.

## Boundary Phase E — CI enforcement

Enforcement and installed-consumer evidence are recorded in
[WORKSPACE §2.4](../architecture/WORKSPACE.md#24-enforcement), the
[capability matrix](../reference/CAPABILITY_MATRIX.md#1-contract) and the
[changelog](../../CHANGELOG.md#unreleased). Source expansion follows these
gates; this phase carries no remaining implementation checklist.

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
