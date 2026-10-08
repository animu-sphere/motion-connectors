# Generic tracking ownership review

This page holds the remaining placement decision under
[DESIGN_POLICY §47.5](../design/DESIGN_POLICY.md#475-tracking-and-placement-decisions).
Current structure and dependency enforcement belong to
[WORKSPACE §2](../architecture/WORKSPACE.md#2-dependency-directions);
acquisition and test evidence belong to the
[capability matrix](../reference/CAPABILITY_MATRIX.md). Historical Boundary
Phases A–E are recorded in the [changelog](../../CHANGELOG.md).

## Review scope

Retain `motionConnectorTracking` while reviewing its algorithms against the
OpenXR, WebXR, MediaPipe and VRChat OSC observation contracts.

- Keep source observation identity, tracker region hints, raw assignment
  metadata, generic tracker assignment and acquisition-side tracking-space
  normalization in `motion-connectors`.
- Assess anatomical solve, humanoid pose reconstruction, confidence fusion
  and multi-tracker semantic synthesis for `usd-motion-plugins`. The deciding
  question is whether the algorithm organizes observations or generates motion
  semantics, without requiring a source/device name.
- Resolve observation and region types before proposing a move. A downstream
  algorithm must not introduce a reverse repository dependency or copy the
  shared contract.

## Completion criteria

Record the ownership decision and any type-boundary decision in the owning
repositories' architecture/design documents. Keep assignment and semantic solve
distinct even if they currently share a library. Schedule any resulting code
move explicitly; this review does not assume that all tracking code moves.

The review precedes the C ABI decision in
[current.md](current.md#v020-execution-order), so ABI ownership can account for
the observation boundary.
